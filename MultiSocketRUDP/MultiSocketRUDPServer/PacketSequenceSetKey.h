#pragma once
#include "../Common/etc/CoreType.h"

namespace MultiSocketRUDP
{
	// ----------------------------------------
	// @brief 송신 캐시에서 채널·응답 여부·시퀀스를 구분합니다.
	// 서로 다른 채널의 같은 시퀀스가 중복 패킷으로 제거되지 않도록 합니다.
	// ----------------------------------------
	struct PacketSequenceSetKey
	{
		PacketSequenceSetKey(const bool inIsReplyType, const PacketSequence inPacketSequence, const bool inIsUnreliable = false)
			: isReplyType(inIsReplyType), isUnreliable(inIsUnreliable), packetSequence(inPacketSequence)
		{
		}

		// ----------------------------------------
		// @brief std::set이 패킷 종류와 시퀀스 순으로 항목을 정렬할 수 있도록 비교합니다.
		// ----------------------------------------
		bool operator<(const PacketSequenceSetKey& other) const
		{
			if (isUnreliable != other.isUnreliable) return isUnreliable < other.isUnreliable;
			if (isReplyType != other.isReplyType)
			{
				return isReplyType < other.isReplyType;
			}

			return packetSequence < other.packetSequence;
		}

		bool isReplyType{};
		bool isUnreliable{};
		PacketSequence packetSequence{};
	};
}
