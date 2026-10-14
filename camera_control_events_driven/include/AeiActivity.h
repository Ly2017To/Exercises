#ifndef AEI_ACTIVITY_H
#define AEI_ACTIVITY_H

#include <atomic>
#include <chrono>

// Shared timestamp state used by the camera-loop ALERT logic to distinguish
// "event source fully silent" (genuine stall) from "WHEEL-only silence while
// the event source still communicates" (tail / post-session idle). Producer:
// MessageListener (every received frame). Consumer:
// HikCamera::processCommands (the ALERT decision point).
//
// Header-only inline functions are used so the two translation units share one
// atomic instance per call site via the function-local static. steady_clock
// is used here (matches HikCamera's lastUpdateTime) so the age math is unit
// consistent.
namespace AeiActivity {

inline long long nowSteadyMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

inline std::atomic<long long>& lastAnyMsgMs() {
    static std::atomic<long long> v{0};
    return v;
}

inline std::atomic<long long>& lastWheelMsgMs() {
    static std::atomic<long long> v{0};
    return v;
}

inline void markAnyMsg() {
    long long t = nowSteadyMs();
    lastAnyMsgMs().store(t, std::memory_order_relaxed);
}

inline void markWheelMsg() {
    long long t = nowSteadyMs();
    lastAnyMsgMs().store(t, std::memory_order_relaxed);
    lastWheelMsgMs().store(t, std::memory_order_relaxed);
}

}  // namespace AeiActivity

#endif  // AEI_ACTIVITY_H