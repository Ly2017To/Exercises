# Event-Driven Video Capture Service

A C++17 **event-driven video capture service**. It connects to multiple **device servers** over TCP, subscribes to their state-change events, and drives IP cameras to record short video clips whenever an event arrives — with no polling of the capture side.

> The `_events_driven` suffix marks the architecture: every business action (start / stop recording, state transitions, sequence-number allocation) is **dispatched from an incoming network event**, not polled on a timer. An older timer-polled variant may exist in adjacent folders.

> **Demo — version `0.0.0`.** This repository is a **reference / demonstration implementation**. It is provided "as is" with **no warranty of any kind**, is **not intended or fit for production or any practical use**, and the authors accept **no liability** for any damage, loss or malfunction arising from its use. Do not deploy it in a production or safety-critical environment.

---

## Features

- **Event-driven core** — recording is triggered by state-change *edges* received from the device servers, not by periodic sampling. Duplicate status messages are absorbed by an internal state machine, so a repeated event never re-triggers capture.
- **Fully asynchronous I/O** — every network operation runs on a single `boost::asio::io_context` with non-blocking `async_resolve` / `async_connect` / `async_read_some` / `async_write`. See [Asynchronous networking](#asynchronous-networking-boostasio).
- **Multi-device TCP client** — connect to several device servers concurrently; each has its own connection state, exponential-backoff reconnection (3s → 6s → 12s → 24s → 30s, capped at 30s, never gives up), and an answer watchdog.
- **Edge-triggered recording** — a device reporting the *active* state starts recording on its cameras; the *idle* state stops it.
- **Per-device sequence numbering** — every clip gets a `001`–`999` rolling sequence. It wraps back to `001` after `999`.
- **Hikvision SDK integration** — `NET_DVR_Login_V40` + `RealPlay_V40` + `SaveRealData`, wrapped in a per-camera `CameraDevice`. On multi-NIC hosts the SDK can be pinned to a specific camera-side NIC via the `localIP` config field.
- **Auto-stop safety net** — any clip in progress is stopped at `maxRecordSeconds` (180s) or `maxRecordSizeMB` (150MB), whichever comes first. Both limits are evaluated on every monitor tick.
- **Watchdog** — if no valid packet arrives within `answerTimeoutSec`, the connection to that device is dropped and re-established.
- **Graceful shutdown** — `SIGINT` / `SIGTERM` handlers stop all recordings, log out from cameras, and release the SDK before exit (no `std::terminate`, no `bad_weak_ptr`).
- **Daily rotating logs** — `<install>/logs/camera_control_video_YYYYMMDD.log`, with a configurable level (`debug` / `info` / `warn` / `error`).

---

## Architecture

```
                       ┌────────────────────────────┐
   Device server ─TCP─▶│  DeviceClient (per device) │
   (N devices)         │  - connect / reconnect     │
                       │  - query timer (5s)        │
                       │  - watchdog (10s)          │
                       │  - parse answer packets    │
                       └─────────────┬──────────────┘
                                     │ state-change edge
                                     ▼
                       ┌────────────────────────────┐
                       │  CameraManager             │
                       │  - state machine (per dev) │
                       │  - sequence numbers        │
                       │  - worker thread (SDK)     │
                       │  - monitor thread (limits) │
                       └─────────────┬──────────────┘
                                     │ start / stop record
                                     ▼
                       ┌────────────────────────────┐
                       │  CameraDevice (per cam)    │
                       │  - Hikvision SDK wrapper   │
                       │  - NET_DVR_Login_V40       │
                       │  - RealPlay_V40 / SaveData │
                       └────────────────────────────┘
```

All device clients share one `boost::asio::io_context` running on the main thread. SDK calls are serialized on a dedicated worker thread to avoid concurrent access, and the auto-stop limits are checked on a separate monitor thread.

---

## Tech stack

| Component | Version | Notes |
| --- | --- | --- |
| Language | C++17 | `CMAKE_CXX_STANDARD 17`, required |
| Networking | **Boost 1.71 (exact)** | `find_package(Boost 1.71 EXACT REQUIRED COMPONENTS system)` — header-only Asio + `Boost::system` |
| JSON | nlohmann/json **3.12.0** | single-header, vendored at `src/json.hpp` (MIT) |
| Camera SDK | Hikvision HCNetSDK, Linux x64 | **not included** — download it yourself and extract under `lib/hcsdk/` (see [below](#camera-sdk--hikvision-hcnetsdk-not-included)) |
| Build | **CMake ≥ 3.16** | `cmake_minimum_required(VERSION 3.16)` |
| Threads | `Threads::Threads` (pthread) | `find_package(Threads REQUIRED)` |
| Service | systemd | user or system unit |

### Camera SDK — Hikvision HCNetSDK (not included)

The camera side is driven by Hikvision's **HCNetSDK** (the "Device Network SDK"). The SDK binaries are **not redistributed in this repository** — download the package yourself and extract it under `lib/hcsdk/` before building.

**What it does here** — the SDK provides the entire camera control path used by this project:

| API | Used for |
| --- | --- |
| `NET_DVR_Init()` / `NET_DVR_Cleanup()` | SDK lifecycle |
| `NET_DVR_SetConnectTime()` / `NET_DVR_SetReconnect()` | connect / reconnect timeouts |
| `NET_DVR_Login_V40()` / `NET_DVR_Logout()` | camera login / logout |
| `NET_DVR_RealPlay_V40()` | open a live preview stream |
| `NET_DVR_SaveRealData()` | write the live stream to an `.mp4` file |
| `NET_DVR_StopRealPlay()` | stop the stream (end of recording) |

**Version** — HCNetSDK **V6.1.x**, Linux x64 build. The SDK exposes no usable compile-time version macro (nothing in `HCNetSDK.h`), so the exact build behind this project is not recorded anywhere in the tree; query it at runtime with `NET_DVR_GetSDKVersion()` / `NET_DVR_GetSDKBuildVersion()`. The current release on the download portal at the time of writing is **V6.1.11.30 (build 2026-08-05)**. Note that the Windows and Linux builds, and the 32-bit and 64-bit builds, are **not** interchangeable. The SDK package ships its own copies of OpenSSL 1.1.1, OpenAL 1.x and zlib — no system copies are required.

**Download** (official; a Hikvision account may be required):

- Hikvision Open Platform — "Device Network SDK": <https://open.hikvision.com>
- Hikvision global support / download portal: <https://www.hikvision.com/en/support/>

**Expected layout** — after extraction, `lib/hcsdk/` must contain:

```
lib/hcsdk/
├── include/                          # HCNetSDK.h, DataType.h, plaympeg4.h, DecodeCardSdk.h
├── HCNetSDKCom/                      # 13 component .so plug-ins
├── libhcnetsdk.so                    # core
├── libHCCore.so                      # core support
├── libPlayCtrl.so                    # stream decode / save (PlayMPEG4)
├── libhpr.so                         # platform runtime
├── libNPQos.so                       # QoS transport
├── libAudioRender.so / libSuperRender.so
├── libcrypto.so.1.1 / libssl.so.1.1  # bundled OpenSSL 1.1.1
├── libopenal.so.1                    # bundled OpenAL
├── libz.so                           # bundled zlib
└── HCNetSDK_Log_Switch.xml
```

Because `INSTALL_RPATH`/`BUILD_RPATH` are set to `$ORIGIN/../lib/hcsdk;$ORIGIN/../lib/hcsdk/HCNetSDKCom`, the built binary finds the SDK at runtime **without any `LD_LIBRARY_PATH`**.

---

## Project layout

```
camera_control_video_events_driven/
├── CMakeLists.txt             # Build definition (v0.0.0, installs to ./install)
├── src/                       # Application sources
│   ├── main.cpp               # Entry point, io_context, signal handling
│   ├── AeiClient.{h,cpp}      # Per-device TCP client + watchdog + reconnect
│   ├── CameraManager.{h,cpp}  # State machine + worker thread + auto-stop monitor
│   ├── CameraDevice.{h,cpp}   # Hikvision SDK wrapper
│   ├── Config.{h,cpp}         # JSON config loader (nlohmann/json)
│   ├── Logger.{h,cpp}         # Thread-safe daily-rotating file logger
│   ├── Protocol.h             # Device packet protocol (header, types, BCD time, XOR)
│   ├── Utils.{h,cpp}          # Path / time / SHA-256 helpers
│   ├── build_version.h.in     # CMake template -> build_version.h (APP_VERSION / APP_BUILD_TIME)
│   └── json.hpp               # nlohmann/json 3.12 (vendored, MIT — do not modify)
├── config/
│   └── aeiconfig.json         # Example config (RFC 5737 placeholder IPs, placeholder passwords)
├── service/                   # systemd unit + helper scripts
│   ├── camera_video.service
│   ├── camera_video.sh
│   └── setup_camera_video_service.sh
├── lib/hcsdk/                 # (empty) Hikvision HCNetSDK — download & extract here, see above
└── README.md
```

---

## Build

> **Prerequisite:** the Hikvision SDK is not part of this repository. Download it (see [Camera SDK](#camera-sdk--hikvision-hcnetsdk-not-included)) and extract it under `lib/hcsdk/` first — both the CMake and the manual g++ builds below expect it there.

### With CMake (recommended)

```bash
cd camera_control_video_events_driven
mkdir -p build && cd build
cmake ..
make -j
```

The executable is written to **`build/camera_control_video`** (the target name is `PROJECT_NAME` = `camera_control_video`).

`src/build_version.h.in` is configured at build time, embedding `APP_VERSION` (`0.0.0`) and the build timestamp, which the startup banner prints.

### Install

```bash
make install     # CMAKE_INSTALL_PREFIX defaults to <source>/install
```

This produces:

```
install/
├── bin/camera_control_video
├── config/aeiconfig.json
├── lib/hcsdk/                # incl. HCNetSDKCom/
└── logs/                     # created at runtime
    videos/                   # created at runtime (per "videooutdirs")
```

### With g++ directly

```bash
g++ -std=c++17 -O2 -pthread \
    -I src -I lib/hcsdk/include \
    src/main.cpp src/AeiClient.cpp src/CameraDevice.cpp src/CameraManager.cpp \
    src/Config.cpp src/Logger.cpp src/Utils.cpp \
    -L lib/hcsdk -lhcnetsdk -lHCCore -lPlayCtrl -lhpr -lNPQos \
    -lboost_system -lpthread \
    -Wl,-rpath,'$ORIGIN/../lib/hcsdk:$ORIGIN/../lib/hcsdk/HCNetSDKCom' \
    -o camera_control_video
```

### Convenience targets

| Target | Effect |
| --- | --- |
| `make clean_build` | removes the contents of the build directory (keeps the directory) |
| `make my_clean_target` | removes just the built executable |

### Runtime path resolution

The binary resolves `logs/` and `videos/` as **siblings of `bin/`** by reading `/proc/self/exe` (`Utils::appRootDir()`), so it is independent of the working directory — safe under systemd. If `/proc/self/exe` is unavailable it falls back to `argv[0]` (which must then be absolute), and refuses to start if neither works.

---

## Configuration

Place `aeiconfig.json` under `<install>/config/`. A sanitized example ships at [`config/aeiconfig.json`](config/aeiconfig.json) — replace the placeholder addresses and credentials before deploying.

Top-level fields:

| Field | Type | Default | Notes |
| --- | --- | --- | --- |
| `videooutdirs` | string | `videos` | Recording root. Relative paths are resolved against the install root; absolute paths are used as-is. Paths containing `..` are kept as-is (caller's responsibility). |
| `queryIntervalMs` | int | 5000 | Query interval. Minimum 100 ms. |
| `answerTimeoutSec` | int | 10 | Watchdog: drop the connection if no valid packet arrives within N seconds. Minimum 2 s. |
| `maxRecordSeconds` | int | 180 | Auto-stop a clip after N seconds. |
| `maxRecordSizeMB` | int | 150 | Auto-stop a clip when its size reaches N MB. |
| `logLevel` | string | `info` | One of `debug` / `info` / `warn` / `error`. |
| `localIP` | string | (empty) | Hikvision SDK NIC binding. **Required on multi-NIC hosts.** |
| `AeiInfo[]` | array | — | List of monitored devices. |

Each `AeiInfo` entry:

| Field | Type | Notes |
| --- | --- | --- |
| `enable` | bool | If `false`, this device is skipped entirely. |
| `name` | string | Display name (e.g., `Device-1`). |
| `stationid` | string | 3-character site code identifying the deployment site. |
| `AEIid` | string | Numeric device id, 1–65535 (identifies the device on the wire). |
| `ip` | string | Device server address. |
| `port` | int | Device server port. |
| `cameras[]` | array | Cameras attached to this device. |

Each `cameras[]` entry:

| Field | Type | Notes |
| --- | --- | --- |
| `enable` | bool | If `false`, this camera is skipped. |
| `name` | string | Display name (e.g., `Camera11`). |
| `type` | string | Only `HikCamera` is currently supported. |
| `code` | int | Camera number 1–9. |
| `ip` / `port` | string / int | Camera address. |
| `username` / `password` | string | Hikvision credentials. |
| `channel` | int | Hikvision channel (default `1`). |
| `streamType` | int | `0` = main stream, `1` = sub stream. |

---

## Running

### Foreground

```bash
./install/bin/camera_control_video -c ./install/config/aeiconfig.json
```

### As a systemd service

See `service/camera_video.service`. Two deployment modes are possible:

**User unit** (recommended for desktop / dev machines):

```bash
mkdir -p ~/.config/systemd/user
cp service/camera_video.service ~/.config/systemd/user/
systemctl --user daemon-reload
systemctl --user enable --now camera_video.service
loginctl enable-linger $USER   # keep the service alive after logout
```

**System unit** (recommended for headless / IPC hosts): copy to `/etc/systemd/system/`, set `WantedBy=` to `multi-user.target`, and drop `--user` from the commands.

View logs:

```bash
journalctl --user -u camera_video.service -f           # user mode
journalctl -u camera_video.service -f                  # system mode
# or the application's own business log:
tail -F <install>/logs/camera_control_video_*.log
```

> **Naming note:** CMake emits the executable as `camera_control_video` (see `project(camera_control_video ...)`), while the shipped `service/camera_video.sh` refers to `camera_video_control`. Pick one name and align both the build output and the service scripts before deploying.

---

## Asynchronous networking (Boost.Asio)

This section explains how the network layer works and why it is structured the way it is.

### One `io_context`, N clients

`main.cpp` creates a single `boost::asio::io_context` and runs it on the main thread:

```cpp
boost::asio::io_context io;
for (const auto& aei : cfg.aeis)
{
    auto client = std::make_shared<AeiClient>(io, aei, cfg.queryIntervalMs,
                                              cfg.answerTimeoutSec, cameraManager);
    client->start();
    clients.push_back(std::move(client));
}
io.run();          // main thread becomes the network thread
```

Every `AeiClient` is a handle to the *same* `io_context` and owns five async objects:

| Member | Purpose |
| --- | --- |
| `socket_` | the TCP connection |
| `resolver_` | DNS / endpoint resolution (`async_resolve`) |
| `queryTimer_` | periodic query packet (5 s) |
| `reconnectTimer_` | reconnect backoff |
| `watchdogTimer_` | 1 s answer-watchdog tick |

Because **one thread** drives `io.run()`, handlers never execute concurrently. That is why the per-client state machine (`started_`, `connected_`, `reconnectDelaySec_`, `rxBuf_`, `sendQueue_`) needs **no mutex and no strand** today.

### Connection lifecycle

```
start()
  └─ doConnect()
       ├─ if (connected_) return;                 ← guard against a stale reconnect timer
       └─ resolver_.async_resolve(ip, port)
            ├─ error  → scheduleReconnect()
            └─ ok     → async_connect(socket_, results)
                          ├─ error  → closeSocket() + scheduleReconnect()
                          └─ ok     → onConnected()
                                       ├─ reconnectTimer_.cancel()   ← no duplicate connect
                                       ├─ reconnectDelaySec_ = 3     ← reset backoff
                                       ├─ startWatchdog()
                                       ├─ sendQuery()                ← immediate first query
                                       └─ doRead()                   ← enter the read loop
```

There is no blocking call anywhere: resolution, connect, read and write are all completion-handler based.

### Reconnect — exponential backoff, capped, never gives up

```cpp
reconnectTimer_.expires_after(std::chrono::seconds(reconnectDelaySec_));
reconnectTimer_.async_wait([this, self](error_code ec) {
    if (ec || !started_) return;          // cancelled or stopped
    doConnect();
});
reconnectDelaySec_ = std::min(reconnectDelaySec_ * 2, 30);   // 3→6→12→24→30, capped
```

Observed behaviour in production: 3 s → 6 s → 12 s → 24 s → 30 s (capped), retried indefinitely. A device that was unreachable for **28 minutes** reconnected on its own the moment the link came back, and the next disconnect was retried at **3 s** again — proving the backoff counter is reset in `onConnected()`.

### Watchdog — a self-rearming 1 s timer

```cpp
void AeiClient::watchdogTick()
{
    if (!started_ || !connected_) return;
    watchdogTimer_.expires_after(std::chrono::seconds(1));
    watchdogTimer_.async_wait([this, self](error_code ec) {
        if (ec || !started_ || !connected_) return;   // not re-armed when disconnected
        if (now - lastAnswerTime_ >= seconds(answerTimeoutSec_))
        {
            queryTimer_.cancel();
            closeSocket();
            scheduleReconnect();
            return;                                    // onConnected() restarts the watchdog
        }
        watchdogTick();                                // re-arm from INSIDE the handler
    });
}
```

Two deliberate details:

1. **Re-arming happens inside the previous handler**, so at most one `async_wait` is ever pending. A periodic timer left running on its own would pile up expirations if the loop ever stalled.
2. **The watchdog stops itself when disconnected** and is restarted by `onConnected()`. There is no "watchdog running against a dead socket" state.

Any valid packet — not just an answer — refreshes `lastAnswerTime_`. Data, fault, self-test and real-time packets all prove the device processed the query, so treating only answers as a heartbeat would cause spurious disconnects.

### Read loop and buffer resynchronisation

```cpp
socket_.async_read_some(buffer(scratch_), [this, self](error_code ec, size_t bytes) {
    if (ec == operation_aborted || !started_) return;
    if (ec) { queryTimer_.cancel(); closeSocket(); scheduleReconnect(); return; }
    handleRead(ec, bytes);      // append to rxBuf_, processBuffer(), then doRead() again
});
```

`processBuffer()` loops on `proto::tryExtractPacket()`, which returns:

| Return | Meaning | Action |
| --- | --- | --- |
| `> 0` | complete packet of N bytes | dispatch, erase from buffer, loop again |
| `0` | not enough data yet | return and wait for more |
| `< 0` | not a header / illegal length | drop **one** byte and resynchronise |

Dropping a single byte (rather than flushing) means one corrupt byte costs one byte, not the whole buffer.

### Write serialisation — at most one `async_write` in flight

```cpp
void AeiClient::sendPacket(std::vector<uint8_t> pkt)
{
    sendQueue_.push_back(std::move(pkt));
    if (!writing_) doWrite();          // only start a chain if none is running
}
```

Two concurrent `async_write` calls on the same socket would interleave bytes and corrupt the stream. The `sendQueue_` / `writing_` pair guarantees a strictly ordered, one-at-a-time write chain, and it also absorbs the burst when a single incoming packet triggers both a confirm reply and the next query.

### Object lifetime

Every handler captures `auto self = shared_from_this();`, so the `AeiClient` outlives any pending asynchronous operation. `stop()` posts a lambda that cancels all three timers and closes the socket; cancelled handlers receive `operation_aborted` and return early.

The destructor deliberately **does not** call `stop()`: by the time the last `shared_ptr` is released the internal `weak_ptr` is expired and `shared_from_this()` would throw `std::bad_weak_ptr`. Cleanup is left to the member destructors, and `main.cpp` guarantees `io.run()` has returned before the clients are dropped.

---

## Threading model & future thread-pool optimisation

### Threads today

| Thread | Created in | Responsibility |
| --- | --- | --- |
| **main** | `main.cpp` → `io.run()` | all network handlers (connect / read / write / timers) for every device |
| **worker** | `CameraManager::workerLoop()` | every Hikvision SDK call, strictly serialised |
| **monitor** | `CameraManager::monitorLoop()` | wakes every ~1 s and enqueues `monitorTick()` onto the worker |

Why the split exists: the Hikvision SDK is **not thread-safe** for a given login handle and its calls can block for seconds, so they must never run on the network thread. Conversely the SDK must never be called concurrently, hence a single worker with a task queue.

### What actually runs on the SDK worker

Camera login is a **startup-only** operation. In steady state the worker does nothing but start and stop capture:

| Phase | SDK activity |
| --- | --- |
| **Startup** (once) | `CameraManager::start()` enqueues `login()` for every enabled camera. |
| **Retry** (rare) | `monitorTick()` re-tries `login()` once per minute, but **only** for cameras that are not recording and have not logged in yet. A camera that logged in successfully at startup is **never** re-logged in at runtime. |
| **Steady state** | `startRecord()` / `stopRecord()` only — dispatched by state-change edges arriving from the device servers. Nothing else. |

Steady-state SDK load is therefore low, bursty and entirely event-driven: the worker thread is idle for most of its life, and there is no periodic re-login traffic.

### Why this is sufficient at the current scale

- 4 devices × 1 packet / 5 s ≈ **1 packet per second** — far below what one `io_context` thread can handle.
- No CPU-bound work runs on the network thread: parsing is a handful of bytes, and file hashing (SHA-256) happens once at startup.
- At runtime the SDK worker only performs start / stop capture, so serialising it costs no latency that anyone can observe.
- Consequently no strand, no lock, and no thread pool is needed on the I/O side today.

### Where it would break first

1. **Startup login is serialised.** Every camera logs in one after another on the single worker, and `NET_DVR_Login_V40` can block for the full connect timeout. With many cameras this makes the **startup** phase — not steady state — the only place where serialisation is actually visible. Cameras that fail are retried once a minute from the monitor thread, so one persistently unreachable camera never blocks the others indefinitely.
2. **The monitor thread is a polling loop** (`10 × sleep_for(100ms)`) that enqueues a task every second whether or not there is work. It costs a wakeup per second and adds queue churn; a `steady_timer` on the `io_context` would do the same job with zero polling.
3. **The task queue is unbounded** — no back-pressure if the SDK stalls for a long time.
4. **`CameraManager::handleAnswer()`** is called on the network thread but touches `aeiPowerOn_` / `seqMap_`, so it needs `stateMtx_` today. Any move to multiple I/O threads would require revisiting this.

### Optimisation options

#### Option A — thread pool on the `io_context` + a strand per device

```cpp
boost::asio::io_context io;
boost::asio::thread_pool  net_pool(std::thread::hardware_concurrency());

// per client:
auto strand = boost::asio::make_strand(io);
// ... and wrap EVERY handler:
resolver_.async_resolve(cfg_.ip, port,
    boost::asio::bind_executor(strand, [this, self](error_code ec, auto results) { /* ... */ }));
```

- **Pros:** scales network throughput; a per-client strand keeps each client's state lock-free (handlers for one client are serialised, different clients run in parallel).
- **Cons:** *every* handler — resolve, connect, read, write, and all three timers — must be wrapped, or two threads will touch `rxBuf_` / `sendQueue_` concurrently. `CameraManager::handleAnswer()` also becomes concurrent, so `stateMtx_` must be replaced by a manager strand. Blocking SDK calls still must **not** run here.
- **When:** only if the packet rate grows by orders of magnitude. Not the bottleneck today.

#### Option B — keep the single-threaded `io_context`, pool only the SDK side *(recommended)*

```cpp
class CameraManager
{
    boost::asio::thread_pool sdk_pool_{ std::thread::hardware_concurrency() };
    std::map<CameraDevice*, boost::asio::strand<boost::asio::thread_pool::executor_type>> camStrand_;
};

void CameraManager::enqueueFor(CameraDevice* cam, Task t)
{
    boost::asio::post(camStrand_.at(cam), std::move(t));
}
```

- The network side stays exactly as it is — no strands, no locks, no rewrite.
- The concrete win is at **startup**: cameras log in in parallel instead of one after another, which shortens the time before the service is fully armed. (Steady state gains little, since only start / stop capture run there.)
- A **per-camera strand** satisfies the SDK's "one operation per login handle at a time" rule while letting different cameras proceed in parallel.
- Smallest diff: replace `std::thread worker_` + the `queueMtx_ / queueCv_ / deque` hand-rolled queue with `post()` onto a strand.
- Remember to `sdk_pool_.join()` in `stop()` before tearing down cameras.

#### Option C — one `io_context` (and thread) per device

- With N = 4 this is trivially simple and gives perfect isolation: no strands needed at all, because each thread owns exactly one client.
- Does not scale past a few dozen devices, and the SDK worker is still needed.

#### Also worth doing regardless

- Replace the monitor `sleep_for` loop with a `boost::asio::steady_timer` posted on the existing `io_context`.
- Bound the task queue (e.g. drop-with-warning above a few hundred pending tasks) so a stalled SDK cannot grow memory without limit.

**Recommendation:** the current design is adequate — login happens once at startup and steady-state traffic is only start / stop capture, so none of this is urgent. If the camera count grows enough that the **startup** login phase becomes noticeably slow, Option B is the targeted fix. The timer and back-pressure cleanups are worth doing regardless. Reach for Option A only when measured packet rates justify it.

---

## Known limitations

- **Hikvision SDK only** — `CameraDevice` implements just the `HikCamera` type.
- **One clip per device at a time** — matches the protocol semantics: a device reports a single state, so only one capture is active per device.
- **No packet body parsing** — data, fault, self-test and real-time packets are received and acknowledged, but their bodies are not interpreted (they only count as "device alive" for the watchdog).
- **Single SDK worker thread** — all camera operations are serialised. This is only observable at startup, where cameras log in one after another; a slow `NET_DVR_Login_V40` delays the cameras queued behind it. Steady-state impact is minimal because login is not repeated at runtime. See [Threading model](#threading-model--future-thread-pool-optimisation).
- **No TLS** — the device protocol runs over plain TCP.
- **No config hot-reload** — `SIGHUP` is not handled; restart the service to pick up config changes.

---

## License

Proprietary. **This is demo code at version `0.0.0` — see the disclaimer at the top of this document: no warranty, no liability, not for practical use.** The Hikvision HCNetSDK is **not distributed** in this repository; it remains subject to Hikvision's EULA and must be downloaded separately. `src/json.hpp` is nlohmann/json 3.12 (MIT).
