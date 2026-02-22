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
    void CloseSocket();

    [[nodiscard]] SOCKET GetSocket() const;
    void SetSocket(SOCKET inSocket);

    [[nodiscard]] PortType GetServerPort() const;
    void SetServerPort(PortType port);

    [[nodiscard]] std::shared_mutex& GetSocketMutex() const;

private:
    SOCKET socket = INVALID_SOCKET;
    mutable std::shared_mutex socketLock;
    PortType serverPort = INVALID_PORT_NUMBER;
};