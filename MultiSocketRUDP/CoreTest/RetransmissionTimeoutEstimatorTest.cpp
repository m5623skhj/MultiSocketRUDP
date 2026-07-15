#include "PreCompile.h"
#include <gtest/gtest.h>
#include "RetransmissionTimeoutEstimator.h"

// ============================================================
// RTO calculation
//
// The estimator uses RFC 6298 style SRTT/RTTVAR calculation with integer
// millisecond weights: alpha=1/8, beta=1/4.
// ============================================================

TEST(RetransmissionTimeoutEstimatorTest, Configure_UsesInitialRtoBeforeRttSample)
{
	RetransmissionTimeoutEstimator estimator;
	estimator.Configure(50, 50, 1000);

	EXPECT_EQ(estimator.GetRtoMs(), 50);
}

TEST(RetransmissionTimeoutEstimatorTest, FirstRttSample_InitializesRtoFromSrttAndRttVariation)
{
	RetransmissionTimeoutEstimator estimator;
	estimator.Configure(50, 50, 1000);

	estimator.OnRttSample(std::chrono::milliseconds(80));

	EXPECT_EQ(estimator.GetRtoMs(), 240);
}

TEST(RetransmissionTimeoutEstimatorTest, NextRttSample_UsesIntegerWeightedAverage)
{
	RetransmissionTimeoutEstimator estimator;
	estimator.Configure(50, 50, 1000);

	estimator.OnRttSample(std::chrono::milliseconds(80));
	estimator.OnRttSample(std::chrono::milliseconds(100));

	EXPECT_EQ(estimator.GetRtoMs(), 222);
}

// ============================================================
// Timeout backoff
//
// Dead-heap timers are packet based, so several packets can expire in one
// loss window. Backoff must be applied once per current RTO window.
// ============================================================

TEST(RetransmissionTimeoutEstimatorTest, Timeout_BackoffIsAppliedOncePerCurrentWindow)
{
	RetransmissionTimeoutEstimator estimator;
	estimator.Configure(50, 50, 1000);
	const auto now = std::chrono::steady_clock::now();

	EXPECT_TRUE(estimator.OnTimeout(now));
	EXPECT_EQ(estimator.GetRtoMs(), 100);
	EXPECT_FALSE(estimator.OnTimeout(now + std::chrono::milliseconds(99)));
	EXPECT_EQ(estimator.GetRtoMs(), 100);
	EXPECT_TRUE(estimator.OnTimeout(now + std::chrono::milliseconds(100)));
	EXPECT_EQ(estimator.GetRtoMs(), 200);
}

TEST(RetransmissionTimeoutEstimatorTest, RttSample_ResetsTimeoutBackoffSuppressionWindow)
{
	RetransmissionTimeoutEstimator estimator;
	estimator.Configure(50, 50, 1000);
	const auto now = std::chrono::steady_clock::now();

	ASSERT_TRUE(estimator.OnTimeout(now));
	ASSERT_FALSE(estimator.OnTimeout(now + std::chrono::milliseconds(40)));

	estimator.OnRttSample(std::chrono::milliseconds(80));

	EXPECT_TRUE(estimator.OnTimeout(now + std::chrono::milliseconds(41)));
}

TEST(RetransmissionTimeoutEstimatorTest, Rto_IsClampedToMinAndMax)
{
	RetransmissionTimeoutEstimator estimator;
	estimator.Configure(10, 50, 300);
	EXPECT_EQ(estimator.GetRtoMs(), 50);

	estimator.OnRttSample(std::chrono::milliseconds(500));
	EXPECT_EQ(estimator.GetRtoMs(), 300);
}

// ------------------------------------------------------------
// 0 이하의 RTT 표본이 입력되면 계산에 1밀리초 하한을 적용하는지 확인합니다.
// ------------------------------------------------------------
TEST(RetransmissionTimeoutEstimatorTest, NonPositiveRttSampleUsesOneMillisecondFloor)
{
	RetransmissionTimeoutEstimator estimator;
	estimator.Configure(10, 1, 100);

	estimator.OnRttSample(std::chrono::milliseconds(0));
	EXPECT_EQ(estimator.GetRtoMs(), 2);

	estimator.Configure(10, 1, 100);
	estimator.OnRttSample(std::chrono::milliseconds(-5));
	EXPECT_EQ(estimator.GetRtoMs(), 2);
}

// ------------------------------------------------------------
// 0 또는 역전된 RTO 범위 설정을 유효한 최소·최대 범위로 정규화하는지 확인합니다.
// ------------------------------------------------------------
TEST(RetransmissionTimeoutEstimatorTest, ConfigureNormalizesZeroAndInvertedBounds)
{
	RetransmissionTimeoutEstimator estimator;
	estimator.Configure(0, 0, 0);
	EXPECT_EQ(estimator.GetRtoMs(), 1);

	estimator.Configure(50, 100, 10);
	EXPECT_EQ(estimator.GetRtoMs(), 100);
	estimator.OnRttSample(std::chrono::milliseconds(500));
	EXPECT_EQ(estimator.GetRtoMs(), 100);
}

// ------------------------------------------------------------
// 연속 timeout의 지수 backoff가 설정된 최대 RTO를 초과하지 않는지 확인합니다.
// ------------------------------------------------------------
TEST(RetransmissionTimeoutEstimatorTest, TimeoutBackoffSaturatesAtMaximum)
{
	RetransmissionTimeoutEstimator estimator;
	estimator.Configure(80, 10, 100);
	const auto now = std::chrono::steady_clock::time_point(std::chrono::seconds(1));

	EXPECT_TRUE(estimator.OnTimeout(now));
	EXPECT_EQ(estimator.GetRtoMs(), 100);
	EXPECT_TRUE(estimator.OnTimeout(now + std::chrono::milliseconds(100)));
	EXPECT_EQ(estimator.GetRtoMs(), 100);
}

// ------------------------------------------------------------
// RTO 재설정이 이전 timeout 억제 구간을 초기화하여 새 timeout을 허용하는지 확인합니다.
// ------------------------------------------------------------
TEST(RetransmissionTimeoutEstimatorTest, ConfigureResetsBackoffSuppressionWindow)
{
	RetransmissionTimeoutEstimator estimator;
	estimator.Configure(50, 50, 1000);
	const auto now = std::chrono::steady_clock::time_point(std::chrono::seconds(1));
	ASSERT_TRUE(estimator.OnTimeout(now));
	ASSERT_FALSE(estimator.OnTimeout(now + std::chrono::milliseconds(1)));

	estimator.Configure(50, 50, 1000);
	EXPECT_TRUE(estimator.OnTimeout(now + std::chrono::milliseconds(1)));
}
