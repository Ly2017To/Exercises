#include "MessageListener.h"
#include <boost/asio.hpp>
#include <iostream>
#include <vector>
#include <cmath>
#include <string>
#include <iomanip>  // For hex formatting

using namespace std;

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
			return;
		}
		//cout << "Client connected." << endl;
		thread(&MessageListener::handleMessage, this, move(socket)).detach();
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
            size_t nbytes = socket.read_some(boost::asio::buffer(buf.data() + nbytes + offset, bytesLeft), error);
            if (error) {
					cout << nbytes << endl;
                	//cerr << "Error receiving data: " << error.message() << endl;
                    return 0;  // return 0 to indicate error
         }

         offset += nbytes;
         bytesLeft -= nbytes;
         bytesRead += nbytes;
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

// Function to prepare ACK message
vector<unsigned char> MessageListener::prepareInquiryAck(vector<unsigned char>& buf, Utility& utility) {
    return ackMessage;
}

void MessageListener::handleMessage(boost::asio::ip::tcp::socket socket) {
    try {
        // Get the remote endpoint (client's IP address and port)
      	boost::asio::ip::tcp::endpoint remote_endpoint = socket.remote_endpoint();
      	string client_ip = remote_endpoint.address().to_string();
       unsigned short client_port = remote_endpoint.port();

        // Print out the client's IP address and port
        // cout << "Client connected: " << client_ip << ":" << client_port << endl;
		Logger::getInstance().log("Client connected: " + client_ip + ":" + to_string(client_port),Logger::PROD); 

        // Now read the data from the socket
      	boost::system::error_code error;
		// Buffer to read the data
      	vector<unsigned char> buf(BUFF_SIZE);  // Dynamic buffer size
        size_t msgLength=0, bytesRead = 0;
        bool gotFlagAndMsgLen = false;
        // Acks
		vector<unsigned char> inquiryAck;
		string message;
  		// Create an object of Utility class
    	Utility utility = Utility::createInstance();

		while(!stop_flag_){
        	bytesRead = readSocketData(socket, buf, msgLength, gotFlagAndMsgLen);
            if (bytesRead == 0) {
                // An error occurred or connection was closed
                break;
         	}

            // Once the message is fully received, process it
            if (gotFlagAndMsgLen && bytesRead == msgLength) {
				gotFlagAndMsgLen = false;  // Reset flag for the next message
				/*
             	cout << "Received message in hex: ";
    			for (size_t i = 0; i < msgLength; ++i) {
        			cout <<hex<<setw(2)<<setfill('0')<< (unsigned int)(unsigned char)buf[i] << " ";
    			}
    			cout << endl;
				*/
					
                //get the type of msg
				switch(buf[TYPE_BYTE]){
					case INQUIRY:
                        //an example, send the command to the camera based on its name 
                        controller_.sendCommandToCamera("cameraName","command");
						break;
					default:
						Logger::getInstance().log("got unknown message type",Logger::TEST);
            			// Optionally, handle error or unrecognized message types
            			break;
 				}
         }	
       }
	   // Ensure socket is closed when done processing
       socket.close();  // Close the socket explicitly after the work is done
    } catch (std::exception& e) {
		//cerr << "Exception in handleMessage: " << e.what() << endl;
		Logger::getInstance().log("Exception in handleMessage: " + string(e.what()),Logger::PROD);
		// Close the socket even if an exception was thrown
        try {
      		socket.close();  // Close socket in case of an exception
      	} catch (const std::exception& close_e) {
       		// Handle any exceptions that might occur during socket closing
     		Logger::getInstance().log("Exception during socket close: " + string(close_e.what()), Logger::PROD);
      	}
    }
}
