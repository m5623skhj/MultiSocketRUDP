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
	static bool IsAlive() { return isAlive.load(std::memory_order_acquire); }

#pragma region Thread
public:
	void RunLoggerThread(const bool isAlsoPrintToConsole);
	void Worker();
	void StopLoggerThread();

	template<typename LogType>
	requires std::is_base_of_v<LogBase, LogType>
	static std::shared_ptr<LogType> MakeLogObject()
	{
		return std::make_shared<LogType>();
	}

private:
	static void CreateFolderIfNotExists(const std::string& folderPath);
	void WriteLogImpl(std::queue<std::shared_ptr<LogBase>>& copyLogWaitingQueue);

private:
	std::jthread loggerThread;
	// 0. LogHandle
	// 1. StopHandle
	HANDLE loggerEventHandles[2];
#pragma endregion Thread

#pragma region LogWaitingQueue
public:
	void WriteLog(std::shared_ptr<LogBase> logObject);

private:
	void WriteLogToFile(const std::shared_ptr<LogBase>& logObject);

private:
	std::mutex logQueueLock;
	std::queue<std::shared_ptr<LogBase>> logWaitingQueue;

	std::ofstream logFileStream;
#pragma endregion LogWaitingQueue

	bool printToConsole{};
	std::string logFolder = "Log Folder";
	std::atomic_int16_t currentConnectedClientCount{};

	static inline std::atomic<bool> isAlive{ false };
};

namespace LogHelper
{
	template<typename T>
	std::shared_ptr<T> MakeLogObject()
	requires (std::is_base_of_v<LogBase, T>)
	{
		return std::make_shared<T>();
	}
}
