# Camera Control Photo

[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://en.cppreference.com/w/cpp/17)
[![Platform](https://img.shields.io/badge/platform-Linux-lightgrey.svg)](https://www.kernel.org/)
[![CMake](https://img.shields.io/badge/build-CMake%20%3E%3D%203.16-064F8C.svg)](https://cmake.org/)
[![Boost](https://img.shields.io/badge/deps-Boost.Asio-orange.svg)](https://www.boost.org/)
[![SDK](https://img.shields.io/badge/camera-Hikvision%20HCNetSDK-red.svg)](https://www.hikvision.com/)
[![Version](https://img.shields.io/badge/version-0.0.0--test-orange.svg)]()

An **event-driven camera image capture application** for Linux. It listens for events from a remote event source over TCP, and drives one or more Hikvision IP cameras to capture JPEG images — synchronized with the incoming event stream — while writing per-session marker files for downstream processing.

> **⚠️ Test version** — This repository publishes a **demo** for reference and learning purposes only. Use it at your own risk.

---

## Table of Contents

- [How It Works](#how-it-works)
- [Features](#features)
- [Architecture](#architecture)
- [Event Message Protocol](#event-message-protocol)
- [Output Files](#output-files)
- [Project Structure](#project-structure)
- [Requirements](#requirements)
- [Build](#build)
- [Configuration](#configuration)
- [Deployment (systemd)](#deployment-systemd)
- [Built-in Black Box](#built-in-black-box)
- [Logging](#logging)
- [Extending](#extending)
- [License](#license)

---

## How It Works

```
                 TCP :8055                    HCNetSDK
   ┌─────────┐  ┌──────────────┐  ┌─────────────┐  ┌─────────────┐
   │  Event  │─▶│   Message    │─▶│     Main    │─▶│  HikCamera  │
   │ source  │◀─│   Listener   │  │  Controller │  │  (per path) │
   └─────────┘  └──────────────┘  └─────────────┘  └─────────────┘
    frames + ACKs   frame parser     command queue    login + JPEG
```

1. A **remote event source** connects to this daemon over TCP and streams frames (`INQUIRY`, `WHEEL_DATA`, `TAG_DATA`, `TRAIN_DATA`). Each frame is ACKed with an XOR-checksummed reply.
2. On `INQUIRY` transitions of the *trigger* bits, the daemon generates `TRIGGERON` / `TRIGGEROFF` commands and routes them to the camera bound to that capture path.
3. On `TRIGGERON` the camera logs in (with retry), switches its **alarm output** on (strobe/light), syncs the local system time into the camera, and starts capturing **JPEG pictures at 10 fps**.
4. Each `WHEEL_DATA` frame updates the sequence counters, so every picture file name encodes *session / group / member / shot index*.
5. On `TRIGGEROFF` (or an automatic **3-minute timeout**) capture stops, the alarm output is switched off, and a `.str` marker file listing the **per-group counts** of the session is written.
6. `TAG_DATA` / `TRAIN_DATA` frames are acknowledged and timed; no capture action is taken.

## Features

- **Multi-camera, multi-path** — cameras are configured per capture path in a JSON file; commands are routed by path.
- **Robust TCP framing** — streaming parser with a per-connection residual buffer, little-endian length field, header resync (`0xFD`) on misalignment, and protection against coalesced/split packets.
- **Low-latency socket tuning** — `TCP_NODELAY` + `TCP_QUICKACK` on every accepted connection.
- **Resilient camera login** — SDK init retry loop (60 × 2 s) for cold-boot NIC bring-up; camera login retry (10 × 3 s); re-login on trigger if the session dropped.
- **Time synchronization** — camera clock is synced from the host after each login (NTP auto-disabled on the device so the manual time sticks).
- **Auto-recovery** — a 3-minute watchdog auto-terminates a capture session if `TRIGGEROFF` never arrives.
- **Built-in black box** — lock-free timing instrumentation, stall watchdog and automatic diagnostic dumps (see [Built-in Black Box](#built-in-black-box)).
- **Async batched logger** — business threads never block on disk; per-day log rotation, drop counter, PROD/TEST log levels.
- **systemd integration** — ships a unit file that waits for the camera NIC IP to appear before start (fixes cold-boot "login error 3"), with `Restart=always`.

## Architecture

| Component | File(s) | Responsibility |
|---|---|---|
| `main` | `src/main.cpp` | Bootstrap: config load, SDK init w/ retry, camera start, listener threads, graceful shutdown |
| `MessageListener` | `src/MessageListener.cpp` | Boost.Asio TCP server: accepts event-source connections, parses frames, sends ACKs, generates trigger/update commands |
| `MainController` | `src/MainController.cpp` | Registry of cameras; routes commands to the camera owning a capture path |
| `Camera` (abstract) | `src/Camera.cpp` | Base class: command queue + condition variable + worker thread |
| `HikCamera` | `src/HikCamera.cpp` | Hikvision implementation: login/logout, JPEG capture loop, sequence counting, `.str` marker files, alarm output |
| `HikSDKManager` | `include/HikSDKManager.h` | Singleton wrapper of the HCNetSDK: init bound to a local NIC IP, cleanup, last-error |
| `Configurations` | `src/Configurations.cpp` | JSON config loader (nlohmann/json), camera factory, output folder creation |
| `blackbox` | `src/blackbox.cpp` | Timing instrumentation + stall watchdog + exception dumps |
| `logger` | `src/logger.cpp` | Thread-safe async file logger |
| `EventActivity` | `include/AeiActivity.h` | Lock-free shared timestamps used by the stall-ALERT heuristics |

## Event Message Protocol

All frames start with `0xFD`, followed by a 2-byte little-endian length, payload, and end with an XOR checksum. Handled message types:

| Type | Code | ACK | Action |
|---|---|---|---|
| `INQUIRY` | `0x01` | `0x81` (+ time sync flag) | Path on/off transitions → `TRIGGERON`/`TRIGGEROFF` |
| `TAG_DATA` | `0x03` | `0x83` | Acknowledge only |
| `TRAIN_DATA` | `0x08` | `0x88` | Acknowledge only |
| `WHEEL_DATA` | `0x31` | `0xB1` | `UPDATE` command: advance sequence counters |

`INQUIRY` frames carry BCD-encoded device time; the daemon compares it against local RTC time and flags `0xFF` in the ACK when a correction is needed (seconds compared in decimal to avoid BCD decade-carry errors).

## Output Files

For each capture session, per camera:

- **JPEG pictures** — captured at 100 ms intervals, named
  `B<telegraphic><station><code><path><dir>_<trigger-time>_<SSSGGMM_PPPP>.jpg`
  where the counters encode *session (3) / group (2) / member (2) / picture (4)*, each zero-padded and wrapping at its maximum.
- **`.str` marker files** — `...<time>0000.str` created at trigger-on and `...<time>9999.str` written at trigger-off (or timeout), containing one line per group with its **member count**.
- **Logs** — daily files under `../logs/`.
- **Black-box dumps** — `../logs/blackbox/blackbox_<time>_<reason>.log`.

## Project Structure

```
camera_control_photo/
├── CMakeLists.txt              # Build script (CMake ≥ 3.16)
├── camera.sh                   # Production launcher script
├── camera.service              # systemd unit with cold-boot IP wait
├── configuration/
│   └── camera_config.json      # Runtime configuration
├── include/                    # Headers (Camera, HikCamera, blackbox, json.hpp, ...)
├── lib/
│   └── hcsdk/                # Hikvision HCNetSDK (Linux) — NOT in the repo, see Requirements
├── src/                        # Implementation
└── install/                    # (generated) production layout after `make install`
    ├── bin/camera_control
    ├── lib/hcsdk/              # SDK libraries (referenced via $ORIGIN rpath)
    └── configuration/
```

## Requirements

- Linux (tested on Kylin/x86-64; glibc with `TCP_QUICKACK` support)
- C++17 compiler (GCC ≥ 8 / Clang ≥ 7)
- CMake ≥ 3.16
- Boost (system component)
- Hikvision HCNetSDK for Linux — **not bundled with this repository** (proprietary, licensed by Hikvision). Download it yourself, see below.

> **Getting the SDK**
>
> The HCNetSDK binaries and headers are copyrighted by Hangzhou Hikvision Digital Technology Co., Ltd. and may not be redistributed through this repository. Download the **Device Network SDK (Linux)** from the official channels:
>
> - Hikvision Open Platform: <https://open.hikvision.com/downloadNew/5cda567cf47ae80dd41a54b3?type=10> (SDK for Device Network — download portal, Chinese)
> - Hikvision official website / your regional partner portal (search "Hikvision Device Network SDK Linux")
>
> This project is built and tested against **HCNetSDK V6.1.9.48 build20230410 (Linux, x86-64)**; newer 6.x releases are generally API-compatible, but stick to the tested version for production deployments.
>
> After downloading, extract it so that `lib/hcsdk/` looks like:
>
> ```
> lib/hcsdk/
> ├── include/                 # HCNetSDK.h, DataType.h, ...
> ├── HCNetSDKCom/             # libHCPreview.so, libStreamTransClient.so, ...
> ├── libhcnetsdk.so
> ├── libHCCore.so
> └── ...                      # libPlayCtrl.so, libssl.so.1.1, etc.
> ```
>
> If `lib/hcsdk/` is missing, CMake will fail to configure with a Boost/SDK link error — that is expected until the SDK is in place.

## Build

```bash
# 0. Prerequisite: place the Hikvision HCNetSDK (Linux) under lib/hcsdk/
#    — it is NOT part of this repo; download it from the official
#    Hikvision open platform (see "Getting the SDK" above).

# 1. Configure and build
mkdir build
cd build
cmake ..
make

# 2. Install into the production layout (./install)
make install
```

The executable is installed to `install/bin/camera_control`; SDK libraries are installed to `install/lib/hcsdk/` and found at runtime via `$ORIGIN`-relative RPATH — no `LD_LIBRARY_PATH` tweaks needed.

Run it directly from the install tree:

```bash
cd install/bin
./camera_control
```

## Configuration

Edit `configuration/camera_config.json`:

```jsonc
{
  "testLog": false,          // enable verbose TEST-level logs
  "port": 8055,              // TCP port the event source connects to
  "telegraphic": "1234",     // station code (file-name prefix)
  "aeiStation": 1,           // station number (file-name prefix)
  "folderCommon": "../pictures/",  // shared output folder for all cameras
  "ipLocal": "192.168.1.10", // local NIC IP the SDK binds to
  "cameras": [
    {
      "name": "Camera1",
      "enable": true,
      "type": "HikCamera",   // camera driver type (extensible)
      "code": 5,             // camera code (file-name prefix)
      "path": 1,             // capture path this camera watches
      "ip": "192.168.1.64",
      "port": 8000,
      "username": "admin",
      "password": "********",
      "folder": "../pictures/"   // per-camera override (folderCommon wins if non-empty)
    }
    // ... more cameras
  ]
}
```

## Deployment (systemd)

1. Copy the `install/` tree to the target machine, e.g. `/opt/camera/`.
2. Edit `camera.sh` and set `CAMERA_DIR` / `CAMERA_PATH` to your install location.
3. Edit `camera.service`:
   - set `Environment="LOCAL_IP=<same as ipLocal in camera_config.json>"`,
   - set `ExecStart` to the path of `camera.sh`.
4. Install and start:

```bash
sudo cp camera.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now camera.service
journalctl -u camera.service -f
```

The unit polls for up to 120 s until the configured static IP appears on any NIC, then waits 30 s extra for the routing table and SDK to stabilize — this avoids the classic cold-boot issue where `network-online.target` fires before the IP is assigned, causing HCNetSDK login error 3. The service restarts automatically (`Restart=always`, 10 s delay).

## Built-in Black Box

`blackbox` is a zero-dependency diagnostics subsystem compiled into the binary (`BB_DISABLE=1` in the environment turns it off):

- **Lock-free events** — `bbEvent()` uses a per-thread ring buffer; ~100 ns per event, no IO, no allocation on the hot path.
- **Stall watchdog** — business loops heartbeat on channels (`CameraLoop`, `PictureCapture`, `ListenerRecv`, `StrWrite`, `LoggerFlush`); a 100 ms patrol thread flags gaps ≥ 500 ms and triggers a dump at ≥ 2 s. Channels are armed/disarmed around their active periods to avoid idle false alarms.
- **Section timing** — `BbSpan("tag", thresholdMs)` measures any scope; slow sections become events, ≥ 2 s triggers a dump.
- **Dump files** — written to `../logs/blackbox/blackbox_<timestamp>_<reason>.log` containing a 30-second event timeline window; the newest files are kept (rate-limited per reason, LRU buckets so different failure signatures don't suppress each other).
- **Statistics** — `bbPrintStats()` dumps per-tag count/sum/max/over-threshold stats at shutdown.

Additionally, the camera loop runs a two-level UPDATE-stall heuristic: **L1** (UPDATE silent > 3.5 s *and* the event source fully silent > 8 s → log ALERT + dump) vs **L2** (only WHEEL silent, the event source still talking → session tail, counted and summarized per session, with a `multi_silent_wheel` dump if it repeats).

## Logging

- Singleton async logger (`Logger::getInstance()`), `PROD` and `TEST` levels (`testLog` config flag enables TEST).
- Log writes are fully decoupled from business threads: the queue critical section does memory moves only; a dedicated flusher thread writes batches and flushes once per batch.
- Daily log-file rotation; queue-full drops are summarized in a single warning line instead of spamming.
- Black-box instrumentation observes the flusher itself (heartbeat + per-batch span; a > 2 s flush auto-triggers a dump).

## Extending

To support a new camera vendor:

1. Derive from `Camera` and implement `processCommands()` (see `HikCamera` for the command protocol: `UPDATE`, `TRIGGERON`, `TRIGGEROFF` bytes followed by path/direction/time fields).
2. Add a branch in `Configurations::createCameras()` keyed on the `type` field of the JSON config.

## License

Proprietary — demo release, provided for reference only.

**Hikvision HCNetSDK**: the SDK is copyrighted by Hangzhou Hikvision Digital Technology Co., Ltd. Its binaries and headers are **not** committed to this repository and may not be redistributed here; each user must obtain a licensed copy from Hikvision's official download portal (see [Requirements](#requirements)). By building this project you agree to be bound by the Hikvision SDK license terms that accompany your download.

All other code in this repository is provided for internal use.
