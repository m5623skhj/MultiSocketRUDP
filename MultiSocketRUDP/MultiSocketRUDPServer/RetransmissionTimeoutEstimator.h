#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>

// ----------------------------------------
// @brief Estimates retransmission timeout from RTT samples.
// @details Uses the RFC 6298 SRTT/RTTVAR formula with integer millisecond units,
//          and applies exponential backoff once per timeout window.
// ----------------------------------------
class RetransmissionTimeoutEstimator
{
public:
	// ----------------------------------------
	// @brief Initializes RTO bounds and resets measured RTT state.
	// @param inInitialRtoMs RTO used before a valid RTT sample exists.
	// @param inMinRtoMs Lower bound for calculated RTO.
	// @param inMaxRtoMs Upper bound for calculated RTO and timeout backoff.
	// ----------------------------------------
	void Configure(unsigned int inInitialRtoMs, unsigned int inMinRtoMs, unsigned int inMaxRtoMs);

	// ----------------------------------------
	// @brief Returns the currently cached RTO in milliseconds.
	// ----------------------------------------
	[[nodiscard]]
	unsigned int GetRtoMs() const noexcept;

	// ----------------------------------------
	// @brief Updates SRTT, RTTVAR and RTO from a valid RTT sample.
	// @param sample RTT measured from a packet that has never been retransmitted.
	// ----------------------------------------
	void OnRttSample(std::chrono::steady_clock::duration sample);

	// ----------------------------------------
	// @brief Applies RTO backoff once per timeout window.
	// @param now Current steady clock time.
	// @return true when this call applied a new backoff, false when suppressed.
	// ----------------------------------------
	[[nodiscard]]
	bool OnTimeout(std::chrono::steady_clock::time_point now);

private:
	[[nodiscard]]
	unsigned int ClampRto(uint64_t rtoMs) const noexcept;

private:
	static constexpr uint64_t CLOCK_GRANULARITY_MS = 1;

	mutable std::mutex lock;
	bool hasRttSample{};
	uint64_t smoothedRttMs{};
	uint64_t rttVariationMs{};
	unsigned int minRtoMs{ 1 };
	unsigned int maxRtoMs{ 1 };
	std::atomic_uint cachedRtoMs{ 1 };
	std::chrono::steady_clock::time_point backoffSuppressedUntil{};
};
