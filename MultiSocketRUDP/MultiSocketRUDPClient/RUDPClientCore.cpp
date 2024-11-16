#include "PreCompile.h"
#include "RUDPClientCore.h"

RUDPClientCore& RUDPClientCore::GetInst()
{
	static RUDPClientCore instance;
	return instance;
}

bool RUDPClientCore::Start(const std::wstring& optionFilePath)
{
#if USE_IOCP_SESSION_BROKER
	if (not sessionGetter.Start(optionFilePath))
	{
		return false;
	}
#else
	if (not RunGetSessionFromServer(optionFilePath))
	{
		return false;
	}
#endif

	ConnectToServer();
	RunThreads();

	return true;
}

void RUDPClientCore::Stop()
{
	closesocket(sessionBrokerSocket);
	closesocket(rudpSocket);

	SetEvent(sendEventHandles[1]);

	isStopped = true;
}

bool RUDPClientCore::IsStopped()
{
	return isStopped;
}

bool RUDPClientCore::ConnectToServer()
{
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(port);
	InetPtonA(AF_INET, serverIp.c_str(), &serverAddr.sin_addr);

	if (bind(rudpSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR)
	{
		std::cout << "bind() failed with error " << WSAGetLastError();
		return false;
	}

	return true;
}

void RUDPClientCore::RunThreads()
{
	sendEventHandles[0] = CreateEvent(NULL, FALSE, FALSE, NULL);
	sendEventHandles[1] = CreateEvent(NULL, TRUE, FALSE, NULL);

	recvThread = std::thread([this]() { this->RunRecvThread(); });
	sendThread = std::thread([this]() { this->RunSendThread(); });
}

void RUDPClientCore::RunRecvThread()
{
	int senderLength = sizeof(serverAddr);
	while (true)
	{
		NetBuffer* buffer = NetBuffer::Alloc();

		int bytesReceived = recvfrom(rudpSocket, buffer->GetBufferPtr(), dfDEFAULTSIZE, 0, (sockaddr*)&serverAddr, &senderLength);
		if (bytesReceived == SOCKET_ERROR)
		{
			const int error = WSAGetLastError();
			if (error == WSAENOTSOCK || error == WSAEINTR)
			{
				std::cout << "Recv thread stopped" << std::endl;
				break;
			}
			else
			{
				std::cout << "recvfrom() error with " << error << std::endl;
				break;
			}
		}

		// Check is valid packet

		recvBufferQueue.Enqueue(buffer);
	}
}

void RUDPClientCore::RunSendThread()
{
	while (true)
	{
		const auto waitResult = WaitForMultipleObjects(static_cast<DWORD>(sendEventHandles.size()), sendEventHandles.data(), FALSE, INFINITE);
		switch (waitResult)
		{
		case WAIT_OBJECT_0:
		{
			// do send
		}
		break;
		case WAIT_OBJECT_0 + 1:
		{
			std::cout << "Send thread stopped" << std::endl;
			break;
		}
		break;
		default:
		{
			std::cout << "Invalid send thread wait result. Error is " << WSAGetLastError() << std::endl;
			g_Dump.Crash();
		}
		break;
		}
	}
}

unsigned int RUDPClientCore::GetRemainPacketSize()
{
	return recvBufferQueue.GetRestSize();
}

NetBuffer* RUDPClientCore::GetReceivedPacket()
{
	NetBuffer* buffer = nullptr;
	recvBufferQueue.Dequeue(&buffer);

	return buffer;
}