#include "PreCompile.h"
#include "Logger.h"
#include <syncstream>
#include <filesystem>

#define LOG_HANDLE  WAIT_OBJECT_0
#define STOP_HANDLE WAIT_OBJECT_0 + 1

Logger::Logger()
{
	CreateFolderIfNotExists(logFolder);

	for (int i = 0; i < 2; ++i)
	{
		loggerEventHandles[i] = nullptr;
	}

	const auto now = std::chrono::system_clock::now();
	std::time_t currentTime = std::chrono::system_clock::to_time_t(now);
	std::tm utcTime;

	if (auto error = gmtime_s(&utcTime, &currentTime); error != 0)
	{
		std::cout << "Error in gmtime_s() : " << error << std::endl;
		g_Dump.Crash();
	}

	char buffer[80];
	std::strftime(buffer, sizeof(buffer), "%Y-%m-%d_%H", &utcTime);

	std::string fileName = logFolder;
	fileName += std::string("/log_") + buffer + ".txt";

	logFileStream.open(fileName, std::ios::app);
	if (!logFileStream.is_open())
	{
		std::cout << "Logger : Failed to open file " << fileName << std::endl;
		g_Dump.Crash();
	}
}

Logger::~Logger()
{
	logFileStream.close();
}

Logger& Logger::GetInstance()
{
	static Logger inst;
	return inst;
}

void Logger::RunLoggerThread(const bool isAlsoPrintToConsole)
{
	printToConsole = isAlsoPrintToConsole;

	loggerEventHandles[0] = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	loggerEventHandles[1] = CreateEvent(nullptr, TRUE, FALSE, nullptr);
	if (loggerEventHandles[0] == nullptr || loggerEventHandles[1] == nullptr)
	{
		std::cout << "Logger event handle is invalid" << std::endl;
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
	SetEvent(loggerEventHandles[1]);
	loggerThread.join();
}

void Logger::CreateFolderIfNotExists(const std::string& folderPath)
{
	if (not std::filesystem::exists(folderPath))
	{
		if (not std::filesystem::create_directory(folderPath))
		{
			std::cout << "Folder create failed with error code " << GetLastError() << std::endl;
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

void Logger::WriteLog(const std::shared_ptr<LogBase>& logObject)
{
	logObject->SetLogTime();

	std::lock_guard guardLock(logQueueLock);
	logWaitingQueue.push(logObject);

	SetEvent(loggerEventHandles[0]);
}

void Logger::WriteLogToFile(const std::shared_ptr<LogBase>& logObject)
{
	const auto logJson = logObject->ObjectToJsonImpl();
	logFileStream << logJson << '\n';

	if (printToConsole == true)
	{
		std::osyncstream syncOut(std::cout);
		syncOut << logJson << std::endl;
	}
}