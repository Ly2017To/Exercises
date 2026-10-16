#include "AeiClient.h"
#include "CameraManager.h"
#include "Logger.h"
#include "Protocol.h"
#include "Utils.h"

#include <algorithm>
#include <cstring>

using boost::asio::ip::tcp;

AeiClient::AeiClient(boost::asio::io_context& io, const AeiConfig& cfg,
                     int queryIntervalMs, int answerTimeoutSec, CameraManager& manager)
    : io_(io),
      cfg_(cfg),
      queryIntervalMs_(queryIntervalMs),
      answerTimeoutSec_(answerTimeoutSec),
      manager_(manager),
      aeiIdNum_(proto::aeiIdToUint16(cfg.aeiId)),
      socket_(io),
      queryTimer_(io),
      reconnectTimer_(io),
      watchdogTimer_(io),
      resolver_(io)
{
}

AeiClient::~AeiClient()
{
    // Do NOT call stop() from the destructor: stop() needs shared_from_this(),
    // but when the last std::shared_ptr is released the weak_ptr is already
    // expired, and shared_from_this() would throw std::bad_weak_ptr and
    // terminate the process.
    // Resource cleanup is delegated to member destructors (socket / timer /
    // resolver), which automatically cancel any pending async operations on
    // destruction.
    // The caller MUST ensure the io_context has stopped the event loop before
    // the last shared_ptr is released.
}

void AeiClient::start()
{
    if (started_)
        return;
    started_ = true;
    LOG_INFO("AeiClient [%s](id=%s) starting, server=%s:%d",
             cfg_.name.c_str(), cfg_.aeiId.c_str(), cfg_.ip.c_str(), cfg_.port);
    doConnect();
}

void AeiClient::stop()
{
    if (!started_)
        return;
    started_ = false;

    auto self = shared_from_this();
    boost::asio::post(io_, [this, self] {
        boost::system::error_code ignore;
        queryTimer_.cancel(ignore);
        reconnectTimer_.cancel(ignore);
        watchdogTimer_.cancel(ignore);
        closeSocket();
    });
}

// ---------------------------------------------------------------------------
// Connection management
// ---------------------------------------------------------------------------
void AeiClient::doConnect()
{
    // If already connected, do nothing (avoids redundant connects triggered
    //    by a residual reconnect timer)
    if (connected_)
        return;

    auto self = shared_from_this();
    resolver_.async_resolve(
        cfg_.ip, std::to_string(cfg_.port),
        [this, self](boost::system::error_code ec, tcp::resolver::results_type results) {
            if (!started_)
                return;
            if (ec)
            {
                LOG_WARN("AeiClient [%s] resolve failed: %s",
                         cfg_.name.c_str(), ec.message().c_str());
                scheduleReconnect();
                return;
            }

            boost::asio::async_connect(
                socket_, results,
                [this, self](boost::system::error_code ec2, const tcp::endpoint&) {
                    if (!started_)
                        return;
                    if (ec2)
                    {
                        LOG_WARN("AeiClient [%s] connect %s:%d failed: %s",
                                 cfg_.name.c_str(), cfg_.ip.c_str(), cfg_.port,
                                 ec2.message().c_str());
                        boost::system::error_code ignore;
                        socket_.close(ignore);
                        scheduleReconnect();
                        return;
                    }
                    onConnected();
                });
        });
}

void AeiClient::onConnected()
{
    connected_ = true;
    reconnectDelaySec_ = 3;

    // Cancel any reconnect timer that may still be pending, to avoid
    //    "already connected but schedules another reconnect"
    boost::system::error_code ignore;
    reconnectTimer_.cancel(ignore);

    rxBuf_.clear();
    sendQueue_.clear();
    writing_ = false;
    LOG_INFO("AeiClient [%s] connected to %s:%d",
             cfg_.name.c_str(), cfg_.ip.c_str(), cfg_.port);

    // Start the answer watchdog, then immediately send one query and
    //    continue sending on the configured interval
    startWatchdog();
    sendQuery();
    doRead();
}

void AeiClient::closeSocket()
{
    if (socket_.is_open())
    {
        boost::system::error_code ignore;
        socket_.shutdown(tcp::socket::shutdown_both, ignore);
        socket_.close(ignore);
    }
    connected_ = false;
}

void AeiClient::scheduleReconnect()
{
    if (!started_)
        return;

    auto self = shared_from_this();
    LOG_INFO("AeiClient [%s] schedule reconnect after %d s", cfg_.name.c_str(), reconnectDelaySec_);
    reconnectTimer_.expires_after(std::chrono::seconds(reconnectDelaySec_));
    reconnectTimer_.async_wait([this, self](boost::system::error_code ec) {
        if (ec || !started_)
            return; // cancelled or stopped
        doConnect();
    });

    // Exponential backoff, capped at 30 seconds
    reconnectDelaySec_ = std::min(reconnectDelaySec_ * 2, 30);
}

// ---------------------------------------------------------------------------
// Answer watchdog: while connected, if no 0x02 answer is received within
// answerTimeoutSec seconds -> disconnect and reconnect
// ---------------------------------------------------------------------------
void AeiClient::startWatchdog()
{
    lastAnswerTime_ = std::chrono::steady_clock::now();
    watchdogTick();
}

void AeiClient::watchdogTick()
{
    if (!started_ || !connected_)
        return;

    auto self = shared_from_this();
    watchdogTimer_.expires_after(std::chrono::seconds(1));
    watchdogTimer_.async_wait([this, self](boost::system::error_code ec) {
        if (ec || !started_ || !connected_)
            return; // cancelled / stopped / disconnected: don't re-arm

        auto now = std::chrono::steady_clock::now();
        if (now - lastAnswerTime_ >= std::chrono::seconds(answerTimeoutSec_))
        {
            LOG_WARN("AeiClient [%s] no answer for %d s, disconnect and reconnect",
                     cfg_.name.c_str(), answerTimeoutSec_);
            boost::system::error_code ignore;
            queryTimer_.cancel(ignore);
            closeSocket();
            scheduleReconnect();
            return; // not re-armed; onConnected() will restart the watchdog
        }
        watchdogTick(); // re-arm from within the callback so only one async_wait
                        // is ever pending
    });
}

// ---------------------------------------------------------------------------
// Query packet
// ---------------------------------------------------------------------------
void AeiClient::scheduleQuery()
{
    if (!started_ || !connected_)
        return;

    auto self = shared_from_this();
    queryTimer_.expires_after(std::chrono::milliseconds(queryIntervalMs_));
    queryTimer_.async_wait([this, self](boost::system::error_code ec) {
        if (ec || !started_ || !connected_)
            return;
        sendQuery();
    });
}

void AeiClient::sendQuery()
{
    std::tm now = Utils::localTimeNow();
    proto::BcdTime t;
    t.year = now.tm_year + 1900;
    t.month = now.tm_mon + 1;
    t.day = now.tm_mday;
    t.hour = now.tm_hour;
    t.minute = now.tm_min;
    t.second = now.tm_sec;

    sendPacket(proto::buildQueryPacket(aeiIdNum_, t));
    // Demoted to DEBUG because at INFO level a fleet of 4 clients × 5s cadence
    //    would emit ~48 lines per minute.
    // For deeper investigation, temporarily raise logLevel to debug.
    LOG_DEBUG("AeiClient [%s] query sent, time=%04d-%02d-%02d %02d:%02d:%02d",
              cfg_.name.c_str(), t.year, t.month, t.day, t.hour, t.minute, t.second);
    scheduleQuery();
}

// ---------------------------------------------------------------------------
// Send queue (at most one async_write in flight at any time)
// ---------------------------------------------------------------------------
void AeiClient::sendPacket(std::vector<uint8_t> pkt)
{
    sendQueue_.push_back(std::move(pkt));
    if (!writing_)
        doWrite();
}

void AeiClient::doWrite()
{
    if (sendQueue_.empty())
    {
        writing_ = false;
        return;
    }
    writing_ = true;

    auto self = shared_from_this();
    boost::asio::async_write(
        socket_, boost::asio::buffer(sendQueue_.front()),
        [this, self](boost::system::error_code ec, std::size_t) {
            if (ec)
            {
                LOG_WARN("AeiClient [%s] write failed: %s",
                         cfg_.name.c_str(), ec.message().c_str());
                writing_ = false;
                sendQueue_.clear();
                boost::system::error_code ignore;
                queryTimer_.cancel(ignore);
                closeSocket();
                scheduleReconnect();
                return;
            }
            if (!sendQueue_.empty())
                sendQueue_.pop_front();
            doWrite();
        });
}

// ---------------------------------------------------------------------------
// Receive and parse
// ---------------------------------------------------------------------------
void AeiClient::doRead()
{
    auto self = shared_from_this();
    socket_.async_read_some(
        boost::asio::buffer(scratch_),
        [this, self](boost::system::error_code ec, std::size_t bytes) {
            if (ec == boost::asio::error::operation_aborted || !started_)
                return;
            if (ec)
            {
                LOG_WARN("AeiClient [%s] read failed: %s",
                         cfg_.name.c_str(), ec.message().c_str());
                boost::system::error_code ignore;
                queryTimer_.cancel(ignore);
                closeSocket();
                scheduleReconnect();
                return;
            }
            handleRead(ec, bytes);
        });
}

void AeiClient::handleRead(boost::system::error_code /*ec*/, std::size_t bytes)
{
    rxBuf_.insert(rxBuf_.end(), scratch_.data(), scratch_.data() + bytes);
    processBuffer();
    doRead();
}

void AeiClient::processBuffer()
{
    while (true)
    {
        uint8_t type = 0;
        uint16_t station = 0;  // 2-byte little-endian
        int n = proto::tryExtractPacket(rxBuf_.data(), rxBuf_.size(), type, station);
        if (n == 0)
            return; // not enough data yet
        if (n < 0)
        {
            // Not a header byte or illegal length: drop one byte and resync
            ++rxBadPackets_;
            if (rxBadPackets_ % 100 == 1)
                LOG_WARN("AeiClient [%s] resync, drop 1 byte (total bad=%llu)",
                         cfg_.name.c_str(), static_cast<unsigned long long>(rxBadPackets_));
            rxBuf_.erase(rxBuf_.begin());
            continue;
        }

        std::vector<uint8_t> pkt(rxBuf_.begin(), rxBuf_.begin() + n);
        rxBuf_.erase(rxBuf_.begin(), rxBuf_.begin() + n);

        if (!proto::verifyPacket(pkt.data(), pkt.size()))
        {
            ++rxBadPackets_;
            // Fault tolerance: a self-built test simulator may not implement
            //    XOR strictly per spec, but the type byte is still well-formed.
            //    For known types, treat as "soft check":
            //      - refresh the watchdog timestamp (avoids 10s-disconnect
            //        from a missing 0x02 answer)
            //      - reply 0x04 confirm (so the simulator knows we are online)
            //    Unknown types are dropped unconditionally.
            if (type == proto::TypeQuery  || type == proto::TypeAnswer ||
                type == proto::TypeData   || type == proto::TypeConfirm ||
                type == proto::TypeSimCalib || type == proto::TypeSimRF ||
                type == proto::TypeStoreCar || type == proto::TypeFaultMsg ||
                type == proto::TypeSelfTest || type == proto::TypeRealTag)
            {
                lastAnswerTime_ = std::chrono::steady_clock::now();
                ++rxPackets_;
                // Reply with confirm as usual (behavior unchanged). Log only
                // the first occurrence and then every 100th to avoid a WARN
                // every 5s when a simulator has a non-strict XOR
                // (measured ~3% of total log volume).
                ++xorSoftCount_;
                if (xorSoftCount_ == 1 || xorSoftCount_ % 100 == 0)
                {
                    LOG_WARN("AeiClient [%s] packet xor check failed but type=0x%02X is known, "
                             "treated as heartbeat, reply confirm (count=%llu, len=%d)",
                             cfg_.name.c_str(), type,
                             static_cast<unsigned long long>(xorSoftCount_), n);
                }
                sendPacket(proto::buildConfirmPacket(station));
            }
            else
            {
                LOG_WARN("AeiClient [%s] packet xor check failed, type=0x%02X, len=%d, dropped",
                         cfg_.name.c_str(), type, n);
            }
            continue;
        }
        handlePacket(pkt.data(), pkt.size());
    }
}

void AeiClient::handlePacket(const uint8_t* pkt, std::size_t len)
{
    const uint8_t type = pkt[3];
    const uint16_t station = proto::decodeStation2(&pkt[4]);  // 2-byte little-endian
    ++rxPackets_;

    // Any valid packet refreshes the watchdog timestamp:
    //   - 0x02 answer: a "no-data" reply to the query (status only)
    //   - 0x03 data / 0x33 fault / 0x36 self-test / 0x51 real-time tag /
    //     0x0B-0x0D: "with-data" replies to the query (specific content)
    // Both prove the AEI is alive, the TCP link is up, and the AEI has
    // processed this query.
    // If the AEI is truly dead it sends no packets at all, so the 10s
    // timeout still trips (behavior unchanged).
    // (Previously only 0x02 was treated as a heartbeat, so 0x03 etc. were
    // misread as "no answer" and triggered spurious disconnects.)
    lastAnswerTime_ = std::chrono::steady_clock::now();

    switch (type)
    {
    case proto::TypeAnswer:
    {
        // Answer packet: parse and dispatch recording (heartbeat already
        //    refreshed at the top of this function)
        proto::AnswerInfo ans;
        if (!proto::parseAnswerPacket(pkt, len, ans))
        {
            LOG_WARN("AeiClient [%s] malformed answer packet, dropped",
                     cfg_.name.c_str());
            break;
        }
        const char* stateDesc = "idle";
        if (ans.status == 0x01)
            stateDesc = "active (direction 0)";
        else if (ans.status == 0x02)
            stateDesc = "active (direction 1)";

        // Edge-triggered log: only state changes (or the first packet) get
        // INFO; repeated states are demoted to DEBUG.
        // Answers arrive every ~5s and are mostly duplicates of 0x00, so the
        // heartbeats would otherwise drown out real events (measured ~34% of
        // total log volume). Business handling still calls handleAnswer on
        // every packet (idempotent).
        const bool statusChanged = (!haveAnswerStatus_ || ans.status != lastAnswerStatus_);
        haveAnswerStatus_ = true;
        lastAnswerStatus_ = ans.status;

        // L7: echo device id (taken from the answer packet device field;
        //    2-byte little-endian binary read as a uint16_t)
        if (statusChanged)
        {
            LOG_INFO("AeiClient [%s] answer: device=0x%04X status=0x%02X (%s), time=%04d-%02d-%02d %02d:%02d:%02d",
                     cfg_.name.c_str(), ans.station,
                     ans.status, stateDesc,
                     ans.time.year, ans.time.month, ans.time.day,
                     ans.time.hour, ans.time.minute, ans.time.second);
        }
        else
        {
            LOG_DEBUG("AeiClient [%s] answer: device=0x%04X status=0x%02X (%s) unchanged, time=%04d-%02d-%02d %02d:%02d:%02d",
                      cfg_.name.c_str(), ans.station,
                      ans.status, stateDesc,
                      ans.time.year, ans.time.month, ans.time.day,
                      ans.time.hour, ans.time.minute, ans.time.second);
        }

        manager_.handleAnswer(cfg_.aeiId, ans.status, ans.time);
        break;
    }

    case proto::TypeQuery:
        // Query packets are host->AEI, normally not received; reply with
        // confirm only
        LOG_DEBUG("AeiClient [%s] unexpected query packet, reply confirm",
                  cfg_.name.c_str());
        sendPacket(proto::buildConfirmPacket(station));
        break;

    default:
        // Data / message / real-time tag / self-test packets, etc.: only
        // reply with confirm, no processing.
        // These are all "with-data" replies to the query (per the protocol
        // model clarified with the user).
        // Real-time tag (0x51) is a high-frequency stream (~52 lines / 43min
        // in production) and would still spam at INFO, so demote to DEBUG;
        // the rest (0x03 data = event report / 0x33 fault / 0x36 self-test /
        // 0x0B-0x0D) are low-frequency and worth keeping at INFO.
        // Note: this only affects log level; the confirm-reply behavior is
        // unchanged.
        Logger::instance().log((type == proto::TypeRealTag) ? LogLevel::Debug : LogLevel::Info,
                               "AeiClient [%s] packet type=0x%02X len=%zu, reply confirm only",
                               cfg_.name.c_str(), type, len);
        sendPacket(proto::buildConfirmPacket(station));
        break;
    }
}
