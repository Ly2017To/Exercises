#pragma once

#include "Config.h"

#include <ctime>
#include <mutex>
#include <string>

// Wrapper around a single Hikvision network camera:
// maintains login + preview + save-to-disk recording
//   (NET_DVR_Login_V40 / RealPlay_V40 / SaveRealData).
// Note: SDK calls are always serialized on CameraManager's worker thread, so
// member access does not need extra locks; however, the recording state is
// queried from the monitor thread, so state fields are still protected by a
// mutex.
class CameraDevice
{
public:
    explicit CameraDevice(const CameraConfig& cfg);
    ~CameraDevice();

    const CameraConfig& config() const { return cfg_; }

    // Log in (when not logged in or the handle is invalid);
    // called from the CameraManager worker thread
    bool login();

    // Stop recording and log out
    void logout();

    // Start recording: filePath is the full destination path
    bool startRecord(const std::string& filePath);

    // Stop recording
    void stopRecord();

    bool isRecording() const;

    // Current recording file path (for size monitoring)
    std::string recordFile() const;

    // Recording start time (for duration monitoring)
    std::time_t recordStartTime() const;

    // Most recent failure reason (login / startRecord / stopRecord / etc.),
    // used by the upper layer to log both "why" and "at which step" together.
    // Cleared on the success path.
    const std::string& lastError() const { return lastError_; }

private:
    CameraConfig cfg_;

    mutable std::mutex mtx_;
    long userId_ = -1;          // NET_DVR login handle
    long playId_ = -1;          // NET_DVR preview handle
    std::string recordFile_;
    std::time_t recordStart_ = 0;
    bool recording_ = false;

    // Login backoff and failure counter: prevents log spam when monitorTick
    // (60s) and startRecord both try to log in around the same time.
    // The first 3 failures stay at ERROR; after that we demote to WARN and
    // include "next retry ETA".
    std::time_t lastLoginAttempt_ = 0;
    int  consecutiveLoginFails_  = 0;
    std::string lastLoginErrorStr_;     // e.g. "SDK error 7", reused by startRecord
    std::string lastError_;             // most recent failure reason (any step)
};
