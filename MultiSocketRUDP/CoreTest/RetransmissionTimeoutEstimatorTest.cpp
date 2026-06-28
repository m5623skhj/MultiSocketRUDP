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
