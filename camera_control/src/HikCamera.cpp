#include "HikCamera.h"
#include <iostream>
#include <fstream>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

using namespace std;

//Constructor
HikCamera::HikCamera() {
    // Default constructor implementation
    // Initialize the camera or set default values
}

//Destructor
HikCamera::~HikCamera() {
    stop();
}

bool HikCamera::login() {
	if (userId != -1) {
		//cout << "camera already login: " << name << endl;
		Logger::getInstance().log("camera already login: "+name,Logger::PROD);
        return true;
	}

	NET_DVR_USER_LOGIN_INFO struLoginInfo = {0};
	struDeviceInfoV40 = {0};
    struDeviceInfoV40.struDeviceV30.byStartChan = 1;
	struLoginInfo.bUseAsynLogin = false;

	struLoginInfo.wPort = port;
	memcpy(struLoginInfo.sDeviceAddress, ip.c_str(), NET_DVR_DEV_ADDRESS_MAX_LEN);
	memcpy(struLoginInfo.sUserName, username.c_str(), NAME_LEN);
	memcpy(struLoginInfo.sPassword, password.c_str(), NAME_LEN);
	
	userId = NET_DVR_Login_V40(&struLoginInfo, &struDeviceInfoV40);

	if (userId < 0){
		//cout << "pyd1---Login error " << NET_DVR_GetLastError() << endl;
		Logger::getInstance().log("Camera connected failed with error:" + to_string(NET_DVR_GetLastError()),Logger::PROD);
        //throw runtime_error("Camera connected failed.");
		return false;
	}else{
		//cout << "Camera connected successfully: " << ip << endl;
		Logger::getInstance().log("Camera connected:" + ip,Logger::PROD);
	}
    return true;
}

// logout camera
void HikCamera::logout() {
    if (userId != -1) {
		NET_DVR_Logout_V30(userId);
		userId = -1;
		//cout << "camera logout successfully: " << ip << endl;
		Logger::getInstance().log("camera logout successfully: " + ip,Logger::PROD);
   }
}

// override start: first login, then start camera thread
void HikCamera::start() {
    if (!login()) {
        //throw runtime_error("camera login failed, can not start camera thread: " + ip);
		Logger::getInstance().log("Camera login failed: "+ ip,Logger::PROD); 
	}else{
		Logger::getInstance().log("Camera login success: "+ ip,Logger::PROD); 
	}
	Camera::start();
}

// override stop: first stop camera thread, then logout
void HikCamera::stop() {
    Camera::stop();
    logout();
	stopPictureTaking();
}

//method for taking pictures
bool HikCamera::takePicture() {
	NET_DVR_JPEGPARA strPicPara = {0};    
    strPicPara.wPicQuality = 0;  
    strPicPara.wPicSize = 0;
	
	char picturePath[256];
	string picName;
	// Lock fileName before modifying
	{
		std::lock_guard<std::mutex> lock(mtx); // Locking fileName
		// Increment picture count and create the file name
		++countPicture;
		if(countPicture>maxPicture){
			countPicture=1;
		}
		// Use ostringstream to format the strings with leading zeros
		ostringstream oss;
		// Format strPicture (4 digits)
		oss << "_" << std::setw(4) << std::setfill('0') << countPicture;  // Adding "_" between strAxle and strPicture
		string formattedStr = oss.str();
		picName = fileName+utility.getFormattedTime()+"_"+formattedStr+".jpg";
		memcpy(picturePath,picName.c_str(),picName.length());
		picturePath[min(picName.length(), sizeof(picturePath) - 1)] = '\0';
		picName.clear();
	}

    int iRet = NET_DVR_CaptureJPEGPicture(userId, 
                                         struDeviceInfoV40.struDeviceV30.byStartChan, 
                                         &strPicPara, 
                                         picturePath);
                                         
    if (!iRet) {
		//cout << "pyd1---NET_DVR_CaptureJPEGPicture error " << NET_DVR_GetLastError() << endl;
		return false;
	} else {
		//cout << "Camera capture JPEGPicture successfully: " << ip << endl;
        return true;
   }
}

//picture taking thread
void HikCamera::startPictureTaking() {
    try {
        while (pictureThreadRunning) {
            if (!takePicture()) {
				//Logger::getInstance().log("Failed to take picture in thread.");
                break;
          }
          std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Wait for some time before taking the next picture
        }
    } catch (const exception& e) {
       Logger::getInstance().log("Exception in startPictureTaking: " + string(e.what()),Logger::PROD);
    }
}

//picture taking thread join
void HikCamera::stopPictureTaking() {
	if (pictureThreadRunning) {
		pictureThreadRunning = false;
        if (pictureThread.joinable()) {
            pictureThread.join();
      }
      //Logger::getInstance().log("Stopped picture taking thread.",Logger::PROD);
	}
}

// Override process commands
void HikCamera::processCommands() {

    try {
		prefix = folder;
		ofstream file;

		// Store the current day for comparison (midnight reset check)
		time_t currentTime, lastResetTime;
		lastResetTime = time(nullptr);
		tm* currentTM, *lastResetTM;
		lastResetTM = localtime(&lastResetTime);

        while (!stopFlag) {
          string command;
          {
           	unique_lock<std::mutex> lock(mtxQueue);
          	cv.wait(lock, [this] { return !commandQueue.empty() || stopFlag; });
			if (stopFlag) break;
			command = commandQueue.front();
            commandQueue.pop();
          }

			switch(command[commandType]){
				case UPDATE:
					Logger::getInstance().log("receive UPDATE command: " +ip,Logger::TEST);					
					// Lock fileName before modifying
					{
						std::lock_guard<std::mutex> lock(mtx); // Locking fileName
						fileName.clear();
						fileName = prefix;
					}
					break;
				case TRIGGERON:
					Logger::getInstance().log("trigger on camera: " +ip,Logger::PROD);
					NET_DVR_SetAlarmOut(userId,0,1); //output 1, turn the light on 
					
					fileNameStart = prefix+utility.getFormattedTime()+"start.str";
					file.open(fileNameStart, ios::out);
					fileNameStart.clear();					
					if(file.is_open()){
						file.close();
					}
							
					// Lock fileName before modifying
					{
						std::lock_guard<std::mutex> lock(mtx); // Locking fileName
						currentTime = time(nullptr);
						currentTM = localtime(&currentTime);
						// Check if the day has changed (midnight)
						if (currentTM->tm_yday != lastResetTM->tm_yday) {
							lastResetTime = currentTime;  // Update last reset time
							lastResetTM = currentTM;      // Update last reset date
						}
						fileName.clear();
						fileName = prefix;
					}

					if (!pictureThreadRunning) {
                    	// Start a thread to take pictures
						Logger::getInstance().log("Start picture taking thread.",Logger::PROD);
						pictureThreadRunning = true;
						pictureThread = std::thread(&HikCamera::startPictureTaking, this);
					}

					break;
				case TRIGGEROFF:
					Logger::getInstance().log("trigger off camera: " +ip,Logger::PROD);
					NET_DVR_SetAlarmOut(userId,0,0); //output 0, turn the light off
					
				    if (pictureThreadRunning) {
                        // Stop the picture-taking thread
						Logger::getInstance().log("Stop picture taking thread.",Logger::PROD);
						stopPictureTaking();
					}
					fileNameEnd = prefix+utility.getFormattedTime()+"end.str";
					file.open(fileNameEnd, ios::out);
					fileNameEnd.clear();
					if(file.is_open()){
						file.close();
					}
					
					{
						std::lock_guard<std::mutex> lock(mtx); // Locking fileName
						countPicture=0;
					}

					break;
				default:
					//cout << " received an unknown command.\n";
					Logger::getInstance().log("received an unknown command. Camera: " + ip,Logger::PROD); 
					break;
			}
       }
    } catch (const exception& e) {
        //cerr << "Exception in processCommands: " << e.what() << std::endl;
		Logger::getInstance().log("Exception in processCommands: " + string(e.what()),Logger::PROD); 
    }
}
