#ifndef MESSAGE_LISTENER_H
#define MESSAGE_LISTENER_H

#include "MainController.h"
#include "Utility.h"
#include "logger.h"
#include <boost/asio.hpp>
#include <iostream>
#include <vector>
#include <thread>
#include <atomic>
#include <map>
#include <mutex>

using namespace std;

class MessageListener {

private:
    //void handleMessage(boost::asio::ip::tcp::socket socket);
    void handleMessage(shared_ptr<boost::asio::ip::tcp::socket> socket_ptr);
    size_t readSocketData(boost::asio::ip::tcp::socket& socket, vector<unsigned char>& buf, size_t& msgLength, bool& gotFlagAndMsgLen);
    vector<unsigned char> prepareInquiryAck(vector<unsigned char>& buf,Utility& utility);
	vector<unsigned char> prepareTagDataAck(vector<unsigned char>& buf);
	vector<unsigned char> prepareTrainDataAck(vector<unsigned char>& buf);
	vector<unsigned char> prepareWheelDataAck(vector<unsigned char>& buf);
    unsigned char timeACK(vector<unsigned char> &timeBuf, vector<unsigned char>& buf, Utility& utility);
    void sendAckMessage(boost::asio::ip::tcp::socket& socket, const std::vector<unsigned char>& ackMessage);
        
    boost::asio::io_service& io_service_;
    boost::asio::ip::tcp::acceptor acceptor_;

    MainController& controller_;
    atomic<bool> stop_flag_;

	// a map IP -> socket
    std::map<std::string, std::shared_ptr<boost::asio::ip::tcp::socket>> active_ip_sockets_;
    // a mutex
    std::mutex socket_map_mutex_;


public:
    MessageListener(boost::asio::io_service& io_service, short port, MainController& controller);
    void startListening();
    void stopListening();
};

#endif
