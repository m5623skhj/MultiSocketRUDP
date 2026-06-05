#pragma once
#include <shared_mutex>
#include <winsock2.h>
#include "../Common/etc/CoreType.h"

class SessionSocketContext
{
public:
    SessionSocketContext() = default;
    ~SessionSocketContext();

    SessionSocketContext(const SessionSocketContext&) = delete;
    SessionSocketContext& operator=(const SessionSocketContext&) = delete;
    SessionSocketContext(SessionSocketContext&&) = delete;
    SessionSocketContext& operator=(SessionSocketContext&&) = delete;

public:
    // 반드시 호출자가 GetSocketMutex()를 unique(exclusive)로 잡은 상태에서 호출해야 합니다.
    void CloseSocket();

    [[nodiscard]] SOCKET GetSocket() const;
    void SetSocket(SOCKET inSocket);

    [[nodiscard]] PortType GetServerPort() const;
    void SetServerPort(PortType port);

    // 소켓/RIO 자원의 수명을 함께 보호하는 세션 단위 락입니다.
    // recv/send 등록(post) : shared(읽기)
    // close + RIO 정리 : unique(쓰기)
    [[nodiscard]] std::shared_mutex& GetSocketMutex() const;

private:
    SOCKET socket = INVALID_SOCKET;
    mutable std::shared_mutex socketLock;
    PortType serverPort = INVALID_PORT_NUMBER;
};
