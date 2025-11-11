#include <boost/asio.hpp>
#include <iostream>
#include "MessageListener.h"
#include "MainController.h"
#include "HikCamera.h"
#include "HikSDKManager.h"
#include "Configurations.h"
#include "logger.h"

using namespace std;

int main() {

	std::cout << "Camera Control Program Starts, version 0.0.0" << std::endl;

	// Instantiate the Logger singleton
	Logger& logger = Logger::getInstance();

	logger.log("Camera Control Program Starts, version 0.0.0",Logger::PROD);
	
	// initialize HikSDK manager globally (singleton)
	if (!HikSDKManager::getInstance().init()) {
		logger.log("Hik SDK initialization failed with code:"+to_string(HikSDKManager::getInstance().getLastError()),Logger::PROD);
		std::cout << "Hik SDK initialization failed with code:"+to_string(HikSDKManager::getInstance().getLastError()) << std::endl;
	}else{
		logger.log("HikSDK initialized successfully",Logger::PROD);
		std::cout << "HikSDK initialized successfully" << std::endl;
	} 

	// Create the controller that manages the cameras
	MainController controller;

    try {

    	Configurations config("../configuration/camera_config.json");
		std::cout << "finish configuration" << std::endl;
		
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

    	// When the program is quitting, call stop to ensure the logger cleans up
		logger.log("Stopped Logger", Logger::PROD);
   		logger.stop();

    } catch (const std::runtime_error& e) {
		logger.log("Runtime error: " + string(e.what()),Logger::PROD);
		controller.stopAll();
		HikSDKManager::getInstance().cleanup();
		logger.stop();
    } catch (const std::exception& e) {
		logger.log("General exception: " + string(e.what()),Logger::PROD);
		controller.stopAll();
		HikSDKManager::getInstance().cleanup();
		logger.stop();
    }

    return 0;
}
