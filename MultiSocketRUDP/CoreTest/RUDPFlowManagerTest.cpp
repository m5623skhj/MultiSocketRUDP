#include "PreCompile.h"
#include <gtest/gtest.h>
#include "../Common/FlowController/RUDPFlowManager.h"

class RUDPFlowManagerTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		fm.Reset(0);
	}

	static constexpr BYTE WINDOW_SIZE = 16;
	RUDPFlowManager fm{ WINDOW_SIZE };
};


// ------------------------------------------------------------
// �ʱ� ���¿��� cwnd(4) ���� �� ������ �����ؾ� �Ѵ�
// ------------------------------------------------------------
TEST_F(RUDPFlowManagerTest, CanSend_ReturnsTrue_WithinInitialCwnd)
{
	EXPECT_TRUE(fm.CanSend(1));
	EXPECT_TRUE(fm.CanSend(4));
}

// ------------------------------------------------------------
// cwnd�� �ʰ��ϴ� ������ �Ұ��ؾ� �Ѵ�
// ------------------------------------------------------------
TEST_F(RUDPFlowManagerTest, CanSend_ReturnsFalse_WhenExceedsCwnd)
{
	EXPECT_FALSE(fm.CanSend(5));
}

// ------------------------------------------------------------
// ACK ���� �� cwnd�� �þ �߰� ������ �����ؾ� �Ѵ�
// ------------------------------------------------------------
TEST_F(RUDPFlowManagerTest, CanSend_AllowsMore_AfterAckReceived)
{
	EXPECT_FALSE(fm.CanSend(5));

	fm.OnAckReceived(1);
	EXPECT_TRUE(fm.CanSend(5));
}

// ------------------------------------------------------------
// ������ ���� �� �������� ���� �����ؾ� �Ѵ�
// ------------------------------------------------------------
TEST_F(RUDPFlowManagerTest, CanAccept_ReturnsTrue_WhenWithinWindow)
{
	EXPECT_TRUE(fm.CanAccept(0));
	EXPECT_TRUE(fm.CanAccept(WINDOW_SIZE - 1));
}

// ------------------------------------------------------------
// ������ ������ ��� �������� ���� �Ұ��ؾ� �Ѵ�
// ------------------------------------------------------------
TEST_F(RUDPFlowManagerTest, CanAccept_ReturnsFalse_WhenOutsideWindow)
{
	EXPECT_FALSE(fm.CanAccept(WINDOW_SIZE));
}

// ------------------------------------------------------------
// MarkReceived �� �����찡 �����̵��Ǿ� �� �������� ���� �����ؾ� �Ѵ�
// ------------------------------------------------------------
TEST_F(RUDPFlowManagerTest, MarkReceived_SlidesWindow_AllowsNewSequence)
{
	EXPECT_FALSE(fm.CanAccept(WINDOW_SIZE));

	fm.MarkReceived(0);
	EXPECT_TRUE(fm.CanAccept(WINDOW_SIZE));
}

// ------------------------------------------------------------
// Ÿ�Ӿƿ� �� cwnd�� 1�� �پ� ���� ���� ������ �������� �Ѵ�
// ------------------------------------------------------------
TEST_F(RUDPFlowManagerTest, OnTimeout_HalvesCwnd)
{
	const uint16_t cwndBeforeTimeout = fm.GetCwnd();
	fm.OnTimeout();
	EXPECT_EQ(fm.GetCwnd(), std::max<uint8_t>(cwndBeforeTimeout / 2, 1));

	EXPECT_TRUE(fm.CanSend(1));
	EXPECT_TRUE(fm.CanSend(2));
	EXPECT_FALSE(fm.CanSend(3));
}

// ------------------------------------------------------------
// Reset �� ������ ���������� ���� �����ؾ� �Ѵ�
// ------------------------------------------------------------
TEST_F(RUDPFlowManagerTest, Reset_SetsReceiveWindowToGivenSequence)
{
	fm.MarkReceived(0);
	fm.MarkReceived(1);
	fm.OnAckReceived(1);

	fm.Reset(100);

	EXPECT_TRUE(fm.CanAccept(100));
	EXPECT_FALSE(fm.CanAccept(99));
	EXPECT_EQ(fm.GetCwnd(), 4);
}

// ------------------------------------------------------------
// GetReceiveWindowEnd �׽�Ʈ
// ------------------------------------------------------------
TEST_F(RUDPFlowManagerTest, GetReceiveWindowEnd_ReturnsCorrectEnd)
{
	EXPECT_EQ(fm.GetReceiveWindowEnd(), WINDOW_SIZE);

	fm.MarkReceived(0);
	EXPECT_EQ(fm.GetReceiveWindowEnd(), WINDOW_SIZE + 1);
}
