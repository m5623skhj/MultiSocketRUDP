#pragma once
#include <memory>
#include <set>
#include <shared_mutex>
#include <winsock2.h>
#include <MSWSock.h>
#include <bcrypt.h>
#include "NetServerSerializeBuffer.h"

#include "../Common/etc/CoreType.h"

class RUDPSession;
struct IOContext;
struct RecvBuffer;
struct SendPacketInfo;

namespace MultiSocketRUDP { struct PacketSequenceSetKey; }

enum class IO_MODE : unsigned int;
enum class SESSION_STATE : unsigned char;

// ----------------------------------------
// @brief 서버 구성 요소가 RUDPSession의 내부 컨텍스트와 상태 동작에 접근하기 위한 위임 인터페이스입니다.
// RIO 처리, 패킷 처리 및 세션 관리 로직을 세션 구현과 분리하여 테스트 가능성을 유지합니다.
// 세션 내부 컨텍스트의 raw 포인터와 참조는 세션 재초기화 이후 보관하면 안 됩니다.
// 공유 소유권 또는 참조 카운트를 반환하는 API는 각 타입의 별도 수명 규칙을 따릅니다.
// ----------------------------------------
class ISessionDelegate
{
public:
	virtual ~ISessionDelegate() = default;

	// ----------------------------------------
	// RIO 요청 큐와 세션 식별 정보 초기화
	// ----------------------------------------
	[[nodiscard]]
	virtual bool InitializeSessionRIO(RUDPSession& session,
		const RIO_EXTENSION_FUNCTION_TABLE& rioFunctionTable,
		const RIO_CQ& recvCQ, const RIO_CQ& sendCQ) = 0;

	virtual void SetSessionId(RUDPSession& session, SessionIdType sessionId) = 0;
	virtual void SetThreadId(RUDPSession& session, ThreadIdType threadId) = 0;

	// ----------------------------------------
	// 소켓 및 수신 컨텍스트 접근
	// GetRecvBufferContext가 반환하는 컨텍스트는 공유 소유권으로 완료 처리까지 유지됩니다.
	// ----------------------------------------
	[[nodiscard]]
	virtual SOCKET GetSocket(const RUDPSession& session) = 0;
	[[nodiscard]]
	virtual std::shared_mutex& GetSocketMutex(const RUDPSession& session) = 0;

	virtual void RecvContextReset(RUDPSession& session) = 0;
	[[nodiscard]]
	virtual std::shared_ptr<IOContext> GetRecvBufferContext(const RUDPSession& session) = 0;
	[[nodiscard]]
	virtual RecvBuffer& GetRecvBuffer(RUDPSession& session) = 0;
	[[nodiscard]]
	virtual RIO_RQ GetRecvRIORQ(const RUDPSession& session) = 0;

	// ----------------------------------------
	// 송신 큐, 예약 패킷 및 RIO 송신 버퍼 접근
	// 큐와 IO_MODE의 동기화 규칙은 RUDPSession과 RUDPIOHandler가 공동으로 준수합니다.
	// ----------------------------------------
	[[nodiscard]]
	virtual std::atomic<IO_MODE>& GetSendIOMode(RUDPSession& session) = 0;
	virtual bool IsNothingToSend(RUDPSession& session) = 0;
	virtual bool IsSendPacketInfoQueueEmpty(RUDPSession& session) = 0;
	[[nodiscard]]
	virtual SendPacketInfo* TryGetFrontAndPop(RUDPSession& session) = 0;
	[[nodiscard]]
	virtual SendPacketInfo* GetReservedSendPacketInfo(RUDPSession& session) = 0;
	[[nodiscard]]
	virtual SendPacketInfo* TakeReservedSendPacketInfo(RUDPSession& session) = 0;
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

	// ----------------------------------------
	// 연결 수립, 패킷 처리 및 세션 생명주기 위임
	// ----------------------------------------
	[[nodiscard]]
	virtual bool TryConnect(RUDPSession& session, NetBuffer& recvPacket, const sockaddr_in& clientAddr) = 0;
	[[nodiscard]]
	virtual bool CanProcessPacket(const RUDPSession& session, const sockaddr_in& clientAddr) = 0;
	[[nodiscard]]
	virtual bool OnRecvPacket(RUDPSession& session, NetBuffer& recvPacket) = 0;
	virtual void RefreshLastRecvPacketTime(RUDPSession& session, unsigned long long now) = 0;
	virtual void OnSendReply(RUDPSession& session, NetBuffer& recvPacket) = 0;
	virtual void Disconnect(RUDPSession& session, NetBuffer& recvPacket) = 0;

	virtual void SendHeartbeatPacket(RUDPSession& session, const unsigned long long now) = 0;
	[[nodiscard]]
	virtual bool CheckReservedSessionTimeout(const RUDPSession& session, unsigned long long now) = 0;
	virtual void AbortReservedSession(RUDPSession& session) = 0;
	virtual void InitializeSession(RUDPSession& session) = 0;
	virtual void SetSessionReservedTime(RUDPSession& session, unsigned long long now) = 0;

	// ----------------------------------------
	// 세션별 대칭키와 솔트 및 BCrypt 키 핸들 접근
	// 키 객체 버퍼와 키 핸들의 생성·해제 수명은 세션 암호 컨텍스트가 소유합니다.
	// ----------------------------------------
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

	// ----------------------------------------
	// @brief 세션 브로커 응답에 필요한 서버 포트와 세션 ID를 함께 반환합니다.
	// ----------------------------------------
	virtual void GetServerPortAndSessionId(const RUDPSession& session, PortType& outServerPort, SessionIdType& outSessionId) = 0;
};
