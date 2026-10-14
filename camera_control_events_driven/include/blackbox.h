#ifndef BLACKBOX_H
#define BLACKBOX_H

/*
 * Built-in black box (timing instrumentation + stall watchdog + exception dump)
 *
 * Design notes:
 *   - Hot-path bbEvent is lock-free, IO-free and allocation-free (per-thread
 *     ring buffer); overhead is ~100ns per event.
 *   - A watchdog thread patrols "armed" channels every 100ms; a stall >=500ms
 *     records an event, >=2s triggers a dump.
 *   - BbSpan measures the elapsed time of any code section; above its threshold
 *     it records an event, above the dump threshold (2s) it triggers a dump.
 *   - Dumps are written to ../logs/blackbox/blackbox_<time>_<reason>.log and
 *     the most recent 20 files are kept automatically.
 *
 * Usage:
 *   Call bbStart() at program startup; call bbDump("shutdown") + bbStop()
 *   before exit. In business code: bbEvent("tag", value) records an event;
 *   BbSpan span("tag", 50) measures a code section; long-running loops call
 *   bbHeartbeat(ch); bbArm/bbDisarm mark whether a channel is in its active
 *   period.
 */

#include <atomic>
#include <chrono>
#include <string>

// Watchdog patrol channels (only "armed" channels take part in stall
// detection, avoiding false alarms while idle)
enum class BbChannel {
    CameraLoop = 0,     // camera command / capture loop thread
    PictureCapture,     // picture capture (incl. SDK acquisition and disk write)
    ListenerRecv,       // message reception / processing
    StrWrite,           // .str marker file writing
    LoggerFlush,        // log flushing
    Count
};

// Record an event (value can hold a duration in ms, a length, etc.)
void bbEvent(const char* tag, long long value = 0);

// Channel heartbeat: long-running loops call it every iteration; the watchdog
// uses it to decide whether the thread is stuck
void bbHeartbeat(BbChannel ch);

// Arm/disarm a channel: only heartbeat gaps during the armed period count
// toward stall detection
void bbArm(BbChannel ch);
void bbDisarm(BbChannel ch);

// Proactively report a stall (milliseconds)
void bbStall(BbChannel ch, long long ms, const char* detail = "");

// Request a dump (the black box performs the asynchronous write itself;
// rate-limited: no repeat dump within a short interval)
void bbDump(const char* reason);

// Lifecycle
void bbStart();
void bbStop();
void bbSetEnabled(bool enable);
bool bbEnabled();

// Print per-tag timing statistics (count/sum/max/over-threshold hits)
void bbPrintStats();

// RAII timing helper: wrap any code section; above thresholdMs it records an
// event, above 2s it triggers a dump
class BbSpan {
public:
    explicit BbSpan(const char* tag, long long thresholdMs = 50);
    ~BbSpan();
    long long elapsedMs() const;

private:
    const char* tag_;
    long long thresholdMs_;
    std::chrono::steady_clock::time_point t0_;
};

#endif  // BLACKBOX_H
