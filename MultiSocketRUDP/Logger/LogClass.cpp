#include "PreCompile.h"
#include "LogClass.h"

nlohmann::json LogBase::ObjectToJsonImpl()
{
	nlohmann::json logJsonObject;
	logJsonObject["LogTime"] = loggingTime;
	logJsonObject["GetLastErrorCode"] = lastErrorCode;
	logJsonObject["Log"] = ObjectToJson();

	return logJsonObject;
}

void LogBase::SetLogTime()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t currentTime = std::chrono::system_clock::to_time_t(now);

    std::tm utcTime = {};

    if (const auto error = gmtime_s(&utcTime, &currentTime); error != 0)
    {
        std::cerr << "Error in gmtime_s() : " << error << '\n';
        loggingTime = "INVALID_TIME";
        return;
    }

    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count() % 1000;

    char buffer[128];
    std::snprintf(buffer, sizeof(buffer),
        "%04d-%02d-%02d %02d:%02d:%02d.%03lld",
        utcTime.tm_year + 1900,
        utcTime.tm_mon + 1,
        utcTime.tm_mday,
        utcTime.tm_hour,
        utcTime.tm_min,
        utcTime.tm_sec,
        milliseconds);

    loggingTime = buffer;
}