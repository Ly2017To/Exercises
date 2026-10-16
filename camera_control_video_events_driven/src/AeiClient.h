#pragma once

#include "Config.h"

#include <boost/asio.hpp>
#include <boost/system/error_code.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <vector>

class CameraManager;

// TCP client for a single device server:
// 1. Establishes and maintains the connection to the device server (auto-reconnect
//    on disconnect with exponential backoff starting at 3s, capped at 30s);
// 2. Periodically sends query packets (0x01, fixed period queryIntervalMs);
// 3. Parses answer packets (0x02) and dispatches recording actions
//    (the power-on / power-off state machine lives in CameraManager);
// 4. For all other packet types, only replies with confirm packets (0x04);
//    no business processing;
// 5. Answer watchdog: if no valid packet (any of 0x02 answer / 0x03 data /
//    0x33 fault / 0x36 self-test / 0x51 real-time tag) is received within
//    answerTimeoutSec seconds, proactively disconnects and re-enters the
//    reconnect flow (prevents silent hangs from a half-open connection).
class AeiClient : public std::enable_shared_from_this<AeiClient>
{
public:
    AeiClient(boost::asio::io_context& io, const AeiConfig& cfg,
              int queryIntervalMs, int answerTimeoutSec, CameraManager& manager);
    ~AeiClient();

    void start();
    void stop();

private:
    void doConnect();
    void onConnected();
    void closeSocket();
    void scheduleReconnect();
    void scheduleQuery();
    void sendQuery();
    void sendPacket(std::vector<uint8_t> pkt);
    void doWrite();
    void doRead();
    void handleRead(boost::system::error_code ec, std::size_t bytes);
    void processBuffer();

    // Answer watchdog (1-second granularity)
    void startWatchdog();
    void watchdogTick();

    // Full packet dispatch
    void handlePacket(const uint8_t* pkt, std::size_t len);

    boost::asio::io_context& io_;
    AeiConfig cfg_;
    int queryIntervalMs_;
    int answerTimeoutSec_;
    CameraManager& manager_;

    // Protocol layer uses a uint16_t device id (2-byte little-endian binary).
    // cfg_.aeiId remains a string (used for the file-name N field); the
    // numeric form is cached at construction time.
    uint16_t aeiIdNum_ = 0;

    boost::asio::ip::tcp::socket socket_;
    boost::asio::steady_timer queryTimer_;
    boost::asio::steady_timer reconnectTimer_;
    boost::asio::steady_timer watchdogTimer_;
    boost::asio::ip::tcp::resolver resolver_;

    bool started_ = false;
    bool connected_ = false;
    int reconnectDelaySec_ = 3;

    // Time of the most recently received 0x02 answer packet (steady_clock,
    //    immune to system-clock jumps)
    std::chrono::steady_clock::time_point lastAnswerTime_;

    std::vector<uint8_t> rxBuf_;
    std::array<uint8_t, 4096> scratch_;

    std::deque<std::vector<uint8_t>> sendQueue_;
    bool writing_ = false;

    // Packet reception statistics
    uint64_t rxPackets_ = 0;
    uint64_t rxBadPackets_ = 0;

    // Most recent answer status: used for the "edge-triggered" log line.
    // Answers arrive every ~5s and are mostly duplicates of 0x00; only state
    // changes (edges) get INFO, repeated values are demoted to DEBUG so that
    // heartbeats do not drown out real events during long runs.
    bool    haveAnswerStatus_ = false;
    uint8_t lastAnswerStatus_ = 0;

    // Cumulative count of XOR soft-checks (known type but failed XOR):
    // we still reply with confirm (behavior unchanged), but log only the
    // first occurrence and then every 100th, to avoid log spam.
    uint64_t xorSoftCount_ = 0;
};
