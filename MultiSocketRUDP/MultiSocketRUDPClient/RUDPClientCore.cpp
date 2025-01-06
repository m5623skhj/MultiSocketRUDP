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

bool RUDPClientCore::IsConnected()
{
	return isConnected;
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

	NetBuffer& connectPacket = *NetBuffer::Alloc();
	PACKET_TYPE packetType = PACKET_TYPE::ConnectType;
	
	connectPacket << packetType << sessionId << sessionKey;
	SendPacket(connectPacket, ++lastSendPacketSequence);

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
	NetBuffer* buffer = nullptr;
	while (true)
	{
		if (buffer != nullptr)
		{
			NetBuffer::Free(buffer);
		}
		buffer = NetBuffer::Alloc();

		int bytesReceived = recvfrom(rudpSocket, buffer->GetBufferPtr(), dfDEFAULTSIZE, 0, (sockaddr*)&serverAddr, &senderLength);
		if (bytesReceived == SOCKET_ERROR)
		{
			const int error = WSAGetLastError();
			if (error == WSAENOTSOCK || error == WSAEINTR)
			{
				std::cout << "Recv thread stopped" << std::endl;
				continue;
			}
			else
			{
				std::cout << "recvfrom() error with " << error << std::endl;
				continue;
			}
		}

		if (not buffer->Decode() || buffer->GetUseSize() != GetPayloadLength(*buffer))
		{
			continue;
		}

		ProcessRecvPacket(*buffer);
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
			DoSend();
		}
		break;
		case WAIT_OBJECT_0 + 1:
		{
			DoSend();
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

void RUDPClientCore::ProcessRecvPacket(OUT NetBuffer& receivedBuffer)
{
	PACKET_TYPE packetType;
	PacketSequence packetSequence;
	receivedBuffer >> packetType >> packetSequence;

	switch (packetType)
	{
	case PACKET_TYPE::SendType:
	{
		NetBuffer::AddRefCount(&receivedBuffer);

		std::scoped_lock lock(recvPacketHoldingQueueLock);
		recvPacketHoldingQueue.emplace(RecvPacketInfo{ &receivedBuffer, packetSequence });
	}
		break;
	case PACKET_TYPE::SendReplyType:
		break;
	default:std::cout << "Invalid packet type " << static_cast<unsigned char>(packetType) << std::endl;
		break;
	}
}

void RUDPClientCore::DoSend()
{
	while (sendBufferQueue.GetRestSize() > 0)
	{
		NetBuffer* packet = nullptr;
		if (not sendBufferQueue.Dequeue(&packet))
		{
			std::cout << "sendBufferQueue.Dequeue() failed" << std::endl;
			continue;
		}

		EncodePacket(*packet);
		if (sendto(rudpSocket, packet->GetBufferPtr(), packet->GetUseSize(), 0, (const sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR)
		{
			std::cout << "sendto() failed with error code " << WSAGetLastError() << std::endl;
			continue;
		}
	}
}

unsigned int RUDPClientCore::GetRemainPacketSize()
{
	std::scoped_lock lock(recvPacketHoldingQueueLock);
	return static_cast<unsigned int>(recvPacketHoldingQueue.size());
}

NetBuffer* RUDPClientCore::GetReceivedPacket()
{
	std::scoped_lock lock(recvPacketHoldingQueueLock);
	auto holdingPacketInfo = recvPacketHoldingQueue.top();

	if (holdingPacketInfo.packetSequence != recvPacketSequence)
	{
		return nullptr;
	}

	recvPacketHoldingQueue.pop();
	return holdingPacketInfo.buffer;
}

void RUDPClientCore::SendPacket(OUT IPacket& packet)
{
	NetBuffer* buffer = NetBuffer::Alloc();
	if (buffer == nullptr)
	{
		std::cout << "Buffer is nullptr in RUDPSession::SendPacket()" << std::endl;
		return;
	}

	PACKET_TYPE packetType = PACKET_TYPE::SendType;
	PacketSequence packetSequence = ++lastSendPacketSequence;
	*buffer << packetType << packetSequence << packet.GetPacketId();
	packet.PacketToBuffer(*buffer);

	return SendPacket(*buffer, packetSequence);
}

void RUDPClientCore::SendPacket(OUT NetBuffer& buffer, const PacketSequence inSendPacketSequence)
{
	auto sendPacketInfo = sendPacketInfoPool->Alloc();
	if (sendPacketInfo == nullptr)
	{
		std::cout << "SendPacketInfo is nullptr in RUDPSession::SendPacket()" << std::endl;
		NetBuffer::Free(&buffer);
		return;
	}

	sendPacketInfo->Initialize(&buffer, inSendPacketSequence);
	{
		std::unique_lock lock(sendPacketInfoMapLock);
		sendPacketInfoMap.insert({ inSendPacketSequence, sendPacketInfo });
	}

	{
		std::scoped_lock lock(sendBufferQueueLock);
		sendBufferQueue.Enqueue(&buffer);
	}

	SetEvent(sendEventHandles[0]);
}

WORD RUDPClientCore::GetPayloadLength(OUT NetBuffer& buffer)
{
	BYTE code;
	WORD payloadLength;
	buffer >> code >> payloadLength;

	if (code != NetBuffer::m_byHeaderCode)
	{
		std::cout << "code : " << code << std::endl;
		return 0;
	}

	return payloadLength;
}

void RUDPClientCore::EncodePacket(OUT NetBuffer& packet)
{
	if (packet.m_bIsEncoded == false)
	{
		packet.m_iWriteLast = packet.m_iWrite;
		packet.m_iWrite = 0;
		packet.m_iRead = 0;
		packet.Encode();
	}
}