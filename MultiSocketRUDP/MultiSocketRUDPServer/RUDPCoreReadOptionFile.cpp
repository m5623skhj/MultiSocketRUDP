#include "PreCompile.h"
#include "MultiSocketRUDPCore.h"
#include <Windows.h>

bool MultiSocketRUDPCore::ReadOptionFile(const std::wstring& coreOptionFilePath, const std::wstring& sessionBrokerOptionFilePath)
{
	WCHAR buffer[BUFFER_MAX];
	LoadParsingText(buffer, coreOptionFilePath.c_str(), BUFFER_MAX);

	// core
	if (g_Paser.GetValue_Byte(buffer, L"CORE", L"THREAD_COUNT", &numOfWorkerThread) == false)
	{
		return false;
	}
	if (g_Paser.GetValue_Short(buffer, L"CORE", L"NUM_OF_SOCKET", reinterpret_cast<short*>(&numOfSockets)) == false)
	{
		return false;
	}
	if (g_Paser.GetValue_Short(buffer, L"CORE", L"MAX_PACKET_RETRANSMISSION_COUNT", reinterpret_cast<short*>(&maxPacketRetransmissionCount)) == false)
	{
		return false;
	}
	if (g_Paser.GetValue_Int(buffer, L"CORE", L"WORKER_THREAD_ONE_FRAME_MS", reinterpret_cast<int*>(&workerThreadOneFrameMs)) == false)
	{
		return false;
	}
	if (g_Paser.GetValue_Int(buffer, L"CORE", L"RETRANSMISSION_MS", reinterpret_cast<int*>(&retransmissionMs)) == false)
	{
		return false;
	}
	const bool hasMinRetransmissionMs = g_Paser.GetValue_Int(buffer, L"CORE", L"MIN_RETRANSMISSION_MS", reinterpret_cast<int*>(&minRetransmissionMs));
	const bool hasMaxRetransmissionMs = g_Paser.GetValue_Int(buffer, L"CORE", L"MAX_RETRANSMISSION_MS", reinterpret_cast<int*>(&maxRetransmissionMs));
	if (hasMinRetransmissionMs != hasMaxRetransmissionMs)
	{
		return false;
	}
	if (hasMinRetransmissionMs == false)
	{
		minRetransmissionMs = retransmissionMs;
		maxRetransmissionMs = retransmissionMs;
	}
	if (minRetransmissionMs == 0 || minRetransmissionMs > retransmissionMs || retransmissionMs > maxRetransmissionMs)
	{
		return false;
	}
	if (g_Paser.GetValue_Int(buffer, L"CORE", L"HEARTBEAT_THREAD_SLEEP_MS", reinterpret_cast<int*>(&heartbeatThreadSleepMs)) == false)
	{
		return false;
	}
	if (g_Paser.GetValue_Int(buffer, L"CORE", L"TIMER_TICK_MS", reinterpret_cast<int*>(&timerTickMs)) == false)
	{
		return false;
	}
	if (g_Paser.GetValue_Byte(buffer, L"CORE", L"MAX_HOLDING_PACKET_QUEUE_SIZE", &maxHoldingPacketQueueSize) == false)
	{
		return false;
	}

	if (g_Paser.GetValue_Int(buffer, L"CORE", L"SIMULATED_PACKET_LOSS_PERCENT", reinterpret_cast<int*>(&simulatedPacketLossPercent)) == false)
	{
		simulatedPacketLossPercent = 0;
	}

	if (g_Paser.GetValue_Int(buffer, L"CORE", L"SIMULATED_PACKET_LOSS_SEED", &simulatedPacketLossSeed) == false)
	{
		simulatedPacketLossSeed = 0;
	}
	
	// buffer
	if (g_Paser.GetValue_Byte(buffer, L"SERIALIZEBUF", L"PACKET_CODE", &NetBuffer::m_byHeaderCode) == false)
	{
		return false;
	}
	if (g_Paser.GetValue_Byte(buffer, L"SERIALIZEBUF", L"PACKET_KEY", &NetBuffer::m_byXORCode) == false)
	{
		return false;
	}

	ZeroMemory(buffer, BUFFER_MAX * sizeof(WCHAR));
	LoadParsingText(buffer, sessionBrokerOptionFilePath.c_str(), BUFFER_MAX);

	// session broker
	WCHAR targetIP[64];
	if (g_Paser.GetValue_String(buffer, L"SESSION_BROKER", L"CORE_IP", targetIP) == false)
	{
		return false;
	}

	constexpr size_t maxIPv4StringLength = 15;
	if (const size_t ipLength = wcslen(targetIP); ipLength == 0 || ipLength > maxIPv4StringLength)
	{
		return false;
	}
	
	if (g_Paser.GetValue_Short(buffer, L"SESSION_BROKER", L"SESSION_BROKER_PORT", reinterpret_cast<short*>(&sessionBrokerPort)) == false)
	{
		return false;
	}

	const int length = static_cast<int>(wcslen(targetIP));
	const int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, targetIP, length, nullptr, 0, nullptr, nullptr);
	std::string ipString(sizeNeeded, 0);
	WideCharToMultiByte(CP_UTF8, 0, targetIP, length, &ipString[0], sizeNeeded, nullptr, nullptr);
	coreServerIp = ipString;

	return true;
}
