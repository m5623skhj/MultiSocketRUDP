#pragma once
#include <memory>
#include <set>
#include <shared_mutex>
#include <winsock2.h>
#include <MSWSock.h>
#include <bcrypt.h>

#include "../Common/etc/CoreType.h"

class RUDPSession;
class NetBuffer;
struct IOContext;
struct RecvBuffer;
struct SendPacketInfo;

namespace MultiSocketRUDP { struct PacketSequenceSetKey; }

enum class IO_MODE : unsigned int;
enum class SESSION_STATE : unsigned char;

class ISessionDelegate
{
public:
	virtual ~ISessionDelegate() = default;

	[[nodiscard]]
	virtual bool InitializeSessionRIO(RUDPSession& session,
		const RIO_EXTENSION_FUNCTION_TABLE& rioFunctionTable,
		const RIO_CQ& recvCQ, const RIO_CQ& sendCQ) = 0;

	virtual void SetSessionId(RUDPSession& session, SessionIdType sessionId) = 0;
	virtual void SetThreadId(RUDPSession& session, ThreadIdType threadId) = 0;

	virtual void CloseSocket(RUDPSession& session) = 0;
	[[nodiscard]]
	virtual SOCKET GetSocket(const RUDPSession& session) = 0;
	[[nodiscard]]
	virtual std::shared_mutex& GetSocketMutex(const RUDPSession& session) = 0;

	virtual void RecvContextReset(RUDPSession& session) = 0;
	[[nodiscard]]
	virtual std::shared_ptr<IOContext> GetRecvBufferContext(const RUDPSession& session) = 0;
	[[nodiscard]]
	virtual RecvBuffer& GetRecvBuffer(RUDPSession& session) = 0;
	virtual void EnqueueToRecvBufferList(RUDPSession& session, NetBuffer* buffer) = 0;
	[[nodiscard]]
	virtual RIO_RQ GetRecvRIORQ(const RUDPSession& session) = 0;

	[[nodiscard]]
	virtual IO_MODE& GetSendIOMode(RUDPSession& session) = 0;
	virtual bool IsNothingToSend(RUDPSession& session) = 0;
	virtual bool IsSendPacketInfoQueueEmpty(RUDPSession& session) = 0;
	[[nodiscard]]
	virtual SendPacketInfo* TryGetFrontAndPop(RUDPSession& session) = 0;
	[[nodiscard]]
	virtual SendPacketInfo* GetReservedSendPacketInfo(const RUDPSession& session) = 0;
	virtual void SetReservedSendPacketInfo(RUDPSession& session, SendPacketInfo* info) = 0;
	[[nodiscard]]
	virtual size_t GetSendPacketInfoQueueSize(RUDPSession& session) = 0;
	[[nodiscard]]
	virtual char* GetRIOSendBuffer(RUDPSession& session) = 0;
	[[nodiscard]]
	virtual RIO_BUFFERID GetSendBufferId(const RUDPSession& session) = 0;
	[[nodiscard]]
	virtual RIO_RQ GetSendRIORQ(const RUDPSession& session) = 0;
	[[nodiscard]]
	virtual std::set<MultiSocketRUDP::PacketSequenceSetKey>& GetCachedSequenceSet(RUDPSession& session) = 0;
	[[nodiscard]]
	virtual std::mutex& GetCachedSequenceSetMutex(RUDPSession& session) = 0;

	[[nodiscard]]
	virtual bool TryConnect(RUDPSession& session, NetBuffer& recvPacket, const sockaddr_in& clientAddr) = 0;
	[[nodiscard]]
	virtual bool CanProcessPacket(const RUDPSession& session, const sockaddr_in& clientAddr) = 0;
	[[nodiscard]]
	virtual bool OnRecvPacket(RUDPSession& session, NetBuffer& recvPacket) = 0;
	virtual void OnSendReply(RUDPSession& session, NetBuffer& recvPacket) = 0;
	virtual void Disconnect(RUDPSession& session, NetBuffer& recvPacket) = 0;

	virtual void SendHeartbeatPacket(RUDPSession& session) = 0;
	[[nodiscard]]
	virtual bool CheckReservedSessionTimeout(const RUDPSession& session, unsigned long long now) = 0;
	virtual void AbortReservedSession(RUDPSession& session) = 0;
	virtual void SetSessionReservedTime(RUDPSession& session, unsigned long long now) = 0;

	[[nodiscard]]
	virtual const unsigned char* GetSessionKey(const RUDPSession& session) = 0;
	virtual void SetSessionKey(RUDPSession& session, const unsigned char* inSessionKey) = 0;
	[[nodiscard]]
	virtual const unsigned char* GetSessionSalt(const RUDPSession& session) = 0;
	virtual void SetSessionSalt(RUDPSession& session, const unsigned char* inSessionSalt) = 0;
	[[nodiscard]]
	virtual const BCRYPT_KEY_HANDLE& GetSessionKeyHandle(const RUDPSession& session) = 0;
	virtual void SetSessionKeyHandle(RUDPSession& session, const BCRYPT_KEY_HANDLE& inKeyHandle) = 0;
	[[nodiscard]]
	virtual unsigned char* GetSessionKeyObjectBuffer(const RUDPSession& session) = 0;
	virtual void SetSessionKeyObjectBuffer(RUDPSession& session, unsigned char* inKeyObjectBuffer) = 0;

	virtual void GetServerPortAndSessionId(const RUDPSession& session, PortType& outServerPort, SessionIdType& outSessionId) = 0;
};
