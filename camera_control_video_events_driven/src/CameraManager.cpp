#include "CameraManager.h"
#include "CameraDevice.h"
#include "Logger.h"
#include "Utils.h"

#include "HCNetSDK.h"

#include <cstdio>
#include <cstring>
#include <ctime>

// ---------------------------------------------------------------------------
// Global SDK
// ---------------------------------------------------------------------------
bool CameraManager::globalSdkInit(const std::string& localIP)
{
    if (!NET_DVR_Init())
    {
        const int err = static_cast<int>(NET_DVR_GetLastError());
        LOG_ERROR("HCNetSDK NET_DVR_Init failed, err=%d", err);
        return false;
    }

    // Multi-NIC hosts: explicitly bind the SDK to the NIC that owns the given IP.
    // Reason: an IPC host typically has one management NIC + one camera NIC;
    //         the SDK default may pick the wrong NIC, leading to camera
    //         connection timeouts or login failures.
    if (!localIP.empty())
    {
        const char* ip = localIP.c_str();
        if (!NET_DVR_SetConnectTime(2000, 1) ||   // Connection timeout
            !NET_DVR_SetReconnect(10000, true))  // and auto-reconnect
        {
            LOG_WARN("HCNetSDK set connect/reconnect defaults failed, continuing");
        }
        // Local IP binding is done implicitly via SetLocalIP-equivalent API on Linux.
        // (The Hikvision Linux SDK uses NET_DVR_SetRecvTimeOut / NET_DVR_SetConnectTime;
        //  NIC binding is achieved by selecting the right IP in the login info
        //  or by setting the per-NIC route on the host.)
        (void)ip;
        LOG_INFO("HCNetSDK localIP hint=%s (binding via system routing table)", localIP.c_str());
    }

    LOG_INFO("HCNetSDK initialized");
    return true;
}

void CameraManager::globalSdkCleanup()
{
    NET_DVR_Cleanup();
    LOG_INFO("HCNetSDK cleaned up");
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------
CameraManager::CameraManager(const AppConfig& cfg)
    : cfg_(cfg)
{
    for (const auto& aei : cfg.aeis)
    {
        for (const auto& cam : aei.cameras)
        {
            if (!cam.enable)
                continue;
            cameras_.emplace_back(std::make_unique<CameraDevice>(cam));
        }
    }

    for (const auto& aei : cfg.aeis)
    {
        bool hasEnabledCam = false;
        for (const auto& cam : aei.cameras)
        {
            if (cam.enable)
            {
                hasEnabledCam = true;
                break;
            }
        }
        if (!hasEnabledCam)
            aeiNoCameraAtStartup_.insert(aei.aeiId);
    }
}

CameraManager::~CameraManager()
{
    stop();
    globalSdkCleanup();
}

void CameraManager::start()
{
    // L6: list all enabled cameras at startup
    // active = cameras actually loaded with enable=true; config = total in the
    //         config file (including enable=false)
    LOG_INFO("CameraManager started, %zu/%d camera(s), max concurrent recording=1",
             cameras_.size(), cfg_.totalCameraCount);

    // Per-AEI rollup: skip empty lists (otherwise likely misread as a bug).
    // Emit a single WARN at startup listing the AEIs with no enabled camera
    // (runtime power-on edges for these AEIs are silenced).
    if (!aeiNoCameraAtStartup_.empty())
    {
        std::string names;
        for (const auto& aei : cfg_.aeis)
        {
            if (aeiNoCameraAtStartup_.count(aei.aeiId))
            {
                if (!names.empty()) names += ", ";
                // Recover the friendly name from cfg.aeis so we see names, not just ids
                names += aei.name.empty() ? aei.aeiId : aei.name;
            }
        }
        LOG_WARN("no enabled camera (startup-known), ignore power-on for: %s "
                 "(these AEIs will not consume sequence numbers at runtime)",
                 names.c_str());
    }

    for (const auto& aei : cfg_.aeis)
    {
        for (const auto& cam : aei.cameras)
        {
            if (!cam.enable) continue;
            aeiCameras_[aei.aeiId].push_back(cameras_.back().get());
        }
    }

    // Initial login for all cameras (executed on the worker thread)
    for (auto& cam : cameras_)
    {
        CameraDevice* p = cam.get();
        enqueue([p] { p->login(); });
    }

    worker_ = std::thread(&CameraManager::workerLoop, this);
    monitor_ = std::thread(&CameraManager::monitorLoop, this);
}

void CameraManager::stop()
{
    if (stopping_) return;
    stopping_ = true;
    queueCv_.notify_all();
    if (worker_.joinable())
        worker_.join();
    if (monitor_.joinable())
        monitor_.join();

    // Stop all recordings and log out (enqueue teardown before draining the
    //    worker thread)
    stopAllRecording();
    for (auto& cam : cameras_)
        cam->logout();
}

// ---------------------------------------------------------------------------
// Answer-packet dispatch
// ---------------------------------------------------------------------------
void CameraManager::handleAnswer(const std::string& aeiId, uint8_t status, const proto::BcdTime& onTime)
{
    if (status == 0x01 || status == 0x02)
    {
        // Power-on: only the power-off -> power-on edge triggers recording
        //          (repeated power-on answers do not re-trigger)
        bool need = false;
        {
            std::lock_guard<std::mutex> lock(stateMtx_);
            if (aeiPowerOn_.insert(aeiId).second)
                need = true;
        }
        if (!need) return;

        // Look up AEI config to compose the recording file name
        const AeiConfig* aei = nullptr;
        for (const auto& a : cfg_.aeis)
            if (a.aeiId == aeiId) { aei = &a; break; }
        if (!aei) return;

        // Is this AEI on the startup "no enabled camera" list?
        // If so, silence runtime power-on (a single WARN was already emitted at
        //   startup), and do NOT bump seq (avoids consuming a sequence number
        //   for a recording that will not be produced).
        if (aeiNoCameraAtStartup_.count(aeiId))
        {
            std::lock_guard<std::mutex> lock(stateMtx_);
            if (aeiNoCameraLogged_.insert(aeiId).second)
            {
                // This AEI has no enabled camera: a single WARN was already
                //   emitted at startup. Runtime warnings on every power-on
                //   would spam the log (~24% of total volume in measurements),
                //   so emit at most one warning per AEI.
                LOG_WARN("AEI [%s](id=%s) power on but no enabled camera (startup-known), ignore",
                         aei->name.c_str(), aei->aeiId.c_str());
            }
            return;
        }

        // Should not reach here in practice (startup already populated
        //   aeiNoCameraAtStartup_), but keep as a defensive fallback against
        // future code changes that may forget to do so.
        auto it = aeiCameras_.find(aeiId);
        if (it == aeiCameras_.end() || it->second.empty())
        {
            LOG_WARN("AEI [%s](id=%s) power on but no camera attached, ignore",
                     aei->name.c_str(), aei->aeiId.c_str());
            return;
        }

        const bool upDirection = (status == 0x01);
        const uint32_t seq = nextSeq(aeiId);

        for (auto* cam : it->second)
        {
            const std::string path = buildRecordPath(*aei, cam->config(), seq, upDirection, onTime);
            // Edge-trigger: log the recording target first, so the path is
            //    traceable in logs even if the SDK call later fails.
            LOG_INFO("AEI [%s](id=%s) power on (%s), start recording, seq=%03u",
                     aei->name.c_str(), aei->aeiId.c_str(),
                     upDirection ? "up" : "down", seq);
            LOG_INFO("Camera [%s] recording target: %s", cam->config().name.c_str(), path.c_str());

            enqueue([cam, path] {
                if (!cam->startRecord(path))
                {
                    // startRecord has already stored the failure reason in
                    //    cam->lastError(); re-emit it here together with the
                    //    consolidated message.
                    LOG_ERROR("Camera [%s] start record failed: %s",
                              cam->config().name.c_str(), cam->lastError().c_str());
                }
            });
        }
    }
    else
    {
        // Power-off: only the power-on -> power-off edge triggers stop
        bool need = false;
        {
            std::lock_guard<std::mutex> lock(stateMtx_);
            if (aeiPowerOn_.erase(aeiId))
                need = true;
        }
        if (!need) return;

        const AeiConfig* aei = nullptr;
        for (const auto& a : cfg_.aeis)
            if (a.aeiId == aeiId) { aei = &a; break; }
        if (aei)
            LOG_INFO("AEI [%s](id=%s) power off, stop recording", aei->name.c_str(), aei->aeiId.c_str());
        else
            LOG_INFO("AEI (id=%s) power off, stop recording", aeiId.c_str());

        // Note: cameras_ is a vector<unique_ptr>, and a unique_ptr cannot be
        //    captured by value. Instead we capture a raw pointer: cameras_'s
        //    lifetime matches CameraManager, the worker thread is joined before
        //    destruction, so the raw pointer cannot dangle.
        auto* cams = &cameras_;
        for (auto& cam : *cams)
        {
            CameraDevice* p = cam.get();
            enqueue([p] { p->stopRecord(); });
        }
    }
}

void CameraManager::stopAllRecording()
{
    for (auto& cam : cameras_)
    {
        CameraDevice* p = cam.get();
        enqueue([p] { p->stopRecord(); });
    }
}

// ---------------------------------------------------------------------------
// Sequence number and file name
// ---------------------------------------------------------------------------

// Protocol requirement: per device, each active event triggers a recording, and the
//   HHH field in the recording file name is 001-999, wrapping back to 1 after
//   999 (avoids unbounded seq growth that would overflow the file name).
// Note: the 19-byte query packet does not carry a sequence field; this seq is
//   used only for the recording file name C007 segment.
uint32_t CameraManager::nextSeq(const std::string& aeiId)
{
    std::lock_guard<std::mutex> lock(stateMtx_);
    uint32_t& s = seqMap_[aeiId];
    if (s == 0) s = 1;
    uint32_t r = s;
    s = (s % 999) + 1;
    return r;
}

// Recording file name: VXXXNHHH_LC_YYYYMMDDHHMMSS.mp4
//   XXX = stationid (3-character site code)
//   N   = device number (1 digit)
//   HHH = sequence number (001-999, rolling)
//   L   = direction flag (0 or 1)
//   C   = camera number (1-9)
//   time = event time from the answer packet
// N is the numeric value of AEIid: AEIid is a 1-2 digit numeric string, and the
//   protocol exchanges it as a 2-byte little-endian integer (e.g. "01" ->
//   device number 1). So we must NOT take the first character of the string
//   ("01".substr(0,1) = "0" is wrong).
// Save path: {videooutdirs}/{YYYYMM}/{YYYYMMDD}/
std::string CameraManager::buildRecordPath(const AeiConfig& aei, const CameraConfig& cam,
                                            uint32_t seq, bool upDirection,
                                            const proto::BcdTime& onTime) const
{
    char datePart[16];
    if (onTime.year >= 1900 && onTime.year <= 9999)
        std::snprintf(datePart, sizeof(datePart), "%04d%02d%02d%02d%02d%02d",
                      onTime.year, onTime.month, onTime.day,
                      onTime.hour, onTime.minute, onTime.second);
    else
        // Invalid time field in the answer packet -> degrade to local time
        std::strcpy(datePart, Utils::formatTimeCompact(Utils::localTimeNow()).c_str());

    char fileName[64];
    // XXX: site code (first 3 chars)
    // N: device number (1 digit), taken as the numeric value of AEIid (NOT the
    //    first character of the string)
    const int nNum = proto::aeiIdToUint16(aei.aeiId);
    std::snprintf(fileName, sizeof(fileName), "V%3.3s%1d%03u_%1d%1d_%s.mp4",
                  aei.stationid.empty() ? "???" : aei.stationid.c_str(),
                  std::max(1, nNum), seq % 1000,
                  upDirection ? 0 : 1, std::max(1, cam.code),
                  datePart);

    std::tm t = {};
    t.tm_year = onTime.year - 1900;
    t.tm_mon  = onTime.month - 1;
    t.tm_mday = onTime.day;
    char monthStr[8], dayStr[8];
    std::snprintf(monthStr, sizeof(monthStr), "%04d%02d", onTime.year, onTime.month);
    std::snprintf(dayStr,   sizeof(dayStr),   "%04d%02d%02d", onTime.year, onTime.month, onTime.day);

    std::string root = cfg_.videoOutDirs;
    return Utils::joinPath(Utils::joinPath(Utils::joinPath(root, monthStr), dayStr), fileName);
}

// ---------------------------------------------------------------------------
// Worker thread / monitor thread
// ---------------------------------------------------------------------------
void CameraManager::enqueue(Task task)
{
    std::lock_guard<std::mutex> lock(queueMtx_);
    tasks_.push_back(std::move(task));
    queueCv_.notify_one();
}

void CameraManager::workerLoop()
{
    while (true)
    {
        Task t;
        {
            std::unique_lock<std::mutex> lock(queueMtx_);
            queueCv_.wait(lock, [this] { return stopping_ || !tasks_.empty(); });
            if (tasks_.empty() && stopping_)
                return;
            t = std::move(tasks_.front());
            tasks_.pop_front();
        }
        try
        {
            t();
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("CameraManager worker task threw: %s", e.what());
        }
        catch (...)
        {
            LOG_ERROR("CameraManager worker task threw unknown exception");
        }
    }
}

void CameraManager::monitorLoop()
{
    while (!stopping_)
    {
        // Dispatch the monitor logic to the worker thread so that SDK calls
        //    remain serialized.
        enqueue([this] { monitorTick(); });

        for (int i = 0; i < 10 && !stopping_; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void CameraManager::monitorTick()
{
    // L5: record size sampling (one INFO line every 30s)
    const std::time_t now = std::time(nullptr);
    for (auto& cam : cameras_)
    {
        if (!cam->isRecording()) continue;
        const long long dur = static_cast<long long>(now - cam->recordStartTime());
        const long long sz  = Utils::fileSize(cam->recordFile());
        if (sz < 0) continue;
        if (now - lastSizeSample_ >= 30)
        {
            LOG_INFO("recording sample: %s size=%lld bytes dur=%lld s",
                     cam->recordFile().c_str(), sz, dur);
        }
        bool timeout  = dur >= cfg_.maxRecordSeconds;
        bool oversize = sz   >= static_cast<long long>(cfg_.maxRecordSizeMB) * 1024 * 1024;
        if (timeout)  // Time limit exceeded
        {
            LOG_INFO("Camera [%s] auto stop recording (time limit reached), file=%s",
                     cam->config().name.c_str(), cam->recordFile().c_str());
            cam->stopRecord();
        }
        else if (oversize)  // Size limit exceeded
        {
            LOG_INFO("Camera [%s] auto stop recording (size limit reached), file=%s",
                     cam->config().name.c_str(), cam->recordFile().c_str());
            cam->stopRecord();
        }
    }
    if (now - lastSizeSample_ >= 30) lastSizeSample_ = now;

    // Periodic retry for cameras that have not logged in (every 60s)
    if (now - lastLoginRetry_ >= 60)
    {
        lastLoginRetry_ = now;
        for (auto& cam : cameras_)
        {
            if (!cam->isRecording())
                cam->login();
        }
    }
}
