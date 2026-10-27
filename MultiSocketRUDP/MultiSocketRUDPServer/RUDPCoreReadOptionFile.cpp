#include "PreCompile.h"
#include "MultiSocketRUDPCore.h"
#include <Windows.h>
#include <regex>

bool MultiSocketRUDPCore::ReadOptionFile(const std::wstring& coreOptionFilePath, const std::wstring& sessionBrokerOptionFilePath)
{
	WCHAR buffer[BUFFER_MAX]{};
	if (!LoadParsingText(buffer, coreOptionFilePath.c_str(), BUFFER_MAX)) return false;

	// Optional fixed UDP range. Validate text before narrowing; malformed or half
	// specified ranges must never silently fall back to ephemeral ports.
	udpPortStart = udpPortEnd = 0;
	const std::wstring coreText(buffer);
	const auto readPort = [&](const wchar_t* key, unsigned short& result)
	{
		const std::wregex keyPattern(std::wstring(L"(^|[\r\n])\\s*") + key + L"\\b");
		const std::wregex valuePattern(std::wstring(L"(^|[\r\n])[ \t]*") + key +
			L"[ \t]*=[ \t]*([0-9]{1,5})[ \t]*(?=[\r\n]|$)");
		std::wsregex_iterator keys(coreText.begin(), coreText.end(), keyPattern), noKeys;
		if (keys == noKeys) return 0;
		if (++keys != noKeys) return -1;
		std::wsregex_iterator it(coreText.begin(), coreText.end(), valuePattern), end;
		if (it == end) return -1;
		const auto value = std::stoul((*it)[2].str());
		if (++it != end || value == 0 || value > 65535) return -1;
		result = static_cast<unsigned short>(value);
		return 1;
	};
	const int hasStart = readPort(L"UDP_PORT_START", udpPortStart);
	const int hasEnd = readPort(L"UDP_PORT_END", udpPortEnd);
	if (hasStart < 0 || hasEnd < 0 || hasStart != hasEnd ||
		(hasStart == 1 && udpPortStart > udpPortEnd)) return false;

	// core
	if (g_Paser.GetValue_Byte(buffer, L"CORE", L"THREAD_COUNT", &numOfWorkerThread) == false)
	{
		return false;
	}
	if (g_Paser.GetValue_Short(buffer, L"CORE", L"NUM_OF_SOCKET", reinterpret_cast<short*>(&numOfSockets)) == false)
	{
		return false;
	}
	if (numOfSockets == 0 || (udpPortStart != 0 &&
		static_cast<unsigned int>(udpPortEnd) - udpPortStart + 1 < numOfSockets)) return false;
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
	
	int queueCapacity = DEFAULT_UNRELIABLE_QUEUE_CAPACITY;
	g_Paser.GetValue_Int(buffer, L"CORE", L"UNRELIABLE_QUEUE_CAPACITY", &queueCapacity);
	if (queueCapacity < 1 || queueCapacity > 65535) return false;
	unreliableQueueCapacity = static_cast<unsigned int>(queueCapacity);

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
	if (!LoadParsingText(buffer, sessionBrokerOptionFilePath.c_str(), BUFFER_MAX)) return false;

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
