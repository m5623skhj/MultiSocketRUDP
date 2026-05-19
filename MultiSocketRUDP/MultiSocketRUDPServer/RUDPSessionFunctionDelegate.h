#pragma once
#include "ISessionDelegate.h"
#include <shared_mutex>
#include "NetServerSerializeBuffer.h"

namespace MultiSocketRUDP
{
	struct PacketSequenceSetKey;
}

struct RecvBuffer;
struct SendPacketInfo;
enum class IO_MODE : unsigned int;
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
class RUDPSessionFunctionDelegate : public ISessionDelegate
{
	friend RIOManager;
	friend RUDPSessionManager;
	friend RUDPPacketProcessor;
	friend RUDPIOHandler;
	friend RUDPSessionBroker;

public:
	RUDPSessionFunctionDelegate() = default;
	~RUDPSessionFunctionDelegate() override = default;

private:
#pragma region For RIOManager
	[[nodiscard]]
	bool InitializeSessionRIO(RUDPSession& session, const RIO_EXTENSION_FUNCTION_TABLE& rioFunctionTable, const RIO_CQ& recvCQ, const RIO_CQ& sendCQ) override;
#pragma endregion For RIOManager

#pragma region For SessionManager
	void SetSessionId(RUDPSession& session, SessionIdType sessionId) override;
	void SetThreadId(RUDPSession& session, ThreadIdType threadId) override;
	void CloseSocket(RUDPSession& session) override;
	void RecvContextReset(RUDPSession& session) override;
	void SendHeartbeatPacket(RUDPSession& session) override;
	bool CheckReservedSessionTimeout(const RUDPSession& session, unsigned long long now) override;
	void AbortReservedSession(RUDPSession& session) override;
	void InitializeSession(RUDPSession& session) override;
#pragma endregion For SessionManager

#pragma region For RUDPPacketProcessor
	bool TryConnect(RUDPSession& session, NetBuffer& recvPacket, const sockaddr_in& clientAddr) override;
	bool CanProcessPacket(const RUDPSession& session, const sockaddr_in& clientAddr) override;
	void OnSendReply(RUDPSession& session, NetBuffer& recvPacket) override;
	bool OnRecvPacket(RUDPSession& session, NetBuffer& recvPacket) override;
#pragma endregion For RUDPPacketProcessor

#pragma region For RUDPIOHandler
	std::shared_ptr<IOContext> GetRecvBufferContext(const RUDPSession& session) override;
	RIO_BUFFERID GetSendBufferId(const RUDPSession& session) override;
	std::atomic<IO_MODE>& GetSendIOMode(RUDPSession& session) override;
	bool IsSendPacketInfoQueueEmpty(RUDPSession& session) override;
	SendPacketInfo* TryGetFrontAndPop(RUDPSession& session) override;
	SendPacketInfo* GetReservedSendPacketInfo(RUDPSession& session) override;
	bool IsNothingToSend(RUDPSession& session) override;
	void EnqueueToRecvBufferList(RUDPSession& session, NetBuffer* buffer) override;
	std::vector<MultiSocketRUDP::PacketSequenceSetKey>& GetCachedSequences(RUDPSession& session) override;
	size_t GetSendPacketInfoQueueSize(RUDPSession& session) override;
	char* GetRIOSendBuffer(RUDPSession& session) override;
	void SetReservedSendPacketInfo(RUDPSession& session, SendPacketInfo* reserveSendPacketInfo) override;
	static SendPacketInfo* GetSendPacketInfoQueueFrontAndPop(RUDPSession& session);
	RecvBuffer& GetRecvBuffer(RUDPSession& session) override;
#pragma endregion For RUDPIOHandler

#pragma region For RUDPSessionBroker
	static void SetAbortReservedSession(RUDPSession& session);
	void SetSessionReservedTime(RUDPSession& session, unsigned long long now) override;
	unsigned char* GetSessionKeyObjectBuffer(const RUDPSession& session) override;
	void SetSessionKeyObjectBuffer(RUDPSession& session, unsigned char* inKeyObjectBuffer) override;
#pragma endregion For RUDPSessionBroker

#pragma region Util
	void Disconnect(RUDPSession& session, NetBuffer& recvPacket);
	std::shared_mutex& GetSocketMutex(const RUDPSession& session) override;
	SOCKET GetSocket(const RUDPSession& session) override;
	RIO_RQ GetRecvRIORQ(const RUDPSession& session) override;
	RIO_RQ GetSendRIORQ(const RUDPSession& session) override;
	const unsigned char* GetSessionKey(const RUDPSession& session) override;
	void SetSessionKey(RUDPSession& session, const unsigned char* inSessionKey) override;
	const unsigned char* GetSessionSalt(const RUDPSession& session) override;
	void SetSessionSalt(RUDPSession& session, const unsigned char* inSessionSalt) override;
	const BCRYPT_KEY_HANDLE& GetSessionKeyHandle(const RUDPSession& session) override;
	void SetSessionKeyHandle(RUDPSession& session, const BCRYPT_KEY_HANDLE& inKeyHandle) override;
	void GetServerPortAndSessionId(const RUDPSession& session, OUT PortType& outServerPort, OUT SessionIdType& outSessionId) override;
#pragma endregion Util
};
