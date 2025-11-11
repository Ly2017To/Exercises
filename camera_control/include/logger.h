#ifndef LOGGER_H
#define LOGGER_H

#include <iostream>
#include <fstream>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <chrono>

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
    void log(const std::string& message, LogType logType);

    // Set flags to enable/disable production and test logs
    void enableProdLogs(bool enable);
    void enableTestLogs(bool enable);

    // Stop the logging thread gracefully
    void stop();

private:
    Logger();  // Private constructor for singleton pattern
    ~Logger();

    // Internal method to process logs asynchronously
    void processLogs();

	// if the log folder is not created, then create it
	void checkAndCreateLogFolder();

    // Internal method to check and update the log file if the date changes
    void checkAndUpdateLogFile();

    // Get the log file name based on the current date
    std::string getLogFileName();

    // Member variables
    bool stopLogging;
    bool prodLogsEnabled;  // Flag for enabling/disabling production logs
    bool testLogsEnabled;  // Flag for enabling/disabling test logs
    std::string currentDate;
    std::string logTypeString;
    std::ofstream logFile;
    std::queue<std::string> logQueue;
    std::mutex queueMutex;
    std::condition_variable queueCondition;
    std::thread loggingThread;
};

#endif // LOGGER_H

