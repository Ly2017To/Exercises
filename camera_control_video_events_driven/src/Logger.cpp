#include "Logger.h"
#include "Utils.h"

#include <cstdarg>
#include <cerrno>
#include <cstdio>
#include <cstring>

Logger& Logger::instance()
{
    static Logger g_logger;
    return g_logger;
}

LogLevel Logger::parseLevel(const std::string& name)
{
    if (name == "debug" || name == "DEBUG") return LogLevel::Debug;
    if (name == "warn" || name == "WARN" || name == "warning") return LogLevel::Warn;
    if (name == "error" || name == "ERROR") return LogLevel::Error;
    return LogLevel::Info;
}

const char* Logger::levelName(LogLevel level)
{
    switch (level)
    {
    case LogLevel::Debug: return "DEBUG";
    case LogLevel::Info:  return "INFO ";
    case LogLevel::Warn:  return "WARN ";
    case LogLevel::Error: return "ERROR";
    }
    return "?????";
}

void Logger::init(const std::string& dir, LogLevel level)
{
    std::lock_guard<std::mutex> lock(mtx_);
    dir_ = dir.empty() ? "logs" : dir;
    level_ = level;
    // Diagnostic: print the directory the Logger actually received to stderr
    // (note: this happens before LOG_INFO is available, so we must use stderr)
    std::fprintf(stderr, "[Logger] init: dir param='%s' (empty=%d) -> dir_='%s' (absolute=%d)\n",
                 dir.c_str(), dir.empty() ? 1 : 0,
                 dir_.c_str(), (!dir_.empty() && dir_[0] == '/') ? 1 : 0);
    Utils::ensureDirectory(dir_);
}

void Logger::openFile(int year, int month, int day)
{
    // L-fix: before Logger::init() (dir_ is empty), disable file output.
    // Previously dir_ defaulted to "logs", which made LOG_INFO calls before
    // init() (e.g. inside ConfigLoader::load) write to a cwd-relative
    // bin/logs/camera_control_video_*.log, scattering the logs.
    if (dir_.empty())
        return;

    char date[16];
    std::snprintf(date, sizeof(date), "%04d%02d%02d", year, month, day);
    if (fp_ && curDate_ == date)
        return;
    if (fp_)
    {
        std::fclose(fp_);
        fp_ = nullptr;
    }
    Utils::ensureDirectory(dir_);
    std::string path = dir_ + "/camera_control_video_" + date + ".log";
    // Diagnostic: print the actual fopen path and return value to stderr
    std::fprintf(stderr, "[Logger] openFile: dir_='%s' (absolute=%d) path='%s'\n",
                 dir_.c_str(), (!dir_.empty() && dir_[0] == '/') ? 1 : 0, path.c_str());
    FILE* f = std::fopen(path.c_str(), "a");
    std::fprintf(stderr, "[Logger] openFile: fopen returned %p, errno=%d (%s)\n",
                 (void*)f, f ? 0 : errno, f ? "OK" : std::strerror(errno));
    if (f)
    {
        fp_ = f;
        curDate_ = date;
        lastOpenError_.clear();
        lastOpenPath_.clear();
    }
    else
    {
        // L9: record the fopen error (do NOT re-emit LOG_* under the same lock;
        //    save it for the next log() entry to flush out)
        lastOpenError_ = std::strerror(errno);
        lastOpenPath_  = path;
    }
}

void Logger::log(LogLevel level, const char* fmt, ...)
{
    if (level < level_)
        return;

    std::lock_guard<std::mutex> lock(mtx_);

    std::tm now = Utils::localTimeNow();
    openFile(now.tm_year + 1900, now.tm_mon + 1, now.tm_mday);

    char prefix[64];
    std::snprintf(prefix, sizeof(prefix), "%04d-%02d-%02d %02d:%02d:%02d [%s] ",
                  now.tm_year + 1900, now.tm_mon + 1, now.tm_mday,
                  now.tm_hour, now.tm_min, now.tm_sec, levelName(level));

    // L9: report the most recent fopen failure (right after the timestamp;
    //    does not block the current message)
    if (!lastOpenError_.empty())
    {
        std::fprintf(stderr,
                     "%s [ERROR] log file open failed: path=%s err=%s (subsequent logs only to stdout)\n",
                     prefix, lastOpenPath_.c_str(), lastOpenError_.c_str());
        lastOpenError_.clear();
    }

    char body[2048];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(body, sizeof(body), fmt, args);
    va_end(args);

    if (fp_)
        std::fprintf(fp_, "%s%s\n", prefix, body);
    std::fprintf(stdout, "%s%s\n", prefix, body);
}
