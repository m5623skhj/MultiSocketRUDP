#pragma once
#include <atomic>
#include "../Common/etc/CoreType.h"

class RUDPSession;

// ----------------------------------------
// @brief 세션의 상태 전이를 관리하는 상태 머신 클래스입니다.
// @details std::atomic을 사용하여 스레드 안전하게 세션 상태를 관리하고, 명시적인 상태 전이 함수들을 제공합니다.
// ----------------------------------------
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
    // ----------------------------------------
    // @brief 현재 세션의 상태를 반환합니다.
    // @return 현재 세션의 SESSION_STATE 값
    // ----------------------------------------
    [[nodiscard]]
    SESSION_STATE GetSessionState() const noexcept;
    // ----------------------------------------
    // @brief 세션이 현재 연결 상태인지 확인합니다.
    // @return 연결 상태이면 true, 아니면 false
    // ----------------------------------------
	[[nodiscard]]
    bool IsConnected() const noexcept;
    // ----------------------------------------
    // @brief 세션이 현재 예약 상태인지 확인합니다.
    // @return 예약 상태이면 true, 아니면 false
    // ----------------------------------------
	[[nodiscard]]
    bool IsReserved() const noexcept;
    // ----------------------------------------
    // @brief 세션이 현재 해제 중 상태인지 확인합니다.
    // @return 해제 중 상태이면 true, 아니면 false
    // ----------------------------------------
	[[nodiscard]]
    bool IsReleasing() const noexcept;
    // ----------------------------------------
    // @brief 세션이 현재 사용 중 (예약 또는 연결) 상태인지 확인합니다.
    // @return 사용 중이면 true, 아니면 false
    // ----------------------------------------
	[[nodiscard]]
    bool IsUsingSession() const noexcept;

public:
	// ----------------------------------------
	// @brief 세션을 예약 상태로 설정합니다.
	// // ----------------------------------------
    void SetReserved() noexcept;
    [[nodiscard]]
    bool TryTransitionToConnected() noexcept;
	// ----------------------------------------
	// @brief 예약된 세션을 연결 상태로 전환하려 시도합니다.
	// @param recvPacket 수신된 연결 요청 패킷
	// @param inClientAddr 연결을 시도하는 클라이언트 주소
	// @return 연결에 성공하면 true, 실패하면 false
	// ----------------------------------------
    [[nodiscard]]
    bool TryTransitionToReleasing() noexcept;
    [[nodiscard]]
    bool TryAbortReserved() noexcept;
    void SetDisconnected() noexcept;
	// ----------------------------------------
	// @brief 세션 내부 상태 머신을 초기화하여 DISCONNECTED 상태로 설정합니다.
	// ----------------------------------------
    void Reset() noexcept;

private:
    std::atomic<SESSION_STATE> state{ SESSION_STATE::DISCONNECTED };
};