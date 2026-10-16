#pragma once

#include <string>
#include <vector>

// Per-camera configuration
struct CameraConfig
{
    std::string name;
    bool        enable = true;
    std::string type = "HikCamera";   // Only HikCamera is currently supported
    int         code = 1;             // Camera number 1-9 (file-name C field)
    std::string ip;
    int         port = 8000;
    std::string username;
    std::string password;
    int         channel = 1;          // Hikvision channel number, default 1
    int         streamType = 0;       // 0 = main stream, 1 = sub stream
};

// Per-device configuration
struct AeiConfig
{
    bool        enable = true;        // Whether to enable (false skips this device)
    std::string name;                 // Display name (e.g. "Device-1")
    std::string stationid;            // 3-character site code
                                 //   (file-name XXX field)
    std::string aeiId;                // Device id (1-2 digit numeric string,
                                 //   used as the wire device id and the
                                 //   file-name N field)
    std::string ip;                   // Device server address
    int         port = 4000;          // Device server port
    std::vector<CameraConfig> cameras;
};

// Global configuration
struct AppConfig
{
    std::string videoOutDirs = "videos";     // Recording root (default <appRoot>/videos)
    int         queryIntervalMs = 5000;     // Query packet period (fixed 5s)
    int         answerTimeoutSec = 10;      // Answer timeout: drop + reconnect on expiry
    int         maxRecordSeconds = 180;     // Auto-stop recording after N seconds
                                       //   (per spec: 3 minutes)
    int         maxRecordSizeMB = 150;      // Auto-stop recording when file size
                                       //   reaches N MB (per spec: 150M)
    std::string logLevel = "info";          // Log level: debug/info/warn/error
                                       //   (read by the binary itself)
    std::string localIP;                    // Local IP for Hikvision SDK binding
                                       //   (required on multi-NIC hosts;
                                       //    empty = no binding)
    int         totalAeiCount = 0;          // Total AEIs in the config file
                                       //   (including enable=false)
    int         totalCameraCount = 0;       // Total cameras in the config file
                                       //   (including enable=false)
    std::vector<AeiConfig> aeis;            // AEI list after enable filter
};

// Config loader: strict nlohmann/json parse (RFC 8259); falls back to defaults
// on missing or wrong-type fields.
class ConfigLoader
{
public:
    // Returns true on success; on failure, err contains a human-readable reason
    static bool load(const std::string& filePath, AppConfig& cfg, std::string& err);
};
