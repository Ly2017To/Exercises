#ifndef CAMERA_H
#define CAMERA_H

#include <iostream>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <string>
#include <cstring>

class Camera {
public:
    // Constructor accepting camera name
    Camera();

    // Virtual destructor for safe polymorphic deletion
    virtual ~Camera();

    // Virtual methods
    virtual void start();
    virtual void stop();

    // Set configuration parameters for the camera
    void setCameraParams(const std::string& name, bool enable, const std::string& telegraphic,
                        int aeiStation, const std::string& type,
                         int code, int path, const std::string& ip, int port,
                         const std::string& username, const std::string& password, const std::string& folder);

    // Function to add commands to the queue
    void addCommand(const std::string& command);

    // Get camera name
    std::string getName() const { return name; }

    // Get the camera path
	int getPath() const {return path;}

	// Get the camera IP
	std::string getIP() const {return ip;} 

protected:
    // Camera attributes
	std::string name;
    bool enable;
    std::string telegraphic;
    int  aeiStation;
    std::string type;
    int code;
    int path;
    std::string ip;
    int port;
    std::string username;
    std::string password;
	std::string folder;

    // Atomic flag for controlling thread termination
    std::atomic<bool> stopFlag;

    // Queue for commands
    std::queue<std::string> commandQueue;

    // Mutex for thread-safe queue access
    std::mutex mtxQueue;

    // Condition variable for synchronization
    std::condition_variable cv;

    // Thread to handle camera operations
    std::thread cameraThread;

    // Abstract method for derived classes to process commands
    virtual void processCommands() = 0;
};

#endif // CAMERA_H

