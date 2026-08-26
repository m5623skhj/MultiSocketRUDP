#pragma once
#include "../Common/etc/CoreType.h"

namespace MultiSocketRUDP
{
	// ----------------------------------------
	// @brief 송신 캐시에서 일반 패킷과 응답 패킷의 시퀀스를 함께 정렬하기 위한 키입니다.
	// 일반 패킷(false)이 응답 패킷(true)보다 먼저 오고 같은 종류에서는 시퀀스 오름차순을 사용합니다.
	// ----------------------------------------
	struct PacketSequenceSetKey
	{
		PacketSequenceSetKey(const bool inIsReplyType, const PacketSequence inPacketSequence)
			: isReplyType(inIsReplyType), packetSequence(inPacketSequence)
		{
		}

		// ----------------------------------------
		// @brief std::set이 패킷 종류와 시퀀스 순으로 항목을 정렬할 수 있도록 비교합니다.
		// ----------------------------------------
		bool operator<(const PacketSequenceSetKey& other) const
		{
			if (isReplyType != other.isReplyType)
			{
				return isReplyType < other.isReplyType;
			}

			return packetSequence < other.packetSequence;
		}

		bool isReplyType{};
		PacketSequence packetSequence{};
	};
}
