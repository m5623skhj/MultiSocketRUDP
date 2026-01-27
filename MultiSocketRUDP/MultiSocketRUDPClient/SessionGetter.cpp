#include "PreCompile.h"
#include "RUDPClientCore.h"
#include "Logger.h"
#include "LogExtension.h"
#include "../Common/Crypto/CryptoHelper.h"

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
		LOG_ERROR("RUDPClientCore::tlsHelper.Initialize() failed");
		WSACleanup();
		return false;
	}

	if (not GetSessionFromServer())
	{
		LOG_ERROR("RUDPClientCore::GetSessionFromServer() failed");
		WSACleanup();
		return false;
	}

	return true;
}

bool RUDPClientCore::ReadSessionGetterOptionFile(const std::wstring& optionFilePath)
{
	_wsetlocale(LC_ALL, L"Korean");

	CParser parser;
	WCHAR cBuffer[BUFFER_MAX];

	FILE* fp;
	_wfopen_s(&fp, optionFilePath.c_str(), L"rt, ccs=UNICODE");

	const int iJumpBOM = ftell(fp);
	fseek(fp, 0, SEEK_END);
	const int iFileSize = ftell(fp);
	fseek(fp, iJumpBOM, SEEK_SET);
	const int fileSize = static_cast<int>(fread_s(cBuffer, BUFFER_MAX, sizeof(WCHAR), iFileSize / 2, fp));
	const int iAmend = iFileSize - fileSize; // 개행 문자와 파일 사이즈에 대한 보정값
	fclose(fp);

	cBuffer[iFileSize - iAmend] = '\0';
	WCHAR* pBuff = cBuffer;

	if (!parser.GetValue_String(pBuff, L"SESSION_BROKER", L"IP", sessionBrokerIP))
		return false;
	if (!parser.GetValue_Short(pBuff, L"SESSION_BROKER", L"PORT", reinterpret_cast<short*>(&sessionBrokerPort)))
		return false;
	if (!parser.GetValue_Byte(pBuff, L"SERIALIZEBUF", L"PACKET_CODE", &NetBuffer::m_byHeaderCode))
		return false;
	if (!parser.GetValue_Byte(pBuff, L"SERIALIZEBUF", L"PACKET_KEY", &NetBuffer::m_byXORCode))
		return false;

	return true;
}

bool RUDPClientCore::GetSessionFromServer()
{
	WSADATA wsaData;
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
	{
		LOG_ERROR(std::format("WSAStartup failed GetSessionFromServer() with error code {}", WSAGetLastError()));
		WSACleanup();
		return false;
	}

	sessionBrokerSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sessionBrokerSocket == INVALID_SOCKET)
	{
		LOG_ERROR(std::format("socket() failed in GetSessionFromServer() with error code {}", WSAGetLastError()));
		WSACleanup();
		return false;
	}

	if (not TryConnectToSessionBroker())
	{
		LOG_ERROR(std::format("Connection failed in GetSessionFromServer() with error code {}", WSAGetLastError()));
		closesocket(sessionBrokerSocket);
		WSACleanup();
		return false;
	}

	if (not tlsHelper.Handshake(sessionBrokerSocket))
	{
		LOG_ERROR("TLS Handshake failed in GetSessionFromServer()");
		closesocket(sessionBrokerSocket);
		WSACleanup();
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
			NetBuffer::Free(&recvBuffer);

			return false;
		}

		if (plainSize > 0)
		{
			recvBuffer.m_iWrite = 0;
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
		keyObjectBuffer = new unsigned char[CryptoHelper::GetTLSInstance().GetKeyOjbectSize()];
	}
	sessionKeyHandle = CryptoHelper::GetTLSInstance().GetSymmetricKeyHandle(keyObjectBuffer, sessionKey);

	return sessionKeyHandle != nullptr;
}

#endif