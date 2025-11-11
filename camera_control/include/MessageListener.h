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

using namespace std;

class MessageListener {

private:
   void handleMessage(boost::asio::ip::tcp::socket socket);
   size_t readSocketData(boost::asio::ip::tcp::socket& socket, vector<unsigned char>& buf, size_t& msgLength, bool& gotFlagAndMsgLen);
   vector<unsigned char> prepareInquiryAck(vector<unsigned char>& buf,Utility& utility);
   void sendAckMessage(boost::asio::ip::tcp::socket& socket, const std::vector<unsigned char>& ackMessage);
        
   boost::asio::io_service& io_service_;
   boost::asio::ip::tcp::acceptor acceptor_;

   MainController& controller_;
   atomic<bool> stop_flag_;

public:
   MessageListener(boost::asio::io_service& io_service, short port, MainController& controller);
   void startListening();
   void stopListening();
};

#endif
