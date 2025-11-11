#ifndef UTILITY_H
#define UTILITY_H

#include <iostream>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <ctime>

enum CommandType{
	UPDATE = 0x00,
	TRIGGERON = 0x01,
	TRIGGEROFF = 0x02
};

constexpr int maxPicture= 9999;

struct msgTime_t {
	unsigned int nYear;
    unsigned char cMonth;
    unsigned char cDay;
    unsigned char cHour;
    unsigned char cMinute;
    unsigned char cSecond;
};

class Utility {
private:
    // Private constructor (optional: not needed for the non-singleton version)
    Utility() {}

public:
	static Utility createInstance() {
        return Utility();  // returns a **new instance** each time
   }

    // Convert decimal to BCD
    unsigned char dec2Bcd(unsigned char iNum) {
        return static_cast<unsigned char>(((iNum / 10) << 4) + (iNum % 10));
    }

    // Convert BCD to decimal
    unsigned char bcd2Dec(unsigned char iNum) {
        return static_cast<unsigned char>(((iNum >> 4) * 10) + (iNum & 0x0F));
    }

    // Convert struct tm to custom time format (msgTime_t)
    void timeFmtCvtTm2Std(msgTime_t &pTime, const struct tm &pTm) {
        pTime.nYear = pTm.tm_year + 1900;
        pTime.cMonth = pTm.tm_mon + 1;
        pTime.cDay = pTm.tm_mday;
        pTime.cHour = pTm.tm_hour;
        pTime.cMinute = pTm.tm_min;
        pTime.cSecond = pTm.tm_sec;
    }

    // Get current system time and convert it to STRUCT_TIME format
    void getRTCTime(msgTime_t &pTime) {
        time_t rawTime;
          struct tm *tm1;
        time(&rawTime);
        tm1 = localtime(&rawTime);
        timeFmtCvtTm2Std(pTime, *tm1);
    }

    // Get the current system time in YYYYMMDDHHmmssms format
    std::string getFormattedTime() {
        // Get current time point
        auto now = std::chrono::system_clock::now();

        // Get the time since epoch and convert it to a tm struct
      std::time_t currentTime = std::chrono::system_clock::to_time_t(now);
      std::tm* tm = std::localtime(&currentTime);

        // Get milliseconds (fractional part of the current time)
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;

        // Use stringstream to format the string
      std::stringstream timeStream;
      timeStream << std::put_time(tm, "%Y%m%d%H%M%S")  // Format as YYYYMMDDHHmmss
                   << std::setw(3) << std::setfill('0')  // Pad milliseconds with leading zeros
                   << ms;  // Add milliseconds

        return timeStream.str();
    }

    // Destructor (optional)
    ~Utility() = default;
};

#endif // UTILITY_H

