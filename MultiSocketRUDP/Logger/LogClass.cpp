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
	std::time_t currentTime = std::chrono::system_clock::to_time_t(now);
	std::tm utcTime;

	auto error = gmtime_s(&utcTime, &currentTime);
	if (error != 0)
	{
		std::cout << "Error in gmtime_s() : " << error << std::endl;
		g_Dump.Crash();
	}
	auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;

	std::stringstream stream;
	stream << std::put_time(&utcTime, "%Y-%m-%d %H:%M:%S.") << std::setfill('0') << std::setw(3) << milliseconds;

	loggingTime = stream.str();
}
