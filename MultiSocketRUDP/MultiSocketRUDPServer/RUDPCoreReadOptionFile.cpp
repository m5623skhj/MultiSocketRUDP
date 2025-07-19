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
	if (g_Paser.GetValue_Int(buffer, L"CORE", L"RETRANSMISSION_THREAD_SLEEP_MS", reinterpret_cast<int*>(&retransmissionThreadSleepMs)) == false)
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

	// buffer
	if (g_Paser.GetValue_Byte(buffer, L"SERIALIZEBUF", L"PACKET_CODE", &NetBuffer::m_byHeaderCode) == false)
	{
		return false;
	}
	if (g_Paser.GetValue_Byte(buffer, L"SERIALIZEBUF", L"PACKET_KEY", &NetBuffer::m_byXORCode) == false)
	{
		return false;
	}

	ZeroMemory(buffer, BUFFER_MAX);
	LoadParsingText(buffer, sessionBrokerOptionFilePath.c_str(), BUFFER_MAX);

	// session broker
	WCHAR targetIP[16];
	if (g_Paser.GetValue_String(buffer, L"SESSION_BROKER", L"CORE_IP", targetIP) == false)
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
