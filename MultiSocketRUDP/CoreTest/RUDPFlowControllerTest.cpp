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
// outstanding�� cwnd �̸��̸� ���� �����ؾ� �Ѵ�
// ------------------------------------------------------------
TEST_F(RUDPFlowControllerTest, CanSendPacket_ReturnsTrue_WhenOutstandingBelowCwnd)
{
	EXPECT_TRUE(fc.CanSendPacket(1, 0));
	EXPECT_TRUE(fc.CanSendPacket(4, 0));
}

// ------------------------------------------------------------
// outstanding�� cwnd �̻��̸� ���� �Ұ��ؾ� �Ѵ�
// ------------------------------------------------------------
TEST_F(RUDPFlowControllerTest, CanSendPacket_ReturnsFalse_WhenOutstandingReachesCwnd)
{
	EXPECT_FALSE(fc.CanSendPacket(5, 0));
	EXPECT_FALSE(fc.CanSendPacket(6, 0));
}

// ------------------------------------------------------------
// lastAcked�� nextSend�� ������ outstanding=0���� ���� �����ؾ� �Ѵ�
// ------------------------------------------------------------
TEST_F(RUDPFlowControllerTest, CanSendPacket_ReturnsTrue_WhenNoOutstanding)
{
	EXPECT_TRUE(fc.CanSendPacket(1, 1));
}

// ------------------------------------------------------------
// ���� ACK ���� �� cwnd�� 1 �����ؾ� �Ѵ�
// ------------------------------------------------------------
TEST_F(RUDPFlowControllerTest, OnReplyReceived_IncrementsCwnd)
{
	const uint16_t before = fc.GetCwnd();
	fc.OnReplyReceived(1);
	EXPECT_EQ(fc.GetCwnd(), before + 1);
}

// ------------------------------------------------------------
// ���� ACK ���� �� lastReplySequence�� ���ŵǾ�� �Ѵ�
// ------------------------------------------------------------
TEST_F(RUDPFlowControllerTest, OnReplyReceived_UpdatesLastAckedSequence)
{
	fc.OnReplyReceived(3);
	EXPECT_EQ(fc.GetLastAckedSequence(), 3);
}

// ------------------------------------------------------------
// �ߺ� ACK(���� ������) ���� �� cwnd�� ������ �ʾƾ� �Ѵ�
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
// GAP_THRESHOLD(5) �ʰ� �� ȥ�� �̺�Ʈ�� �߻��ؾ� �Ѵ�
// ------------------------------------------------------------
TEST_F(RUDPFlowControllerTest, OnReplyReceived_LargeGap_TriggersCongestion)
{
	fc.OnReplyReceived(1);
	const uint16_t cwndBefore = fc.GetCwnd();

	fc.OnReplyReceived(8);
	EXPECT_LT(fc.GetCwnd(), cwndBefore);
}

// ------------------------------------------------------------
// GAP_THRESHOLD(5) ���� gap�� ȥ�� �̺�Ʈ�� �߻���Ű�� �ʾƾ� �Ѵ�
// ------------------------------------------------------------
TEST_F(RUDPFlowControllerTest, OnReplyReceived_SmallGap_DoesNotTriggerCongestion)
{
	fc.OnReplyReceived(1);
	const uint16_t cwndBefore = fc.GetCwnd();

	fc.OnReplyReceived(6);
	EXPECT_GE(fc.GetCwnd(), cwndBefore);
}

// ------------------------------------------------------------
// cwnd�� MAX_CWND�� �ʰ����� �ʾƾ� �Ѵ�
// ------------------------------------------------------------
TEST_F(RUDPFlowControllerTest, OnReplyReceived_CwndDoesNotExceedMaxCwnd)
{
	GrowCwndTo(250);
	const uint16_t cwndAtMax = fc.GetCwnd();

	fc.OnReplyReceived(fc.GetLastAckedSequence() + 1);
	EXPECT_EQ(fc.GetCwnd(), cwndAtMax);
}

// ------------------------------------------------------------
// ȥ�� �̺�Ʈ �߻� �� cwnd�� �������� �پ�� �Ѵ�
// ------------------------------------------------------------
TEST_F(RUDPFlowControllerTest, OnCongestionEvent_HalvesCwnd)
{
	GrowCwndTo(8);
	const uint16_t before = fc.GetCwnd();

	fc.OnCongestionEvent();
	EXPECT_EQ(fc.GetCwnd(), before / 2);
}

// ------------------------------------------------------------
// cwnd�� 1�� �� ȥ�� �̺�Ʈ �߻� �� �ּڰ� 1�� �����ؾ� �Ѵ�
// ------------------------------------------------------------
TEST_F(RUDPFlowControllerTest, OnCongestionEvent_CwndMinimumIsOne)
{
	fc.OnTimeout();
	fc.OnCongestionEvent();
	EXPECT_EQ(fc.GetCwnd(), 1);
}

// ------------------------------------------------------------
// Ÿ�Ӿƿ� �߻� �� cwnd�� 1�� �ʱ�ȭ�Ǿ�� �Ѵ�
// ------------------------------------------------------------
TEST_F(RUDPFlowControllerTest, OnTimeout_HalvesCwndWithMinimumOne)
{
	GrowCwndTo(10);
	const uint16_t cwndBeforeTimeout = fc.GetCwnd();
	fc.OnTimeout();
	EXPECT_EQ(fc.GetCwnd(), std::max<uint8_t>(cwndBeforeTimeout / 2, 1));
}

// ------------------------------------------------------------
// Ÿ�Ӿƿ� ���� ACK ���� �� recovery ���·� cwnd�� �ٷ� �������� �ʾƾ� �Ѵ�
// ------------------------------------------------------------
TEST_F(RUDPFlowControllerTest, OnTimeout_EntersRecovery_CwndNotIncreasedOnFirstAck)
{
	GrowCwndTo(8);
	fc.OnTimeout();

	const uint16_t cwndAfterTimeout = fc.GetCwnd();
	fc.OnReplyReceived(fc.GetLastAckedSequence() + 1);

	// recovery ���¿��� ù ACK�� cwnd�� ������Ű�� �ʰ� recovery ������ ��
	EXPECT_EQ(fc.GetCwnd(), cwndAfterTimeout);
}

// ------------------------------------------------------------
// recovery ���� �� ACK ���� �� cwnd�� ���������� �����ؾ� �Ѵ�
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
// Reset �� �ʱ� ���·� ���ƿ;� �Ѵ�
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
