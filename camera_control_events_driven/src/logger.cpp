#include "logger.h"
#include "blackbox.h"
#include <iostream>
#include <chrono>
#include <iomanip>
#include <ctime>
#include <sstream>
#include <vector>
#include <filesystem>
#include <sys/stat.h>  // For chmod

namespace fs = std::filesystem;  // Alias for filesystem namespace

Logger::Logger()
    : stopLogging(false), prodLogsEnabled(true), testLogsEnabled(false),
      currentDate(""), droppedCount(0), reportedDropped(0) {
	 checkAndCreateLogFolder(); 
    checkAndUpdateLogFile();  // Create the initial log file
    loggingThread = std::thread(&Logger::processLogs, this);  // Start log processing thread
}

Logger::~Logger() {
    stop();
}

Logger& Logger::getInstance() {
    static Logger instance;
    return instance;
}

/*
 * Log flushing thread (P0-1 decoupling refactor)
 *
 * Before: inside the critical section holding queueMutex, each entry was written
 *         with logFile << ... << std::endl (forced flush per entry). Once the disk
 *         stalled, the lock was held for seconds, freezing every business thread
 *         calling Logger::log (command processing thread, message listener thread,
 *         etc.) for 5~19s; picture naming then froze on stale axle numbers.
 *
 * After: inside the lock only a batch memory move happens (microseconds); disk
 *        writes are moved outside the lock; one write per batch, one flush at the
 *        end of the batch. Business threads' Logger::log only enqueues and is
 *        fully decoupled from disk state.
 */
void Logger::processLogs() {
    while (!stopLogging) {
        std::vector<std::string> batch;
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            queueCondition.wait(lock, [this] { return !logQueue.empty() || stopLogging; });

            // Inside the lock: memory move only; no disk IO whatsoever
            batch.reserve(logQueue.size());
            while (!logQueue.empty()) {
                batch.push_back(std::move(logQueue.front()));
                logQueue.pop();
            }
        }  // <- release the lock immediately; the disk writes below no longer block any business thread

        if (batch.empty()) {
            continue;
        }

        bbHeartbeat(BbChannel::LoggerFlush);
        BbSpan flushSpan("log_flush", 100);  // per-batch flush duration; over 2s automatically triggers a black-box dump

        checkAndUpdateLogFile();  // Check if we need to switch the log file

        for (const std::string& line : batch) {
            logFile << line << '\n';
        }

        // Summarize drops caused by a full queue in one line (no per-entry spam)
        uint64_t dropped = droppedCount.load();
        uint64_t reported = reportedDropped.load();
        if (dropped > reported) {
            logFile << "[log] WARNING: " << (dropped - reported)
                    << " message(s) dropped because log queue is full (total dropped: "
                    << dropped << ")\n";
            reportedDropped.store(dropped);
        }

        logFile.flush();  // flush once per batch, replacing the per-entry forced std::endl flush
    }

    // Drain the remaining queue before exiting so tail entries are not lost at shutdown (logFile is not yet closed by stop() here)
    std::vector<std::string> tail;
    {
        std::unique_lock<std::mutex> lock(queueMutex);
        while (!logQueue.empty()) {
            tail.push_back(std::move(logQueue.front()));
            logQueue.pop();
        }
    }
    if (!tail.empty()) {
        checkAndUpdateLogFile();
        for (const std::string& line : tail) {
            logFile << line << '\n';
        }
        logFile.flush();
    }
}

void Logger::log(const std::string& message, LogType logType) {
    // Only log if the type of log is enabled
    if ((logType == TEST && !testLogsEnabled) || (logType == PROD && !prodLogsEnabled)) {
        return;  // Do not log if the log type is disabled
   }

    // Timestamp and type tag are assembled at enqueue time: the flush thread no longer touches shared state
    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm timeInfo{};
#if defined(_WIN32)
    localtime_s(&timeInfo, &now);
#else
    localtime_r(&now, &timeInfo);
#endif
    char timeBuffer[100];
    std::strftime(timeBuffer, sizeof(timeBuffer), "%Y-%m-%d %H:%M:%S", &timeInfo);

    std::string line;
    line.reserve(message.size() + 40);
    line.append("[").append(timeBuffer).append("] ")
        .append(logType == PROD ? "[PROD]" : "[TEST]")
        .append(" ").append(message);

    {
        std::unique_lock<std::mutex> lock(queueMutex);
        if (logQueue.size() >= MAX_QUEUE_SIZE) {
            // Bounded queue: drop the oldest? No - drop this entry and count it, so the caller never blocks
            droppedCount.fetch_add(1);
            return;
        }
        logQueue.push(std::move(line));
    }
    bbEvent("log_enqueue", 0);
    queueCondition.notify_one();  // Notify logging thread
}

void Logger::enableProdLogs(bool enable) {
    prodLogsEnabled.store(enable);
}

void Logger::enableTestLogs(bool enable) {
    testLogsEnabled.store(enable);
}

void Logger::stop() {
    if (stopLogging.exchange(true)) {
        return;  // already stopped
    }
    queueCondition.notify_one();  // Notify logging thread to exit
    if (loggingThread.joinable()) {
        loggingThread.join();
    }

    if (logFile.is_open()) {
        logFile.close();
    }
}

void Logger::checkAndCreateLogFolder() {
    // Ensure the log directory exists
    std::string logDir = "../logs";
    if (!fs::exists(logDir)) {
        try {
            fs::create_directories(logDir);  // Create the log directory if it doesn't exist
            std::cout << "Created folder for Logs" << std::endl;

            // Set permissions to 777 (read, write, execute for owner, group, and others)
            if (chmod(logDir.c_str(), S_IRWXU | S_IRWXG | S_IRWXO) != 0) {
         			std::cerr << "Error setting permissions for logs folde" << std::endl;
            } else {
                std::cout <<  "Set 777 permissions for logs folder" << std::endl;
            }
        } catch (const fs::filesystem_error& e) {
            std::cerr << "Error creating folder for Logs " + std::string(e.what()) <<std::endl;;
        }
    } else {
		std::cout << "Folder already exists for Logs" << std::endl;
	}
}

std::string Logger::getLogFileName() {
    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm timeInfo{};
#if defined(_WIN32)
    localtime_s(&timeInfo, &now);
#else
    localtime_r(&now, &timeInfo);
#endif

    // Format the date as YYYY-MM-DD
    char dateBuffer[20];
    std::strftime(dateBuffer, sizeof(dateBuffer), "%Y-%m-%d", &timeInfo);

    // Construct the log file name as "YYYY-MM-DD.log"
    std::string fileName = dateBuffer;
    fileName += ".log";

    std::string prefix = "../logs/";
    fileName = prefix + fileName;

    return fileName;
}

void Logger::checkAndUpdateLogFile() {
    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm timeInfo{};
#if defined(_WIN32)
    localtime_s(&timeInfo, &now);
#else
    localtime_r(&now, &timeInfo);
#endif

    // Get today's date in YYYY-MM-DD format
    char dateBuffer[20];
    std::strftime(dateBuffer, sizeof(dateBuffer), "%Y-%m-%d", &timeInfo);
    std::string newDate = dateBuffer;

    // If the date has changed, create a new log file
    if (newDate != currentDate) {
        currentDate = newDate;

        // Close the old log file if it is open
        if (logFile.is_open()) {
            logFile.close();
        }

        // Open the new log file
        logFile.open(getLogFileName(), std::ios::out | std::ios::app);
        if (!logFile.is_open()) {
            std::cerr << "Failed to open log file!" << std::endl;
        }
    }
}
