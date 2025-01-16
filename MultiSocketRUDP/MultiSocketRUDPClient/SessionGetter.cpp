#include "PreCompile.h"
#include "RUDPClientCore.h"
#include "Logger.h"
#include "LogExtention.h"

#if USE_IOCP_SESSION_BROKER
bool RUDPClientCore::SessionGetter::Start(const std::wstring& optionFilePath)
{
	if (CNetClient::Start(optionFilePath.c_str()) == false)
	{
		auto log = std::make_shared<ClientLog>();
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
	if (not ReadSessionGetterOptionFile(optionFilePath))
	{
		auto log = std::make_shared<ClientLog>();
		log->logString = "RunGetSessionFromServer::ReadOptionFile() failed";
		Logger::GetInstance().WriteLog(log);
		return false;
	}

	if (not GetSessionFromServer())
	{
		auto log = std::make_shared<ClientLog>();
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
	BYTE headerCode, xOrCode;

	if (!parser.GetValue_String(pBuff, L"SESSION_BROKER", L"IP", sessionBrokerIP))
		return false;
	if (!parser.GetValue_Short(pBuff, L"SESSION_BROKER", L"PORT", (short*)&sessionBrokerPort))
		return false;
	if (!parser.GetValue_Byte(pBuff, L"SERIALIZEBUF", L"PACKET_CODE", &headerCode))
		return false;
	if (!parser.GetValue_Byte(pBuff, L"SERIALIZEBUF", L"PACKET_KEY", &xOrCode))
		return false;

	CNetServerSerializationBuf::m_byHeaderCode = headerCode;
	CNetServerSerializationBuf::m_byXORCode = xOrCode;

	return true;
}

bool RUDPClientCore::GetSessionFromServer()
{
	WSADATA wsaData;
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
	{
		auto log = std::make_shared<ClientLog>();
		log->logString = "WSAStartup failed GetSessionFromServer() with error code " + WSAGetLastError();
		Logger::GetInstance().WriteLog(log);
		return false;
	}

	sessionBrokerSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sessionBrokerSocket == INVALID_SOCKET)
	{
		auto log = std::make_shared<ClientLog>();
		log->logString = "Socket creation failed in GetSessionFromServer() with error code " + WSAGetLastError();
		Logger::GetInstance().WriteLog(log);
		return false;
	}

	sockaddr_in sessionGetterAddr;
	sessionGetterAddr.sin_family = AF_INET;
	sessionGetterAddr.sin_port = htons(sessionBrokerPort);
	InetPton(AF_INET, sessionBrokerIP, &sessionGetterAddr.sin_addr);

	if (connect(sessionBrokerSocket, (struct sockaddr*)&sessionGetterAddr, sizeof(sessionGetterAddr)) == SOCKET_ERROR)
	{
		auto log = std::make_shared<ClientLog>();
		log->logString = "Connection failed in GetSessionFromServer() with error code " + WSAGetLastError();
		Logger::GetInstance().WriteLog(log);
		closesocket(sessionBrokerSocket);
		return false;
	}

	auto& recvBuffer = *NetBuffer::Alloc();
	int totalReceivedBytes = 0;
	while (true)
	{
		int bytesReceived = recv(sessionBrokerSocket, recvBuffer.GetBufferPtr(), recvBufferSize + df_HEADER_SIZE, 0);
		if (bytesReceived <= 0)
		{
			int error = WSAGetLastError();

			if (error == 0)
			{
				break;
			}
			else
			{
				auto log = std::make_shared<ClientLog>();
				log->logString = "recv() failed in GetSessionFromServer() with error code " + error;
				Logger::GetInstance().WriteLog(log);
				closesocket(sessionBrokerSocket);
				return false;
			}
		}

		totalReceivedBytes += bytesReceived;
		if (totalReceivedBytes > sessionInfoSize)
		{
			auto log = std::make_shared<ClientLog>();
			log->logString = "Received byte is invalid. Total received size " + totalReceivedBytes;
			Logger::GetInstance().WriteLog(log);
			closesocket(sessionBrokerSocket);
			return false;
		}
		else if (totalReceivedBytes == sessionInfoSize)
		{
			break;
		}
	}
	closesocket(sessionBrokerSocket);

	if (SetTargetSessionInfo(recvBuffer))
	{
		NetBuffer::Free(&recvBuffer);
		return false;
	}

	NetBuffer::Free(&recvBuffer);
	return true;
}

bool RUDPClientCore::SetTargetSessionInfo(OUT NetBuffer& receivedBuffer)
{
	if (not receivedBuffer.Decode())
	{
		auto log = std::make_shared<ClientLog>();
		log->logString = "SetTargetSessionInfo()";
		Logger::GetInstance().WriteLog(log);
		return false;
	}

	if (receivedBuffer.GetUseSize() != sessionInfoSize)
	{
		auto log = std::make_shared<ClientLog>();
		log->logString = "SetTargetSessionInfo() : Invalid session info size. receivedBuffer size is " + receivedBuffer.GetUseSize();
		Logger::GetInstance().WriteLog(log);
		return false;
	}

	receivedBuffer >> serverIp >> port >> sessionId >> sessionKey;
	return true;
}

#endif