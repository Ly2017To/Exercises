#include "HikCamera.h"
#include "blackbox.h"
#include "AeiActivity.h"
#include <iostream>
#include <fstream>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <sys/stat.h>  // For chmod

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
        Logger::getInstance().log("camera already login: " + name, Logger::PROD);
        return true;
    }

    NET_DVR_USER_LOGIN_INFO struLoginInfo = {0};
    memset(&struDeviceInfoV40, 0, sizeof(NET_DVR_DEVICEINFO_V40));
    struDeviceInfoV40.struDeviceV30.byStartChan = 1;
    struLoginInfo.bUseAsynLogin = false;

    struLoginInfo.wPort = port;
    strncpy(struLoginInfo.sDeviceAddress, ip.c_str(), NET_DVR_DEV_ADDRESS_MAX_LEN);
    strncpy(struLoginInfo.sUserName, username.c_str(), NAME_LEN);
    strncpy(struLoginInfo.sPassword, password.c_str(), NAME_LEN);

    const int retry_count = 10;
    const int retry_delay = 3;

    for (int i = 0; i < retry_count; ++i) {
        userId = NET_DVR_Login_V40(&struLoginInfo, &struDeviceInfoV40);

        if (userId >= 0) {
            Logger::getInstance().log("Camera connected:" + ip, Logger::PROD);

            // ========== New: sync the local Linux system time to the camera after login ==========
            // Note: NET_DVR_TIME members in Linux SDK 5.2 are dwYear/dwMonth/... (DWORD type)
            // First try to disable NTP (if the device supports it), then set the time manually
            DWORD dwErr = 0;
            DWORD dwBytesReturned = 0;

            // 1. Try to disable NTP (so the manual time setting is not overwritten by NTP)
            NET_DVR_NTPPARA struNtpCfg = {0};
            BOOL bGetNtp = NET_DVR_GetDVRConfig(
                userId,
                NET_DVR_GET_NTPCFG,
                -1,
                &struNtpCfg,
                sizeof(NET_DVR_NTPPARA),
                &dwBytesReturned
            );
            if (bGetNtp) {
                struNtpCfg.byEnableNTP = 0;
                NET_DVR_SetDVRConfig(
                    userId,
                    NET_DVR_SET_NTPCFG,
                    -1,
                    &struNtpCfg,
                    sizeof(NET_DVR_NTPPARA)
                );
            } else {
                dwErr = NET_DVR_GetLastError();
                Logger::getInstance().log("Get NTP config failed (may not supported), err:" + std::to_string(dwErr), Logger::PROD);
            }

            // 2. Get the local Linux system time
            NET_DVR_TIME struLocalTime = {0};
            time_t now = time(nullptr);
            struct tm* pLocalTm = localtime(&now);
            struLocalTime.dwYear    = pLocalTm->tm_year + 1900;
            struLocalTime.dwMonth   = pLocalTm->tm_mon + 1;
            struLocalTime.dwDay     = pLocalTm->tm_mday;
            struLocalTime.dwHour    = pLocalTm->tm_hour;
            struLocalTime.dwMinute  = pLocalTm->tm_min;
            struLocalTime.dwSecond  = pLocalTm->tm_sec;

            // 3. Send the time setting to the camera
            BOOL bSetTime = NET_DVR_SetDVRConfig(
                userId,
                NET_DVR_SET_TIMECFG,
                -1,
                &struLocalTime,
                sizeof(NET_DVR_TIME)
            );

            if (bSetTime) {
                std::string timeStr = std::to_string(struLocalTime.dwYear) + "-"
                                    + std::to_string(struLocalTime.dwMonth) + "-"
                                    + std::to_string(struLocalTime.dwDay) + " "
                                    + std::to_string(struLocalTime.dwHour) + ":"
                                    + std::to_string(struLocalTime.dwMinute) + ":"
                                    + std::to_string(struLocalTime.dwSecond);
                Logger::getInstance().log("Sync camera time success, time:" + timeStr, Logger::PROD);
            } else {
                dwErr = NET_DVR_GetLastError();
                Logger::getInstance().log("Sync camera time failed, err:" + std::to_string(dwErr), Logger::PROD);
            }
            // =================================================================

            return true;
        }

        Logger::getInstance().log(
            "Login retry " + std::to_string(i + 1) + "/" + std::to_string(retry_count)
            + " error: " + std::to_string(NET_DVR_GetLastError()),
            Logger::PROD
        );
        std::this_thread::sleep_for(std::chrono::seconds(retry_delay));
    }

    Logger::getInstance().log("Camera connected failed with error:" + std::to_string(NET_DVR_GetLastError()), Logger::PROD);
    return false;
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
	//auto t0 = std::chrono::high_resolution_clock::now();
    if (!login()) {
        //throw runtime_error("camera login failed, can not start camera thread: " + ip);
		Logger::getInstance().log("Camera login failed: "+ ip,Logger::PROD); 
	}else{
		Logger::getInstance().log("Camera login success: "+ ip,Logger::PROD); 
	}
	//auto t1 = std::chrono::high_resolution_clock::now();
	//auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
	//Logger::getInstance().log("[TIMING] Camera Login(): " + to_string(diff),Logger::PROD);
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
	// Black box: picture capture (incl. SDK acquisition and disk write) duration; also acts as the capture channel heartbeat
	BbSpan captureSpan("takePicture", 200);
	bbHeartbeat(BbChannel::PictureCapture);
	bbEvent("pic_start", 0);

	NET_DVR_JPEGPARA strPicPara = {0};    
    strPicPara.wPicQuality = 0;  
    strPicPara.wPicSize = 0;
	
	char picturePath[256];
	string picName;
	// Lock fileName before modifying
	{
		//std::lock_guard<std::mutex> lock(mtx); // Locking fileName comment 0.0.5
		// Increment picture count and create the file name
		++countPicture;
		if(countPicture>maxPicture){
			countPicture=1;
		}
		// Use ostringstream to format the strings with leading zeros
		ostringstream oss;
		// Format strTrain (3 digits), strCar (2 digits), strAxle (2 digits), and strPicture (4 digits)
		oss << std::setw(3) << std::setfill('0') << countTrain
		<< std::setw(2) << std::setfill('0') << countCar
        << std::setw(2) << std::setfill('0') << countAxle
        << "_" << std::setw(4) << std::setfill('0') << countPicture;  // Adding "_" between strAxle and strPicture
		string formattedStr = oss.str();
		//picName = fileName+utility.getFormattedTime()+"_"+formattedStr+".jpg";
		picName = fileName+pic_time+"_"+formattedStr+".jpg";
		memcpy(picturePath,picName.c_str(),picName.length());
		picturePath[min(picName.length(), sizeof(picturePath) - 1)] = '\0';
		picName.clear();
	}

    int iRet = NET_DVR_CaptureJPEGPicture(userId, 
                                         struDeviceInfoV40.struDeviceV30.byStartChan, 
                                         &strPicPara, 
                                         picturePath);
                                         
    if (!iRet) {
		Logger::getInstance().log("takePicture error, path:" + string(picturePath) + " err:" + to_string(NET_DVR_GetLastError()), Logger::PROD);
		return false;
	} else {
		// D1 log noise reduction: summarize every 10 successful shots into one line
		// (failure logs are still recorded immediately, one per entry), reducing the
		// PROD log write traffic while a train is passing (previously one line per
		// picture, which was the largest log source)
		if (++picLogCounter >= 10) {
			picLogCounter = 0;
			Logger::getInstance().log("takePicture success x10, last: " + string(picturePath), Logger::PROD);
		}
        return true;
   }
}

//picture taking thread
void HikCamera::startPictureTaking() {
	
	//time_t trigger_on_time = 0;

    try {
		
		/*
		trigger_on_time = time(nullptr);

		pic_time.clear();
		pic_time = utility.getFormattedTime();

		fileNameStart = prefix+to_string(command[commandDir])+pic_time+"0000.str";
						
		file.open(fileNameStart, ios::out);
		fileNameStart.clear();					
		if(file.is_open()){
			file.close();
		}

		std::lock_guard<std::mutex> lock(mtx); // Locking fileName
		currentTime = time(nullptr);
		currentTM = localtime(&currentTime);
		// Check if the day has changed (midnight)
		if (currentTM->tm_yday != lastResetTM->tm_yday) {
			// Reset countTrain at a new day
			countTrain = 1;
			lastResetTime = currentTime;  // Update last reset time
			lastResetTM = currentTM;      // Update last reset date
		}else{
			++countTrain;
			if(countTrain>maxTrain){
				countTrain=1;					
			}
		}
		fileName.clear();
		fileName = prefix+to_string(command[commandDir]);
		*/
        while (pictureThreadRunning) {
            takePicture();
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
		string station = to_string(aeiStation);
		station.insert(0, 2 - station.length(), '0');  // Insert leading zeros
		prefix = folder + "B"+ telegraphic + station + to_string(code) + to_string(path);
		ofstream file;

		// Store the current day for comparison (midnight reset check)
		time_t currentTime, lastResetTime;
		lastResetTime = time(nullptr);
		tm* currentTM, *lastResetTM;
		lastResetTM = localtime(&lastResetTime);

		time_t trigger_on_time = 0;
		const int timeout_sec = 180;

		std::chrono::steady_clock::time_point lastCaptureTime; 
		bool hasCommand = false;

	    int preCountCar = 1, axlesSum = 0;
		vector<int> axlesVec = {};
		// ALERT criteria: count and max gap of "WHEEL-only silence" (tail / waiting for command) within a session
		int silentWheelOnlyCount = 0;
		long long silentWheelOnlyMax = 0;

		// ---- Black box: observation state for this thread ----
		bbArm(BbChannel::CameraLoop);   // heartbeat channel of the command/capture loop (the watchdog can sense when it gets stuck)
		bbEvent("camera_thread_start", 0);
		// C: UPDATE stream-stall alert (observation only; does not change capture behavior)
		std::chrono::steady_clock::time_point lastUpdateTime{};
		bool updateStallAlertActive = false;
		// D2: cache the direction byte and trigger time from TRIGGERON for the 3-minute timeout branch
		unsigned char triggerDir = 0;
		string triggerPicTime;

       while (!stopFlag) {
         	string command;
			hasCommand = false;
			bbHeartbeat(BbChannel::CameraLoop);
          {
           	unique_lock<std::mutex> lock(mtxQueue);
          		if(cv.wait_for(lock, std::chrono::milliseconds(10), [this] { return !commandQueue.empty() || stopFlag; })){
					if (stopFlag) break;
					command = commandQueue.front();
            		commandQueue.pop();
					hasCommand = true;
				}
          }


			if(hasCommand){
				switch(command[commandType]){
					case UPDATE:
						Logger::getInstance().log("receive UPDATE command: " +ip,Logger::PROD);					
						// ---- Observation: record when this UPDATE was processed (used by the C stall alert and the black-box timeline) ----
						lastUpdateTime = std::chrono::steady_clock::now();
						updateStallAlertActive = false;
						bbHeartbeat(BbChannel::CameraLoop);
						// Lock fileName before modifying
						//{
						//std::lock_guard<std::mutex> lock(mtx); // Locking fileName comment 0.0.5

						countCar=static_cast<int>(command[commandCarNum]);

						if(countCar>preCountCar){
								axlesVec.push_back(countAxle-axlesSum);
								axlesSum = countAxle;
						}

						preCountCar = countCar;

						if(countCar>maxCar){
							countCar=1;					
						}
						++countAxle;
						if(countAxle>maxAxle){
							countAxle=1;					
						}
						fileName.clear();
						fileName = prefix+to_string(command[commandDir]);
						//}
						bbEvent("update_done", 0);
						break;
					case TRIGGERON:
						Logger::getInstance().log("trigger on camera: " +ip,Logger::PROD);
						//add to check login status
 						if (userId == -1) {
                        	if (!login()) {
                      		Logger::getInstance().log("TRIGGERON failed: camera offline", Logger::PROD);
                   		}
                	}
						//cancel it, because serial port is going to do this
						NET_DVR_SetAlarmOut(userId,0,1); //output 1, turn the light on 
						trigger_on_time = time(nullptr);
						bbEvent("trigger_on", 0);

						if (!pictureThreadRunning) {
							pic_time.clear();
							//pic_time = utility.getFormattedTime();
							pic_time = utility.getFormattedTimeFromBCD(command);

							// D2: cache the direction byte and trigger time for safe use by the 3-minute timeout branch
							triggerDir = command[commandDir];
							triggerPicTime = pic_time;

							fileNameStart = prefix+to_string(command[commandDir])+pic_time+"0000.str";

							//fileNameStart = prefix+to_string(command[commandDir])+utility.getFormattedTime()	+"0000.str";
						
							{	// A2: keep the existing synchronous .str write, only measure its duration (>50ms records an event, >2s triggers a black-box dump)
								BbSpan strSpan("str_write_on", 50);
								file.open(fileNameStart, ios::out);
								fileNameStart.clear();					
								if(file.is_open()){
									file.close();
								}
							}
						}
							
						// Lock fileName before modifying
						if (!pictureThreadRunning) {
							//std::lock_guard<std::mutex> lock(mtx); // Locking fileName comment 0.0.5
							currentTime = time(nullptr);
							currentTM = localtime(&currentTime);
							// Check if the day has changed (midnight)
							if (currentTM->tm_yday != lastResetTM->tm_yday) {
								// Reset countTrain at a new day
								countTrain = 1;
								lastResetTime = currentTime;  // Update last reset time
								lastResetTM = currentTM;      // Update last reset date
							}else{
								++countTrain;
								if(countTrain>maxTrain){
									countTrain=1;					
								}
							}
							fileName.clear();
							fileName = prefix+to_string(command[commandDir]);
							preCountCar = 1;
							axlesVec.clear();
							axlesSum = 0;
						}

						if (!pictureThreadRunning) {
                    		// Start a thread to take pictures
							//Logger::getInstance().log("Start picture taking thread.",Logger::PROD);
							//pictureThread = std::thread(&HikCamera::startPictureTaking, this);
							pictureThreadRunning = true;
							bbArm(BbChannel::PictureCapture);   // capture channel enters its active period; the watchdog starts patrolling it
							Logger::getInstance().log("Start picture taking function.",Logger::PROD);
							takePicture();
							lastCaptureTime = std::chrono::steady_clock::now();
						}

						break;
					case TRIGGEROFF:

						trigger_on_time = 0;
						bbEvent("trigger_off", 0);
						Logger::getInstance().log("trigger off camera: " +ip,Logger::PROD);
						NET_DVR_SetAlarmOut(userId,0,0); //output 0, turn the light off
					
				    	if (pictureThreadRunning) {
                        	// Stop the picture-taking thread
							// Logger::getInstance().log("Stop picture taking thread.",Logger::PROD);
							// stopPictureTaking();
							Logger::getInstance().log("Stop picture taking function.",Logger::PROD);
							pictureThreadRunning = false;
					
							//fileNameEnd = prefix+to_string(command[2])+utility.getFormattedTime()+"9999.str";
							fileNameEnd = prefix+to_string(command[2])+pic_time+"9999.str";						
							{	// A2: keep the existing synchronous .str write, only measure its duration (>50ms records an event, >2s triggers a black-box dump)
								BbSpan strSpan("str_write_off", 50);
								file.open(fileNameEnd, ios::out);
								chmod(fileNameEnd.c_str(), 0777);
								axlesVec.push_back(countAxle-axlesSum);
								if(file.is_open()){
									for(int num:axlesVec){
										file << num << '\n';
										//cout << num << endl;								
									}
									file.flush();
									file.close();
								}
							}
							fileNameEnd.clear();
					
							//std::lock_guard<std::mutex> lock(mtx); // Locking fileName comment 0.0.5
							countAxle=0;
							countPicture=0;
							countCar=1;
						}
						bbDisarm(BbChannel::PictureCapture);   // capture channel leaves its active period

						// L2 session summary: n=1 is usually tail silence (normal); n>=2
						// hints at repeated WHEEL gaps during the active period = likely a
						// genuine stall; investigate with the multi_silent_wheel dump.
						Logger::getInstance().log(
							"session silent-wheel-only: n=" + std::to_string(silentWheelOnlyCount)
							+ " max=" + std::to_string(silentWheelOnlyMax) + "ms, camera: " + ip,
							Logger::PROD);
						silentWheelOnlyCount = 0;
						silentWheelOnlyMax = 0;

						break;
					default:
						//cout << " received an unknown command.\n";
						Logger::getInstance().log("received an unknown command. Camera: " + ip,Logger::PROD); 
						break;
				}
			}

			// C: UPDATE stream-stall alert (observation only; does not change capture behavior)
			// With the active capture-stop protection dropped, this is the only immediate
			// signal when the issue recurs. The criterion is now dual-layer:
			//   L1 (true stall): UPDATE silent > 3500ms AND the event source fully
			//       silent > 8000ms -> log alert + dump
			//   L2 (tail / waiting-for-TRIGGEROFF): UPDATE silent > 3500ms but the
			//       event source still communicates (INQUIRY etc.) -> bbEvent +
			//       counting only, no log spam, no dump
			// If L2 count >= 2 within a session, dump once (multi_silent_wheel)
			// to preserve the evidence.
			if (pictureThreadRunning && lastUpdateTime.time_since_epoch().count() != 0) {
				long long updateGapMs = std::chrono::duration_cast<std::chrono::milliseconds>(
					std::chrono::steady_clock::now() - lastUpdateTime).count();
				// N1: threshold 2000ms -> 3500ms. A long axle (8.75m @ 12.55km/h) produces a 2.51s
				// physical no-UPDATE window, which would otherwise be misread as a stall.
				// The truly-stuck case (physical window < 1s but 2s+ of silence) is unaffected.
				if (updateGapMs > 3500) {
					if (!updateStallAlertActive) {
						updateStallAlertActive = true;
						long long aeiAgeMs = AeiActivity::nowSteadyMs()
							- AeiActivity::lastAnyMsgMs().load(std::memory_order_relaxed);
						if (aeiAgeMs > 8000) {
							// L1: genuine stall, neither the event-source link nor its thread responds
							Logger::getInstance().log(
								"ALERT(L1) UPDATE stalled " + to_string(updateGapMs)
								+ "ms, aei_silent=" + to_string(aeiAgeMs)
								+ "ms, camera: " + ip, Logger::PROD);
							bbEvent("update_stall_L1", updateGapMs);
							// The reason encodes the event-source silence duration;
							// per-reason dump rate-limiting buckets on it
							std::string dumpReason = std::string("update_stall_L1:aei")
								+ std::to_string(aeiAgeMs);
							bbDump(dumpReason.c_str());
						} else {
							// L2: WHEEL-only silence, event source still communicating (tail / waiting for TRIGGEROFF)
							++silentWheelOnlyCount;
							if (updateGapMs > silentWheelOnlyMax) {
								silentWheelOnlyMax = updateGapMs;
							}
							bbEvent("update_silent_wheel_only", updateGapMs);
							if (silentWheelOnlyCount >= 2) {
								bbDump("multi_silent_wheel");
							}
						}
					}
				} else {
					updateStallAlertActive = false;
				}
			}

			//add logic to check if trigger on but not off within 3min
			if (pictureThreadRunning && trigger_on_time != 0) {

				time_t now = time(nullptr);
				if (now - trigger_on_time >= timeout_sec) {
					Logger::getInstance().log("Timeout 3min, auto trigger off: " + ip, Logger::PROD);
					
					//power off
					NET_DVR_SetAlarmOut(userId, 0, 0);
					//stopPictureTaking();
					pictureThreadRunning = false;

					trigger_on_time = 0;
					bbEvent("trigger_timeout", 0);
					// D2 fix: the original code indexed the empty command[2] of this loop
					// iteration (out-of-bounds index, undefined behavior); now it uses the
					// direction byte and trigger time cached at TRIGGERON
					// fileNameEnd = prefix+to_string(command[2])+utility.getFormattedTime()+"9999.str";
 					fileNameEnd = prefix+to_string(triggerDir)+triggerPicTime+"9999.str";
					{	// A2: keep the existing synchronous .str write, only measure its duration
						BbSpan strSpan("str_write_timeout", 50);
						file.open(fileNameEnd, ios::out);
						chmod(fileNameEnd.c_str(), 0777);
						axlesVec.push_back(countAxle-axlesSum);
						if(file.is_open()){
							for(int num:axlesVec){
								file << num << '\n';
								//cout << num << endl;									
							}
							file.flush();
							file.close();
						}
					}
					fileNameEnd.clear();
					
					//{
					//std::lock_guard<std::mutex> lock(mtx); // Locking fileName
					countAxle=0;
					countPicture=0;
					countCar=1;
					//}
					bbDisarm(BbChannel::PictureCapture);
				}
			}
			
			if(pictureThreadRunning) {

				auto now = std::chrono::steady_clock::now();
    			auto interval = std::chrono::milliseconds(100);

				if (now - lastCaptureTime >= interval){
					lastCaptureTime = now;
            		takePicture();
				}
        	}
       }
    } catch (const exception& e) {
        //cerr << "Exception in processCommands: " << e.what() << std::endl;
		Logger::getInstance().log("Exception in processCommands: " + string(e.what()),Logger::PROD); 
    }

	// ---- Black box: thread teardown (disarm so the watchdog does not report a false stall after the thread exits) ----
	bbEvent("camera_thread_end", 0);
	bbDisarm(BbChannel::CameraLoop);
	bbDisarm(BbChannel::PictureCapture);
}
