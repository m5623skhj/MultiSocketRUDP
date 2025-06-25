#include "PreCompile.h"
#include "RUDPClientCore.h"
#include "Logger.h"
#include "LogExtension.h"

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
	if (not GetSessionFromServer())
	{
		auto log = Logger::MakeLogObject<ClientLog>();
		log->logString = "RunGetSessionFromServer::GetSessionFromServer() failed";
		Logger::GetInstance().WriteLog(log);
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

	int iJumpBOM = ftell(fp);
	fseek(fp, 0, SEEK_END);
	int iFileSize = ftell(fp);
	fseek(fp, iJumpBOM, SEEK_SET);
	int FileSize = (int)fread_s(cBuffer, BUFFER_MAX, sizeof(WCHAR), iFileSize / 2, fp);
	int iAmend = iFileSize - FileSize; // 개행 문자와 파일 사이즈에 대한 보정값
	fclose(fp);

	cBuffer[iFileSize - iAmend] = '\0';
	WCHAR* pBuff = cBuffer;

	if (!parser.GetValue_String(pBuff, L"SESSION_BROKER", L"IP", sessionBrokerIP))
		return false;
	if (!parser.GetValue_Short(pBuff, L"SESSION_BROKER", L"PORT", (short*)&sessionBrokerPort))
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
		auto log = Logger::MakeLogObject<ClientLog>();
		log->logString = std::format("WSAStartup failed GetSessionFromServer() with error code {}", WSAGetLastError());
		Logger::GetInstance().WriteLog(log);
		return false;
	}

	sessionBrokerSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sessionBrokerSocket == INVALID_SOCKET)
	{
		auto log = Logger::MakeLogObject<ClientLog>();
		log->logString = std::format("Socket creation failed in GetSessionFromServer() with error code {}", WSAGetLastError());
		Logger::GetInstance().WriteLog(log);
		return false;
	}

	if (not TryConnectToSessionBroker())
	{
		auto log = Logger::MakeLogObject<ClientLog>();
		log->logString = std::format("Connection failed in GetSessionFromServer() with error code {}", WSAGetLastError());
		Logger::GetInstance().WriteLog(log);
		closesocket(sessionBrokerSocket);
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
		if (connect(sessionBrokerSocket, (sockaddr*)&sessionGetterAddr, sizeof(sessionGetterAddr)) == SOCKET_ERROR)
		{
			Sleep(1000);
			{
				continue;
			}
		}
		else
		{
			connected = true;
			break;
		}
	}

	return connected;
}

bool RUDPClientCore::TrySetTargetSessionInfo()
{
	auto& recvBuffer = *NetBuffer::Alloc();
	recvBuffer.m_iRead = 0;
	int totalReceivedBytes = 0;
	BYTE code{};
	WORD payloadLength{};

	while (true)
	{
		int bytesReceived = recv(sessionBrokerSocket, recvBuffer.GetBufferPtr(), RECV_BUFFER_SIZE + df_HEADER_SIZE, 0);
		if (bytesReceived <= 0)
		{
			int error = WSAGetLastError();

			if (error == 0)
			{
				break;
			}
			else
			{
				auto log = Logger::MakeLogObject<ClientLog>();
				log->logString = std::format("recv() failed in GetSessionFromServer() with error code {}", error);
				Logger::GetInstance().WriteLog(log);
				closesocket(sessionBrokerSocket);
				return false;
			}
		}

		totalReceivedBytes += bytesReceived;
		recvBuffer.MoveWritePos(bytesReceived - df_HEADER_SIZE);
		if (totalReceivedBytes >= df_HEADER_SIZE && payloadLength == 0)
		{
			recvBuffer >> code >> payloadLength;
		}

		if (totalReceivedBytes < df_HEADER_SIZE)
		{
			continue;
		}
		else if (totalReceivedBytes == payloadLength + df_HEADER_SIZE)
		{
			break;
		}
	}
	closesocket(sessionBrokerSocket);

	const bool retval = SetTargetSessionInfo(recvBuffer);
	NetBuffer::Free(&recvBuffer);

	return retval;
}

bool RUDPClientCore::SetTargetSessionInfo(OUT NetBuffer& receivedBuffer)
{
	if (not receivedBuffer.Decode())
	{
		const auto log = Logger::MakeLogObject<ClientLog>();
		log->logString = "SetTargetSessionInfo() failed with Decode()";
		Logger::GetInstance().WriteLog(log);
		return false;
	}

	char connectResultCode;
	receivedBuffer >> connectResultCode;
	if (connectResultCode != 0)
	{
		const auto log = Logger::MakeLogObject<ClientLog>();
		log->logString = std::format("SetTargetSessionInfo() failed with connectResultCode {}", connectResultCode);
		Logger::GetInstance().WriteLog(log);
		return false;
	}

	receivedBuffer >> serverIp >> port >> sessionId >> sessionKey;
	return true;
}

#endif