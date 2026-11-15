#pragma once
#include "nlohmann/json.hpp"
#include <chrono>

class Logger;

#define OBJECT_TO_JSON_LOG(...)\
    virtual nlohmann::json ObjectToJson() override {\
        nlohmann::json jsonObject;\
        { __VA_ARGS__ }\
        return jsonObject;\
    }\

#define SET_LOG_ITEM(logObject){\
    jsonObject[#logObject] = logObject;\
}

class LogBase
{
	friend Logger;

public:
	LogBase() = default;
	virtual ~LogBase() = default;

public:
	virtual nlohmann::json ObjectToJson() = 0;

private:
	nlohmann::json ObjectToJsonImpl();
	void SetLogTime();
	void SetLogTime(std::chrono::system_clock::time_point inTime);

public:
	void SetLastErrorCode(const DWORD inLastErrorCode)
	{
		lastErrorCode = inLastErrorCode;
	}

private:
	std::string loggingTime;
	DWORD lastErrorCode = 0;
};