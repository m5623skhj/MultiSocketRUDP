#pragma once
#include <queue>
#include <map>
#include <set>
#include <mutex>
#include <shared_mutex>
#include <atomic>
#include <functional>
#include <MSWSock.h>
#include "PacketSequenceSetKey.h"

struct SendPacketInfo;

enum class IO_MODE : LONG
{
	IO_NONE_SENDING = 0
	, IO_SENDING
};

class SessionSendContext
{
public:
	SessionSendContext();
	~SessionSendContext() = default;

	SessionSendContext(const SessionSendContext&) = delete;
	SessionSendContext(SessionSendContext&&) = delete;
	SessionSendContext& operator=(const SessionSendContext&) = delete;
	SessionSendContext& operator=(SessionSendContext&&) = delete;

public:
	// ----------------------------------------
	// @brief RIO 송신 버퍼를 등록하고 초기화합니다.
	// @param rioFunctionTable RIO 확장 함수 테이블
	// @return 초기화 성공 여부 (true: 성공, false: 실패)
	// ----------------------------------------
	[[nodiscard]]
	bool Initialize(const RIO_EXTENSION_FUNCTION_TABLE& rioFunctionTable);
	// ----------------------------------------
	// @brief 등록된 RIO 송신 버퍼를 Deregister 합니다.
	// @param rioFunctionTable RIO 확장 함수 테이블
	// ----------------------------------------
	void Cleanup(const RIO_EXTENSION_FUNCTION_TABLE& rioFunctionTable);

	// ----------------------------------------
	// @brief 송신 시퀀스, 예약 패킷 및 큐에 남아있는 모든 SendPacketInfo를 정리합니다.
	// ----------------------------------------
	void Reset();

	// ----------------------------------------
	// @brief 송신 패킷 큐가 비어있는지 확인합니다.
	// @return 큐가 비어있으면 true, 아니면 false
	// ----------------------------------------
	[[nodiscard]]
	bool IsSendPacketInfoQueueEmpty();
	// ----------------------------------------
	// @brief 송신 패킷 큐의 현재 크기를 반환합니다.
	// @return 송신 패킷 큐 크기
	// ----------------------------------------
	[[nodiscard]]
	size_t GetSendPacketInfoQueueSize();
	// ----------------------------------------
	// @brief 송신 패킷 정보를 큐에 추가합니다.
	// @param info 추가할 SendPacketInfo 포인터
	// ----------------------------------------
	void PushSendPacketInfo(SendPacketInfo* info);
	// ----------------------------------------
	// @brief 큐의 맨 앞 송신 패킷을 반환하고 제거합니다.
	// @return 존재하면 SendPacketInfo 포인터, 없으면 nullptr
	// ----------------------------------------
	[[nodiscard]]
	SendPacketInfo* TryGetFrontAndPop();
	// ----------------------------------------
	// @brief 송신 대기 중인 패킷이 전혀 없는지 확인합니다.
	// @return 송신 큐와 예약 패킷이 모두 비어있으면 true
	// ----------------------------------------
	[[nodiscard]]
	bool IsNothingToSend();

	// ----------------------------------------
	// @brief 현재 예약된 송신 패킷 정보를 반환합니다.
	// @return 예약된 SendPacketInfo 포인터
	// ----------------------------------------
	[[nodiscard]]
	SendPacketInfo* GetReservedSendPacketInfo() const;
	// ----------------------------------------
	// @brief 예약 송신 패킷 정보를 설정합니다.
	// @param info 설정할 SendPacketInfo 포인터
	// ----------------------------------------
	void SetReservedSendPacketInfo(SendPacketInfo* info);

	// ----------------------------------------
	// @brief RIO 송신 버퍼의 시작 주소를 반환합니다.
	// @return 송신 버퍼 포인터
	// ----------------------------------------
	[[nodiscard]]
	char* GetRIOSendBuffer();
	// ----------------------------------------
	// @brief 등록된 RIO 송신 버퍼 ID를 반환합니다.
	// @return RIO_BUFFERID 값
	// ----------------------------------------
	[[nodiscard]]
	RIO_BUFFERID GetSendBufferId() const;
	// ----------------------------------------
	// @brief RIO 송신 버퍼 ID를 설정합니다.
	// @param id 설정할 RIO_BUFFERID
	// ----------------------------------------
	void SetSendRIOBufferId(RIO_BUFFERID id);

	// ----------------------------------------
	// @brief 현재 송신 IO 상태값에 대한 참조를 반환합니다.
	// @return IO_MODE 참조
	// ----------------------------------------
	[[nodiscard]]
	IO_MODE& GetIOMode();

	// ----------------------------------------
	// @brief 시퀀스를 키로 송신 패킷 정보를 맵에 등록합니다.
	// @param sequence 패킷 시퀀스
	// @param info 등록할 SendPacketInfo 포인터
	// ----------------------------------------
	void InsertSendPacketInfo(PacketSequence sequence, SendPacketInfo* info);

	// ----------------------------------------
	// @brief 시퀀스를 기준으로 송신 패킷 정보를 조회합니다.
	// @param sequence 패킷 시퀀스
	// @return 존재하면 SendPacketInfo 포인터, 없으면 nullptr
	// ----------------------------------------
	[[nodiscard]]
	SendPacketInfo* FindSendPacketInfo(PacketSequence sequence);

	// ----------------------------------------
	// @brief 시퀀스를 기준으로 송신 패킷 정보를 제거합니다.
	// @param sequence 패킷 시퀀스
	// ----------------------------------------
	void EraseSendPacketInfo(PacketSequence sequence);
	// ----------------------------------------
	// @brief 시퀀스를 기준으로 송신 패킷 정보를 찾아 제거 후 반환합니다.
	// @param sequence 패킷 시퀀스
	// @return 제거된 SendPacketInfo 포인터, 없으면 nullptr
	// ----------------------------------------
	[[nodiscard]]
	SendPacketInfo* FindAndEraseSendPacketInfo(PacketSequence sequence);
	// ----------------------------------------
	// @brief 각 SendPacketInfo에 대해 func를 호출한 뒤 맵을 비웁니다.
	// @param func 각 SendPacketInfo에 대해 호출할 함수
	// ----------------------------------------
	void ForEachAndClearSendPacketInfoMap(const std::function<void(SendPacketInfo*)>& func);

	// ----------------------------------------
	// @brief 캐시된 시퀀스 집합에 대한 참조를 반환합니다.
	// @return PacketSequenceSetKey 집합 참조
	// ----------------------------------------
	[[nodiscard]]
	std::set<MultiSocketRUDP::PacketSequenceSetKey>& GetCachedSequenceSet();
	// ----------------------------------------
	// @brief 캐시된 시퀀스 집합 보호용 mutex를 반환합니다.
	// @return mutex 참조
	// ----------------------------------------
	[[nodiscard]]
	std::mutex& GetCachedSequenceSetLock();

	// ----------------------------------------
	// @brief 마지막으로 사용된 송신 패킷 시퀀스를 반환합니다.
	// @return PacketSequence 값
	// ----------------------------------------
	[[nodiscard]]
	PacketSequence GetLastSendPacketSequence() const;
	// ----------------------------------------
	// @brief 마지막 송신 패킷 시퀀스를 증가시키고 반환합니다.
	// @return 증가된 PacketSequence 값
	// ----------------------------------------
	[[nodiscard]]
	PacketSequence IncrementLastSendPacketSequence();

private:
	SendPacketInfo* reservedSendPacketInfo = nullptr;
	char rioSendBuffer[MAX_SEND_BUFFER_SIZE]{};
	RIO_BUFFERID sendBufferId = RIO_INVALID_BUFFERID;
	IO_MODE ioMode = IO_MODE::IO_NONE_SENDING;

	std::mutex sendPacketInfoQueueLock;
	std::queue<SendPacketInfo*> sendPacketInfoQueue;

	std::atomic<PacketSequence> lastSendPacketSequence{};
	std::map<PacketSequence, SendPacketInfo*> sendPacketInfoMap;
	std::shared_mutex sendPacketInfoMapLock;

	std::set<MultiSocketRUDP::PacketSequenceSetKey> cachedSequenceSet;
	std::mutex cachedSequenceSetLock;
};