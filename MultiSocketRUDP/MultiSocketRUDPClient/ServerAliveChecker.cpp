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
	serverAliveCheckThread = std::jthread(&ServerAliveChecker::RunServerAliveCheckerThread, this);
}

void ServerAliveChecker::StopServerAliveCheck()
{
	if (isStopped)
	{
		return;
	}

	isStopped = true;
	if (serverAliveCheckThread.joinable())
	{
		serverAliveCheckThread.join();
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