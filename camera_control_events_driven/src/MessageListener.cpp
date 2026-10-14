#include "MessageListener.h"
#include "blackbox.h"
#include "AeiActivity.h"
#include <boost/asio.hpp>
#include <iostream>
#include <vector>
#include <cmath>
#include <string>
#include <iomanip>  // For hex formatting
#include <sys/socket.h>  // setsockopt for TCP_QUICKACK (Linux)
#include <netinet/tcp.h> // TCP_QUICKACK (Linux)
#include <poll.h>        // C1 fix: poll() on the native socket handle for the waitReadable helper

using namespace std;

constexpr uint8_t HEADER_BYTE = 0x00;
constexpr uint8_t LENGTH_BYTE_HIGH = 0x01;
constexpr uint8_t LENGTH_BYTE_LOW = 0x02;
constexpr uint8_t TYPE_BYTE = 0x03;
constexpr uint8_t STATION_BYTE_HIGH = 0x04;
constexpr uint8_t STATION_BYTE_LOW = 0x05;
constexpr uint8_t PATH_ONE_ON = 0x06;
constexpr uint8_t PATH_ONE_DIR = 0x07;
constexpr uint8_t PATH_TWO_ON = 0x08;
constexpr uint8_t PATH_TWO_DIR = 0x09;

constexpr uint8_t YEAR_BYTE_LOW = 0x0b;
constexpr uint8_t YEAR_BYTE_HIGH = 0x0c;
constexpr uint8_t MONTH_BYTE = 0x0d;
constexpr uint8_t DAY_BYTE = 0x0e;
constexpr uint8_t HOUR_BYTE = 0x0f;
constexpr uint8_t MINUTE_BYTE = 0x10;
constexpr uint8_t SECOND_BYTE = 0x11;

constexpr uint8_t VERSION = 0x21;
constexpr uint8_t IA_LENGTH_BYTE_HIGH = 0x13;
constexpr uint8_t IA_LENGTH_BYTE_LOW = 0x00;
constexpr uint8_t DA_LENGTH_BYTE_HIGH = 0x0D;
constexpr uint8_t DA_LENGTH_BYTE_LOW = 0x00;

constexpr uint8_t CAR_NUMBER = 0x06;
constexpr uint8_t WHEEL_PATH = 0x07;
constexpr uint8_t WHEEL_DIR = 0x08;

constexpr uint8_t PATH_ONE = 0x01;
constexpr uint8_t PATH_TWO = 0x02;

constexpr size_t BUFF_SIZE = 1024;
constexpr size_t CHUNK_SIZE = 256;
constexpr int kWaitTickMs = 100;  // C1: idle heartbeat tick (matches watchdog tick)

enum LocomotiveMsgType{
	HEADER = 0xfd,
	INQUIRY = 0x01,
	INQUIRY_ACK = 0X81,           
	TAG_DATA = 0X03,                         
	TAG_DATA_ACK = 0X83,           
	TRAIN_DATA = 0X08,                       
	TRAIN_DATA_ACK = 0X88,           
	WHEEL_DATA = 0X31,
	WHEEL_DATA_ACK = 0xB1         
};

MessageListener::MessageListener(boost::asio::io_service& io_service, short port, MainController& controller)
    : io_service_(io_service),
      acceptor_(io_service, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), port)),
      controller_(controller),
		stop_flag_(false){ }
    
void MessageListener::startListening() {
    while (!stop_flag_) {
		boost::asio::ip::tcp::socket socket(io_service_);
		//cout << "Waiting for client connection..." << endl;
		
		boost::system::error_code ec;
		acceptor_.accept(socket, ec);
		if (ec) {
			 //cerr << "Error accepting connection: " << ec.message() << endl;
			Logger::getInstance().log("Error accepting connection: " + string(ec.message()),Logger::PROD);
			continue;
		}

		//P2-1: reduce small-message latency (disable Nagle on this connection)
		boost::system::error_code tcpOptEc;
		socket.set_option(boost::asio::ip::tcp::no_delay(true), tcpOptEc);
		if (tcpOptEc) {
			Logger::getInstance().log("set TCP_NODELAY failed: " + tcpOptEc.message(), Logger::PROD);
		}
		//P2-1: prompt ACK on the receiving side (counteract delayed ACK)
		int quickAckFlag = 1;
		if (setsockopt(socket.native_handle(), IPPROTO_TCP, TCP_QUICKACK, &quickAckFlag, sizeof(quickAckFlag)) != 0) {
			Logger::getInstance().log("set TCP_QUICKACK failed", Logger::PROD);
		}

      string client_ip;
        try {
            client_ip = socket.remote_endpoint().address().to_string();
      } catch (...) {
            continue;
      }
      //Logger::getInstance().log("Client connected: " + client_ip, Logger::PROD);

      lock_guard<std::mutex> lock(socket_map_mutex_);

       if (active_ip_sockets_.count(client_ip)) {
            boost::system::error_code ignore_ec;
            active_ip_sockets_[client_ip]->close(ignore_ec);
      }

        auto socket_ptr = make_shared<boost::asio::ip::tcp::socket>(std::move(socket));
      active_ip_sockets_[client_ip] = socket_ptr;

     
       thread(&MessageListener::handleMessage, this, socket_ptr).detach();
    }
}

void MessageListener::stopListening() {
	stop_flag_ = true;  // Set flag to stop
	acceptor_.close();  // Close the acceptor to stop listening for new connections
}

// Function to read data from the socket
// Streaming read (P0-1 refactor): socket data first goes into recvBuf (a per-connection
// residual buffer preserved across calls); once a complete frame is available (header 0xFD
// + length low byte + length high byte + payload) it is copied into buf and the frame
// length is returned. If the frame is incomplete, keep reading the socket; on error or
// peer close return 0.
// Fixes: underflow infinite loop of msgLength-nbytes on coalesced packets, out-of-bounds
//        writes to buf, cross-call state loss (bytesLeft/offset were function locals),
//        byte loss for multiple frames in one packet due to the missing residual buffer,
//        and no recovery after frame-header misalignment.
size_t MessageListener::readSocketData(boost::asio::ip::tcp::socket& socket, vector<unsigned char>& buf, vector<unsigned char>& recvBuf, size_t& msgLength, bool& gotFlagAndMsgLen) {
    boost::system::error_code error;
    unsigned char chunk[CHUNK_SIZE];

    while (true) {
        // 1) Resync: drop bytes before the frame header so misalignment can recover
        while (!recvBuf.empty() && recvBuf[0] != HEADER) {
            recvBuf.erase(recvBuf.begin());
        }

        // 2) Once the 3-byte header is complete, parse the frame length
        if (recvBuf.size() >= 3) {
            // Little-endian: byte 1 is the low byte, byte 2 is the high byte
            msgLength = (size_t)recvBuf[1] | ((size_t)recvBuf[2] << 8);

            // Guard against illegal lengths (<3 or above the buffer limit): treat as misalignment, drop the 3 header bytes and resync
            if (msgLength < 3 || msgLength > BUFF_SIZE) {
                recvBuf.erase(recvBuf.begin(), recvBuf.begin() + 3);
                continue;
            }

            // 3) A complete frame has arrived: copy it out into buf and consume it
            if (recvBuf.size() >= msgLength) {
                buf.assign(recvBuf.begin(), recvBuf.begin() + msgLength);
                recvBuf.erase(recvBuf.begin(), recvBuf.begin() + msgLength);
                gotFlagAndMsgLen = true;
                return msgLength;
            }
        }

        // 4) Frame incomplete: keep reading the socket; if abnormal input overflows the residual buffer, clear it and resync
        if (recvBuf.size() > (size_t)BUFF_SIZE + CHUNK_SIZE) {
            recvBuf.clear();
            gotFlagAndMsgLen = false;
            continue;
        }
        size_t n = socket.read_some(boost::asio::buffer(chunk, CHUNK_SIZE), error);
        if (error || n == 0) {
            gotFlagAndMsgLen = false;
            return 0;  // return 0 to indicate error
        }
        recvBuf.insert(recvBuf.end(), chunk, chunk + n);
    }
}

// C1 fix: wait up to timeoutMs for the socket to become readable. Returns
// true if poll() saw POLLIN/POLLERR/POLLHUP (caller proceeds to read_some,
// which will return 0 on peer close and exit the loop normally). Returns
// false on poll() timeout, in which case the caller heartbeats and loops.
//
// Why poll() and not asio's available()/async_wait: keeping the socket
// blocking avoids changing write semantics (sendAckMessage uses synchronous
// boost::asio::write), and poll() on the fd honours SO_RCVTIMEO indirectly
// through the timeout arg. readSocketData's "read until a full frame" loop
// remains untouched so residual-buffer semantics and frame-header resync
// behaviour are preserved.
static bool waitReadable(boost::asio::ip::tcp::socket& s, int timeoutMs) {
    struct pollfd pfd;
    pfd.fd = s.native_handle();
    pfd.events = POLLIN;
    pfd.revents = 0;
    int r = ::poll(&pfd, 1, timeoutMs);
    return r > 0;  // POLLIN / POLLERR / POLLHUP all treated as "readable"
}

void MessageListener::sendAckMessage(boost::asio::ip::tcp::socket& socket, const std::vector<unsigned char>& ackMessage) {
    try {
		boost::system::error_code error;
        size_t totalBytesSent = 0;
        size_t messageLength = ackMessage.size();
        size_t chunkSize = CHUNK_SIZE;  // Define chunk size for sending in parts
		/*
      cout << "message will be sent in hex: " << endl;
    	for (size_t i = 0; i < messageLength; ++i) {
			cout <<hex<<setw(2)<<setfill('0')<< (unsigned int)(unsigned char)ackMessage[i] << " ";
		}
		cout << endl;
      */

        while (totalBytesSent < messageLength) {
			size_t bytesToSend = std::min(chunkSize, messageLength - totalBytesSent);

            // Write a chunk of the message
            size_t bytesSent = boost::asio::write(socket, boost::asio::buffer(ackMessage.data() + totalBytesSent, bytesToSend), error);

            if (error) {
				//cerr << "Error sending ACK message: " << error.message() << endl;
                break;
			}

          totalBytesSent += bytesSent;
            //cout << "Sent " << bytesSent << " bytes of ACK message" << endl;
      }

        //if (totalBytesSent == messageLength) {
       //    cout << "Successfully sent complete ACK message" << endl;
       //}

    } catch (const std::exception& e) {
       //cerr << "Exception in sendAckMessage: " << e.what() << endl;
		Logger::getInstance().log("Exception in sendAckMessage: "+ string(e.what()),Logger::PROD);
    }
}

// Get AEI time in compressed BCD format
unsigned char MessageListener::timeACK(vector<unsigned char> &timeBuf, vector<unsigned char>& buf, Utility& utility){
	msgTime_t pTime;
	utility.getRTCTime(pTime);   
	
	timeBuf.push_back(utility.dec2Bcd(pTime.nYear % 100));  // Low byte first
	timeBuf.push_back(utility.dec2Bcd(pTime.nYear / 100));  // High byte
	timeBuf.push_back(utility.dec2Bcd(pTime.cMonth));
	timeBuf.push_back(utility.dec2Bcd(pTime.cDay));
	timeBuf.push_back(utility.dec2Bcd(pTime.cHour));
	timeBuf.push_back(utility.dec2Bcd(pTime.cMinute));
	timeBuf.push_back(utility.dec2Bcd(pTime.cSecond));

	//Logger::getInstance().log(to_string(timeBuf[0])+" "+to_string(buf[YEAR_BYTE_LOW]), Logger::PROD);
	//Logger::getInstance().log(to_string(timeBuf[1])+" "+to_string(buf[YEAR_BYTE_HIGH]), Logger::PROD);
	//Logger::getInstance().log(to_string(timeBuf[2])+" "+to_string(buf[MONTH_BYTE]), Logger::PROD);
	//Logger::getInstance().log(to_string(timeBuf[3])+" "+to_string(buf[DAY_BYTE]), Logger::PROD);
	//Logger::getInstance().log(to_string(timeBuf[4])+" "+to_string(buf[HOUR_BYTE]), Logger::PROD);
	//Logger::getInstance().log(to_string(timeBuf[5])+" "+to_string(buf[MINUTE_BYTE]), Logger::PROD);
	//Logger::getInstance().log(to_string(timeBuf[6])+" "+to_string(buf[SECOND_BYTE]), Logger::PROD);
	
	if(timeBuf[0]!=buf[YEAR_BYTE_LOW]) return 0xff;
	if(timeBuf[1]!=buf[YEAR_BYTE_HIGH]) return 0xff;
	if(timeBuf[2]!=buf[MONTH_BYTE]) return 0xff;
	if(timeBuf[3]!=buf[DAY_BYTE]) return 0xff;
	if(timeBuf[4]!=buf[HOUR_BYTE]) return 0xff;
	if(timeBuf[5]!=buf[MINUTE_BYTE]) return 0xff;
	//Second-difference check: compare in DECIMAL, not raw BCD. Raw BCD values
	//are not contiguous (0x09 jumps to 0x10), so subtracting them directly adds
	//an error of 6 across a decade carry: e.g. 9s vs 19s would look like 16s
	//instead of the real 10s and wrongly request a time correction.
	if(abs((int)utility.bcd2Dec(timeBuf[6]) - (int)utility.bcd2Dec(buf[SECOND_BYTE])) > 10) return 0xff;

    return 0x00;
}

// Function to prepare ACK message
vector<unsigned char> MessageListener::prepareInquiryAck(vector<unsigned char>& buf, Utility& utility) {
	vector<unsigned char> ackMessage;
	ackMessage.push_back(HEADER); 
	ackMessage.push_back(IA_LENGTH_BYTE_HIGH); 
	ackMessage.push_back(IA_LENGTH_BYTE_LOW);
   ackMessage.push_back(INQUIRY_ACK);  // Define this constant for ACK type
	ackMessage.push_back(buf[STATION_BYTE_HIGH]);  // High byte of station number
	ackMessage.push_back(buf[STATION_BYTE_LOW]);   // Low byte of station number
	vector<unsigned char> timeBuf;
    unsigned char sync = timeACK(timeBuf,buf, utility);
   ackMessage.push_back(sync);
   ackMessage.push_back(0x00); //reverse trigger on
   ackMessage.push_back(VERSION); //version
	ackMessage.push_back(0x00); 
	ackMessage.push_back(0x00); 
	for(unsigned char byte: timeBuf) {
		ackMessage.push_back(byte); 
   }
	//ackMessage.insert(ackMessage.end(), 7, 0x00);
	unsigned char xorCheck = 0;
    for (unsigned char byte : ackMessage){
		xorCheck ^= byte;  // Compute XOR checksum
   }
   ackMessage.push_back(xorCheck);  // Append XOR checksum to the end of the message

    return ackMessage;
}

vector<unsigned char> MessageListener::prepareTagDataAck(vector<unsigned char>& buf) {
	vector<unsigned char> ackMessage;
	ackMessage.push_back(HEADER); 
	ackMessage.push_back(DA_LENGTH_BYTE_HIGH); 
	ackMessage.push_back(DA_LENGTH_BYTE_LOW);
   ackMessage.push_back(TAG_DATA_ACK);  // Define this constant for ACK type
   ackMessage.push_back(buf[STATION_BYTE_HIGH]);  // High byte of station number
	ackMessage.push_back(buf[STATION_BYTE_LOW]);   // Low byte of station number
	ackMessage.push_back(0x00); 
	ackMessage.push_back(0x00); 
	ackMessage.push_back(0x00); 
	ackMessage.push_back(0x00); 
	ackMessage.push_back(0x00); 
	ackMessage.push_back(0x00); 
	unsigned char xorCheck = 0;
    for (unsigned char byte : ackMessage){
		xorCheck ^= byte;  // Compute XOR checksum
   }
   ackMessage.push_back(xorCheck);  // Append XOR checksum to the end of the message

    return ackMessage;
}

vector<unsigned char> MessageListener::prepareTrainDataAck(vector<unsigned char>& buf) {
	vector<unsigned char> ackMessage;
	ackMessage.push_back(HEADER); 
	ackMessage.push_back(DA_LENGTH_BYTE_HIGH); 
	ackMessage.push_back(DA_LENGTH_BYTE_LOW);
   ackMessage.push_back(TRAIN_DATA_ACK);  // Define this constant for ACK type
	ackMessage.push_back(buf[STATION_BYTE_HIGH]);  // High byte of station number
	ackMessage.push_back(buf[STATION_BYTE_LOW]);   // Low byte of station number
	ackMessage.push_back(0x00); 
	ackMessage.push_back(0x00); 
	ackMessage.push_back(0x00); 
	ackMessage.push_back(0x00); 
	ackMessage.push_back(0x00); 
	ackMessage.push_back(0x00); 
	unsigned char xorCheck = 0;
    for (unsigned char byte : ackMessage){
		xorCheck ^= byte;  // Compute XOR checksum
   }
   ackMessage.push_back(xorCheck);  // Append XOR checksum to the end of the message

    return ackMessage;
}

vector<unsigned char> MessageListener::prepareWheelDataAck(vector<unsigned char>& buf) {
	vector<unsigned char> ackMessage;
	ackMessage.push_back(HEADER); 
	ackMessage.push_back(DA_LENGTH_BYTE_HIGH); 
	ackMessage.push_back(DA_LENGTH_BYTE_LOW);
   ackMessage.push_back(WHEEL_DATA_ACK);  // Define this constant for ACK type
	ackMessage.push_back(buf[STATION_BYTE_HIGH]);  // High byte of station number
	ackMessage.push_back(buf[STATION_BYTE_LOW]);   // Low byte of station number
	ackMessage.push_back(0x00); 
	ackMessage.push_back(0x00); 
	ackMessage.push_back(0x00); 
	ackMessage.push_back(0x00); 
	ackMessage.push_back(0x00); 
	ackMessage.push_back(0x00); 
	unsigned char xorCheck = 0;
    for (unsigned char byte : ackMessage){
		xorCheck ^= byte;  // Compute XOR checksum
   }
   ackMessage.push_back(xorCheck);  // Append XOR checksum to the end of the message

    return ackMessage;
}

void MessageListener::handleMessage(shared_ptr<boost::asio::ip::tcp::socket> socket_ptr) {
    string client_ip;
    try {
         // B1: arm the listener-receive channel so the watchdog can sense when this thread stalls
         // (10.4 design gap: ListenerRecv was never armed, so stalls here never triggered dumps)
        bbArm(BbChannel::ListenerRecv);
        bbEvent("listener_arm", 0);

         // get ip
        boost::system::error_code ec;
        auto remote_ep = socket_ptr->remote_endpoint(ec);
        if (!ec) {
            client_ip = remote_ep.address().to_string();
        }

        // socket → (*socket_ptr)
        boost::asio::ip::tcp::endpoint remote_endpoint = socket_ptr->remote_endpoint();
        string client_ip_full = remote_endpoint.address().to_string();
        unsigned short client_port = remote_endpoint.port();

        Logger::getInstance().log("Client connected: " + client_ip_full + ":" + to_string(client_port), Logger::PROD);

        vector<unsigned char> buf(BUFF_SIZE);
        size_t msgLength = 0, bytesRead = 0;
        bool gotFlagAndMsgLen = false;
        // Residual buffer for this connection: keeps unconsumed bytes across
        // readSocketData calls. Must be one per connection (it cannot be a member
        // variable: threads of multiple connections share one MessageListener instance)
        vector<unsigned char> recvBuf;
        vector<unsigned char> inquiryAck, tagDataAck, trainDataAck, wheelDataAck;
        unsigned char path1On, path1Dir, path2On, path2Dir, path1OnPre = 0, path2OnPre = 0, path, direction;
        string message;
        Utility utility = Utility::createInstance();

        while (!stop_flag_) {
             // C1 fix: wait briefly for socket readability first. This decouples
             // the heartbeat cadence from the frame-arrival cadence: when AEI is
             // idle (no frames for ~5s around an INQUERY period), the loop ticks
             // every kWaitTickMs and heartbeats each tick. A genuine stall in
             // read_some or sendAckMessage still leaves a 100ms heartbeat gap.
            if (!waitReadable(*socket_ptr, kWaitTickMs)) {
                bbHeartbeat(BbChannel::ListenerRecv);  // idle tick
                continue;
            }

             // socket → (*socket_ptr)
            bytesRead = readSocketData(*socket_ptr, buf, recvBuf, msgLength, gotFlagAndMsgLen);
            if (bytesRead == 0) {
                break;
            }
            bbHeartbeat(BbChannel::ListenerRecv);  // post-recv tick

            if (gotFlagAndMsgLen && bytesRead == msgLength) {
                gotFlagAndMsgLen = false;

                // Black box: message arrival time (timeline start, used to reconstruct end-to-end latency)
                bbEvent("recv_frame", static_cast<long long>(bytesRead));
                // ALERT criteria: any event-source frame counts as "event source active"
                AeiActivity::markAnyMsg();

                switch (buf[TYPE_BYTE]) {
                    case WHEEL_DATA:
                        path = buf[WHEEL_PATH];
                        direction = buf[WHEEL_DIR];
                        bbEvent("recv_wheel", 0);
                        AeiActivity::markWheelMsg();  // also stamps lastWheelMsgMs
                        message.push_back(UPDATE);
                        message.push_back(path);
                        message.push_back(direction);
                        message.push_back(buf[CAR_NUMBER]);
                        {
                            BbSpan enqSpan("cmd_enqueue", 20);  // enqueue duration (>20ms records an event)
                            controller_.sendCommandToCamera(message);
                        }
                        Logger::getInstance().log("got wheel msg", Logger::TEST);
                        message.clear();
                        break;

                    case INQUIRY:
                        Logger::getInstance().log("got inquiry msg", Logger::TEST);
                        bbEvent("recv_inquiry", 0);
                        inquiryAck = prepareInquiryAck(buf, utility);
                        {   // A3: synchronous ACK send duration (observation only, no behavior change; evaluate async only after blocking is confirmed)
                            BbSpan ackSpan("ack_send_inquiry", 100);
                            sendAckMessage(*socket_ptr, inquiryAck);  //
                        }
                        path1On = buf[PATH_ONE_ON];
                        path1Dir = buf[PATH_ONE_DIR];
                        path2On = buf[PATH_TWO_ON];
                        path2Dir = buf[PATH_TWO_DIR];

                        if (path1OnPre == 0 && path1On == 1) {
                            Logger::getInstance().log("power on path1", Logger::TEST);
                            message.push_back(TRIGGERON);
                            message.push_back(PATH_ONE);
                            message.push_back(path1Dir);
								  message.push_back(buf[YEAR_BYTE_HIGH]);
								  message.push_back(buf[YEAR_BYTE_LOW]);
								  message.push_back(buf[MONTH_BYTE]);
								  message.push_back(buf[DAY_BYTE]);
								  message.push_back(buf[HOUR_BYTE]);
								  message.push_back(buf[MINUTE_BYTE]);
                            message.push_back(buf[SECOND_BYTE]);

                            controller_.sendCommandToCamera(message);
                            message.clear();
                        }
                        if (path2OnPre == 0 && path2On == 1) {
                            Logger::getInstance().log("power on path2", Logger::TEST);
                            message.push_back(TRIGGERON);
                            message.push_back(PATH_TWO);
                            message.push_back(path2Dir);
								  message.push_back(buf[YEAR_BYTE_HIGH]);
								  message.push_back(buf[YEAR_BYTE_LOW]);
								  message.push_back(buf[MONTH_BYTE]);
								  message.push_back(buf[DAY_BYTE]);
								  message.push_back(buf[HOUR_BYTE]);
								  message.push_back(buf[MINUTE_BYTE]);
                            message.push_back(buf[SECOND_BYTE]);

                            controller_.sendCommandToCamera(message);
                            message.clear();
                        }
                        if (path1OnPre == 1 && path1On == 0) {
                            Logger::getInstance().log("power off path1", Logger::TEST);
                            message.push_back(TRIGGEROFF);
                            message.push_back(PATH_ONE);
                            message.push_back(path1Dir);
                            controller_.sendCommandToCamera(message);
                            message.clear();
                        }
                        if (path2OnPre == 1 && path2On == 0) {
                            Logger::getInstance().log("power off path2", Logger::TEST);
                            message.push_back(TRIGGEROFF);
                            message.push_back(PATH_TWO);
                            message.push_back(path2Dir);
                            controller_.sendCommandToCamera(message);
                            message.clear();
                        }
                        inquiryAck.clear();
                        path1OnPre = path1On;
                        path2OnPre = path2On;
                        break;

                    case TAG_DATA:
                        Logger::getInstance().log("got tag data msg", Logger::TEST);
                        bbEvent("recv_tag", 0);
                        tagDataAck = prepareTagDataAck(buf);
                        {   // A3: synchronous ACK send duration (observation only)
                            BbSpan ackSpan("ack_send_tag", 100);
                            sendAckMessage(*socket_ptr, tagDataAck);  //
                        }
                        tagDataAck.clear();
                        break;

                    case TRAIN_DATA:
                        Logger::getInstance().log("got train data msg", Logger::TEST);
                        bbEvent("recv_train", 0);
                        trainDataAck = prepareTrainDataAck(buf);
                        {   // A3: synchronous ACK send duration (observation only)
                            BbSpan ackSpan("ack_send_train", 100);
                            sendAckMessage(*socket_ptr, trainDataAck);  //
                        }
                        trainDataAck.clear();
                        break;

                    default:
                        Logger::getInstance().log("got unknown message type", Logger::TEST);
                        break;
                }
            }
        }

        // B1 companion fix: stop the watchdog from patrolling this channel once the recv loop exits
        bbDisarm(BbChannel::ListenerRecv);

        // no need to close，shared_ptr will handle it
        // socket.close();
    }
    catch (std::exception& e) {
        Logger::getInstance().log("Exception in handleMessage: " + string(e.what()), Logger::PROD);
        // Disarm even on exception so the watchdog doesn't keep watching a dead socket
        bbDisarm(BbChannel::ListenerRecv);
    }

    //  when the thread ends, clear it
    std::lock_guard<std::mutex> lock(socket_map_mutex_);
    if (!client_ip.empty()) {
        active_ip_sockets_.erase(client_ip);
        Logger::getInstance().log("Client disconnected, IP removed: " + client_ip, Logger::PROD);
    }
}
