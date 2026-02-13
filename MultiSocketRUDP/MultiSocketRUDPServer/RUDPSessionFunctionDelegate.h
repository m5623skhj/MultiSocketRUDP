#pragma once
#include <shared_mutex>
#include <set>

#include "RIOManager.h"
#include "NetServerSerializeBuffer.h"

namespace MultiSocketRUDP
{
	struct PacketSequenceSetKey;
}

struct RecvBuffer;
struct SendPacketInfo;
enum class IO_MODE : LONG;
struct IOContext;
class RUDPSession;
class RIOManager;
class RUDPSessionManager;
class RUDPPacketProcessor;
class RUDPIOHandler;
class RUDPSessionBroker;

// ----------------------------------------
// @brief RUDPSession 클래스의 특정 private/protected 메서드에 대한 접근을 위임하는 클래스입니다.
// ----------------------------------------
class RUDPSessionFunctionDelegate
{
	friend RIOManager;
	friend RUDPSessionManager;
	friend RUDPPacketProcessor;
	friend RUDPIOHandler;
	friend RUDPSessionBroker;

private:
	RUDPSessionFunctionDelegate() = default;
	~RUDPSessionFunctionDelegate() = default;

private:
#pragma region For RIOManager
	[[nodiscard]]
	static bool InitializeSessionRIO(RUDPSession& session, const RIO_EXTENSION_FUNCTION_TABLE& rioFunctionTable, const RIO_CQ& recvCQ, const RIO_CQ& sendCQ);
#pragma endregion For RIOManager

#pragma region For SessionManager
	static void SetSessionId(RUDPSession& session, SessionIdType sessionId);
	static void SetThreadId(RUDPSession& session, ThreadIdType threadId);
	static void CloseSocket(RUDPSession& session);
	static void RecvContextReset(RUDPSession& session);
	static void SendHeartbeatPacket(RUDPSession& session);
	static bool CheckReservedSessionTimeout(const RUDPSession& session, unsigned long long now);
	static void AbortReservedSession(RUDPSession& session);
#pragma endregion For SessionManager

#pragma region For RUDPPacketProcessor
	static bool TryConnect(RUDPSession& session, NetBuffer& recvPacket, const sockaddr_in& clientAddr);
	static bool CanProcessPacket(const RUDPSession& session, const sockaddr_in& clientAddr);
	static void OnSendReply(RUDPSession& session, NetBuffer& recvPacket);
	static bool OnRecvPacket(RUDPSession& session, NetBuffer& recvPacket);
#pragma endregion For RUDPPacketProcessor

#pragma region For RUDPIOHandler
	static std::shared_ptr<IOContext> GetRecvBufferContext(const RUDPSession& session);
	static RIO_BUFFERID GetSendBufferId(const RUDPSession& session);
	static IO_MODE& GetSendIOMode(RUDPSession& session);
	static bool IsSendPacketInfoQueueEmpty(RUDPSession& session);
	static SendPacketInfo* GetReservedSendPacketInfo(const RUDPSession& session);
	static void EnqueueToRecvBufferList(RUDPSession& session, NetBuffer* buffer);
	static std::set<MultiSocketRUDP::PacketSequenceSetKey>& GetCachedSequenceSet(RUDPSession& session);
	static std::mutex& GetCachedSequenceSetMutex(RUDPSession& session);
	static size_t GetSendPacketInfoQueueSize(const RUDPSession& session);
	static char* GetRIOSendBuffer(RUDPSession& session);
	static void SetReservedSendPacketInfo(RUDPSession& session, SendPacketInfo* reserveSendPacketInfo);
	static SendPacketInfo* GetSendPacketInfoQueueFrontAndPop(RUDPSession& session);
	static RecvBuffer& GetRecvBuffer(RUDPSession& session);
#pragma endregion For RUDPIOHandler

#pragma region For RUDPSessionBroker
	static void SetAbortReservedSession(RUDPSession& session);
	static void SetSessionReservedTime(RUDPSession& session, unsigned long long now);
	static unsigned char* GetSessionKeyObjectBuffer(const RUDPSession& session);
	static void SetSessionKeyObjectBuffer(RUDPSession& session, unsigned char* inKeyObjectBuffer);
#pragma endregion For RUDPSessionBroker

#pragma region Util
	static void Disconnect(RUDPSession& session, NetBuffer& recvPacket);
	static std::shared_mutex& GetSocketMutex(const RUDPSession& session);
	static std::mutex& AcquireSendPacketInfoQueueLock(RUDPSession& session);
	static SOCKET GetSocket(const RUDPSession& session);
	static RIO_RQ GetRecvRIORQ(const RUDPSession& session);
	static RIO_RQ GetSendRIORQ(const RUDPSession& session);
	static const unsigned char* GetSessionKey(const RUDPSession& session);
	static void SetSessionKey(RUDPSession& session, const unsigned char* inSessionKey);
	static const unsigned char* GetSessionSalt(const RUDPSession& session);
	static void SetSessionSalt(RUDPSession& session, const unsigned char* inSessionSalt);
	static const BCRYPT_KEY_HANDLE& GetSessionKeyHandle(const RUDPSession& session);
	static void SetSessionKeyHandle(RUDPSession& session, const BCRYPT_KEY_HANDLE& inKeyHandle);
	static void GetServerPortAndSessionId(const RUDPSession& session, OUT PortType& outServerPort, OUT SessionIdType& outSessionId);
#pragma endregion Util
};