#pragma once
#include "NetServerSerializeBuffer.h"
#include "BuildConfig.h"
#include "../Common/etc/CoreType.h"
#include <atomic>
#include <thread>
#include <mutex>
#include <array>
#include <map>
#include "ServerAliveChecker.h"

#include "Queue.h"
#include <queue>
#include "../Common/TLS/TLSHelper.h"

#pragma comment(lib, "ws2_32.lib")

class IPacket;

// ----------------------------------------
// @brief ACK 또는 재전송 완료까지 유지되는 클라이언트 송신 패킷 상태입니다.
// buffer와 객체 자체는 참조 카운트로 관리되며 송신 맵과 송신·재전송 경로가 참조를 공유합니다.
// ----------------------------------------
struct SendPacketInfo
{
	NetBuffer* buffer{};
	PacketRetransmissionCount retransmissionCount{};
	PacketSequence sendPacketSequence{};
	unsigned long long retransmissionTimeStamp{};
	std::list<SendPacketInfo*>::iterator listItor;
	std::atomic<int8_t> refCount{ 0 };

	// ----------------------------------------
	// @brief 버퍼 소유 참조를 추가하고 새 송신 시퀀스의 재전송 상태를 초기화합니다.
	// ----------------------------------------
	void Initialize(NetBuffer* inBuffer, const PacketSequence inSendPacketSequence)
	{
		buffer = inBuffer;
		sendPacketSequence = inSendPacketSequence;
		retransmissionCount = {};
		retransmissionTimeStamp = {};
		NetBuffer::AddRefCount(inBuffer);
		refCount = 1;
	}

	void AddRefCount()
	{
		refCount.fetch_add(1, std::memory_order_relaxed);
	}

	static void Free(SendPacketInfo* target);

	[[nodiscard]]
	NetBuffer* GetBuffer() const { return buffer; }
};

// ----------------------------------------
// @brief 순서가 맞을 때까지 수신 보류 큐에서 유지되는 패킷 정보입니다.
// ----------------------------------------
struct RecvPacketInfo
{
	explicit RecvPacketInfo(NetBuffer* inBuffer, const PacketSequence inPacketSequence, const PACKET_TYPE inPacketType)
		: buffer(inBuffer)
		, packetSequence(inPacketSequence)
		, packetType(inPacketType)
	{
	}

	NetBuffer* buffer{};
	PacketSequence packetSequence{};
	PACKET_TYPE packetType{};
};

// ----------------------------------------
// @brief 세션 브로커 연결, RUDP 송수신, 흐름 제어 및 재전송을 담당하는 클라이언트 코어입니다.
// Stop과 Disconnect의 생명주기 변경은 lifecycleLock으로 직렬화되며 Start는 외부에서 직렬 호출해야 합니다.
// 송신·수신 공유 컨테이너는 각 전용 mutex로 보호됩니다.
// SendPacket은 입력 패킷을 내부 버퍼로 직렬화하며 GetReceivedPacket이 반환한 NetBuffer의 해제 책임은 호출자에게 전달됩니다.
// ----------------------------------------
class RUDPClientCore
{
public:
	RUDPClientCore();
	virtual ~RUDPClientCore() = default;
	RUDPClientCore& operator=(const RUDPClientCore&) = delete;
	RUDPClientCore(RUDPClientCore&&) = delete;

protected:
	// ----------------------------------------
	// @brief 옵션을 읽고 세션을 발급받은 뒤 RUDP 소켓과 작업 스레드를 시작합니다.
	// @return 초기화 단계 중 하나라도 실패하면 false를 반환합니다.
	// ----------------------------------------
	virtual bool Start(const std::wstring& clientCoreOptionFile, const std::wstring& sessionGetterOptionFilePath, bool printLogToConsole);
	// ----------------------------------------
	// @brief 작업 스레드와 감시기를 중단하고 소켓·암호 및 송신 추적·보류 자원을 정리합니다.
	// ----------------------------------------
	virtual void Stop();
	// ----------------------------------------
	// @brief 서버 생존 감시를 중단하고 실행 중인 송수신 및 재전송 스레드를 join합니다.
	// ----------------------------------------
	void JoinThreads();
	[[nodiscard]]
	virtual bool ShouldSendConnectPacketOnStart() const { return true; }
	[[nodiscard]]
	virtual bool ShouldSendReplyToServer(PacketSequence inRecvPacketSequence, unsigned int inPacketId) const { return true; }

public:
	bool IsStopped() const { return isStopped.load(std::memory_order_acquire); }
	bool IsConnected() const { return isConnected; }

private:
	// ----------------------------------------
	// 프로세스 전역 Winsock은 첫 클라이언트가 초기화하고 마지막 클라이언트가 해제합니다.
	// clientCountInThisProcessLock이 참조 횟수와 초기화·해제 구간을 보호합니다.
	// ----------------------------------------
	bool CreateRUDPSocket();
	void SendConnectPacket();
	bool AcquireClientProcessReference();
	void ReleaseClientProcessReference();

private:
	std::atomic_bool isStopped{ true };
	std::atomic_bool threadStopFlag{};
	std::atomic_bool isConnected{};
	std::atomic_bool hasClientProcessReference{};
	std::mutex lifecycleLock;

#pragma region SessionGetter
#if USE_IOCP_SESSION_GETTER
private:
	// ----------------------------------------
	// @brief IOCP NetClient를 사용하여 세션 브로커 응답을 수신하는 내부 구현입니다.
	// ----------------------------------------
	class SessionGetter : public CNetClient
	{
	public:
		bool Start(const std::wstring& optionFilePath);

	private:
		virtual void OnConnectionComplete();
		virtual void OnRecv(CNetServerSerializationBuf* recvBuffer);
		virtual void OnSend(int sendsize);

		virtual void OnWorkerThreadBegin();
		virtual void OnWorkerThreadEnd();
		virtual void OnError(st_Error* error);
	};

	SessionGetter sessionGetter;
#else
private:
	// ----------------------------------------
	// TLS 세션 브로커 연결과 응답 스트림 조립
	// TrySetTargetSessionInfo는 TLS close_notify와 전체 RUDP 세션 페이로드를 모두 확인합니다.
	// ----------------------------------------
	bool RunGetSessionFromServer(const std::wstring& optionFilePath);
	bool GetSessionFromServer();
	bool TryConnectToSessionBroker() const;
	bool TrySetTargetSessionInfo();

private:
	WCHAR sessionBrokerIP[16]{};
	PortType sessionBrokerPort{};

	SOCKET sessionBrokerSocket{ INVALID_SOCKET };

#endif

	TLSHelper::TLSHelperClient tlsHelper;

private:
	// ----------------------------------------
	// @brief 브로커 응답에서 RUDP 서버 주소, 세션 식별자 및 암호 컨텍스트를 설정합니다.
	// ----------------------------------------
	bool SetTargetSessionInfo(OUT NetBuffer& receivedBuffer);

private:
	std::string serverIp{};
	PortType port{};
	SessionIdType sessionId{};
	unsigned char sessionKey[SESSION_KEY_SIZE];
	unsigned char sessionSalt[SESSION_SALT_SIZE];
	unsigned char* keyObjectBuffer{};
	BCRYPT_KEY_HANDLE sessionKeyHandle{};

#pragma endregion SessionGetter

#pragma region RUDP
private:
	// ----------------------------------------
	// RUDP 작업 스레드
	// 수신, 송신 및 재전송 루프는 threadStopFlag와 Windows 이벤트를 사용해 종료를 조정합니다.
	// ----------------------------------------
	bool RunThreads();
	void RunRecvThread();
	void RunSendThread();
	void RunRetransmissionThread();

	// ----------------------------------------
	// 수신 데이터그램 검증·복호화와 패킷 종류별 ACK 및 보류 큐 처리
	// ----------------------------------------
	void OnRecvStream(NetBuffer& recvBuffer, int recvSize);
	void ProcessRecvPacket(OUT NetBuffer& receivedBuffer);
	void OnSendReply(NetBuffer& recvPacket, PacketSequence packetSequence);
	void SendReplyToServer(PacketSequence inRecvPacketSequence, PACKET_TYPE packetType = PACKET_TYPE::SEND_REPLY_TYPE);
	void DoSend();
	static void SleepRemainingFrameTime(OUT TickSet& tickSet, unsigned int intervalMs);

	PacketSequence GetNextRecvPacketSequence() const
	{
		std::scoped_lock lock(recvPacketHoldingQueueLock);
		return nextRecvPacketSequence;
	}

private:
	SOCKET rudpSocket{ INVALID_SOCKET };
	sockaddr_in serverAddr{};

	std::jthread recvThread{};
	std::jthread sendThread{};
	std::jthread retransmissionThread{};
	std::array<HANDLE, 2> sendEventHandles{};

private:
	// ----------------------------------------
	// ACK 대기 송신 패킷 맵은 sendPacketInfoMapLock으로 보호됩니다.
	// ----------------------------------------
	std::atomic<PacketSequence> lastSendPacketSequence{};
	std::map<PacketSequence, SendPacketInfo*> sendPacketInfoMap;
	std::mutex sendPacketInfoMapLock;

	struct RecvPacketInfoPriority
	{
		bool operator()(const RecvPacketInfo& lfh, const RecvPacketInfo& rfh) const
		{
			return lfh.packetSequence > rfh.packetSequence;
		}
	};
	// ----------------------------------------
	// 순서가 맞지 않은 수신 패킷은 최소 시퀀스 우선 큐에 보관합니다.
	// 큐와 nextRecvPacketSequence 접근은 recvPacketHoldingQueueLock으로 직렬화됩니다.
	// ----------------------------------------
	std::priority_queue<RecvPacketInfo, std::vector<RecvPacketInfo>, RecvPacketInfoPriority> recvPacketHoldingQueue;
	mutable std::mutex recvPacketHoldingQueueLock;

	std::atomic<BYTE> remoteAdvertisedWindow{ 1 };
	std::atomic<PacketSequence> lastAckedSequence{ 0 };
	PacketSequence nextRecvPacketSequence{ 1 };
#pragma endregion RUDP

public:
	// ----------------------------------------
	// @brief 현재 수신 보류 큐에 저장된 패킷 수를 반환합니다.
	// ----------------------------------------
	unsigned int GetRemainPacketSize();
	// ----------------------------------------
	// @brief 다음 기대 시퀀스의 콘텐츠 패킷을 큐에서 꺼냅니다.
	// 중복·하트비트 패킷은 내부에서 해제하며 반환된 NetBuffer의 해제 책임은 호출자에게 있습니다.
	// @return 시퀀스 갭이 있거나 큐가 비어 있으면 nullptr을 반환합니다.
	// ----------------------------------------
	NetBuffer* GetReceivedPacket();
	// ----------------------------------------
	// @brief 콘텐츠 패킷을 직렬화하고 흐름 제어 윈도우에 따라 즉시 송신하거나 대기시킵니다.
	// ----------------------------------------
	void SendPacket(OUT IPacket& packet);
	// ----------------------------------------
	// @brief 연결 상태를 해제로 표시하고 서버에 연결 해제 코어 패킷을 보냅니다.
	// ----------------------------------------
	void Disconnect();

#if _DEBUG
	void SendPacketForTest(char* streamData, int streamSize);
#endif

private:
	// ----------------------------------------
	// @brief 패킷을 암호화한 뒤 코어 패킷은 즉시 등록하고 콘텐츠 패킷은 원격 광고 윈도우에 따라 등록하거나 보류합니다.
	// @param buffer 암호화하고 송신할 NetBuffer
	// @param inSendPacketSequence 패킷 순서 번호
	// @param isCorePacket 흐름 제어 윈도우를 우회하는 코어 패킷 여부
	// ----------------------------------------
	void SendPacket(OUT NetBuffer& buffer, PacketSequence inSendPacketSequence, bool isCorePacket);
	// ----------------------------------------
	// @brief 이미 추적 중인 패킷 버퍼의 참조를 추가하고 실제 소켓 송신 큐에 넣은 뒤 송신 스레드를 깨웁니다.
	// ----------------------------------------
	void SendPacket(const SendPacketInfo& sendPacketInfo);
	// ----------------------------------------
	// @brief NetBuffer의 송신 정보를 생성하여 ACK 대기 맵과 실제 소켓 송신 큐에 등록합니다.
	// @param buffer 전송할 NetBuffer(이미 인코딩되어 있어야 함)
	// @param inSendPacketSequence 패킷 시퀀스 번호
	// ----------------------------------------
	void RegisterSendPacketInfo(NetBuffer& buffer, PacketSequence inSendPacketSequence);
	// ----------------------------------------
	// @brief 원격 광고 윈도우에 여유가 생긴 만큼 시퀀스 순서로 대기 패킷을 송신 맵에 등록합니다.
	// ----------------------------------------
	void TryFlushPendingQueue();
	static inline WORD GetPayloadLength(const NetBuffer& buffer);
	bool ReadOptionFile(const std::wstring& clientCoreOptionFile, const std::wstring& sessionGetterOptionFilePath);
	bool ReadClientCoreOptionFile(const std::wstring& optionFilePath);
	bool ReadSessionGetterOptionFile(const std::wstring& optionFilePath);

private:
	// ----------------------------------------
	// 실제 소켓 송신 대기 큐. 큐 접근은 sendBufferQueueLock으로 보호되고 semaphore가 소비자를 깨웁니다.
	// ----------------------------------------
	CListBaseQueue<NetBuffer*> sendBufferQueue;
	std::mutex sendBufferQueueLock;

	struct PendingPacketInfo
	{
		PacketSequence sequence;
		NetBuffer* buffer;

		bool operator>(const PendingPacketInfo& other) const
		{
			return sequence > other.sequence;
		}
	};

	// ----------------------------------------
	// 원격 수신 윈도우가 가득 찼을 때 보류한 패킷을 시퀀스 오름차순으로 유지합니다.
	// ----------------------------------------
	std::priority_queue<PendingPacketInfo, std::vector<PendingPacketInfo>, std::greater<PendingPacketInfo>> pendingPacketQueue;
	std::mutex pendingPacketQueueLock;

private:
	PacketRetransmissionCount maxPacketRetransmissionCount{};
	unsigned int retransmissionThreadSleepMs{};
	unsigned int serverAliveCheckMs{};

private:
	ServerAliveChecker serverAliveChecker;

private:
	static inline unsigned short clientCountInThisProcess{};
	static inline std::mutex clientCountInThisProcessLock;
};

static auto sendPacketInfoPool = new CTLSMemoryPool<SendPacketInfo>(2, true);
