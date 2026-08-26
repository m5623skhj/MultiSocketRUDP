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
// 초기 상태에서 cwnd(4) 범위 내 전송은 가능해야 한다
// ------------------------------------------------------------
TEST_F(RUDPFlowManagerTest, CanSend_ReturnsTrue_WithinInitialCwnd)
{
	EXPECT_TRUE(fm.CanSend(1));
	EXPECT_TRUE(fm.CanSend(4));
}

// ------------------------------------------------------------
// cwnd를 초과하는 전송은 불가해야 한다
// ------------------------------------------------------------
TEST_F(RUDPFlowManagerTest, CanSend_ReturnsFalse_WhenExceedsCwnd)
{
	EXPECT_FALSE(fm.CanSend(5));
}

// ------------------------------------------------------------
// ACK 수신 후 cwnd가 늘어나 추가 전송이 가능해야 한다
// ------------------------------------------------------------
TEST_F(RUDPFlowManagerTest, CanSend_AllowsMore_AfterAckReceived)
{
	EXPECT_FALSE(fm.CanSend(5));

	fm.OnAckReceived(1);
	EXPECT_TRUE(fm.CanSend(5));
}

// ------------------------------------------------------------
// 윈도우 범위 내 시퀀스는 수신 가능해야 한다
// ------------------------------------------------------------
TEST_F(RUDPFlowManagerTest, CanAccept_ReturnsTrue_WhenWithinWindow)
{
	EXPECT_TRUE(fm.CanAccept(0));
	EXPECT_TRUE(fm.CanAccept(WINDOW_SIZE - 1));
}

// ------------------------------------------------------------
// 윈도우 범위를 벗어난 시퀀스는 수신 불가해야 한다
// ------------------------------------------------------------
TEST_F(RUDPFlowManagerTest, CanAccept_ReturnsFalse_WhenOutsideWindow)
{
	EXPECT_FALSE(fm.CanAccept(WINDOW_SIZE));
}

// ------------------------------------------------------------
// MarkReceived 후 윈도우가 슬라이딩되어 새 시퀀스를 수신 가능해야 한다
// ------------------------------------------------------------
TEST_F(RUDPFlowManagerTest, MarkReceived_SlidesWindow_AllowsNewSequence)
{
	EXPECT_FALSE(fm.CanAccept(WINDOW_SIZE));

	fm.MarkReceived(0);
	EXPECT_TRUE(fm.CanAccept(WINDOW_SIZE));
}

// ------------------------------------------------------------
// 타임아웃 후 cwnd가 1로 줄어 전송 가능 범위가 좁아져야 한다
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
// Reset 후 지정한 시퀀스부터 수신 가능해야 한다
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
// GetReceiveWindowEnd 테스트
// ------------------------------------------------------------
TEST_F(RUDPFlowManagerTest, GetReceiveWindowEnd_ReturnsCorrectEnd)
{
	EXPECT_EQ(fm.GetReceiveWindowEnd(), WINDOW_SIZE);

	fm.MarkReceived(0);
	EXPECT_EQ(fm.GetReceiveWindowEnd(), WINDOW_SIZE + 1);
}
