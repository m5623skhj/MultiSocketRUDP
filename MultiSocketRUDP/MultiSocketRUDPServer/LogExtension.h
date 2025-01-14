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