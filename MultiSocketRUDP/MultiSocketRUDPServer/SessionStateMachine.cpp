#include "PreCompile.h"
#include "SessionStateMachine.h"

SESSION_STATE SessionStateMachine::GetSessionState() const noexcept
{
	return state.load(std::memory_order_acquire);
}

bool SessionStateMachine::IsConnected() const noexcept
{
	return state.load(std::memory_order_acquire) == SESSION_STATE::CONNECTED;
}

bool SessionStateMachine::IsReserved() const noexcept
{
	return state.load(std::memory_order_acquire) == SESSION_STATE::RESERVED;
}

bool SessionStateMachine::IsReleasing() const noexcept
{
	return state.load(std::memory_order_acquire) == SESSION_STATE::RELEASING;
}

bool SessionStateMachine::IsUsingSession() const noexcept
{
	const auto s = state.load(std::memory_order_acquire);
	return s == SESSION_STATE::RESERVED || s == SESSION_STATE::CONNECTED;
}

void SessionStateMachine::SetReserved() noexcept
{
	state.store(SESSION_STATE::RESERVED, std::memory_order_release);
}

bool SessionStateMachine::TryTransitionToConnected() noexcept
{
	auto expected = SESSION_STATE::RESERVED;
	return state.compare_exchange_strong(expected
		, SESSION_STATE::CONNECTED
		, std::memory_order_acq_rel
		, std::memory_order_acquire);
}

bool SessionStateMachine::TryTransitionToReleasing() noexcept
{
	if (auto expectReserved = SESSION_STATE::RESERVED; state.compare_exchange_strong(expectReserved
	                                                                                 , SESSION_STATE::RELEASING
	                                                                                 , std::memory_order_acq_rel
	                                                                                 , std::memory_order_acquire))
	{
		return true;
	}

	auto expectConnected = SESSION_STATE::CONNECTED;
	return state.compare_exchange_strong(expectConnected
		, SESSION_STATE::RELEASING
		, std::memory_order_acq_rel
		, std::memory_order_acquire);
}

bool SessionStateMachine::TryAbortReserved() noexcept
{
	auto expected = SESSION_STATE::RESERVED;
	return state.compare_exchange_strong(expected
		, SESSION_STATE::RELEASING
		, std::memory_order_acq_rel 
		, std::memory_order_acquire);
}

void SessionStateMachine::SetDisconnected() noexcept
{
	state.store(SESSION_STATE::DISCONNECTED, std::memory_order_release);
}

void SessionStateMachine::Reset() noexcept
{
	state.store(SESSION_STATE::DISCONNECTED, std::memory_order_release);
}

