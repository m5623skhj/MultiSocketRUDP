#include "PreCompile.h"
#include <gtest/gtest.h>
#include "../Common/FlowController/RUDPFlowController.h"

class RUDPFlowControllerTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		fc.Reset();
	}

	RUDPFlowController fc;

	void GrowCwndTo(const uint8_t targetCwnd)
	{
		PacketSequence seq = fc.GetLastAckedSequence() + 1;
		while (fc.GetCwnd() < targetCwnd)
		{
			fc.OnReplyReceived(seq++);
		}
	}
};

TEST_F(RUDPFlowControllerTest, InitialState_CwndIsInitialValue)
{
	EXPECT_EQ(fc.GetCwnd(), 4);
}

TEST_F(RUDPFlowControllerTest, InitialState_LastAckedSequenceIsZero)
{
	EXPECT_EQ(fc.GetLastAckedSequence(), 0);
}

// ------------------------------------------------------------
// outstanding이 cwnd 미만이면 전송 가능해야 한다
// ------------------------------------------------------------
TEST_F(RUDPFlowControllerTest, CanSendPacket_ReturnsTrue_WhenOutstandingBelowCwnd)
{
	EXPECT_TRUE(fc.CanSendPacket(1, 0));
	EXPECT_TRUE(fc.CanSendPacket(4, 0));
}

// ------------------------------------------------------------
// outstanding이 cwnd 이상이면 전송 불가해야 한다
// ------------------------------------------------------------
TEST_F(RUDPFlowControllerTest, CanSendPacket_ReturnsFalse_WhenOutstandingReachesCwnd)
{
	EXPECT_FALSE(fc.CanSendPacket(5, 0));
	EXPECT_FALSE(fc.CanSendPacket(6, 0));
}

TEST_F(RUDPFlowControllerTest, CanSendPacket_DoesNotTruncateLargeOutstandingCount)
{
	EXPECT_FALSE(fc.CanSendPacket(256, 0));
	EXPECT_FALSE(fc.CanSendPacket(257, 0));
	EXPECT_FALSE(fc.CanSendPacket(258, 0));
	EXPECT_FALSE(fc.CanSendPacket(1025, 0));
	EXPECT_FALSE(fc.CanSendPacket((PacketSequence{ 1 } << 32) + 1, 0));
}

TEST_F(RUDPFlowControllerTest, CanSendPacket_HandlesSequenceWraparound)
{
	constexpr PacketSequence maxSequence = ~PacketSequence{ 0 };

	EXPECT_TRUE(fc.CanSendPacket(1, maxSequence - 1));
	EXPECT_FALSE(fc.CanSendPacket(4, maxSequence - 1));
	EXPECT_TRUE(fc.CanSendPacket(PacketSequence{ 1 } << 63, 0));
}

// ------------------------------------------------------------
// lastAcked와 nextSend가 같으면 outstanding=0으로 전송 가능해야 한다
// ------------------------------------------------------------
TEST_F(RUDPFlowControllerTest, CanSendPacket_ReturnsTrue_WhenNoOutstanding)
{
	EXPECT_TRUE(fc.CanSendPacket(1, 1));
}

// ------------------------------------------------------------
// 정상 ACK 수신 시 cwnd가 1 증가해야 한다
// ------------------------------------------------------------
TEST_F(RUDPFlowControllerTest, OnReplyReceived_IncrementsCwnd)
{
	const uint16_t before = fc.GetCwnd();
	fc.OnReplyReceived(1);
	EXPECT_EQ(fc.GetCwnd(), before + 1);
}

// ------------------------------------------------------------
// 정상 ACK 수신 시 lastReplySequence가 갱신되어야 한다
// ------------------------------------------------------------
TEST_F(RUDPFlowControllerTest, OnReplyReceived_UpdatesLastAckedSequence)
{
	fc.OnReplyReceived(3);
	EXPECT_EQ(fc.GetLastAckedSequence(), 3);
}

TEST_F(RUDPFlowControllerTest, OnReplyReceived_DoesNotAliasTwoToTheThirtySecondGap)
{
	constexpr PacketSequence replySequence = PacketSequence{ 1 } << 32;

	fc.OnReplyReceived(replySequence);

	EXPECT_EQ(fc.GetLastAckedSequence(), replySequence);
}

// ------------------------------------------------------------
// 중복 ACK(이전 시퀀스) 수신 시 cwnd가 변하지 않아야 한다
// ------------------------------------------------------------
TEST_F(RUDPFlowControllerTest, OnReplyReceived_DuplicateAck_DoesNotChangeCwnd)
{
	fc.OnReplyReceived(5);
	const uint16_t cwndAfterFirst = fc.GetCwnd();

	fc.OnReplyReceived(5);
	fc.OnReplyReceived(3);
	EXPECT_EQ(fc.GetCwnd(), cwndAfterFirst);
}

// ------------------------------------------------------------
// GAP_THRESHOLD(5) 초과 시 혼잡 이벤트가 발생해야 한다
// ------------------------------------------------------------
TEST_F(RUDPFlowControllerTest, OnReplyReceived_LargeGap_TriggersCongestion)
{
	fc.OnReplyReceived(1);
	const uint16_t cwndBefore = fc.GetCwnd();

	fc.OnReplyReceived(8);
	EXPECT_LT(fc.GetCwnd(), cwndBefore);
}

// ------------------------------------------------------------
// GAP_THRESHOLD(5) 이하 gap은 혼잡 이벤트를 발생시키지 않아야 한다
// ------------------------------------------------------------
TEST_F(RUDPFlowControllerTest, OnReplyReceived_SmallGap_DoesNotTriggerCongestion)
{
	fc.OnReplyReceived(1);
	const uint16_t cwndBefore = fc.GetCwnd();

	fc.OnReplyReceived(6);
	EXPECT_GE(fc.GetCwnd(), cwndBefore);
}

// ------------------------------------------------------------
// cwnd는 MAX_CWND를 초과하지 않아야 한다
// ------------------------------------------------------------
TEST_F(RUDPFlowControllerTest, OnReplyReceived_CwndDoesNotExceedMaxCwnd)
{
	GrowCwndTo(250);
	const uint16_t cwndAtMax = fc.GetCwnd();

	fc.OnReplyReceived(fc.GetLastAckedSequence() + 1);
	EXPECT_EQ(fc.GetCwnd(), cwndAtMax);
}

// ------------------------------------------------------------
// 혼잡 이벤트 발생 시 cwnd가 절반으로 줄어야 한다
// ------------------------------------------------------------
TEST_F(RUDPFlowControllerTest, OnCongestionEvent_HalvesCwnd)
{
	GrowCwndTo(8);
	const uint16_t before = fc.GetCwnd();

	fc.OnCongestionEvent();
	EXPECT_EQ(fc.GetCwnd(), before / 2);
}

// ------------------------------------------------------------
// cwnd가 1일 때 혼잡 이벤트 발생 시 최솟값 1을 유지해야 한다
// ------------------------------------------------------------
TEST_F(RUDPFlowControllerTest, OnCongestionEvent_CwndMinimumIsOne)
{
	fc.OnTimeout();
	fc.OnCongestionEvent();
	EXPECT_EQ(fc.GetCwnd(), 1);
}

// ------------------------------------------------------------
// 타임아웃 발생 시 cwnd가 1로 초기화되어야 한다
// ------------------------------------------------------------
TEST_F(RUDPFlowControllerTest, OnTimeout_HalvesCwndWithMinimumOne)
{
	GrowCwndTo(10);
	const uint16_t cwndBeforeTimeout = fc.GetCwnd();
	fc.OnTimeout();
	EXPECT_EQ(fc.GetCwnd(), std::max<uint8_t>(cwndBeforeTimeout / 2, 1));
}

// ------------------------------------------------------------
// 타임아웃 이후 ACK 수신 시 recovery 상태로 cwnd가 바로 증가하지 않아야 한다
// ------------------------------------------------------------
TEST_F(RUDPFlowControllerTest, OnTimeout_EntersRecovery_CwndNotIncreasedOnFirstAck)
{
	GrowCwndTo(8);
	fc.OnTimeout();

	const uint16_t cwndAfterTimeout = fc.GetCwnd();
	fc.OnReplyReceived(fc.GetLastAckedSequence() + 1);

	// recovery 상태에서 첫 ACK는 cwnd를 증가시키지 않고 recovery 해제만 함
	EXPECT_EQ(fc.GetCwnd(), cwndAfterTimeout);
}

// ------------------------------------------------------------
// recovery 해제 후 ACK 수신 시 cwnd가 정상적으로 증가해야 한다
// ------------------------------------------------------------
TEST_F(RUDPFlowControllerTest, AfterRecovery_CwndIncreasesNormally)
{
	fc.OnTimeout();
	PacketSequence seq = fc.GetLastAckedSequence() + 1;

	fc.OnReplyReceived(seq);
	++seq;
	const uint16_t cwndAfterRecovery = fc.GetCwnd();

	fc.OnReplyReceived(seq);
	EXPECT_EQ(fc.GetCwnd(), cwndAfterRecovery + 1);
}

// ------------------------------------------------------------
// Reset 후 초기 상태로 돌아와야 한다
// ------------------------------------------------------------
TEST_F(RUDPFlowControllerTest, Reset_RestoresInitialState)
{
	GrowCwndTo(20);
	fc.OnReplyReceived(10);
	fc.OnTimeout();

	fc.Reset();

	EXPECT_EQ(fc.GetCwnd(), 4);
	EXPECT_EQ(fc.GetLastAckedSequence(), 0);
}
