#include "logger.h"
#include <iostream>
#include <chrono>
#include <iomanip>
#include <ctime>
#include <sstream>
#include <filesystem>
#include <sys/stat.h>  // For chmod

namespace fs = std::filesystem;  // Alias for filesystem namespace

Logger::Logger()
    : stopLogging(false), prodLogsEnabled(true), testLogsEnabled(false), currentDate("") {
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

void Logger::processLogs() {
    while (!stopLogging) {
        std::unique_lock<std::mutex> lock(queueMutex);
        queueCondition.wait(lock, [this] { return !logQueue.empty() || stopLogging; });

        while (!logQueue.empty()) {
            std::string message = logQueue.front();
            logQueue.pop();

            checkAndUpdateLogFile();  // Check if we need to switch the log file

            auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
            std::tm* timeInfo = std::localtime(&now);

            // Format timestamp
            char timeBuffer[100];
            std::strftime(timeBuffer, sizeof(timeBuffer), "%Y-%m-%d %H:%M:%S", timeInfo);

            // Write the message to the log file, including the log type (PROD or TEST)
            logFile << "[" << timeBuffer << "] " << logTypeString << " " << message << std::endl;
        }
    }
}

void Logger::log(const std::string& message, LogType logType) {
    // Only log if the type of log is enabled
    if ((logType == TEST && !testLogsEnabled) || (logType == PROD && !prodLogsEnabled)) {
        return;  // Do not log if the log type is disabled
    }

    std::unique_lock<std::mutex> lock(queueMutex);

    // Set log type based on the passed enum value
    switch (logType) {
        case PROD:
            logTypeString = "[PROD]";
            break;
        case TEST:
            logTypeString = "[TEST]";
            break;
        default:
            logTypeString = "[UNKNOWN]";
    }
    
    logQueue.push(message);
    queueCondition.notify_one();  // Notify logging thread
}

void Logger::enableProdLogs(bool enable) {
    prodLogsEnabled = enable;
}

void Logger::enableTestLogs(bool enable) {
    testLogsEnabled = enable;
}

void Logger::stop() {
    stopLogging = true;
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
    std::tm* timeInfo = std::localtime(&now);

    // Format the date as YYYY-MM-DD
    char dateBuffer[20];
    std::strftime(dateBuffer, sizeof(dateBuffer), "%Y-%m-%d", timeInfo);

    // Construct the log file name as "YYYY-MM-DD.log"
    std::string fileName = dateBuffer;
    fileName += ".log";

    std::string prefix = "../logs/";
    fileName = prefix + fileName;

    return fileName;
}

void Logger::checkAndUpdateLogFile() {
    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm* timeInfo = std::localtime(&now);

    // Get today's date in YYYY-MM-DD format
    char dateBuffer[20];
    std::strftime(dateBuffer, sizeof(dateBuffer), "%Y-%m-%d", timeInfo);
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

