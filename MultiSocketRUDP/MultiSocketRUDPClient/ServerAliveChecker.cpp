#include "PreCompile.h"
#include "ServerAliveChecker.h"
#include "LogExtension.h"
#include "Logger.h"
#include "RUDPClientCore.h"

ServerAliveChecker::ServerAliveChecker(const std::function<void()>& inCoreStopFunction, const std::function<PacketSequence()>& inGetNextRecvSequenceFunction)
	: coreStopFunction(inCoreStopFunction)
	, getNextRecvSequenceFunction(inGetNextRecvSequenceFunction)
{
}

void ServerAliveChecker::StartServerAliveCheck(const unsigned int inCheckIntervalMs)
{
	checkIntervalMs = inCheckIntervalMs;
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

bool ServerAliveChecker::IsServerAlive(const PacketSequence nowPacketSequence)
{
	if (nowPacketSequence == beforeCheckSequence)
	{
		return false;
	}

	beforeCheckSequence = nowPacketSequence;
	return true;
}

void ServerAliveChecker::RunServerAliveCheckerThread()
{
	while (not isStopped)
	{
		Sleep(checkIntervalMs);
		if (not IsServerAlive(getNextRecvSequenceFunction()))
		{
			const auto log = Logger::MakeLogObject<ClientLog>();
			log->logString = "Server is not alive";
			Logger::GetInstance().WriteLog(log);

			coreStopFunction();
			break;
		}
	}
}
