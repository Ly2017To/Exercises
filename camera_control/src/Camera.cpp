#include "Camera.h"
using namespace std;

Camera::Camera() : stopFlag(false) {
    // DO NOT start the camera thread in the constructor
    // cameraThread = std::thread(&Camera::processCommands, this); // Not starting the thread here
}

Camera::~Camera() {
    stop();  // Ensure the camera thread is properly stopped and joined during destruction
}

void Camera::addCommand(const string& command) {
    unique_lock<mutex> lock(mtxQueue);  // Locking the mutex for thread-safe access to the command queue
    commandQueue.push(command);
    //cv.notify_all();  // Notify the camera thread that a new command has been added
	cv.notify_one();
}

void Camera::start() {
    // Start the camera thread after construction, but only if it’s not already running
    if (!cameraThread.joinable()) {
        stopFlag = false;
        cameraThread = thread(&Camera::processCommands, this);  // Start processing commands in the background
    }
}

void Camera::stop() {
    // Stop the camera thread after the work is done
    stopFlag = true;
    cv.notify_all();  // Notify any waiting threads to exit

    // Ensure the camera thread finishes before destructing the object
    if (cameraThread.joinable()) {
        cameraThread.join();  // Wait for the camera thread to finish execution
    }
}

void Camera::setCameraParams(const std::string& name, bool enable, const std::string& type, const std::string& ip, int port, const std::string& username, const std::string& password, const std::string& folder) {
    // Setting the camera configuration parameters
    this->name = name;
    this->enable = enable;
    this->type = type;
    this->ip = ip;
    this->port = port;
    this->username = username;
    this->password = password;
	this->folder = folder;
}

