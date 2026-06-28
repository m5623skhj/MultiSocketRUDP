#include "PreCompile.h"
#include "RetransmissionTimeoutEstimator.h"
#include <algorithm>

void RetransmissionTimeoutEstimator::Configure(
	const unsigned int inInitialRtoMs,
	const unsigned int inMinRtoMs,
	const unsigned int inMaxRtoMs)
{
	std::scoped_lock scopedLock(lock);

	minRtoMs = (std::max)(1u, inMinRtoMs);
	maxRtoMs = (std::max)(minRtoMs, inMaxRtoMs);
	hasRttSample = false;
	smoothedRttMs = {};
	rttVariationMs = {};
	backoffSuppressedUntil = {};

	cachedRtoMs.store(ClampRto(inInitialRtoMs), std::memory_order_relaxed);
}

unsigned int RetransmissionTimeoutEstimator::GetRtoMs() const noexcept
{
	return cachedRtoMs.load(std::memory_order_relaxed);
}

void RetransmissionTimeoutEstimator::OnRttSample(const std::chrono::steady_clock::duration sample)
{
	const auto sampleMsCount = std::chrono::duration_cast<std::chrono::milliseconds>(sample).count();
	const uint64_t sampleMs = static_cast<uint64_t>(sampleMsCount > 1 ? sampleMsCount : 1);

	std::scoped_lock scopedLock(lock);
	if (hasRttSample == false)
	{
		smoothedRttMs = sampleMs;
		rttVariationMs = sampleMs / 2;
		hasRttSample = true;
	}
	else
	{
		const uint64_t deviation = smoothedRttMs > sampleMs
			? smoothedRttMs - sampleMs
			: sampleMs - smoothedRttMs;

		// Keep the RFC 6298 beta=1/4 and alpha=1/8 weights without floating point work.
		rttVariationMs = ((3 * rttVariationMs) + deviation) / 4;
		smoothedRttMs = ((7 * smoothedRttMs) + sampleMs) / 8;
	}

	const uint64_t rtoMs = smoothedRttMs + (std::max)(CLOCK_GRANULARITY_MS, 4 * rttVariationMs);
	cachedRtoMs.store(ClampRto(rtoMs), std::memory_order_relaxed);
	backoffSuppressedUntil = {};
}

bool RetransmissionTimeoutEstimator::OnTimeout(const std::chrono::steady_clock::time_point now)
{
	std::scoped_lock scopedLock(lock);
	if (backoffSuppressedUntil.time_since_epoch().count() != 0 && now < backoffSuppressedUntil)
	{
		return false;
	}

	const unsigned int currentRtoMs = cachedRtoMs.load(std::memory_order_relaxed);
	const unsigned int nextRtoMs = ClampRto(static_cast<uint64_t>(currentRtoMs) * 2);
	cachedRtoMs.store(nextRtoMs, std::memory_order_relaxed);

	backoffSuppressedUntil = now + std::chrono::milliseconds(nextRtoMs);
	return true;
}

unsigned int RetransmissionTimeoutEstimator::ClampRto(const uint64_t rtoMs) const noexcept
{
	const uint64_t clamped = std::clamp(
		rtoMs,
		static_cast<uint64_t>(minRtoMs),
		static_cast<uint64_t>(maxRtoMs));

	return static_cast<unsigned int>(clamped);
}
