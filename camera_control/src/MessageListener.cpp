#include "MessageListener.h"
#include <boost/asio.hpp>
#include <iostream>
#include <vector>
#include <cmath>
#include <string>
#include <iomanip>  // For hex formatting

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
size_t MessageListener::readSocketData(boost::asio::ip::tcp::socket& socket, vector<unsigned char>& buf, size_t& msgLength, bool& gotFlagAndMsgLen) {
    size_t bytesRead = 0, nbytes=0;
   boost::system::error_code error;
    
    // If we haven't received the flag and message length yet
    if (!gotFlagAndMsgLen) {
      nbytes = socket.read_some(boost::asio::buffer(buf), error);
        if (error) {
			cout << nbytes << endl;
			//cerr << "Error receiving data: " << error.message() << endl;
            return 0;  // return 0 to indicate error
      }
		
        if (nbytes >= TYPE_BYTE) {  // flag and message length
            if (buf[HEADER_BYTE] == HEADER) {  // Check for the expected msgHeader
                msgLength = ((buf[LENGTH_BYTE_LOW]) << 8) + (buf[LENGTH_BYTE_HIGH]);
                gotFlagAndMsgLen = true;
                bytesRead = nbytes;
					//cout << bytesRead << endl;
          }
       }
    }
    
    // Once we have the message length, read the rest of the data
    if (gotFlagAndMsgLen) {
        size_t bytesLeft = msgLength - nbytes;  // Subtract the 3 bytes for the flag and length
        size_t offset = 0;

        if(bytesLeft==0) return bytesRead;
        
        while (bytesLeft > 0) {
            size_t read_bytes = socket.read_some(boost::asio::buffer(buf.data() + nbytes + offset, bytesLeft), error);
            if (error) {
					cout << nbytes << endl;
                	//cerr << "Error receiving data: " << error.message() << endl;
                    return 0;  // return 0 to indicate error
         }

         offset += read_bytes;
         bytesLeft -= read_bytes;
         bytesRead += read_bytes;
      }
    }

    return bytesRead;  // Return the number of bytes read
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
	if(abs(timeBuf[6]-buf[SECOND_BYTE])>10) return 0xff;
    
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
        vector<unsigned char> inquiryAck, tagDataAck, trainDataAck, wheelDataAck;
        unsigned char path1On, path1Dir, path2On, path2Dir, path1OnPre = 0, path2OnPre = 0, path, direction;
        string message;
        Utility utility = Utility::createInstance();

        while (!stop_flag_) {
             // socket → (*socket_ptr)
            bytesRead = readSocketData(*socket_ptr, buf, msgLength, gotFlagAndMsgLen);
            if (bytesRead == 0) {
                break;
            }

            if (gotFlagAndMsgLen && bytesRead == msgLength) {
                gotFlagAndMsgLen = false;

                switch (buf[TYPE_BYTE]) {
                    case WHEEL_DATA:
                        wheelDataAck = prepareWheelDataAck(buf);
                        path = buf[WHEEL_PATH];
                        direction = buf[WHEEL_DIR];
                        message.push_back(UPDATE);
                        message.push_back(path);
                        message.push_back(direction);
                        message.push_back(buf[CAR_NUMBER]);
                        controller_.sendCommandToCamera(message);
                        sendAckMessage(*socket_ptr, wheelDataAck); //
                        Logger::getInstance().log("got wheel msg", Logger::TEST);
                        wheelDataAck.clear();
                        message.clear();
                        break;

                    case INQUIRY:
                        Logger::getInstance().log("got inquiry msg", Logger::TEST);
                        inquiryAck = prepareInquiryAck(buf, utility);
                        sendAckMessage(*socket_ptr, inquiryAck);  //
                        path1On = buf[PATH_ONE_ON];
                        path1Dir = buf[PATH_ONE_DIR];
                        path2On = buf[PATH_TWO_ON];
                        path2Dir = buf[PATH_TWO_DIR];

                        if (path1OnPre == 0 && path1On == 1) {
                            Logger::getInstance().log("power on path1", Logger::TEST);
                            message.push_back(TRIGGERON);
                            message.push_back(PATH_ONE);
                            message.push_back(path1Dir);
                            controller_.sendCommandToCamera(message);
                            message.clear();
                        }
                        if (path2OnPre == 0 && path2On == 1) {
                            Logger::getInstance().log("power on path2", Logger::TEST);
                            message.push_back(TRIGGERON);
                            message.push_back(PATH_TWO);
                            message.push_back(path2Dir);
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
                        tagDataAck = prepareTagDataAck(buf);
                        sendAckMessage(*socket_ptr, tagDataAck);  //
                        tagDataAck.clear();
                        break;

                    case TRAIN_DATA:
                        Logger::getInstance().log("got train data msg", Logger::TEST);
                        trainDataAck = prepareTrainDataAck(buf);
                        sendAckMessage(*socket_ptr, trainDataAck);  //
                        trainDataAck.clear();
                        break;

                    default:
                        Logger::getInstance().log("got unknown message type", Logger::TEST);
                        break;
                }
            }
        }

        // no need to close，shared_ptr will handle it
        // socket.close(); 
    }
    catch (std::exception& e) {
        Logger::getInstance().log("Exception in handleMessage: " + string(e.what()), Logger::PROD);
    }

    //  when the thread ends, clear it
    std::lock_guard<std::mutex> lock(socket_map_mutex_);
    if (!client_ip.empty()) {
        active_ip_sockets_.erase(client_ip);
        Logger::getInstance().log("Client disconnected, IP removed: " + client_ip, Logger::PROD);
    }
}
