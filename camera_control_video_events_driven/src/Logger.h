#pragma once

#include <string>
#include <mutex>

enum class LogLevel
{
    Debug = 0,
    Info = 1,
    Warn = 2,
    Error = 3
};

// Thread-safe, daily-rotating, simple file logger
class Logger
{
public:
    static Logger& instance();

    void init(const std::string& dir, LogLevel level);
    void log(LogLevel level, const char* fmt, ...);

    static LogLevel parseLevel(const std::string& name);
    static const char* levelName(LogLevel level);

private:
    Logger() = default;
    void openFile(int year, int month, int day);

    std::mutex mtx_;
    std::string dir_;        // Empty by default: disable file output before init()
                          //   (stdout only)
    LogLevel level_ = LogLevel::Info;
    std::string curDate_;   // YYYYMMDD
    FILE* fp_ = nullptr;
    std::string lastOpenError_;  // L9: most recent fopen error (reported on stdout)
    std::string lastOpenPath_;   // L9: most recent fopen path that failed
};

#define LOG_DEBUG(...) Logger::instance().log(LogLevel::Debug, __VA_ARGS__)
#define LOG_INFO(...)  Logger::instance().log(LogLevel::Info,  __VA_ARGS__)
#define LOG_WARN(...)  Logger::instance().log(LogLevel::Warn,  __VA_ARGS__)
#define LOG_ERROR(...) Logger::instance().log(LogLevel::Error, __VA_ARGS__)
