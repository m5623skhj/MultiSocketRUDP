#pragma once
#include "LogClass.h"

#pragma comment(lib, "Logger.lib")

class ServerLog : public LogBase
{
public:
	OBJECT_TO_JSON_LOG(
		SET_LOG_ITEM(logString);
	);

public:
	std::string logString;
};

#ifdef NDEBUG
#define LOG_DEBUG(x) ((void)0)
#else
#define LOG_DEBUG(LOG_STRING) auto log = Logger::MakeLogObject<ServerLog>(); \
			log->logString = LOG_STRING; \
			Logger::GetInstance().WriteLog(log);
#endif