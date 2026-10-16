// Event-Driven Video Capture Application (TCP client side)
//
// Features:
//   1. Acts as a TCP client connecting to multiple device servers and periodically
//      sends query packets (protocol type 0x01).
//   2. Processes answer packets (0x02): triggers Hikvision network camera recording
//      on the active state, and stops recording on the idle state.
//   3. For all other packet types, only replies with confirm packets (0x04);
//      no business processing is performed.
//   4. Recording file naming and auto-stop conditions follow the
//      "CPS / Camera Service Communication Protocol" file-naming section.

#include "AeiClient.h"
#include "CameraManager.h"
#include "Config.h"
#include "Logger.h"
#include "Utils.h"
#include "build_version.h"

#include <boost/asio.hpp>

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace
{

void printUsage(const char* prog)
{
    std::printf("Usage: %s [-c config_file]\n", prog);
    std::printf("  -c config_file   config file path (default: ../config/aeiconfig.json)\n");
}

} // namespace

int main(int argc, char* argv[])
{
    std::string configPath = "../config/aeiconfig.json";
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "-c") == 0 && i + 1 < argc)
        {
            configPath = argv[++i];
        }
        else if (std::strcmp(argv[i], "-h") == 0 || std::strcmp(argv[i], "--help") == 0)
        {
            printUsage(argv[0]);
            return 0;
        }
        else
        {
            printUsage(argv[0]);
            return 1;
        }
    }

    // 1. Load configuration
    AppConfig cfg;
    std::string err;
    if (!ConfigLoader::load(configPath, cfg, err))
    {
        std::fprintf(stderr, "load config failed: %s\n", err.c_str());
        return 1;
    }

    // 1.5 Compute appRoot (based on executable location)
    // Rule: when the exe lives at <root>/bin/, appRoot = <root> (logs/videos are
    //       siblings of bin/), independent of cwd at startup — so systemd or any
    //       arbitrary launch directory all work correctly.
    std::string appRoot = Utils::appRootDir();
    if (appRoot.empty())
    {
        // Fallback: when /proc/self/exe is unavailable, derive from argv[0]
        //          (argv[0] must be an absolute path)
        std::string fromArgv = Utils::exeDirFromArgv0(argv[0]);
        if (!fromArgv.empty())
        {
            std::fprintf(stderr,
                "[path-resolve] fallback: using argv[0]=%s to derive exe dir=%s\n",
                argv[0], fromArgv.c_str());
            size_t pos = fromArgv.find_last_of('/');
            std::string last = (pos != std::string::npos) ? fromArgv.substr(pos + 1) : fromArgv;
            if (last == "bin" && pos != std::string::npos && pos != 0)
                appRoot = fromArgv.substr(0, pos);
            else
                appRoot = fromArgv;
        }
    }
    std::fprintf(stderr, "[path-resolve] exeDir=%s appRoot=%s\n",
                 Utils::exeDir().c_str(), appRoot.c_str());

    // 1.6 Log directory is hardcoded to <appRoot>/logs (sibling of bin/;
    //     not configurable in JSON).
    //     Operational convention: logs always live at install/logs/ to make
    //     collection, rotation, and backup straightforward.
    if (appRoot.empty())
    {
        // Failure to resolve appRoot is fatal: the log path is undefined, and
        // we explicitly do NOT fall back to a cwd-relative path (which would
        // otherwise write to bin/logs/ or another wrong location)
        std::fprintf(stderr,
            "[FATAL] cannot determine appRoot (readlink /proc/self/exe failed).\n"
            "        Log path is undefined, refusing to start.\n"
            "        Run binary with absolute path or set LD_PRELOAD to expose /proc.\n");
        return 1;
    }
    std::string logDir = Utils::joinPath(appRoot, "logs");
    std::fprintf(stderr, "[path-resolve] logDir (hardcoded) = %s\n", logDir.c_str());

    // 1.7 Resolve videoOutDirs from config: prepend appRoot to relative paths,
    //     keep absolute paths as-is
    {
        auto resolveRel = [&appRoot](std::string& p, const char* what) {
            if (p.empty()) return;
            if (p[0] == '/') return;  // absolute path: keep as-is
            if (appRoot.empty()) return; // resolution failed: keep as-is
            // Relative paths containing "..": the user has explicitly
            // specified the hierarchy, so keep them as-is and let the OS resolve
            if (p.find("..") != std::string::npos)
            {
                std::fprintf(stderr,
                    "[path-resolve] %s: %s (contains '..', kept as-is; depends on cwd)\n",
                    what, p.c_str());
                return;
            }
            std::string before = p;
            p = Utils::joinPath(appRoot, p);
            std::fprintf(stderr, "[path-resolve] %s: %s -> %s\n", what, before.c_str(), p.c_str());
        };
        resolveRel(cfg.videoOutDirs,  "videoOutDirs");
    }

    // 2. Initialize logger (using the hardcoded logDir)
    Logger::instance().init(logDir, Logger::parseLevel(cfg.logLevel));
    LOG_INFO("===== camera_control_video starting =====");
    LOG_INFO("version=%s build=%s", APP_VERSION, APP_BUILD_TIME);
    LOG_INFO("config=%s", configPath.c_str());
    std::string cfgSha = Utils::fileSha256(configPath);
    if (!cfgSha.empty())
        LOG_INFO("config sha256=%s", cfgSha.c_str());
    // AEI/camera counts: active = runtime count after enable filter; config = total in config file
    LOG_INFO("aei count=%zu/%d (active/config), query interval=%dms, answer timeout=%ds, max record=%ds/%dMB, video dir=%s",
             cfg.aeis.size(), cfg.totalAeiCount, cfg.queryIntervalMs, cfg.answerTimeoutSec,
             cfg.maxRecordSeconds, cfg.maxRecordSizeMB, cfg.videoOutDirs.c_str());
    LOG_INFO("log dir=%s (hardcoded, sibling of bin/)", logDir.c_str());
    LOG_INFO("localIP=%s", cfg.localIP.empty() ? "(not set, use default NIC)" : cfg.localIP.c_str());

    // 2.5 Pre-create the recording root directory (was lazy: created only at the
    //     first recording).
    //     Validate the path and permissions at startup, so issues like
    //     "directory missing", "mistakenly created as a file", "no write
    //     permission" do not lurk until the first event. The log clearly
    //     distinguishes "ready" from "unusable".
    if (!cfg.videoOutDirs.empty())
    {
        if (Utils::ensureDirectory(cfg.videoOutDirs))
            LOG_INFO("video dir ready: %s", cfg.videoOutDirs.c_str());
        else
            LOG_ERROR("video dir not usable: %s "
                      "(create failed, or path exists but is not a directory)",
                      cfg.videoOutDirs.c_str());
    }

    // 3. Initialize Hikvision SDK
    if (!CameraManager::globalSdkInit(cfg.localIP))
    {
        LOG_ERROR("HCNetSDK init failed, exit");
        return 1;
    }

    // 4. Assemble modules
    CameraManager cameraManager(cfg);
    cameraManager.start();

    boost::asio::io_context io;

    std::vector<std::shared_ptr<AeiClient>> clients;
    clients.reserve(cfg.aeis.size());
    for (const auto& aei : cfg.aeis)
    {
        auto client = std::make_shared<AeiClient>(io, aei, cfg.queryIntervalMs,
                                                  cfg.answerTimeoutSec, cameraManager);
        client->start();
        clients.push_back(std::move(client));
    }

    // 5. Signal handling: graceful exit on SIGINT / SIGTERM
    boost::asio::signal_set signals(io, SIGINT, SIGTERM);
    signals.async_wait([&io](boost::system::error_code, int sig) {
        LOG_INFO("received signal %d (%s), shutting down...", sig, strsignal(sig));
        io.stop();
    });

    // 6. Event loop (network thread runs in the main thread)
    io.run();

    // 7. Cleanup
    LOG_INFO("===== shutdown sequence start =====");
    for (auto& c : clients)
        c->stop();
    cameraManager.stopAllRecording();
    cameraManager.stop();
    CameraManager::globalSdkCleanup();
    LOG_INFO("===== camera_control_video exited normally =====");
    return 0;
}
