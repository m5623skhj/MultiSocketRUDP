#pragma once

#include "MultiSocketRUDPCore.h"

class MultiSocketRUDPCoreTestAccess
{
public:
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
};
