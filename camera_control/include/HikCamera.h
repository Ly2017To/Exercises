#ifndef HIKCAMERA_H
#define HIKCAMERA_H

#include "Camera.h"
#include <string>
#include <iostream>
#include "HCNetSDK.h"
#include "Utility.h"
#include "logger.h"

using namespace std;

class HikCamera : public Camera {
public:
    // Constructor that initializes the HikCamera based on inherited configuration
    HikCamera();

    // Destructor to clean up resources
    ~HikCamera() override; 

    // Override processCommands for HikCamera-specific command handling
    void processCommands() override;

    // Override start to add login process
    void start() override;

    // Override stop to add logout process
    void stop() override;
	    
private:
    // Login to the Hikvision camera
    bool login();

    // Logout from the Hikvision camera
    void logout();
	
	// Capture a picture and save it to the specified file path
    bool takePicture();

	// start picture taking thread
	void startPictureTaking();
	
	// stop picture taking thread
	void stopPictureTaking();

    long userId = -1;  // Store the user ID for the camera connection
    bool pictureThreadRunning = false; // Track if the picture-taking thread is running
    thread pictureThread; // Thread for handling picture taking
    NET_DVR_DEVICEINFO_V40 struDeviceInfoV40{}; // Store the device information

   	int countPicture=0;
	string prefix, fileName, fileNameStart, fileNameEnd;

	// Mutex for variables related to picture name
    mutex mtx;

	// Create an object of Utility class
	Utility utility = Utility::createInstance();
};

#endif // HIKCAMERA_H

