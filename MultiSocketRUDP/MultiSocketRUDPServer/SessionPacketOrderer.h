#pragma once
#include <queue>
#include <functional>
#include <atomic>
#include <unordered_set>

#include "NetServerSerializeBuffer.h"
#include "../Common/etc/CoreType.h"

struct RecvPacketInfo
{
	explicit RecvPacketInfo(NetBuffer* inBuffer, const PacketSequence inPacketSequence)
		: buffer(inBuffer)
		, packetSequence(inPacketSequence)
	{
	}

	NetBuffer* buffer{};
	PacketSequence packetSequence{};
};

enum class ON_RECV_RESULT : uint8_t
{
	PROCESSED = 0,
	DUPLICATED_RECV,
	PACKET_HELD,
	ERROR_OCCURED,
};

class SessionPacketOrderer
{
public:
	using PacketProcessCallback = std::function<bool(NetBuffer&, PacketSequence)>;

	explicit SessionPacketOrderer(BYTE inMaxHoldingQueueSize);
	~SessionPacketOrderer();

	SessionPacketOrderer(const SessionPacketOrderer&) = delete;
	SessionPacketOrderer(const SessionPacketOrderer&&) = delete;
	SessionPacketOrderer& operator=(const SessionPacketOrderer&) = delete;
	SessionPacketOrderer& operator=(const SessionPacketOrderer&&) = delete;

public:
	// ----------------------------------------
	// @brief SessionPacketOrderer를 초기화합니다. 보류 큐를 비우고 다음 예상 시퀀스를 재설정합니다.
	// @param inMaxHoldingQueueSize 최대 보류 큐 크기
	// ----------------------------------------
	void Initialize(BYTE inMaxHoldingQueueSize);

	// ----------------------------------------
	// @brief 최대 보류 큐 크기를 설정합니다.
	// @param inMaxHoldingQueueSize 새로운 최대 보류 큐 크기
	// ----------------------------------------
	void SetMaximumHoldingQueueSize(BYTE inMaxHoldingQueueSize);

	// ----------------------------------------
	// @brief 패킷을 수신하고 순서에 맞게 처리하거나 보류 큐에 저장합니다.
	// @param sequence 수신된 패킷의 시퀀스 번호
	// @param buffer 수신된 패킷 데이터
	// @param callback 패킷이 순서대로 처리될 때 호출될 콜백 함수
	// @return 수신 처리 결과
	// ----------------------------------------
	[[nodiscard]]
	ON_RECV_RESULT OnReceive(PacketSequence sequence, NetBuffer& buffer, const PacketProcessCallback& callback);
	// ----------------------------------------
	// @brief 패킷 순서 관리자를 재설정합니다. 보류 큐를 비우고 다음 예상 시퀀스를 설정합니다.
	// @param startSequence 다음으로 예상되는 시작 시퀀스 번호
	// ----------------------------------------
	void Reset(PacketSequence startSequence);

	// ----------------------------------------
	// @brief 다음으로 예상되는 패킷 시퀀스 번호를 반환합니다.
	// @return 다음 예상 패킷 시퀀스 번호
	// ----------------------------------------
	[[nodiscard]]
	PacketSequence GetNextExpected() const noexcept;

private:
	// ----------------------------------------
	// @brief 현재 패킷을 처리하고 다음 예상 시퀀스를 증가시킵니다.
	// @param buffer 처리할 패킷 데이터
	// @param sequence 처리할 패킷의 시퀀스 번호
	// @param callback 패킷 처리 콜백 함수
	// @return 패킷 처리 성공 여부
	// ----------------------------------------
	[[nodiscard]]
	bool ProcessAndAdvance(NetBuffer& buffer, PacketSequence sequence, const PacketProcessCallback& callback);
	// ----------------------------------------
	// @brief 보류 큐에 있는 패킷들을 순서에 맞게 처리합니다.
	// @param callback 패킷 처리 콜백 함수
	// @return 보류 패킷 처리 성공 여부
	// ----------------------------------------
	[[nodiscard]]
	bool ProcessHoldingPacket(const PacketProcessCallback& callback);

	struct RecvPacketInfoPriority
	{
		bool operator()(const RecvPacketInfo& lhs, const RecvPacketInfo& rhs) const
		{
			return lhs.packetSequence > rhs.packetSequence;
		}
	};

	std::atomic<PacketSequence> nextRecvPacketSequence{};
	std::priority_queue<RecvPacketInfo, std::vector<RecvPacketInfo>, RecvPacketInfoPriority> recvPacketHolderQueue;
	std::unordered_set<PacketSequence> recvHoldingPacketSequences;

	BYTE maxHoldingQueueSize{};
};