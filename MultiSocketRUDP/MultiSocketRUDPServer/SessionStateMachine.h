#pragma once
#include <atomic>
#include "../Common/etc/CoreType.h"

class RUDPSession;

class SessionStateMachine
{
public:
	SessionStateMachine() = default;
	~SessionStateMachine() = default;

    SessionStateMachine(const SessionStateMachine&) = delete;
    SessionStateMachine& operator=(const SessionStateMachine&) = delete;
    SessionStateMachine(SessionStateMachine&&) = delete;
    SessionStateMachine& operator=(SessionStateMachine&&) = delete;

public:
    [[nodiscard]]
    SESSION_STATE GetSessionState() const noexcept;
	[[nodiscard]]
    bool IsConnected() const noexcept;
	[[nodiscard]]
    bool IsReserved() const noexcept;
	[[nodiscard]]
    bool IsReleasing() const noexcept;
	[[nodiscard]]
    bool IsUsingSession() const noexcept;

public:
    void SetReserved() noexcept;
    [[nodiscard]]
    bool TryTransitionToConnected() noexcept;
    [[nodiscard]]
    bool TryTransitionToReleasing() noexcept;
    [[nodiscard]]
    bool TryAbortReserved() noexcept;
    void SetDisconnected() noexcept;
    void Reset() noexcept;

private:
    std::atomic<SESSION_STATE> state{ SESSION_STATE::DISCONNECTED };
};