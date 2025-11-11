#ifndef HIK_SDK_MANAGER_H
#define HIK_SDK_MANAGER_H

#include "HCNetSDK.h"
#include <mutex>
#include <atomic>
#include <iostream>

class HikSDKManager {
public:
    // forbid to copy and move
    HikSDKManager(const HikSDKManager&) = delete;
    HikSDKManager& operator=(const HikSDKManager&) = delete;
    HikSDKManager(HikSDKManager&&) = delete;
    HikSDKManager& operator=(HikSDKManager&&) = delete;

    // get singleton
    static HikSDKManager& getInstance() {
        static HikSDKManager instance;
        return instance;
   }

    // initialize SDK
    bool init() {
		std::lock_guard<std::mutex> lock(mtx);
        if (isInitialized) return true; 

        if (!NET_DVR_Init()) {
			lastError = NET_DVR_GetLastError();
          	//std::cerr << "HikSDK initialization failed with error code: " << lastError << std::endl;
            return false;
		}

        // set logs for SDK
		// NET_DVR_SetLogToFile(3, "./hik_sdk_log", false);
		isInitialized = true;
		//std::cout << "HikSDK initialization successfully" << std::endl;
		return true;
    }

    // clean SDK 
    void cleanup() {
		std::lock_guard<std::mutex> lock(mtx);
        if (!isInitialized) return;

		NET_DVR_Cleanup();
		isInitialized = false;
		//std::cout << "HikSDK is already cleaned" << std::endl;
	}

    // check whether SDK is initialized successfully
    bool isReady() const { return isInitialized; }

    // get initialization error
    int getLastError() const { return lastError; }

private:
	HikSDKManager() = default;  
	~HikSDKManager() { cleanup(); } 

	std::mutex mtx;
	std::atomic<bool> isInitialized{false}; 
    int lastError = 0;
};

#endif // HIK_SDK_MANAGER_H

