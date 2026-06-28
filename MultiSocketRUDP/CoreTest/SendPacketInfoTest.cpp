#include "PreCompile.h"
#include <gtest/gtest.h>

#include "SendPacketInfo.h"

namespace
{
	SendPacketInfo* AllocSendPacketInfo(const bool isReplyType = false)
	{
		NetBuffer* buffer = NetBuffer::Alloc();
		SendPacketInfo* info = sendPacketInfoPool->Alloc();
		EXPECT_NE(buffer, nullptr);
		EXPECT_NE(info, nullptr);

		if (buffer == nullptr || info == nullptr)
		{
			NetBuffer::Free(buffer);
			return nullptr;
		}

		info->Initialize(nullptr, 0, buffer, 1, isReplyType);
		return info;
	}
}

// ============================================================
// RTT sampling state
//
// SendPacketInfo owns the per-packet state used by Karn's algorithm.
// A packet can produce an RTT sample only before it has been retransmitted.
// ============================================================

TEST(SendPacketInfoTest, DataPacket_MarkSentThenTryGetRttSample_ReturnsMeasuredDuration)
{
	SendPacketInfo* info = AllocSendPacketInfo(false);
	ASSERT_NE(info, nullptr);

	const auto sentAt = std::chrono::steady_clock::now();
	const auto ackAt = sentAt + std::chrono::milliseconds(37);
	info->MarkSentForRttSample(sentAt);

	std::chrono::steady_clock::duration sample{};
	EXPECT_TRUE(info->TryGetRttSample(ackAt, sample));
	EXPECT_EQ(std::chrono::duration_cast<std::chrono::milliseconds>(sample).count(), 37);

	SendPacketInfo::Free(info);
}

TEST(SendPacketInfoTest, DataPacket_WithoutSendTime_ReturnsFalse)
{
	SendPacketInfo* info = AllocSendPacketInfo(false);
	ASSERT_NE(info, nullptr);

	std::chrono::steady_clock::duration sample{};
	EXPECT_FALSE(info->TryGetRttSample(std::chrono::steady_clock::now(), sample));

	SendPacketInfo::Free(info);
}

TEST(SendPacketInfoTest, ReplyPacket_NeverProducesRttSample)
{
	SendPacketInfo* info = AllocSendPacketInfo(true);
	ASSERT_NE(info, nullptr);

	const auto sentAt = std::chrono::steady_clock::now();
	info->MarkSentForRttSample(sentAt);

	std::chrono::steady_clock::duration sample{};
	EXPECT_FALSE(info->TryGetRttSample(sentAt + std::chrono::milliseconds(10), sample));

	SendPacketInfo::Free(info);
}

TEST(SendPacketInfoTest, InvalidateRttSample_PreventsLaterSample)
{
	SendPacketInfo* info = AllocSendPacketInfo(false);
	ASSERT_NE(info, nullptr);

	const auto sentAt = std::chrono::steady_clock::now();
	info->MarkSentForRttSample(sentAt);
	info->InvalidateRttSample();

	std::chrono::steady_clock::duration sample{};
	EXPECT_FALSE(info->TryGetRttSample(sentAt + std::chrono::milliseconds(50), sample));

	SendPacketInfo::Free(info);
}

TEST(SendPacketInfoTest, Initialize_ResetsRttSampleStateForReusedPacketInfo)
{
	SendPacketInfo* info = AllocSendPacketInfo(false);
	ASSERT_NE(info, nullptr);

	info->MarkSentForRttSample(std::chrono::steady_clock::now());
	info->InvalidateRttSample();
	SendPacketInfo::Free(info);

	SendPacketInfo* reusedInfo = AllocSendPacketInfo(false);
	ASSERT_NE(reusedInfo, nullptr);

	const auto sentAt = std::chrono::steady_clock::now();
	reusedInfo->MarkSentForRttSample(sentAt);

	std::chrono::steady_clock::duration sample{};
	EXPECT_TRUE(reusedInfo->TryGetRttSample(sentAt + std::chrono::milliseconds(25), sample));
	EXPECT_EQ(std::chrono::duration_cast<std::chrono::milliseconds>(sample).count(), 25);

	SendPacketInfo::Free(reusedInfo);
}
