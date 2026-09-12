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
	checkIntervalMs = inCheckIntervalMs;
	beforeCheckReceiveCount = getReceiveCountFunction();
	isStopped.store(false, std::memory_order_release);
	serverAliveCheckThread = std::jthread(&ServerAliveChecker::RunServerAliveCheckerThread, this);
}

void ServerAliveChecker::StopServerAliveCheck()
{
	if (isStopped.exchange(true, std::memory_order_acq_rel))
	{
		return;
	}

	if (not serverAliveCheckThread.joinable())
	{
		return;
	}

	if (serverAliveCheckThread.get_id() != std::this_thread::get_id())
	{
		serverAliveCheckThread.join();
	}
	else
	{
		serverAliveCheckThread.detach();
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
		Sleep(checkIntervalMs);
		if (isStopped.load(std::memory_order_acquire))
		{
			break;
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
