#include "MainController.h"
#include "Camera.h"
#include <iostream>
#include <algorithm>
using namespace std;

MainController::MainController() {
     // initialize any required member variables here if needed
    //cout << "MainController initialized." << endl;
	Logger::getInstance().log("MainController initialized.",Logger::PROD);
}

// Add a camera to the cameras list
void MainController::addCamera(const shared_ptr<Camera>& camera) {
    // Check if the camera already exists in the list
	std::lock_guard<std::mutex> lock(mtx);
    auto it = find_if(cameras.begin(), cameras.end(), 
                           [&camera](const shared_ptr<Camera>& existingCamera) {
                               return existingCamera->getName() == camera->getName();
                           });

    if (it == cameras.end()) {
        cameras.push_back(camera);
        //cout << "Camera " << camera->getName() << " added successfully." << endl;
		Logger::getInstance().log("Camera added successfully: "+camera->getName(),Logger::PROD);
    } else {
        //cout << "Error: Camera with name " << camera->getName() << " already exists." <<endl;
		Logger::getInstance().log("Error camera: "+camera->getName() + "already exists",Logger::PROD);
    }
}

/*
void MainController::sendCommandToCamera(const string& cameraName, const string& command) {
    auto it = find_if(cameras.begin(), cameras.end(), 
                           [&cameraName](const shared_ptr<Camera>& camera) {
                               return camera->getName() == cameraName;
                           });

    if (it != cameras.end()) {
        (*it)->addCommand(command);  
        cout << "Command \"" << command << "\" sent to camera: " << cameraName << endl;
    } else {
        cout << "Error: Camera with name " << cameraName << " not found." << endl;
    }
}
*/

void MainController::sendCommandToCamera(const string& command) {
	int cameraPath = static_cast<int>(command[commandPath]);
    auto it = find_if(cameras.begin(), cameras.end(), 
                           [&cameraPath](const shared_ptr<Camera>& camera) {
                               return camera->getPath() == cameraPath;
                           });

    if (it != cameras.end()) {
        (*it)->addCommand(command);  
          //cout << "Command sent to path: " << cameraPath << endl;
    } else {
         //cout << "Error: Camera with path " << cameraPath << " not found." << endl;
		 Logger::getInstance().log("Error: Camera with path "+ to_string(cameraPath) + "not found",Logger::PROD);
    }
}

void MainController::startAll()
{
	std::lock_guard<std::mutex> lock(mtx);
    for (auto& cam : cameras) {
		if (cam) {
			try {
				cam->start();
			} catch (const std::exception& e) {
				//std::cerr << "camera" << cam->getName() << "start failed: " << e.what() << std::endl;
				Logger::getInstance().log("Camera" + cam->getName() + "start failed: " + string(e.what()),Logger::PROD);
			}
		}
	}
}

void MainController::stopAll()
{
	std::lock_guard<std::mutex> lock(mtx);
	for (auto& cam : cameras) {
		if (cam) {
			cam->stop();
		}
	}
} 

