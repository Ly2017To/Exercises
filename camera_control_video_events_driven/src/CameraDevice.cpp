#include "CameraDevice.h"
#include "Logger.h"
#include "Utils.h"

#include "HCNetSDK.h"

#include <cstring>

CameraDevice::CameraDevice(const CameraConfig& cfg)
    : cfg_(cfg)
{
}

CameraDevice::~CameraDevice()
{
    stopRecord();
    logout();
}

bool CameraDevice::login()
{
    if (userId_ >= 0)
        return true; // already logged in

    // Backoff: avoid log spam when monitorTick (1Hz enqueue) and startRecord
    // trigger login attempts on adjacent ticks. Within 5s, the second and
    // subsequent calls silently return false (no log, no counter increment).
    const std::time_t now = std::time(nullptr);
    if (lastLoginAttempt_ != 0 && now - lastLoginAttempt_ < 5)
    {
        return false;
    }
    lastLoginAttempt_ = now;

    if (cfg_.ip.empty() || cfg_.port <= 0)
    {
        LOG_ERROR("Camera [%s] invalid ip/port", cfg_.name.c_str());
        lastLoginErrorStr_ = "invalid ip/port";
        lastError_         = "login failed: " + lastLoginErrorStr_;
        ++consecutiveLoginFails_;
        return false;
    }

    NET_DVR_USER_LOGIN_INFO loginInfo;
    NET_DVR_DEVICEINFO_V40 deviceInfo;
    std::memset(&loginInfo, 0, sizeof(loginInfo));
    std::memset(&deviceInfo, 0, sizeof(deviceInfo));

    std::strncpy(loginInfo.sDeviceAddress, cfg_.ip.c_str(), sizeof(loginInfo.sDeviceAddress) - 1);
    loginInfo.wPort = static_cast<WORD>(cfg_.port);
    std::strncpy(loginInfo.sUserName, cfg_.username.c_str(), sizeof(loginInfo.sUserName) - 1);
    std::strncpy(loginInfo.sPassword, cfg_.password.c_str(), sizeof(loginInfo.sPassword) - 1);
    loginInfo.bUseAsynLogin = FALSE; // synchronous login
    loginInfo.byLoginMode = 0;       // 0 = private protocol

    userId_ = NET_DVR_Login_V40(&loginInfo, &deviceInfo);
    if (userId_ < 0)
    {
        const int err = static_cast<int>(NET_DVR_GetLastError());
        lastLoginErrorStr_ = "SDK error " + std::to_string(err);
        lastError_         = "login failed: " + lastLoginErrorStr_;
        ++consecutiveLoginFails_;
        if (consecutiveLoginFails_ <= 3)
        {
            // The first 3 failures stay at ERROR (need immediate attention; suspect
            //    IP / cable / password)
            LOG_ERROR("Camera [%s] login failed, ip=%s:%d, user=%s, %s (attempt #%d)",
                      cfg_.name.c_str(), cfg_.ip.c_str(), cfg_.port,
                      cfg_.username.c_str(), lastLoginErrorStr_.c_str(),
                      consecutiveLoginFails_);
        }
        else
        {
            // After that, demote to WARN to avoid long-running ERROR spam;
            //    monitorTick retries every 60s
            LOG_WARN("Camera [%s] still failing login (attempt #%d, %s); next retry on next monitor tick (~60s)",
                     cfg_.name.c_str(), consecutiveLoginFails_, lastLoginErrorStr_.c_str());
        }
        return false;
    }

    if (consecutiveLoginFails_ > 0)
    {
        // Recovered after a string of failures: one INFO line with the recovery
        //    and the total attempt count
        LOG_INFO("Camera [%s] login ok after %d attempts, ip=%s:%d, userid=%d",
                 cfg_.name.c_str(), consecutiveLoginFails_,
                 cfg_.ip.c_str(), cfg_.port, static_cast<int>(userId_));
    }
    else
    {
        LOG_INFO("Camera [%s] login ok, ip=%s:%d, userid=%d",
                 cfg_.name.c_str(), cfg_.ip.c_str(), cfg_.port, static_cast<int>(userId_));
    }
    consecutiveLoginFails_ = 0;
    lastLoginErrorStr_.clear();
    lastError_.clear();
    return true;
}

void CameraDevice::logout()
{
    stopRecord();
    if (userId_ >= 0)
    {
        NET_DVR_Logout(userId_);
        LOG_INFO("Camera [%s] logout", cfg_.name.c_str());
        userId_ = -1;
    }
}

bool CameraDevice::startRecord(const std::string& filePath)
{
    if (recording_)
    {
        LOG_WARN("Camera [%s] already recording: %s", cfg_.name.c_str(), recordFile_.c_str());
        return true;
    }

    if (!login())
    {
        // Failure reason is already stored in lastError_ (including
        //    lastLoginErrorStr_) inside login(); do not re-emit ERROR here.
        // The caller (CameraManager) will emit one consolidated line with the
        // reason.
        return false;
    }

    // Defensive: ensure the recording directory exists.
    // Normally CameraManager::buildRecordPath creates
    //   {videoOutDirs}/{YYYYMM}/{YYYYMMDD}/.
    // We re-check here because there is an async window between
    //   buildRecordPath and the worker thread's startRecord, so the directory
    //   could have been removed externally or the first creation could have
    //   failed. Without this guard the SDK would silently fail to write
    //   (requirement: "recording must always end up under videos/").
    {
        const size_t slash = filePath.find_last_of('/');
        if (slash != std::string::npos)
        {
            const std::string dir = filePath.substr(0, slash);
            if (!Utils::ensureDirectory(dir))
            {
                // Configuration-level error (videos path is unusable), so emit
                //    ERROR for the operator; also set lastError_ so the caller's
                //    "start record failed: ..." line reflects the same reason
                LOG_ERROR("Camera [%s] record dir not usable: %s",
                          cfg_.name.c_str(), dir.c_str());
                lastError_ = "create record dir failed: " + dir;
                return false;
            }
        }
    }

    NET_DVR_PREVIEWINFO previewInfo;
    std::memset(&previewInfo, 0, sizeof(previewInfo));
    previewInfo.lChannel = cfg_.channel;     // channel number
    previewInfo.dwStreamType = static_cast<DWORD>(cfg_.streamType); // 0 = main stream
    previewInfo.dwLinkMode = 0;               // TCP
    // HWND is void* on Windows and unsigned int on Linux. On Linux nullptr
    //    is not usable; we do not play any video here, so 0 is fine
    //    (matches the memset above).
    previewInfo.hPlayWnd = 0;                 // no playback, stream-and-save only
    previewInfo.bBlocked = 0;                 // non-blocking
    previewInfo.bPassbackRecord = 0;
    previewInfo.byPreviewMode = 0;            // normal preview

    playId_ = NET_DVR_RealPlay_V40(userId_, &previewInfo, nullptr, nullptr);
    if (playId_ < 0)
    {
        const int err = static_cast<int>(NET_DVR_GetLastError());
        lastError_ = "preview start failed: SDK error " + std::to_string(err);
        return false;
    }

    if (!NET_DVR_SaveRealData(playId_, const_cast<char*>(filePath.c_str())))
    {
        const int err = static_cast<int>(NET_DVR_GetLastError());
        lastError_ = "save real data failed: SDK error " + std::to_string(err) + ", file=" + filePath;
        NET_DVR_StopRealPlay(playId_);
        playId_ = -1;
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(mtx_);
        recording_ = true;
        recordFile_ = filePath;
        recordStart_ = std::time(nullptr);
    }
    lastError_.clear();
    LOG_INFO("Camera [%s] start recording: %s", cfg_.name.c_str(), filePath.c_str());
    return true;
}

void CameraDevice::stopRecord()
{
    bool wasRecording = false;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        wasRecording = recording_;
    }
    if (!wasRecording)
        return;

    if (playId_ >= 0)
    {
        NET_DVR_StopSaveRealData(playId_);
        NET_DVR_StopRealPlay(playId_);
        playId_ = -1;
    }
    {
        std::lock_guard<std::mutex> lock(mtx_);
        recording_ = false;
    }
    LOG_INFO("Camera [%s] stop recording: %s", cfg_.name.c_str(), recordFile_.c_str());
}

bool CameraDevice::isRecording() const
{
    std::lock_guard<std::mutex> lock(mtx_);
    return recording_;
}

std::string CameraDevice::recordFile() const
{
    std::lock_guard<std::mutex> lock(mtx_);
    return recordFile_;
}

std::time_t CameraDevice::recordStartTime() const
{
    std::lock_guard<std::mutex> lock(mtx_);
    return recordStart_;
}
