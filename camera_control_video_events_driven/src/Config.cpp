#include "Config.h"
#include "Logger.h"

// nlohmann/json 3.12.0 (single-header, vendored at src/json.hpp, MIT).
// Strict RFC 8259 parse; falls back to defaults on type mismatch; error messages
// include line/column for easy diagnosis.
#include "json.hpp"

#include <fstream>
#include <sstream>
#include <string>

using json = nlohmann::json;

namespace
{

// Read a string field (returns def if missing or wrong type)
std::string getStr(const json& j, const char* key, const std::string& def = {})
{
    try
    {
        if (j.contains(key) && j.at(key).is_string())
            return j.at(key).get<std::string>();
    }
    catch (const std::exception&)
    {
    }
    return def;
}

// Read an integer field (returns def if missing or wrong type)
int getInt(const json& j, const char* key, int def)
{
    try
    {
        if (j.contains(key) && j.at(key).is_number_integer())
            return j.at(key).get<int>();
    }
    catch (const std::exception&)
    {
    }
    return def;
}

// Read a boolean field (returns def if missing or wrong type)
bool getBool(const json& j, const char* key, bool def)
{
    try
    {
        if (j.contains(key) && j.at(key).is_boolean())
            return j.at(key).get<bool>();
    }
    catch (const std::exception&)
    {
    }
    return def;
}

CameraConfig parseCamera(const json& j)
{
    CameraConfig c;
    c.name       = getStr(j, "name");
    c.enable     = getBool(j, "enable", true);
    c.type       = getStr(j, "type", "HikCamera");
    c.code       = getInt(j, "code", 1);
    c.ip         = getStr(j, "ip");
    c.port       = getInt(j, "port", 8000);
    c.username   = getStr(j, "username");
    c.password   = getStr(j, "password");
    c.channel    = getInt(j, "channel", 1);
    c.streamType = getInt(j, "streamType", 0);
    return c;
}

AeiConfig parseAei(const json& j)
{
    AeiConfig a;
    a.enable    = getBool(j, "enable", true);
    a.name      = getStr(j, "name");
    a.stationid = getStr(j, "stationid");
    a.aeiId     = getStr(j, "AEIid");
    a.ip        = getStr(j, "ip");
    a.port      = getInt(j, "port", 4000);
    if (j.contains("cameras") && j.at("cameras").is_array())
    {
        for (const auto& jc : j.at("cameras"))
        {
            if (jc.is_object())
                a.cameras.push_back(parseCamera(jc));
        }
    }
    return a;
}

} // namespace

bool ConfigLoader::load(const std::string& filePath, AppConfig& cfg, std::string& err)
{
    std::ifstream in(filePath, std::ios::binary);
    if (!in.is_open())
    {
        err = "cannot open config file: " + filePath;
        return false;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    const std::string text = ss.str();

    json root;
    try
    {
        root = json::parse(text);
    }
    catch (const json::parse_error& e)
    {
        // nlohmann/json already includes line/column in its diagnostic
        err = filePath + ": JSON parse error at byte " + std::to_string(e.byte) +
              ": " + e.what();
        return false;
    }

    if (!root.is_object())
    {
        err = filePath + ": root must be a JSON object";
        return false;
    }

    // Global fields (defaults if missing)
    cfg.videoOutDirs     = getStr(root, "videooutdirs", "videos");
    cfg.queryIntervalMs  = getInt(root, "queryIntervalMs", 5000);
    cfg.answerTimeoutSec = getInt(root, "answerTimeoutSec", 10);
    cfg.maxRecordSeconds = getInt(root, "maxRecordSeconds", 180);
    cfg.maxRecordSizeMB  = getInt(root, "maxRecordSizeMB", 150);
    // logDir is no longer read from JSON: hardcoded to <install>/logs
    //   (sibling of bin/)
    cfg.logLevel         = getStr(root, "logLevel", "info");
    cfg.localIP          = getStr(root, "localIP", "");
    cfg.aeis.clear();

    if (root.contains("AeiInfo") && root.at("AeiInfo").is_array())
    {
        // First count the "config total" (including enable=false) for logging
        // and the test register.
        cfg.totalAeiCount = root.at("AeiInfo").size();
        cfg.totalCameraCount = 0;
        for (const auto& ja : root.at("AeiInfo"))
        {
            if (ja.is_object() && ja.contains("cameras") && ja.at("cameras").is_array())
                cfg.totalCameraCount += ja.at("cameras").size();
        }
        // Then apply the enable filter into cfg.aeis
        cfg.aeis.clear();
        for (const auto& ja : root.at("AeiInfo"))
        {
            if (ja.is_object())
                cfg.aeis.push_back(parseAei(ja));
        }
    }
    else
    {
        err = filePath + ": missing or invalid \"AeiInfo\" array";
        return false;
    }

    // Sanity constraints
    if (cfg.queryIntervalMs < 100)
        cfg.queryIntervalMs = 100;
    if (cfg.answerTimeoutSec < 2)
        cfg.answerTimeoutSec = 2;
    if (cfg.maxRecordSeconds <= 0)
        cfg.maxRecordSeconds = 180;
    if (cfg.maxRecordSizeMB <= 0)
        cfg.maxRecordSizeMB = 150;

    // Drop AEIs with enable=false or missing ip/port
    for (auto it = cfg.aeis.begin(); it != cfg.aeis.end();)
    {
        if (!it->enable)
        {
            LOG_INFO("AEI [%s] disabled (enable=false), skipped", it->name.c_str());
            it = cfg.aeis.erase(it);
            continue;
        }
        if (it->ip.empty() || it->port <= 0)
        {
            LOG_WARN("AEI [%s] missing ip/port, skipped", it->name.c_str());
            it = cfg.aeis.erase(it);
            continue;
        }
        if (it->stationid.empty() || it->aeiId.empty())
            LOG_WARN("AEI [%s] missing stationid/AEIid, recording filename may be abnormal",
                     it->name.c_str());
        ++it;
    }

    if (cfg.aeis.empty())
    {
        err = filePath + ": no valid AeiInfo entry";
        return false;
    }
    return true;
}
