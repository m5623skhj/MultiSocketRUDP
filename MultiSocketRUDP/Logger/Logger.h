#pragma once
#include <thread>
#include <mutex>
#include <queue>
#include <chrono>
#include <string>
#include <memory>
#include <fstream>
#include "LogClass.h"

class Logger
{
private:
	Logger();
	~Logger();

public:
	static Logger& GetInstance();

#pragma region Thread
public:
	void RunLoggerThread(bool isAlsoPrintToConsole);
	void Worker();
	void StopLoggerThread();

private:
	void WriteLogImpl(std::queue<std::shared_ptr<LogBase>>& copyLogWaitingQueue);

private:
	std::thread loggerThread;
	// 0. LogHandle
	// 1. StopHandle
	HANDLE loggerEventHandles[2];
#pragma endregion Thread

#pragma region LogWaitingQueue
public:
	void WriteLog(std::shared_ptr<LogBase> logObject);

private:
	void WriteLogToFile(std::shared_ptr<LogBase> logObject);

private:
	std::mutex logQueueLock;
	std::queue<std::shared_ptr<LogBase>> logWaitingQueue;

	std::ofstream logFileStream;
#pragma endregion LogWaitingQueue

	bool printToConsole{};
};

namespace LogHelper
{
	template<typename T, typename = std::enable_if_t<std::is_base_of_v<LogBase, T>>>
	std::shared_ptr<T> MakeLogObject()
	{
		return std::make_shared<T>();
	}
}