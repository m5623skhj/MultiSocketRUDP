#pragma once
#include "LogClass.h"
#include <string>

#pragma comment(lib, "Logger.lib")

class ClientLog : public LogBase
{
public:
	OBJECT_TO_JSON_LOG(
		SET_LOG_ITEM(logString);
	);

public:
	std::string logString;
};
