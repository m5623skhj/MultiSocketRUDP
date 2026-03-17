#include "PreCompile.h"
#include "SessionSocketContext.h"

SessionSocketContext::~SessionSocketContext()
{
    CloseSocket();
}

void SessionSocketContext::CloseSocket()
{
    std::unique_lock lock(socketLock);
    if (socket == INVALID_SOCKET)
    {
        return;
    }

    closesocket(socket);
    socket = INVALID_SOCKET;
}

SOCKET SessionSocketContext::GetSocket() const
{
	return socket;
}

void SessionSocketContext::SetSocket(const SOCKET inSocket)
{
	socket = inSocket;
}

PortType SessionSocketContext::GetServerPort() const
{
	return serverPort;
}

void SessionSocketContext::SetServerPort(const PortType port)
{
	serverPort = port;
}

std::shared_mutex& SessionSocketContext::GetSocketMutex() const
{
	return socketLock;
}
