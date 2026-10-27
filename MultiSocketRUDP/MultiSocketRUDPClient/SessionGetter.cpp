#include "PreCompile.h"
#include "RUDPClientCore.h"
#include "Logger.h"
#include "LogExtension.h"
#include "../Common/Crypto/CryptoHelper.h"
#include "ClientOptionFile.h"
#include <chrono>
#include <algorithm>

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
bool RUDPClientCore::RunGetSessionFromServer(const std::wstring& optionFilePath, const std::stop_token& inStopToken)
{
	tlsHelper.SetServerName(sessionBrokerIP);
	if (not tlsHelper.Initialize())
	{
		LOG_ERROR(std::format(
			"RUDPClientCore::tlsHelper.Initialize() failed with security status {:#010x}",
			static_cast<unsigned long>(tlsHelper.GetLastStatus())));
		return false;
	}

	if (not GetSessionFromServer(inStopToken))
	{
		LOG_ERROR("RUDPClientCore::GetSessionFromServer() failed");
		return false;
	}

	return true;
}

bool RUDPClientCore::ReadSessionGetterOptionFile(const std::wstring& optionFilePath)
{
	hostLoopback = false;
	ClientOptionFile options;
	if (not options.Load(optionFilePath)) return false;
	const auto ip = options.GetValue(L"SESSION_BROKER", L"IP");
	if (not ip || ip->size() < 3 || ip->front() != L'"' || ip->back() != L'"') return false;
	const auto address = ip->substr(1, ip->size() - 2);
	if (address.empty() || address.size() >= std::size(sessionBrokerIP) ||
		!std::all_of(address.begin(), address.end(), [](wchar_t c) {
			return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') ||
				(c >= L'0' && c <= L'9') || c == L'.' || c == L'-'; })) return false;
	unsigned int brokerPort{}, packetCode{}, packetKey{};
	if (not options.GetNumber(L"SESSION_BROKER", L"PORT", 1, 65535, brokerPort) ||
		not options.GetNumber(L"SERIALIZEBUF", L"PACKET_CODE", 0, 255, packetCode) ||
		not options.GetNumber(L"SERIALIZEBUF", L"PACKET_KEY", 0, 255, packetKey)) return false;
	unsigned int loopback{};
	if (options.GetValue(L"SESSION_BROKER", L"HOST_LOOPBACK") &&
		not options.GetNumber(L"SESSION_BROKER", L"HOST_LOOPBACK", 0, 1, loopback)) return false;
	hostLoopback = loopback != 0;
	wcscpy_s(sessionBrokerIP, address.c_str());
	sessionBrokerPort = static_cast<PortType>(brokerPort);
	NetBuffer::m_byHeaderCode = static_cast<BYTE>(packetCode);
	NetBuffer::m_byXORCode = static_cast<BYTE>(packetKey);

	return true;
}

bool RUDPClientCore::GetSessionFromServer(const std::stop_token& inStopToken)
{
	if (inStopToken.stop_requested()) return false;
	sessionBrokerSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sessionBrokerSocket == INVALID_SOCKET)
	{
		LOG_ERROR(std::format("socket() failed in GetSessionFromServer() with error code {}", WSAGetLastError()));
		return false;
	}

	if (not TryConnectToSessionBroker(inStopToken))
	{
		LOG_ERROR(std::format("Connection failed in GetSessionFromServer() with error code {}", WSAGetLastError()));
		closesocket(sessionBrokerSocket);
		sessionBrokerSocket = INVALID_SOCKET;
		return false;
	}

	// A host-only override must never redirect a remotely reached broker's UDP endpoint.
	// Check the connected peer before closing this socket; TLS name/chain checks still follow.
	if (hostLoopback)
	{
		sockaddr_in brokerPeer{};
		int peerSize = sizeof(brokerPeer);
		if (getpeername(sessionBrokerSocket, reinterpret_cast<sockaddr*>(&brokerPeer), &peerSize) == SOCKET_ERROR ||
			brokerPeer.sin_family != AF_INET || (ntohl(brokerPeer.sin_addr.s_addr) >> 24) != 127)
		{
			LOG_ERROR("HOST_LOOPBACK requires an IPv4 loopback session broker connection");
			closesocket(sessionBrokerSocket);
			sessionBrokerSocket = INVALID_SOCKET;
			return false;
		}
	}

	// Bound sends as well as reads before the TLS handshake starts.
	constexpr DWORD BROKER_IO_TIMEOUT_MS = 6000;
	if (setsockopt(sessionBrokerSocket, SOL_SOCKET, SO_RCVTIMEO,
		reinterpret_cast<const char*>(&BROKER_IO_TIMEOUT_MS), sizeof(BROKER_IO_TIMEOUT_MS)) == SOCKET_ERROR ||
		setsockopt(sessionBrokerSocket, SOL_SOCKET, SO_SNDTIMEO,
		reinterpret_cast<const char*>(&BROKER_IO_TIMEOUT_MS), sizeof(BROKER_IO_TIMEOUT_MS)) == SOCKET_ERROR)
	{
		closesocket(sessionBrokerSocket);
		sessionBrokerSocket = INVALID_SOCKET;
		return false;
	}

	if (not tlsHelper.Handshake(sessionBrokerSocket, inStopToken))
	{
		LOG_ERROR(std::format(
			"TLS Handshake failed in GetSessionFromServer() with security status {:#010x}",
			static_cast<unsigned long>(tlsHelper.GetLastStatus())));
		closesocket(sessionBrokerSocket);
		sessionBrokerSocket = INVALID_SOCKET;
		return false;
	}

	if (inStopToken.stop_requested() || not tlsHelper.SendProtocolVersion(sessionBrokerSocket, RUDP_PROTOCOL_VERSION))
	{
		closesocket(sessionBrokerSocket);
		sessionBrokerSocket = INVALID_SOCKET;
		return false;
	}
	return TrySetTargetSessionInfo(inStopToken);
}

// Poll a nonblocking connect so startup cancellation never waits for the OS TCP timeout.
bool RUDPClientCore::TryConnectToSessionBroker(const std::stop_token& inStopToken) const
{
	sockaddr_in sessionGetterAddr{};
	sessionGetterAddr.sin_family = AF_INET;
	sessionGetterAddr.sin_port = htons(sessionBrokerPort);
	if (InetPtonW(AF_INET, sessionBrokerIP, &sessionGetterAddr.sin_addr) != 1)
	{
		// Async DNS shares startup cancellation; its result storage remains alive
		// until cancellation has completed. The wire's game endpoint stays IPv4.
		ADDRINFOEXW hints{};
		hints.ai_family = AF_INET;
		hints.ai_socktype = SOCK_STREAM;
		PADDRINFOEXW result{};
		OVERLAPPED pending{};
		pending.hEvent = WSACreateEvent();
		if (pending.hEvent == WSA_INVALID_EVENT) return false;
		HANDLE lookup{};
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(6);
		int status = GetAddrInfoExW(sessionBrokerIP, nullptr, NS_DNS, nullptr, &hints,
			&result, nullptr, &pending, nullptr, &lookup);
		if (status == WSA_IO_PENDING)
		{
			DWORD waited = WAIT_TIMEOUT;
			while (!inStopToken.stop_requested() && std::chrono::steady_clock::now() < deadline)
			{
				waited = WaitForSingleObject(pending.hEvent, 100);
				if (waited != WAIT_TIMEOUT) break;
			}
			if (waited != WAIT_OBJECT_0)
			{
				GetAddrInfoExCancel(&lookup);
				// The OS must stop referencing pending/result before these locals die.
				WaitForSingleObject(pending.hEvent, INFINITE);
			}
			status = GetAddrInfoExOverlappedResult(&pending);
		}
		const bool resolved = status == 0 && result && result->ai_addr &&
			result->ai_addrlen >= sizeof(sockaddr_in) && !inStopToken.stop_requested() &&
			std::chrono::steady_clock::now() < deadline;
		if (resolved) sessionGetterAddr.sin_addr = reinterpret_cast<sockaddr_in*>(result->ai_addr)->sin_addr;
		if (result) FreeAddrInfoExW(result);
		WSACloseEvent(pending.hEvent);
		if (!resolved) return false;
	}

	u_long nonblocking = 1;
	if (ioctlsocket(sessionBrokerSocket, FIONBIO, &nonblocking) == SOCKET_ERROR) return false;
	bool connected = false;
	if (!inStopToken.stop_requested())
	{
		const int result = connect(sessionBrokerSocket, reinterpret_cast<sockaddr*>(&sessionGetterAddr), sizeof(sessionGetterAddr));
		connected = result == 0;
		const int connectError = connected ? 0 : WSAGetLastError();
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(6);
		while (!connected && (connectError == WSAEWOULDBLOCK || connectError == WSAEINPROGRESS)
			&& !inStopToken.stop_requested() && std::chrono::steady_clock::now() < deadline)
		{
			fd_set writable{}, errors{};
			FD_SET(sessionBrokerSocket, &writable);
			FD_SET(sessionBrokerSocket, &errors);
			timeval timeout{ 0, 100000 };
			const int ready = select(0, nullptr, &writable, &errors, &timeout);
			if (ready == SOCKET_ERROR) break;
			if (ready == 0) continue;
			int socketError{};
			int errorSize = sizeof(socketError);
			connected = getsockopt(sessionBrokerSocket, SOL_SOCKET, SO_ERROR,
				reinterpret_cast<char*>(&socketError), &errorSize) == 0 && socketError == 0
				&& FD_ISSET(sessionBrokerSocket, &writable);
			break;
		}
	}
	nonblocking = 0;
	return ioctlsocket(sessionBrokerSocket, FIONBIO, &nonblocking) == 0
		&& connected && !inStopToken.stop_requested();
}

bool RUDPClientCore::TrySetTargetSessionInfo(const std::stop_token& inStopToken)
{
	auto& recvBuffer = *NetBuffer::Alloc();
	recvBuffer.m_iRead = 0;
	// Append plaintext across TLS records, including a split session header.
	recvBuffer.m_iWrite = 0;

	constexpr size_t maxTlsPacketSize = 16 * 1024 + 512;
	auto encryptedStream = tlsHelper.TakePendingHandshakeData();
	bool processBufferedData = not encryptedStream.empty();

	int totalPlainReceived{};
	BYTE code{};
	WORD payloadLength{};

	bool payloadComplete{ false };
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(6);

	while (!inStopToken.stop_requested() && std::chrono::steady_clock::now() < deadline)
	{
		if (not processBufferedData)
		{
			fd_set readable{};
			FD_SET(sessionBrokerSocket, &readable);
			timeval timeout{ 0, 100000 };
			const int ready = select(0, &readable, nullptr, nullptr, &timeout);
			if (ready == SOCKET_ERROR) break;
			if (ready == 0) continue;
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
		}
		processBufferedData = false;
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

	if (inStopToken.stop_requested() || not payloadComplete)
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
	// Port, session credentials and the wire layout remain the broker's authenticated values.
	if (hostLoopback) serverIp = "127.0.0.1";

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
