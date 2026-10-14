#include "blackbox.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {

// ===================== Tunable parameters =====================
constexpr size_t kRingCap = 4096;             // ring buffer entries per thread (was 8192; the dump window keeps full coverage)
constexpr long long kStallWarnMs = 500;       // stall warning threshold
constexpr long long kStallDumpMs = 2000;      // stall threshold that triggers a dump
constexpr long long kSpanDumpMs = 2000;       // single-span duration threshold that triggers a dump
constexpr int kWatchdogTickMs = 100;          // watchdog patrol period
constexpr long long kMinDumpIntervalMs = 120000; // per-reason dump rate limit: no repeat within 120s (was 30s, single bucket)
constexpr long long kStallIdleMs = 30000;     // armed-period grace: a channel that was just armed gets this long before its heartbeat gaps count as stalls (prevents listener_arm → first WHEEL_DATA gap from being misread as a stall)
constexpr int kMaxDumpFiles = 8;              // number of dump files to keep (was 20)
constexpr int kReasonHistorySize = 8;         // per-reason rate-limit buckets
constexpr long long kDumpWindowSec = 30;      // timeline window: only the last N seconds around the trigger are dumped
constexpr const char* kDumpDir = "../logs/blackbox";

struct Entry {
    uint64_t tsMs;
    uint64_t tid;
    long long value;
    char tag[28];
};

struct ThreadBuffer {
    Entry entries[kRingCap];
    std::atomic<uint64_t> seq{0};
};

struct Stat {
    char tag[28];
    std::atomic<uint64_t> count{0};
    std::atomic<uint64_t> sumMs{0};
    std::atomic<uint64_t> maxMs{0};
    std::atomic<uint64_t> over{0};
};

constexpr int kMaxStats = 64;

// ===================== Global state =====================
std::atomic<bool> g_enabled{true};
std::atomic<bool> g_running{false};
std::atomic<bool> g_stop{false};
std::thread g_watchdogThread;

std::mutex g_buffersMutex;
std::vector<ThreadBuffer*> g_buffers;
thread_local ThreadBuffer* t_buffer = nullptr;

std::atomic<uint64_t> g_lastBeat[static_cast<int>(BbChannel::Count)];
std::atomic<bool> g_armed[static_cast<int>(BbChannel::Count)];
std::atomic<uint64_t> g_armedAt[static_cast<int>(BbChannel::Count)];  // arm time, used by watchdog to skip the first kStallIdleMs after arm
std::atomic<bool> g_warnReported[static_cast<int>(BbChannel::Count)];
std::atomic<bool> g_dumpReported[static_cast<int>(BbChannel::Count)];
std::mutex g_channelWhoMutex;
std::string g_channelWho[static_cast<int>(BbChannel::Count)];

Stat g_stats[kMaxStats];
std::atomic<int> g_statCount{0};
std::mutex g_statMutex;

// Per-reason rate-limit buckets (LRU by time). Different stall signatures
// (e.g. update_stall_L1 vs watchdog_stall:listener_recv) do not suppress each
// other's evidence within kMinDumpIntervalMs.
struct ReasonBucket {
    char key[48];  // must be larger than the longest reason we emit (e.g. "watchdog_stall:picture_capture:999999")
    std::atomic<uint64_t> lastMs{0};
};
ReasonBucket g_reasonBuckets[kReasonHistorySize];
std::mutex g_reasonBucketMutex;

std::mutex g_dumpReqMutex;
std::string g_dumpReason;
std::atomic<bool> g_dumpRequested{false};

const char* channelName(BbChannel ch) {
    switch (ch) {
        case BbChannel::CameraLoop:     return "camera_loop";
        case BbChannel::PictureCapture: return "picture_capture";
        case BbChannel::ListenerRecv:   return "listener_recv";
        case BbChannel::StrWrite:       return "str_write";
        case BbChannel::LoggerFlush:    return "logger_flush";
        default:                        return "unknown";
    }
}

uint64_t nowMs() {
    using namespace std::chrono;
    return static_cast<uint64_t>(
        duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
}

std::string stampForFile() {
    std::time_t now = std::time(nullptr);
    std::tm tmBuf{};
    localtime_r(&now, &tmBuf);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", &tmBuf);
    return std::string(buf);
}

std::string stampForLine(uint64_t ms) {
    std::time_t sec = static_cast<std::time_t>(ms / 1000);
    std::tm tmBuf{};
    localtime_r(&sec, &tmBuf);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tmBuf);
    char out[48];
    std::snprintf(out, sizeof(out), "%s.%03u", buf, static_cast<unsigned>(ms % 1000));
    return std::string(out);
}

ThreadBuffer* localBuffer() {
    if (t_buffer != nullptr) {
        return t_buffer;
    }
    ThreadBuffer* b = new ThreadBuffer();  // intentionally never freed: lifetime matches the process
    {
        std::lock_guard<std::mutex> lock(g_buffersMutex);
        g_buffers.push_back(b);
    }
    t_buffer = b;
    return b;
}

Stat* findOrCreateStat(const char* tag) {
    int n = g_statCount.load(std::memory_order_acquire);
    for (int i = 0; i < n; ++i) {
        if (std::strncmp(g_stats[i].tag, tag, sizeof(g_stats[i].tag)) == 0) {
            return &g_stats[i];
        }
    }
    std::lock_guard<std::mutex> lock(g_statMutex);
    n = g_statCount.load(std::memory_order_relaxed);  // double check
    for (int i = 0; i < n; ++i) {
        if (std::strncmp(g_stats[i].tag, tag, sizeof(g_stats[i].tag)) == 0) {
            return &g_stats[i];
        }
    }
    if (n >= kMaxStats) {
        return &g_stats[kMaxStats - 1];  // table full: merge into the last slot
    }
    std::strncpy(g_stats[n].tag, tag, sizeof(g_stats[n].tag) - 1);
    g_stats[n].tag[sizeof(g_stats[n].tag) - 1] = '\0';
    g_statCount.store(n + 1, std::memory_order_release);
    return &g_stats[n];
}

void rotateDumpFiles();

void writeDump(const char* reason) {
    std::vector<ThreadBuffer*> buffers;
    {
        std::lock_guard<std::mutex> lock(g_buffersMutex);
        buffers = g_buffers;
    }

    // Merge all per-thread ring buffers (sorted by time)
    std::vector<Entry> all;
    for (ThreadBuffer* b : buffers) {
        uint64_t seq = b->seq.load(std::memory_order_acquire);
        size_t n = static_cast<size_t>(std::min<uint64_t>(seq, kRingCap));
        uint64_t start = seq - n;
        for (uint64_t i = start; i < seq; ++i) {
            all.push_back(b->entries[i % kRingCap]);
        }
    }
    std::sort(all.begin(), all.end(),
              [](const Entry& a, const Entry& b) { return a.tsMs < b.tsMs; });

    // Time window: keep only events within the last kDumpWindowSec seconds
    // of the trigger. Most pre-trigger history is not needed for forensics.
    uint64_t triggerTs = nowMs();
    if (!all.empty()) {
        triggerTs = std::max(triggerTs, all.back().tsMs);
    }
    uint64_t windowStartMs = (kDumpWindowSec > 0 && triggerTs > static_cast<uint64_t>(kDumpWindowSec) * 1000ULL)
        ? triggerTs - static_cast<uint64_t>(kDumpWindowSec) * 1000ULL : 0;
    {
        std::vector<Entry> windowed;
        windowed.reserve(all.size());
        for (const Entry& e : all) {
            if (e.tsMs >= windowStartMs) windowed.push_back(e);
        }
        all = std::move(windowed);
    }

    std::error_code ec;
    fs::create_directories(kDumpDir, ec);

    std::string path = std::string(kDumpDir) + "/blackbox_" + stampForFile() + "_" + reason + ".log";
    std::ofstream f(path, std::ios::out | std::ios::trunc);
    if (!f.is_open()) {
        std::cerr << "[blackbox] failed to open dump file: " << path << std::endl;
        return;
    }

    f << "# blackbox dump\n";
    f << "# reason: " << reason << "\n";
    f << "# dump at: " << stampForLine(nowMs()) << "\n";
    f << "# dump window: last " << kDumpWindowSec << " s (trigger_ts=" << stampForLine(triggerTs) << ")\n";
    f << "# events (in window): " << all.size() << "\n";

    f << "#\n# channel state\n";
    {
        std::lock_guard<std::mutex> lock(g_channelWhoMutex);
        for (int i = 0; i < static_cast<int>(BbChannel::Count); ++i) {
            uint64_t last = g_lastBeat[i].load(std::memory_order_relaxed);
            f << "#   " << channelName(static_cast<BbChannel>(i))
              << " armed=" << (g_armed[i].load() ? 1 : 0)
              << " lastBeat=" << (last ? stampForLine(last) : std::string("never"))
              << " who=" << g_channelWho[i] << "\n";
        }
    }

    f << "#\n# span stats (tag count sum_ms avg_ms max_ms over_threshold)\n";
    int n = g_statCount.load(std::memory_order_acquire);
    for (int i = 0; i < n; ++i) {
        uint64_t c = g_stats[i].count.load();
        uint64_t sum = g_stats[i].sumMs.load();
        f << "#   " << g_stats[i].tag << " " << c << " " << sum << " "
          << (c ? sum / c : 0) << " " << g_stats[i].maxMs.load() << " "
          << g_stats[i].over.load() << "\n";
    }

    // High-volume low-value tags: collapse into per-tag summaries to cut
    // dump size by ~80% without losing forensic signal. BbSpan's over-threshold
    // events for these tags still appear as full timeline entries.
    auto isLowValueTag = [](const char* tag) {
        static const char* low[] = {
            "recv_frame", "pic_start", "update_done", "cmd_enqueue"
        };
        for (const char* t : low) {
            if (std::strcmp(tag, t) == 0) return true;
        }
        return false;
    };

    struct TagSummary {
        std::string tag;
        size_t count = 0;
        uint64_t firstTs = 0;
        uint64_t lastTs = 0;
        long long maxValue = 0;
    };
    std::vector<Entry> kept;
    kept.reserve(all.size());
    std::vector<TagSummary> summaries;
    for (const Entry& e : all) {
        if (isLowValueTag(e.tag)) {
            bool found = false;
            for (auto& s : summaries) {
                if (s.tag == e.tag) {
                    ++s.count;
                    s.lastTs = e.tsMs;
                    if (s.firstTs == 0) s.firstTs = e.tsMs;
                    if (e.value > s.maxValue) s.maxValue = e.value;
                    found = true;
                    break;
                }
            }
            if (!found) {
                TagSummary ns;
                ns.tag = e.tag;
                ns.count = 1;
                ns.firstTs = e.tsMs;
                ns.lastTs = e.tsMs;
                ns.maxValue = e.value;
                summaries.push_back(std::move(ns));
            }
        } else {
            kept.push_back(e);
        }
    }

    f << "#\n# timeline: ts_ms | local_time | tid | tag | value\n";
    for (const Entry& e : kept) {
        f << e.tsMs << " | " << stampForLine(e.tsMs) << " | " << e.tid
          << " | " << e.tag << " | " << e.value << "\n";
    }
    if (!summaries.empty()) {
        f << "#\n# collapsed (high-volume low-value tags): tag | n | first | last | max_value\n";
        for (const auto& s : summaries) {
            f << "#   " << s.tag << " | n=" << s.count
              << " | first=" << stampForLine(s.firstTs)
              << " | last=" << stampForLine(s.lastTs)
              << " | max=" << s.maxValue << "\n";
        }
    }
    f.flush();
    f.close();

    // Keep the most recent kMaxDumpFiles files
    rotateDumpFiles();
}

void rotateDumpFiles() {
    std::error_code ec;
    std::vector<std::pair<std::string, fs::file_time_type>> files;
    for (auto it = fs::directory_iterator(kDumpDir, ec);
         !ec && it != fs::directory_iterator(); it.increment(ec)) {
        if (!it->is_regular_file()) continue;
        std::string name = it->path().filename().string();
        if (name.rfind("blackbox_", 0) != 0) continue;
        files.emplace_back(it->path().string(), it->last_write_time(ec));
    }
    if (static_cast<int>(files.size()) <= kMaxDumpFiles) return;
    std::sort(files.begin(), files.end(),
              [](const std::pair<std::string, fs::file_time_type>& a,
                 const std::pair<std::string, fs::file_time_type>& b) {
                  return a.second < b.second;
              });
    for (size_t i = 0; i + kMaxDumpFiles < files.size(); ++i) {
        fs::remove(files[i].first, ec);
    }
}

// Try to register a dump request for `reason`. Returns true if accepted,
// false if a recent dump for the same reason (within kMinDumpIntervalMs)
// was already written. Bucket eviction is LRU-by-time; only the key update
// is serialized (atomic lastMs reads/writes are lock-free).
bool tryClaimDump(const char* reason) {
    uint64_t now = nowMs();
    std::lock_guard<std::mutex> lock(g_reasonBucketMutex);
    int oldest = 0;
    uint64_t oldestMs = UINT64_MAX;
    for (int i = 0; i < kReasonHistorySize; ++i) {
        if (g_reasonBuckets[i].key[0] != '\0' &&
            std::strncmp(g_reasonBuckets[i].key, reason, sizeof(g_reasonBuckets[i].key)) == 0) {
            uint64_t last = g_reasonBuckets[i].lastMs.load(std::memory_order_relaxed);
            if (last != 0 && now - last < static_cast<uint64_t>(kMinDumpIntervalMs)) {
                return false;
            }
            g_reasonBuckets[i].lastMs.store(now, std::memory_order_relaxed);
            return true;
        }
        uint64_t t = g_reasonBuckets[i].lastMs.load(std::memory_order_relaxed);
        if (t < oldestMs) { oldestMs = t; oldest = i; }
    }
    // New reason: evict oldest slot
    std::strncpy(g_reasonBuckets[oldest].key, reason, sizeof(g_reasonBuckets[oldest].key) - 1);
    g_reasonBuckets[oldest].key[sizeof(g_reasonBuckets[oldest].key) - 1] = '\0';
    g_reasonBuckets[oldest].lastMs.store(now, std::memory_order_relaxed);
    return true;
}

// Dump request (asynchronous): business threads only register the request; the
// actual disk write is done by the watchdog thread, avoiding IO on business threads.
// Rate limit: per-reason, no repeat dump within kMinDumpIntervalMs. Different
// reasons do NOT suppress each other, so e.g. an ALERT and a watchdog stall
// happening close together both leave forensic evidence.
void requestDump(const char* reason) {
    const char* r = (reason != nullptr && *reason != '\0') ? reason : "unknown";
    if (!tryClaimDump(r)) return;
    {
        std::lock_guard<std::mutex> lock(g_dumpReqMutex);
        g_dumpReason = r;
    }
    g_dumpRequested.store(true, std::memory_order_release);
}

void watchdogLoop() {
    while (!g_stop.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(kWatchdogTickMs));

        // Handle dump requests registered by business threads (disk IO runs on the monitoring thread)
        if (g_dumpRequested.exchange(false, std::memory_order_acq_rel)) {
            std::string reason;
            {
                std::lock_guard<std::mutex> lock(g_dumpReqMutex);
                reason = g_dumpReason;
            }
            writeDump(reason.c_str());
        }

        uint64_t now = nowMs();
        for (int i = 0; i < static_cast<int>(BbChannel::Count); ++i) {
            if (!g_armed[i].load(std::memory_order_relaxed)) continue;
            uint64_t last = g_lastBeat[i].load(std::memory_order_relaxed);
            if (last == 0) continue;

            // Idle protection: a channel that was just armed gets kStallIdleMs
            // before its heartbeat gaps count as stalls. Without this, the listener
            // thread (which arms at connection accept but may not see its first
            // frame for several seconds) would dump immediately on the first long gap.
            uint64_t armTs = g_armedAt[i].load(std::memory_order_relaxed);
            if (armTs != 0 && static_cast<long long>(now - armTs) < kStallIdleMs) {
                continue;
            }

            long long gap = static_cast<long long>(now - last);
            if (gap >= kStallDumpMs) {
                // C2 fix: dump branch uses its own flag so it is not suppressed
                // by the warn branch having already set the shared flag.
                if (!g_dumpReported[i].exchange(true)) {
                    bbEvent("watchdog_stall", gap);
                    bbStall(static_cast<BbChannel>(i), gap, "watchdog");
                    std::cerr << "[blackbox] stall on " << channelName(static_cast<BbChannel>(i))
                              << " for " << gap << " ms -> dump" << std::endl;
                    // Reason encodes channel + gap so per-reason rate-limit
                    // does not let one channel starve another.
                    std::string stallReason = std::string("watchdog_stall:")
                        + channelName(static_cast<BbChannel>(i))
                        + ":" + std::to_string(gap);
                    requestDump(stallReason.c_str());
                }
                g_warnReported[i].store(true);  // suppress any redundant warn event in the same gap
            } else if (gap >= kStallWarnMs) {
                if (!g_warnReported[i].exchange(true)) {
                    bbEvent("watchdog_warn", gap);
                    bbStall(static_cast<BbChannel>(i), gap, "watchdog");
                }
            } else {
                g_warnReported[i].store(false, std::memory_order_relaxed);
                g_dumpReported[i].store(false, std::memory_order_relaxed);
            }
        }
    }
}

}  // namespace

// ===================== Public API =====================

void bbEvent(const char* tag, long long value) {
    if (!g_enabled.load(std::memory_order_relaxed)) return;
    ThreadBuffer* b = localBuffer();
    uint64_t seq = b->seq.load(std::memory_order_relaxed);
    Entry& e = b->entries[seq % kRingCap];
    e.tsMs = nowMs();
    e.tid = static_cast<uint64_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
    e.value = value;
    std::strncpy(e.tag, tag, sizeof(e.tag) - 1);
    e.tag[sizeof(e.tag) - 1] = '\0';
    b->seq.store(seq + 1, std::memory_order_release);
}

void bbHeartbeat(BbChannel ch) {
    int i = static_cast<int>(ch);
    if (i < 0 || i >= static_cast<int>(BbChannel::Count)) return;
    g_lastBeat[i].store(nowMs(), std::memory_order_relaxed);
}

void bbArm(BbChannel ch) {
    int i = static_cast<int>(ch);
    if (i < 0 || i >= static_cast<int>(BbChannel::Count)) return;
    g_lastBeat[i].store(nowMs(), std::memory_order_relaxed);
    g_warnReported[i].store(false, std::memory_order_relaxed);
    g_dumpReported[i].store(false, std::memory_order_relaxed);
    g_armedAt[i].store(nowMs(), std::memory_order_relaxed);
    g_armed[i].store(true, std::memory_order_relaxed);
}

void bbDisarm(BbChannel ch) {
    int i = static_cast<int>(ch);
    if (i < 0 || i >= static_cast<int>(BbChannel::Count)) return;
    g_armed[i].store(false, std::memory_order_relaxed);
    g_warnReported[i].store(false, std::memory_order_relaxed);
    g_dumpReported[i].store(false, std::memory_order_relaxed);
}

void bbStall(BbChannel ch, long long ms, const char* detail) {
    char tag[28];
    std::snprintf(tag, sizeof(tag), "stall_%s", channelName(ch));
    bbEvent(tag, ms);
    std::lock_guard<std::mutex> lock(g_channelWhoMutex);
    int i = static_cast<int>(ch);
    if (i >= 0 && i < static_cast<int>(BbChannel::Count) && detail != nullptr && *detail != '\0') {
        g_channelWho[i] = detail;
    }
}

void bbDump(const char* reason) {
    // Async version: business threads only register the dump request and return
    // immediately. The watchdog (kWatchdogTickMs=100ms) consumes the request and
    // does the actual disk write off the hot path. This means a stall on the
    // HikCameraLoop that triggers bbDump("update_stall_alert") will NOT itself
    // be made worse by a 50~200ms synchronous dump on the same thread.
    //
    // Note: requestDump() has a per-reason 120s rate limit (kMinDumpIntervalMs),
    // so two different reasons (e.g. ALERT L1 vs watchdog_stall:listener_recv)
    // occurring close together BOTH leave forensic evidence on disk; only
    // repeat dumps of the SAME reason within 120s are suppressed.
    if (!g_enabled.load(std::memory_order_relaxed)) return;
    requestDump(reason != nullptr ? reason : "manual");
}

void bbStart() {
    bool expected = false;
    if (!g_running.compare_exchange_strong(expected, true)) return;
    if (std::getenv("BB_DISABLE") != nullptr) {
        g_enabled.store(false);
        std::cout << "[blackbox] disabled by BB_DISABLE env" << std::endl;
        return;
    }
    g_stop.store(false);
    g_watchdogThread = std::thread(watchdogLoop);
    bbEvent("blackbox_start", 0);
    std::cout << "[blackbox] started, dump dir: " << kDumpDir
              << " (warn " << kStallWarnMs << "ms, dump " << kStallDumpMs << "ms)" << std::endl;
}

void bbStop() {
    if (!g_running.load()) return;
    g_stop.store(true);
    if (g_watchdogThread.joinable()) {
        g_watchdogThread.join();
    }
    g_running.store(false);
}

void bbSetEnabled(bool enable) {
    g_enabled.store(enable);
}

bool bbEnabled() {
    return g_enabled.load();
}

void bbPrintStats() {
    int n = g_statCount.load(std::memory_order_acquire);
    std::cout << "[blackbox] span stats:" << std::endl;
    for (int i = 0; i < n; ++i) {
        uint64_t c = g_stats[i].count.load();
        uint64_t sum = g_stats[i].sumMs.load();
        std::cout << "  " << g_stats[i].tag << ": count=" << c
                  << " avg=" << (c ? sum / c : 0) << "ms"
                  << " max=" << g_stats[i].maxMs.load() << "ms"
                  << " over=" << g_stats[i].over.load() << std::endl;
    }
}

BbSpan::BbSpan(const char* tag, long long thresholdMs)
    : tag_(tag), thresholdMs_(thresholdMs), t0_(std::chrono::steady_clock::now()) {}

long long BbSpan::elapsedMs() const {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now() - t0_).count();
}

BbSpan::~BbSpan() {
    if (!g_enabled.load(std::memory_order_relaxed) || tag_ == nullptr) return;
    long long ms = elapsedMs();
    Stat* s = findOrCreateStat(tag_);
    if (s != nullptr) {
        s->count.fetch_add(1, std::memory_order_relaxed);
        s->sumMs.fetch_add(static_cast<uint64_t>(ms), std::memory_order_relaxed);
        uint64_t prevMax = s->maxMs.load(std::memory_order_relaxed);
        while (static_cast<uint64_t>(ms) > prevMax &&
               !s->maxMs.compare_exchange_weak(prevMax, static_cast<uint64_t>(ms),
                                               std::memory_order_relaxed)) {
        }
        if (ms >= thresholdMs_) {
            s->over.fetch_add(1, std::memory_order_relaxed);
            bbEvent(tag_, ms);
        }
    }
    if (ms >= kSpanDumpMs) {
        requestDump(tag_);
    }
}
