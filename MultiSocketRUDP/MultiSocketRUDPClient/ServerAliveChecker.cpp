#include "PreCompile.h"
#include "ServerAliveChecker.h"
#include "LogExtension.h"
#include "Logger.h"
#include "RUDPClientCore.h"

ServerAliveChecker::ServerAliveChecker(const std::function<void()>& inCoreStopFunction, const std::function<uint64_t()>& inGetReceiveCountFunction)
	: coreStopFunction(inCoreStopFunction)
	, getReceiveCountFunction(inGetReceiveCountFunction)
{
}

void ServerAliveChecker::StartServerAliveCheck(const unsigned int inCheckIntervalMs)
{
	std::scoped_lock threadGuard(threadLock);
	if (serverAliveCheckThread.joinable()) return;
	checkIntervalMs = inCheckIntervalMs;
	beforeCheckReceiveCount = getReceiveCountFunction();
	isStopped.store(false, std::memory_order_release);
	serverAliveCheckThread = std::jthread(&ServerAliveChecker::RunServerAliveCheckerThread, this);
}

void ServerAliveChecker::StopServerAliveCheck()
{
	std::scoped_lock threadGuard(threadLock);
	{
		std::scoped_lock lock(waitLock);
		isStopped.store(true, std::memory_order_release);
	}
	wake.notify_all();
	if (serverAliveCheckThread.joinable())
	{
		serverAliveCheckThread.join();
	}
}

bool ServerAliveChecker::IsServerAlive(const uint64_t receiveCount)
{
	if (receiveCount == beforeCheckReceiveCount)
	{
		return false;
	}

	beforeCheckReceiveCount = receiveCount;
	return true;
}

void ServerAliveChecker::RunServerAliveCheckerThread()
{
	while (not isStopped)
	{
		{
			std::unique_lock lock(waitLock);
			if (wake.wait_for(lock, std::chrono::milliseconds(checkIntervalMs),
				[this] { return isStopped.load(std::memory_order_acquire); })) break;
		}

		if (not IsServerAlive(getReceiveCountFunction()))
		{
			const auto log = Logger::MakeLogObject<ClientLog>();
			log->logString = "Server is not alive";
			Logger::GetInstance().WriteLog(log);

			coreStopFunction();
			break;
		}
	}
}
