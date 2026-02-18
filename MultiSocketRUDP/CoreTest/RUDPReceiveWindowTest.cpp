#include "PreCompile.h"
#include <gtest/gtest.h>
#include "../Common/FlowController/RUDPReceiveWindow.h"

class RUDPReceiveWindowTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        rw.Reset(0);
    }

    static constexpr BYTE WINDOW_SIZE = 16;
    RUDPReceiveWindow rw{ WINDOW_SIZE };
};

TEST_F(RUDPReceiveWindowTest, InitialState_WindowStartIsZero)
{
    EXPECT_EQ(rw.GetWindowStart(), 0);
}

TEST_F(RUDPReceiveWindowTest, InitialState_WindowEndEqualsWindowSize)
{
    EXPECT_EQ(rw.GetWindowEnd(), WINDOW_SIZE);
}

TEST_F(RUDPReceiveWindowTest, InitialState_WindowSizeIsCorrect)
{
    EXPECT_EQ(rw.GetWindowSize(), WINDOW_SIZE);
}

// ------------------------------------------------------------
// 윈도우 범위 내 시퀀스는 수신 가능해야 한다
// ------------------------------------------------------------
TEST_F(RUDPReceiveWindowTest, CanReceive_ReturnsTrue_WhenWithinWindow)
{
    EXPECT_TRUE(rw.CanReceive(0));
    EXPECT_TRUE(rw.CanReceive(WINDOW_SIZE - 1));
}

// ------------------------------------------------------------
// 윈도우 범위를 벗어난 시퀀스는 수신 불가해야 한다
// ------------------------------------------------------------
TEST_F(RUDPReceiveWindowTest, CanReceive_ReturnsFalse_WhenOutsideWindow)
{
    EXPECT_FALSE(rw.CanReceive(WINDOW_SIZE));
    EXPECT_FALSE(rw.CanReceive(WINDOW_SIZE + 1));
}

// ------------------------------------------------------------
// windowStart보다 이전 시퀀스는 수신 불가해야 한다
// ------------------------------------------------------------
TEST_F(RUDPReceiveWindowTest, CanReceive_ReturnsFalse_WhenBeforeWindowStart)
{
    rw.MarkReceived(0);
    EXPECT_FALSE(rw.CanReceive(0));
}

// ------------------------------------------------------------
// 순서대로 수신 시 windowStart가 순차적으로 이동해야 한다
// ------------------------------------------------------------
TEST_F(RUDPReceiveWindowTest, MarkReceived_InOrder_AdvancesWindowStart)
{
    rw.MarkReceived(0);
    EXPECT_EQ(rw.GetWindowStart(), 1);

    rw.MarkReceived(1);
    EXPECT_EQ(rw.GetWindowStart(), 2);

    rw.MarkReceived(2);
    EXPECT_EQ(rw.GetWindowStart(), 3);
}

// ------------------------------------------------------------
// 순서대로 수신 시 windowEnd도 함께 이동해야 한다
// ------------------------------------------------------------
TEST_F(RUDPReceiveWindowTest, MarkReceived_InOrder_AdvancesWindowEnd)
{
    rw.MarkReceived(0);
    EXPECT_EQ(rw.GetWindowEnd(), WINDOW_SIZE + 1);
}

// ------------------------------------------------------------
// 순서가 바뀐 수신 시 앞선 패킷이 올 때까지 windowStart가 이동하지 않아야 한다
// ------------------------------------------------------------
TEST_F(RUDPReceiveWindowTest, MarkReceived_OutOfOrder_DoesNotAdvanceWindowStart)
{
    rw.MarkReceived(1);
    EXPECT_EQ(rw.GetWindowStart(), 0);
}

// ------------------------------------------------------------
// 순서가 바뀐 수신 후 빠진 패킷이 도착하면 windowStart가 한꺼번에 이동해야 한다
// ------------------------------------------------------------
TEST_F(RUDPReceiveWindowTest, MarkReceived_OutOfOrder_AdvancesWindowStartWhenGapFilled)
{
    rw.MarkReceived(1);
    rw.MarkReceived(2);
    EXPECT_EQ(rw.GetWindowStart(), 0);

    rw.MarkReceived(0);
    EXPECT_EQ(rw.GetWindowStart(), 3);
}

// ------------------------------------------------------------
// 윈도우 범위 밖 시퀀스에 대한 MarkReceived는 무시되어야 한다
// ------------------------------------------------------------
TEST_F(RUDPReceiveWindowTest, MarkReceived_OutOfWindow_IsIgnored)
{
    rw.MarkReceived(WINDOW_SIZE);
    EXPECT_EQ(rw.GetWindowStart(), 0);
}

// ------------------------------------------------------------
// 동일 시퀀스 중복 수신 시 windowStart가 중복 이동하지 않아야 한다
// ------------------------------------------------------------
TEST_F(RUDPReceiveWindowTest, MarkReceived_Duplicate_DoesNotAdvanceWindowStartTwice)
{
    rw.MarkReceived(0);
    EXPECT_EQ(rw.GetWindowStart(), 1);

    rw.MarkReceived(0);
    EXPECT_EQ(rw.GetWindowStart(), 1);
}

// ------------------------------------------------------------
// 윈도우 전체를 순서대로 채우면 windowStart가 windowSize만큼 이동해야 한다
// ------------------------------------------------------------
TEST_F(RUDPReceiveWindowTest, MarkReceived_FullWindow_InOrder_AdvancesCorrectly)
{
    for (BYTE i = 0; i < WINDOW_SIZE; ++i)
    {
        rw.MarkReceived(i);
    }
    EXPECT_EQ(rw.GetWindowStart(), WINDOW_SIZE);
    EXPECT_EQ(rw.GetWindowEnd(), WINDOW_SIZE * 2);
}

// ------------------------------------------------------------
// 윈도우 전체를 역순으로 채우면 마지막 패킷 도착 시 한꺼번에 이동해야 한다
// ------------------------------------------------------------
TEST_F(RUDPReceiveWindowTest, MarkReceived_FullWindow_ReverseOrder_AdvancesCorrectly)
{
    for (int i = WINDOW_SIZE - 1; i > 0; --i)
    {
        rw.MarkReceived(static_cast<PacketSequence>(i));
        EXPECT_EQ(rw.GetWindowStart(), 0);
    }

    rw.MarkReceived(0);
    EXPECT_EQ(rw.GetWindowStart(), WINDOW_SIZE);
}

// ------------------------------------------------------------
// Reset 후 지정한 시퀀스부터 윈도우가 시작되어야 한다
// ------------------------------------------------------------
TEST_F(RUDPReceiveWindowTest, Reset_SetsWindowStartToGivenSequence)
{
    rw.MarkReceived(0);
    rw.MarkReceived(1);

    rw.Reset(100);
    EXPECT_EQ(rw.GetWindowStart(), 100);
    EXPECT_EQ(rw.GetWindowEnd(), 100 + WINDOW_SIZE);
}

// ------------------------------------------------------------
// Reset 후 이전에 수신한 시퀀스는 무효화되어야 한다
// ------------------------------------------------------------
TEST_F(RUDPReceiveWindowTest, Reset_ClearsPreviousReceivedFlags)
{
    rw.MarkReceived(1);
    rw.Reset(0);

    rw.MarkReceived(0);
    EXPECT_EQ(rw.GetWindowStart(), 1);
}

// ------------------------------------------------------------
// 시퀀스 랩어라운드 테스트
// PacketSequence가 uint16_t라면 65535 -> 0으로 순환
// ------------------------------------------------------------
TEST_F(RUDPReceiveWindowTest, Wraparound_CanReceiveAcrossSequenceBoundary)
{
    constexpr PacketSequence nearMax = static_cast<PacketSequence>(0xFFFF) - 2;
    rw.Reset(nearMax);

    EXPECT_TRUE(rw.CanReceive(nearMax));
    EXPECT_TRUE(rw.CanReceive(nearMax + 1));
    EXPECT_TRUE(rw.CanReceive(nearMax + 2));
}

// ------------------------------------------------------------
// 시퀀스 랩어라운드 테스트
// PacketSequence가 uint16_t라면 65535 -> 0으로 순환
// ------------------------------------------------------------
TEST_F(RUDPReceiveWindowTest, Wraparound_MarkReceivedAcrossSequenceBoundary)
{
    constexpr PacketSequence nearMax = static_cast<PacketSequence>(0xFFFF) - 1;
    rw.Reset(nearMax);

    rw.MarkReceived(nearMax);
    rw.MarkReceived(nearMax + 1);
    rw.MarkReceived(nearMax + 2);

    EXPECT_EQ(rw.GetWindowStart(), nearMax + 3);
}
