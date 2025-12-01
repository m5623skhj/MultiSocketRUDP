#include "PreCompile.h"
#include "Logger.h"
#include <syncstream>
#include <filesystem>

#define LOG_HANDLE  WAIT_OBJECT_0
#define STOP_HANDLE WAIT_OBJECT_0 + 1

Logger::Logger()
{
	CreateFolderIfNotExists(logFolder);

	for (auto& loggerEventHandle : loggerEventHandles)
	{
		loggerEventHandle = nullptr;
	}

	const auto now = std::chrono::system_clock::now();
	const std::time_t currentTime = std::chrono::system_clock::to_time_t(now);
	std::tm utcTime;

	if (auto error = gmtime_s(&utcTime, &currentTime); error != 0)
	{
		std::cout << "Error in gmtime_s() : " << error << '\n';
		g_Dump.Crash();
	}

	char buffer[80];
	std::strftime(buffer, sizeof(buffer), "%Y-%m-%d_%H", &utcTime);

	std::string fileName = logFolder;
	fileName += std::string("/log_") + buffer + ".txt";

	logFileStream.open(fileName, std::ios::app);
	if (!logFileStream.is_open())
	{
		std::cout << "Logger : Failed to open file " << fileName << '\n';
		g_Dump.Crash();
	}

	isAlive.store(true, std::memory_order_release);
}

Logger::~Logger()
{
	isAlive.store(false, std::memory_order_release);

	std::queue<std::shared_ptr<LogBase>> remainingLogs;
	{
		std::scoped_lock lock(logQueueLock);
		remainingLogs.swap(logWaitingQueue);
	}
	WriteLogImpl(remainingLogs);

	if (logFileStream.is_open())
	{
		logFileStream.close();
	}
}

Logger& Logger::GetInstance()
{
	static Logger inst;
	return inst;
}

void Logger::RunLoggerThread(const bool isAlsoPrintToConsole)
{
	if (currentConnectedClientCount.fetch_add(1, std::memory_order_relaxed) > 0)
	{
		return;
	}

	printToConsole = isAlsoPrintToConsole;

	loggerEventHandles[0] = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	loggerEventHandles[1] = CreateEvent(nullptr, TRUE, FALSE, nullptr);
	if (loggerEventHandles[0] == nullptr || loggerEventHandles[1] == nullptr)
	{
		std::cout << "Logger event handle is invalid" << '\n';
		g_Dump.Crash();
	}

	loggerThread = std::jthread([this]() { this->Worker(); });
}

void Logger::Worker()
{
	std::queue<std::shared_ptr<LogBase>> copyLogWaitingQueue;

	while (true)
	{
		const auto result = WaitForMultipleObjects(2, loggerEventHandles, FALSE, INFINITE);

		{
			std::scoped_lock lock(logQueueLock);
			copyLogWaitingQueue.swap(logWaitingQueue);
		}

		if (result == LOG_HANDLE)
		{
			WriteLogImpl(copyLogWaitingQueue);
		}
		else if (result == STOP_HANDLE)
		{
			// Wait 10 seconds
			Sleep(10000);

			WriteLogImpl(copyLogWaitingQueue);
			break;
		}
		else
		{
			g_Dump.Crash();
		}
	}
}

void Logger::StopLoggerThread()
{
	if (currentConnectedClientCount.fetch_sub(1, std::memory_order_relaxed) != 1)
	{
		return;
	}

	SetEvent(loggerEventHandles[1]);

	if (loggerThread.joinable())
	{
		loggerThread.join();
	}
}

void Logger::CreateFolderIfNotExists(const std::string& folderPath)
{
	if (not std::filesystem::exists(folderPath))
	{
		if (not std::filesystem::create_directory(folderPath))
		{
			std::cout << "Folder create failed with error code " << GetLastError() << '\n';
			g_Dump.Crash();
		}
	}
}

void Logger::WriteLogImpl(std::queue<std::shared_ptr<LogBase>>& copyLogWaitingQueue)
{
	size_t logSize = copyLogWaitingQueue.size();
	while (logSize > 0)
	{
		WriteLogToFile(copyLogWaitingQueue.front());
		copyLogWaitingQueue.pop();
		--logSize;
	}
}

void Logger::WriteLog(std::shared_ptr<LogBase> logObject)
{
	if (not isAlive.load(std::memory_order_acquire))
	{
		return;
	}

	if (logObject == nullptr)
	{
		return;
	}

	logObject->SetLogTime();

	std::scoped_lock lock(logQueueLock);
	logWaitingQueue.push(std::move(logObject));

	SetEvent(loggerEventHandles[0]);
}

void Logger::WriteLogToFile(const std::shared_ptr<LogBase>& logObject)
{
	const auto logJson = logObject->ObjectToJsonImpl();
	logFileStream << logJson << '\n';

	if (printToConsole == true)
	{
		static std::mutex consoleMutex;
		std::scoped_lock lock(consoleMutex);

		std::cout << logJson << '\n';
	}
}