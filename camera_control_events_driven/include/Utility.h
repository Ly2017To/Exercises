#ifndef UTILITY_H
#define UTILITY_H

#include <iostream>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <ctime>

//put 0xXX for each command type to test
enum CommandType{
	UPDATE = ,
	TRIGGERON = ,
	TRIGGEROFF = 
};

//put 0xXX for each command type to test
constexpr uint8_t commandType = ;
constexpr uint8_t commandPath = ;
constexpr uint8_t commandDir = ;
constexpr uint8_t commandCarNum = ;

// put some number to test
constexpr int maxAxle = 99;
constexpr int maxTrain = 999;
constexpr int maxCar = 99;
constexpr int maxPicture= 9998;

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

     static inline int bcdToInt(uint8_t bcd) {
    const int tens = (bcd >> 4) & 0x0F; // Extract upper nibble (tens digit)
    const int units = bcd & 0x0F;        // Extract lower nibble (units digit)
    
    // Optional: Uncomment to validate BCD (each nibble must be 0-9)
    // if (tens > 9 || units > 9) {
    //     throw std::invalid_argument("Invalid BCD input: nibble out of 0-9 range");
    //     // Or return an error string if exceptions are not allowed
    // }
    
    return tens * 10 + units;
}

	// New function to format BCD time values to your required format
	std::string getFormattedTimeFromBCD(const std::string& buf) {
    	// Convert all BCD bytes to decimal integers
    	const int yearHigh = bcdToInt(static_cast<uint8_t>(buf[3]));
    	const int yearLow = bcdToInt(static_cast<uint8_t>(buf[4]));
    	const int fullYear = yearHigh * 100 + yearLow; // Combine to 4-digit year

    	const int month = bcdToInt(static_cast<uint8_t>(buf[5]));
    	const int day = bcdToInt(static_cast<uint8_t>(buf[6]));
    	const int hour = bcdToInt(static_cast<uint8_t>(buf[7]));   // Assumes 24-hour format BCD
    	const int minute = bcdToInt(static_cast<uint8_t>(buf[8]));
    	const int second = bcdToInt(static_cast<uint8_t>(buf[9]));

    	// Optional: Validate time values (adjust ranges for your use case)
    	// if (month < 1 || month > 12 || day < 1 || day > 31 || hour > 23 || minute > 59 || second > 59) 			{
    	//     return "INVALID_TIME";
    	// }

    	// Format to string with zero-padding
    	std::stringstream timeStream;
    	timeStream << std::setw(4) << std::setfill('0') << fullYear // 4-digit year, zero-padded
               << std::setw(2) << std::setfill('0') << month    // 2-digit month, zero-padded
               << std::setw(2) << std::setfill('0') << day      // 2-digit day, zero-padded
               << std::setw(2) << std::setfill('0') << hour     // 2-digit hour (24h), zero-padded
               << std::setw(2) << std::setfill('0') << minute   // 2-digit minute, zero-padded
               << std::setw(2) << std::setfill('0') << second   // 2-digit second, zero-padded
               << "000"; // Fixed 0ms as requested

    	return timeStream.str();
	}

    // Destructor (optional)
    ~Utility() = default;
};

#endif // UTILITY_H

