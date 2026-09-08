#pragma once

#include "MultiSocketRUDPCore.h"
#include "MultiSocketRUDPCoreFunctionDelegate.h"
#include "RUDPPacketProcessor.h"
#include "RUDPSessionManager.h"

class MultiSocketRUDPCoreTestAccess
{
public:
	static bool InitializeSessionRelease(MultiSocketRUDPCore& core)
	{
		core.sessionReleaseEventHandle = CreateEvent(nullptr, FALSE, FALSE, nullptr);
		if (core.sessionReleaseEventHandle == nullptr)
		{
			return false;
		}
		MultiSocketRUDPCoreFunctionDelegate::Instance().Init(core);
		return true;
	}

	static void CleanupSessionRelease(MultiSocketRUDPCore& core)
	{
		MultiSocketRUDPCoreFunctionDelegate::Instance().Clear(core);
		CloseHandle(core.sessionReleaseEventHandle);
		core.sessionReleaseEventHandle = nullptr;
		for (const auto handle : core.recvLogicThreadEventHandles)
		{
			CloseHandle(handle);
		}
		core.recvLogicThreadEventHandles.clear();
	}

	static bool InitializeRecvLogic(MultiSocketRUDPCore& core)
	{
		const HANDLE eventHandle = CreateEvent(nullptr, FALSE, FALSE, nullptr);
		if (eventHandle == nullptr)
		{
			return false;
		}
		core.recvLogicThreadEventHandles.push_back(eventHandle);
		core.recvIOCompletedContexts.push_back(std::make_unique<MultiSocketRUDPCore::RecvIOCompletedQueue>());
		core.sessionManager = std::make_unique<RUDPSessionManager>(1, core, core.sessionDelegate);
		core.packetProcessor = std::make_unique<RUDPPacketProcessor>(*core.sessionManager, core.sessionDelegate);
		return true;
	}

	static bool EnqueueRecv(MultiSocketRUDPCore& core, const IOContext& context, NetBuffer* buffer)
	{
		return core.EnqueueContextResult(&context, buffer, 0);
	}

	static void ProcessRecvQueue(MultiSocketRUDPCore& core)
	{
		core.OnRecvPacket(0);
	}

	static std::vector<SessionIdType> WaitAndTakeReleaseSessionIds(MultiSocketRUDPCore& core)
	{
		if (WaitForSingleObject(core.sessionReleaseEventHandle, 5000) != WAIT_OBJECT_0)
		{
			return {};
		}
		return core.TakeReleaseSessionIds();
	}

	static std::vector<SessionIdType> TakeReleaseSessionIds(MultiSocketRUDPCore& core)
	{
		return core.TakeReleaseSessionIds();
	}

	static bool ReadOptionFile(
		MultiSocketRUDPCore& core,
		const std::wstring& coreOptionFilePath,
		const std::wstring& sessionBrokerOptionFilePath)
	{
		return core.ReadOptionFile(coreOptionFilePath, sessionBrokerOptionFilePath);
	}

	static void SetTimingOptions(
		MultiSocketRUDPCore& core,
		const unsigned int heartbeatMs,
		const unsigned int initialRtoMs,
		const unsigned int minRtoMs,
		const unsigned int maxRtoMs)
	{
		core.heartbeatThreadSleepMs = heartbeatMs;
		core.retransmissionMs = initialRtoMs;
		core.minRetransmissionMs = minRtoMs;
		core.maxRetransmissionMs = maxRtoMs;
	}

	static BYTE GetPacketHeaderCode() { return MultiSocketRUDPCore::GetPacketHeaderCodeForTest(); }
	static BYTE GetPacketXorCode() { return MultiSocketRUDPCore::GetPacketXorCodeForTest(); }
	static void SetPacketCodes(const BYTE headerCode, const BYTE xorCode)
	{
		MultiSocketRUDPCore::SetPacketCodesForTest(headerCode, xorCode);
	}

	static BYTE GetWorkerThreadCount(const MultiSocketRUDPCore& core) { return core.numOfWorkerThread; }
	static unsigned short GetSocketCount(const MultiSocketRUDPCore& core) { return core.numOfSockets; }
	static PacketRetransmissionCount GetMaxRetransmissionCount(const MultiSocketRUDPCore& core)
	{
		return core.maxPacketRetransmissionCount;
	}
	static unsigned int GetWorkerFrameMs(const MultiSocketRUDPCore& core) { return core.workerThreadOneFrameMs; }
	static unsigned int GetTimerTickMs(const MultiSocketRUDPCore& core) { return core.timerTickMs; }
	static BYTE GetMaximumHoldingQueueSize(const MultiSocketRUDPCore& core) { return core.maxHoldingPacketQueueSize; }
	static unsigned int GetSimulatedPacketLossPercent(const MultiSocketRUDPCore& core)
	{
		return core.simulatedPacketLossPercent;
	}
	static int GetSimulatedPacketLossSeed(const MultiSocketRUDPCore& core) { return core.simulatedPacketLossSeed; }
	static const std::string& GetCoreServerIp(const MultiSocketRUDPCore& core) { return core.coreServerIp; }
	static PortType GetSessionBrokerPort(const MultiSocketRUDPCore& core) { return core.sessionBrokerPort; }
	static void ReportFatalError(MultiSocketRUDPCore& core, const ServerFatalError& error)
	{
		core.ReportFatalError(error);
	}
};
