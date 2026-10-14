#include <boost/asio.hpp>
#include <iostream>
#include <thread>
#include <chrono>
#include "MessageListener.h"
#include "MainController.h"
#include "HikCamera.h"
#include "HikSDKManager.h"
#include "Configurations.h"
#include "logger.h"
#include "blackbox.h"

using namespace std;

int main() {

	std::cout << "Camera Control Program Starts, version 0.0.0" << std::endl;

	// Start the black box: timing instrumentation + stall watchdog + exception dump (can be disabled with the BB_DISABLE=1 env var)
	bbStart();

	// Instantiate the Logger singleton
	Logger& logger = Logger::getInstance();

	logger.log("Camera Control Program Starts, version 0.0.0",Logger::PROD);
	
	/*
	// initialize HikSDK manager globally (singleton)
	if (!HikSDKManager::getInstance().init()) {
		logger.log("Hik SDK initialization failed with code:"+to_string(HikSDKManager::getInstance().getLastError()),Logger::PROD);
		std::cout << "Hik SDK initialization failed with code:"+to_string(HikSDKManager::getInstance().getLastError()) << std::endl;
	}else{
		logger.log("HikSDK initialized successfully",Logger::PROD);
		std::cout << "HikSDK initialized successfully" << std::endl;
	} 
	*/

	// Create the controller that manages the cameras
	MainController controller;

    try {

    	Configurations config("../configuration/camera_config.json");
		std::cout << "finish configuration" << std::endl;

		// initialize HikSDK manager globally (singleton)
		// Retry on cold boot: ipLocal may not be available immediately after
		// network-online.target. NET_DVR_GetLocalIP() may fail to find the
		// configured IP until the NIC driver and NetworkManager fully assign it.
		{
			const int maxRetries = 60;
			const int retryIntervalSec = 2;
			bool sdkReady = false;

			for (int i = 0; i < maxRetries; ++i) {
				if (HikSDKManager::getInstance().init(config.getIpLocal())) {
					sdkReady = true;
					break;
				}
				int errCode = HikSDKManager::getInstance().getLastError();
				logger.log("SDK init retry " + to_string(i + 1) + "/" + to_string(maxRetries)
				           + " error: " + to_string(errCode), Logger::PROD);
				std::cout << "SDK init retry " << (i + 1) << "/" << maxRetries
				          << " error: " << errCode << std::endl;
				std::this_thread::sleep_for(std::chrono::seconds(retryIntervalSec));
			}

			if (sdkReady) {
				logger.log("HikSDK initialized successfully", Logger::PROD);
				std::cout << "HikSDK initialized successfully" << std::endl;
			} else {
				int finalErr = HikSDKManager::getInstance().getLastError();
				logger.log("Hik SDK initialization failed after " + to_string(maxRetries)
				           + " retries, code:" + to_string(finalErr), Logger::PROD);
				std::cout << "Hik SDK initialization failed after " << maxRetries
				          << " retries, code:" << finalErr << std::endl;
			}
		}
		
		auto cameras = config.createCameras();
		config.createCameraFolders();

		for (auto& cam : cameras) {
			controller.addCamera(cam);
		}
        
		controller.startAll();
      std::cout << "cameras started" << std::endl;

		//test log
		logger.enableTestLogs(config.getTestLog());

       // Initialize and start the message listener
      boost::asio::io_service io_service; 
      MessageListener listener(io_service, 8055, controller);
       //listener.startListening();

		// Start listening for incoming connections in the main thread
		thread listener_thread([&listener]() {
			listener.startListening();
		});

        // Run the io_service in a separate thread to handle incoming messages asynchronously
      thread io_service_thread([&io_service]() {
          io_service.run();
      });
		
	   std::cout << "message listening and processing threads started" << std::endl;

		// Join both threads before quitting
		listener_thread.join();    // Ensure the listener thread completes
		io_service_thread.join();  // Ensure the io_service thread completes
		logger.log("Joined both threads before quitting" , Logger::PROD);

		// Clean up and stop listening when done
		listener.stopListening();  // Close the acceptor, unbind socket
		io_service.stop();         // Stop the io_service and complete all asynchronous work
		logger.log("Cleaned up and stop listening when done", Logger::PROD);

		//Stop all the cameras
		controller.stopAll();
		logger.log("Stopped all the cameras", Logger::PROD);

		// clean HikSDK globally (singleton)
		HikSDKManager::getInstance().cleanup();
		logger.log("Cleaned SDK Instance", Logger::PROD);

		// Black box: write a baseline dump before exit and print per-tag timing statistics
		bbDump("shutdown");
		bbPrintStats();
		bbStop();

    	// When the program is quitting, call stop to ensure the logger cleans up
		logger.log("Stopped Logger", Logger::PROD);
   		logger.stop();

    } catch (const std::runtime_error& e) {
		logger.log("Runtime error: " + string(e.what()),Logger::PROD);
		controller.stopAll();
		HikSDKManager::getInstance().cleanup();
		bbDump("runtime_error");
		bbStop();
		logger.stop();
    } catch (const std::exception& e) {
		logger.log("General exception: " + string(e.what()),Logger::PROD);
		controller.stopAll();
		HikSDKManager::getInstance().cleanup();
		bbDump("exception");
		bbStop();
		logger.stop();
    }

    return 0;
}
