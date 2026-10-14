#ifndef LOGGER_H
#define LOGGER_H

#include <iostream>
#include <fstream>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <chrono>
#include <atomic>
#include <cstdint>

class Logger {
public:
    // Enum for Log Type
    enum LogType {
        PROD,
        TEST
    };

    // Singleton pattern to get the instance of the Logger
    static Logger& getInstance();

    // Log a message with a specific type
    // Note: this call only enqueues in memory (microseconds); it does no disk IO at all
    void log(const std::string& message, LogType logType);

    // Set flags to enable/disable production and test logs
    void enableProdLogs(bool enable);
    void enableTestLogs(bool enable);

    // Stop the logging thread gracefully
    void stop();

    // Queue upper bound (prevents logs from occupying memory unboundedly while the disk stalls for a long time)
    static constexpr size_t MAX_QUEUE_SIZE = 5000;

    // Number of log entries dropped because the queue was full
    uint64_t getDroppedCount() const { return droppedCount.load(); }

private:
    Logger();  // Private constructor for singleton pattern
    ~Logger();

    // Internal method to process logs asynchronously
    // Two phases: only move memory inside the lock; do disk writes outside it
    void processLogs();

	// if the log folder is not created, then create it
	void checkAndCreateLogFolder();

    // Internal method to check and update the log file if the date changes
    void checkAndUpdateLogFile();

    // Get the log file name based on the current date
    std::string getLogFileName();

    // Member variables
    std::atomic<bool> stopLogging;
    std::atomic<bool> prodLogsEnabled;  // Flag for enabling/disabling production logs
    std::atomic<bool> testLogsEnabled;  // Flag for enabling/disabling test logs
    std::string currentDate;
    std::ofstream logFile;
    std::queue<std::string> logQueue;
    std::mutex queueMutex;
    std::condition_variable queueCondition;
    std::thread loggingThread;
    std::atomic<uint64_t> droppedCount;      // drop count when the queue is full
    std::atomic<uint64_t> reportedDropped;   // dropped count already reported in the log (avoids duplicate summaries)
};

#endif // LOGGER_H
