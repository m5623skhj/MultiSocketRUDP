#include "PreCompile.h"
#include "RUDPClientCore.h"
#include "Logger.h"
#include "LogExtension.h"
#include "../Common/Crypto/CryptoHelper.h"
#include "ClientOptionFile.h"

#if USE_IOCP_SESSION_GETTER
bool RUDPClientCore::SessionGetter::Start(const std::wstring& optionFilePath)
{
	if (CNetClient::Start(optionFilePath.c_str()) == false)
	{
		auto log = Logger::MakeLogObject<ClientLog>();
		log->logString = "SessionGetter::Start failed";
		Logger::GetInstance().WriteLog(log);
		return false;
	}

	return true;
}

void RUDPClientCore::SessionGetter::OnConnectionComplete()
{
}

void RUDPClientCore::SessionGetter::OnRecv(CNetServerSerializationBuf* recvBuffer)
{

}

void RUDPClientCore::SessionGetter::OnSend(int sendSize)
{
}

void RUDPClientCore::SessionGetter::OnWorkerThreadBegin()
{
}

void RUDPClientCore::SessionGetter::OnWorkerThreadEnd()
{
}

void RUDPClientCore::SessionGetter::OnError(st_Error* error)
{
	std::cout << "sessionGetter::OnError : ServerErr " << error->ServerErr << " GetLastError " << error->GetLastErr << std::endl;
}

#else
bool RUDPClientCore::RunGetSessionFromServer(const std::wstring& optionFilePath)
{
	if (not tlsHelper.Initialize())
	{
		LOG_ERROR(std::format(
			"RUDPClientCore::tlsHelper.Initialize() failed with security status {:#010x}",
			static_cast<unsigned long>(tlsHelper.GetLastStatus())));
		return false;
	}

	if (not GetSessionFromServer())
	{
		LOG_ERROR("RUDPClientCore::GetSessionFromServer() failed");
		return false;
	}

	return true;
}

bool RUDPClientCore::ReadSessionGetterOptionFile(const std::wstring& optionFilePath)
{
	ClientOptionFile options;
	if (not options.Load(optionFilePath)) return false;
	const auto ip = options.GetValue(L"SESSION_BROKER", L"IP");
	if (not ip || ip->size() < 3 || ip->front() != L'"' || ip->back() != L'"') return false;
	const auto address = ip->substr(1, ip->size() - 2);
	IN_ADDR parsedAddress{};
	if (address.size() >= std::size(sessionBrokerIP) ||
		InetPtonW(AF_INET, address.c_str(), &parsedAddress) != 1) return false;
	unsigned int brokerPort{}, packetCode{}, packetKey{};
	if (not options.GetNumber(L"SESSION_BROKER", L"PORT", 1, 65535, brokerPort) ||
		not options.GetNumber(L"SERIALIZEBUF", L"PACKET_CODE", 0, 255, packetCode) ||
		not options.GetNumber(L"SERIALIZEBUF", L"PACKET_KEY", 0, 255, packetKey)) return false;
	wcscpy_s(sessionBrokerIP, address.c_str());
	sessionBrokerPort = static_cast<PortType>(brokerPort);
	NetBuffer::m_byHeaderCode = static_cast<BYTE>(packetCode);
	NetBuffer::m_byXORCode = static_cast<BYTE>(packetKey);

	return true;
}

bool RUDPClientCore::GetSessionFromServer()
{
	sessionBrokerSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sessionBrokerSocket == INVALID_SOCKET)
	{
		LOG_ERROR(std::format("socket() failed in GetSessionFromServer() with error code {}", WSAGetLastError()));
		return false;
	}

	if (not TryConnectToSessionBroker())
	{
		LOG_ERROR(std::format("Connection failed in GetSessionFromServer() with error code {}", WSAGetLastError()));
		closesocket(sessionBrokerSocket);
		sessionBrokerSocket = INVALID_SOCKET;
		return false;
	}

	if (not tlsHelper.Handshake(sessionBrokerSocket))
	{
		LOG_ERROR(std::format(
			"TLS Handshake failed in GetSessionFromServer() with security status {:#010x}",
			static_cast<unsigned long>(tlsHelper.GetLastStatus())));
		closesocket(sessionBrokerSocket);
		sessionBrokerSocket = INVALID_SOCKET;
		return false;
	}

	constexpr DWORD VERSION_TIMEOUT_MS = 6000;
	if (setsockopt(sessionBrokerSocket, SOL_SOCKET, SO_RCVTIMEO,
		reinterpret_cast<const char*>(&VERSION_TIMEOUT_MS), sizeof(VERSION_TIMEOUT_MS)) == SOCKET_ERROR ||
		setsockopt(sessionBrokerSocket, SOL_SOCKET, SO_SNDTIMEO,
			reinterpret_cast<const char*>(&VERSION_TIMEOUT_MS), sizeof(VERSION_TIMEOUT_MS)) == SOCKET_ERROR ||
		not tlsHelper.SendProtocolVersion(sessionBrokerSocket, RUDP_PROTOCOL_VERSION))
	{
		closesocket(sessionBrokerSocket);
		sessionBrokerSocket = INVALID_SOCKET;
		return false;
	}
	return TrySetTargetSessionInfo();
}

bool RUDPClientCore::TryConnectToSessionBroker() const
{
	sockaddr_in sessionGetterAddr;
	sessionGetterAddr.sin_family = AF_INET;
	sessionGetterAddr.sin_port = htons(sessionBrokerPort);
	InetPton(AF_INET, sessionBrokerIP, &sessionGetterAddr.sin_addr);

	bool connected{ false };
	for (int i = 0; i < 5; ++i)
	{
		if (connect(sessionBrokerSocket, reinterpret_cast<sockaddr*>(&sessionGetterAddr), sizeof(sessionGetterAddr)) == SOCKET_ERROR)
		{
			Sleep(1000);
			{
				continue;
			}
		}

		connected = true;
		break;
	}

	return connected;
}

bool RUDPClientCore::TrySetTargetSessionInfo()
{
	auto& recvBuffer = *NetBuffer::Alloc();
	recvBuffer.m_iRead = 0;
	// Append plaintext across TLS records, including a split session header.
	recvBuffer.m_iWrite = 0;

	constexpr size_t maxTlsPacketSize = 16 * 1024 + 512;
	std::vector<char> encryptedStream;

	int totalPlainReceived{};
	BYTE code{};
	WORD payloadLength{};

	bool payloadComplete{ false };

	while (true)
	{
		char tlsRecvBuffer[maxTlsPacketSize];
		const int bytesReceived = recv(sessionBrokerSocket, tlsRecvBuffer, sizeof(tlsRecvBuffer), 0);
		if (bytesReceived <= 0)
		{
			const int error = WSAGetLastError();
			if (bytesReceived == 0 || error == WSAETIMEDOUT)
			{
				break;
			}

			LOG_ERROR(std::format("recv() failed in TrySetTargetSessionInfo() with error code {}", error));
			closesocket(sessionBrokerSocket);
			sessionBrokerSocket = INVALID_SOCKET;
			NetBuffer::Free(&recvBuffer);

			return false;
		}

		encryptedStream.insert(encryptedStream.end(), tlsRecvBuffer, tlsRecvBuffer + bytesReceived);
		char plainBuffer[maxTlsPacketSize];
		size_t plainSize = 0;

		const auto decryptResult = tlsHelper.DecryptDataStream(encryptedStream, plainBuffer, plainSize);
		if (decryptResult == TLSHelper::TlsDecryptResult::Error)
		{
			LOG_ERROR("TLS DecryptDataStream failed");
			closesocket(sessionBrokerSocket);
			sessionBrokerSocket = INVALID_SOCKET;
			NetBuffer::Free(&recvBuffer);

			return false;
		}

		if (plainSize > 0)
		{
			recvBuffer.WriteBuffer(plainBuffer, static_cast<int>(plainSize));
			totalPlainReceived += static_cast<int>(plainSize);
		}

		if (totalPlainReceived < df_HEADER_SIZE)
		{
			continue;
		}

		if (payloadLength == 0)
		{
			recvBuffer >> code >> payloadLength;
		}

		if (totalPlainReceived >= payloadLength + df_HEADER_SIZE)
		{
			payloadComplete = true;
		}

		if (payloadComplete && decryptResult == TLSHelper::TlsDecryptResult::CloseNotify)
		{
			break;
		}
	}

	shutdown(sessionBrokerSocket, SD_BOTH);
	closesocket(sessionBrokerSocket);
	sessionBrokerSocket = INVALID_SOCKET;

	if (not payloadComplete)
	{
		NetBuffer::Free(&recvBuffer);
		return false;
	}

	recvBuffer.m_iRead = df_HEADER_SIZE;
	const bool retval = SetTargetSessionInfo(recvBuffer);
	NetBuffer::Free(&recvBuffer);

	return retval;
}

bool RUDPClientCore::SetTargetSessionInfo(OUT NetBuffer& receivedBuffer)
{
	if (receivedBuffer.GetUseSize() < sizeof(uint32_t) + sizeof(char)) return false;
	uint32_t version{};
	receivedBuffer >> version;
	if (version != RUDP_PROTOCOL_VERSION)
	{
		LOG_ERROR("Unsupported RUDP server protocol version");
		return false;
	}
	char connectResultCode;
	receivedBuffer >> connectResultCode;
	if (connectResultCode != 0)
	{
		LOG_ERROR(std::format("SetTargetSessionInfo() failed with connectResultCode {}", connectResultCode));
		return false;
	}

	receivedBuffer >> serverIp >> port >> sessionId >> sessionKey >> sessionSalt;

	if (keyObjectBuffer == nullptr)
	{
		keyObjectBuffer = new unsigned char[CryptoHelper::GetTLSInstance().GetKeyObjectSize()];
	}

	if (sessionKeyHandle != nullptr)
	{
		CryptoHelper::DestroySymmetricKeyHandle(sessionKeyHandle);
		sessionKeyHandle = nullptr;
	}

	sessionKeyHandle = CryptoHelper::GetTLSInstance().GetSymmetricKeyHandle(keyObjectBuffer, sessionKey);

	return sessionKeyHandle != nullptr;
}

#endif
