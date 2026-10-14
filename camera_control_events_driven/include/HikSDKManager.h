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

	//initialize with binding IP
	bool init(const std::string& ipLocal) {

		//cancel Mac address binding
        //int nDisableMacBind = 1;
       //NET_DVR_SetSDKInitCfg((NET_SDK_INIT_CFG_TYPE)306, &nDisableMacBind);
 
      std::lock_guard<std::mutex> lock(mtx);
        if (isInitialized) return true;

        if (!NET_DVR_Init()) {
          lastError = NET_DVR_GetLastError();
            return false;
      }

    	if (ipLocal.empty()) {
			Logger::getInstance().log("ipLocal is empty, cannot bind NIC",Logger::PROD);
			std::cout << "ipLocal is empty, cannot bind NIC " << std::endl;
			NET_DVR_Cleanup();
            return false;
      }else{
			Logger::getInstance().log("to bind IP: "+ ipLocal,Logger::PROD);
			std::cout << "to bind IP: "+ ipLocal << std::endl;
		}

	  	char localIps[16][16] = {0};  
    	DWORD validIpCount = 0;      
    	BOOL enableBind[16] = {0};

    	BOOL bGetIp = NET_DVR_GetLocalIP(localIps, &validIpCount, enableBind);
    	if (!bGetIp || validIpCount == 0) {
      		Logger::getInstance().log("fail to get local ip",Logger::PROD);
      		NET_DVR_Cleanup();
        	return false;
    	}

    	int targetIndex = -1;
    	for (DWORD i = 0; i < validIpCount; i++) {
        	if (strcmp(localIps[i], ipLocal.c_str()) == 0) {
      			targetIndex = i;
            	break;
      		}
    	}

    	if (targetIndex == -1) {
      		Logger::getInstance().log("can not find ipLocal: " + ipLocal, Logger::PROD);
        	NET_DVR_Cleanup();
        	return false;
    	}

    	// Enable binding to this IP
    	BOOL bSet = NET_DVR_SetValidIP(targetIndex, TRUE);
    	if (!bSet) {
			Logger::getInstance().log("IP bind faild: "+ to_string(NET_DVR_GetLastError()),Logger::PROD);
      		std::cout << "IP bind faild: "+to_string(HikSDKManager::getInstance().getLastError()) << std::endl;
			NET_DVR_Cleanup();
        	return false;
    	}else{
			Logger::getInstance().log("IP bind success",Logger::PROD);
			std::cout << "IP bind success" << std::endl;
		}

      isInitialized = true;
      NET_DVR_SetConnectTime(2000, 1);
      NET_DVR_SetReconnect(10000, true);
        return true;
    }

    // initialize SDK
    bool init() {

		//cancel the Mac address bind
		int nDisableMacBind = 1;
		NET_DVR_SetSDKInitCfg((NET_SDK_INIT_CFG_TYPE)306, &nDisableMacBind);

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

		NET_DVR_SetConnectTime(2000, 1);
		NET_DVR_SetReconnect(10000, true);
		
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

