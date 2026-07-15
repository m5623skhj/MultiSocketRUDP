#include "PreCompile.h"
#include <gtest/gtest.h>

#include "SendPacketInfo.h"
#include "MultiSocketRUDPCore.h"
#include "RUDPSessionTestAccess.h"

namespace
{
	class SendPacketInfoTestSession final : public RUDPSession
	{
	public:
		explicit SendPacketInfoTestSession(MultiSocketRUDPCore& inCore) : RUDPSession(inCore) {}
	};

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

// ------------------------------------------------------------
// 초기화가 패킷 메타데이터와 버퍼를 저장하고 재전송 및 참조 상태를 기본값으로 설정하는지 확인합니다.
// ------------------------------------------------------------
TEST(SendPacketInfoTest, InitializeStoresPacketMetadataAndBuffer)
{
	NetBuffer* buffer = NetBuffer::Alloc();
	SendPacketInfo* info = sendPacketInfoPool->Alloc();
	ASSERT_NE(buffer, nullptr);
	ASSERT_NE(info, nullptr);

	info->Initialize(nullptr, 42, buffer, 77, true);

	EXPECT_EQ(info->GetBuffer(), buffer);
	EXPECT_EQ(info->ownerGeneration, 42u);
	EXPECT_EQ(info->sendPacketSequence, 77);
	EXPECT_TRUE(info->isReplyType);
	EXPECT_EQ(info->retransmissionCount, 0);
	EXPECT_EQ(info->scheduleVersion, 0u);
	EXPECT_EQ(info->refCount.load(), 1);
	SendPacketInfo::Free(info);
}

// ------------------------------------------------------------
// 소유 세션 generation이 변경되면 이전 SendPacketInfo가 유효하지 않은 소유자로 판정되는지 확인합니다.
// ------------------------------------------------------------
TEST(SendPacketInfoTest, OwnerValidityTracksSessionGeneration)
{
	MultiSocketRUDPCore core{ L"", L"" };
	SendPacketInfoTestSession session(core);
	RUDPSessionBehaviorAccess::InitializeSession(session);
	NetBuffer* buffer = NetBuffer::Alloc();
	SendPacketInfo* info = sendPacketInfoPool->Alloc();
	ASSERT_NE(buffer, nullptr);
	ASSERT_NE(info, nullptr);

	info->Initialize(&session, session.GetSessionGeneration(), buffer, 1, false);
	EXPECT_TRUE(info->IsOwnerValid());

	RUDPSessionBehaviorAccess::InitializeSession(session);
	EXPECT_FALSE(info->IsOwnerValid());
	SendPacketInfo::Free(info);
}

// ------------------------------------------------------------
// 송신 시각과 같거나 더 이른 ACK 시각에서는 유효한 RTT 표본을 만들지 않는지 확인합니다.
// ------------------------------------------------------------
TEST(SendPacketInfoTest, EqualOrEarlierAckTimeDoesNotProduceRttSample)
{
	SendPacketInfo* info = AllocSendPacketInfo(false);
	ASSERT_NE(info, nullptr);
	const auto sentAt = std::chrono::steady_clock::time_point(std::chrono::milliseconds(100));
	info->MarkSentForRttSample(sentAt);
	std::chrono::steady_clock::duration sample{};

	EXPECT_FALSE(info->TryGetRttSample(sentAt, sample));
	EXPECT_FALSE(info->TryGetRttSample(sentAt - std::chrono::milliseconds(1), sample));
	SendPacketInfo::Free(info);
}

// ------------------------------------------------------------
// 소유자가 없는 패킷 정보는 무효이며 nullptr 해제가 안전하게 무시되는지 확인합니다.
// ------------------------------------------------------------
TEST(SendPacketInfoTest, NullOwnerIsInvalidAndFreeNullIsSafe)
{
	SendPacketInfo* info = AllocSendPacketInfo(false);
	ASSERT_NE(info, nullptr);
	EXPECT_FALSE(info->IsOwnerValid());
	EXPECT_NO_FATAL_FAILURE(SendPacketInfo::Free(nullptr));
	SendPacketInfo::Free(info);
}
