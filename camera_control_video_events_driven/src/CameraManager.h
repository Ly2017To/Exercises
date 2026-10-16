#pragma once

#include "Config.h"
#include "Protocol.h"

#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

class CameraDevice;

// Camera manager:
// 1. Global Hikvision SDK init/cleanup (NET_DVR_Init / NET_DVR_Cleanup);
// 2. Maintains the AEI -> camera-list mapping;
// 3. Answer-packet dispatch (power-on / power-off state machine, edge-triggered):
//      power-off -> power-on edge: start recording on all cameras of that AEI
//        (file names per the "CPS / Camera Service Communication Protocol");
//      power-on  -> power-off edge: stop recording on all cameras of that AEI;
//      state persists across network reconnects; only a power-off answer or
//        program exit returns the AEI to the off state;
// 4. All Hikvision SDK calls run serially on a dedicated worker thread
//    (task queue);
// 5. Monitor thread: stop recordings that exceed time / size limits (defaults
//    180s / 150MB, configurable), and periodically retry cameras that have not
//    yet logged in.
class CameraManager
{
public:
    explicit CameraManager(const AppConfig& cfg);
    ~CameraManager();

    CameraManager(const CameraManager&) = delete;
    CameraManager& operator=(const CameraManager&) = delete;

    // Global SDK init (call once per process). Returns true on success.
    // If localIP is non-empty, the SDK is bound to the NIC that owns that IP
    //   after init (for multi-NIC IPC hosts).
    // If localIP is empty, only basic init is performed (single-NIC or test).
    static bool globalSdkInit(const std::string& localIP = {});
    static void globalSdkCleanup();

    void start();
    void stop();

    // Answer-packet handler (called from the network thread; internally
    //    dispatched to the worker thread)
    // status: 0x01 / 0x02 = active (two different direction flags),
    //         0x00 / 0xFF = idle
    void handleAnswer(const std::string& aeiId, uint8_t status, const proto::BcdTime& onTime);

    // Program exit: stop all recordings
    void stopAllRecording();

private:
    using Task = std::function<void()>;

    void enqueue(Task task);
    void workerLoop();
    void monitorLoop();
    void monitorTick(); // Runs on the worker thread: stop on size/time
                        // limits + login retry

    uint32_t nextSeq(const std::string& aeiId); // Rolling 001-999 sequence
    std::string buildRecordPath(const AeiConfig& aei, const CameraConfig& cam,
                                uint32_t seq, bool upDirection, const proto::BcdTime& onTime) const;

    AppConfig cfg_;

    std::vector<std::unique_ptr<CameraDevice>> cameras_;
    std::map<std::string, std::vector<CameraDevice*>> aeiCameras_;

    std::mutex stateMtx_;                       // protects aeiPowerOn_ / seqMap_
    std::set<std::string> aeiPowerOn_;          // AEIs currently in the power-on state
    std::map<std::string, uint32_t> seqMap_;    // per-AEI sequence number
    std::set<std::string> aeiNoCameraAtStartup_;// AEIs identified at startup as
                                                // "no enabled camera". Runtime
                                                // power-on edges for these AEIs
                                                // are silenced (a single WARN is
                                                // emitted at startup).
    std::set<std::string> aeiNoCameraLogged_;   // AEIs already warned "no camera" at
                                                // runtime (one warning per AEI)

    std::mutex queueMtx_;
    std::condition_variable queueCv_;
    std::deque<Task> tasks_;
    bool stopping_ = false;

    std::thread worker_;
    std::thread monitor_;
    std::time_t lastLoginRetry_ = 0;
    std::time_t lastSizeSample_ = 0;  // L5: periodic recording size sample
};
