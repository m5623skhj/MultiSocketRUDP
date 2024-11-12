#include "PreCompile.h"
#include "MultiSocketRUDPCore.h"
#include "BuildConfig.h"

void IOContext::InitContext(SessionIdType inOwnerSessionId, RIO_OPERATION_TYPE inIOType)
{
	ownerSessionId = inOwnerSessionId;
	ioType = inIOType;
}

MultiSocketRUDPCore::MultiSocketRUDPCore()
	: contextPool(2, false)
{
}

bool MultiSocketRUDPCore::StartServer(const std::wstring& optionFilePath, const std::wstring& sessionBrokerOptionFilePath)
{
	// Parsing items from option file path

	if (not InitNetwork())
	{
		CloseAllSessions();
		std::cout << "InitNetwork failed" << std::endl;
		return false;
	}

	if (not InitRIO())
	{
		CloseAllSessions();
		std::cout << "InitRIO failed" << std::endl;
		return false;
	}

	if (not RunAllThreads())
	{
		CloseAllSessions();
		StopServer();
		std::cout << "RunAllThreads() failed" << std::endl;
		return false;
	}

	return true;
}

void MultiSocketRUDPCore::StopServer()
{
	threadStopFlag = true;

#if USE_IOCP_SESSION_BROKER
	sessionBroker.Stop();
#else
	sessionBrokerThread.join();
#endif
	CloseAllSessions();

	isServerStopped = true;
	std::cout << "Server stop" << std::endl;
}

bool MultiSocketRUDPCore::IsServerStopped()
{
	return isServerStopped;
}

bool MultiSocketRUDPCore::InitNetwork()
{
	WSADATA wsaData;
	int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (result != 0)
	{
		std::cout << "WSAStartup failed " << result << std::endl;
		return false;
	}

	usingSessionMap.reserve(numOfSockets);
	for (auto socketNumber = 0; socketNumber < numOfSockets; ++socketNumber)
	{
		auto optSocket = CreateRUDPSocket(socketNumber);
		if (not optSocket.has_value())
		{
			WSACleanup();
			return false;
		}

		unusedSessionList.emplace_back(RUDPSession::Create(optSocket.value(), static_cast<PortType>(portStartNumber + socketNumber)));
	}

	return true;
}

bool MultiSocketRUDPCore::InitRIO()
{
	GUID functionTableId = WSAID_MULTIPLE_RIO;
	DWORD bytes = 0;

	auto itor = unusedSessionList.begin();
	if (itor == unusedSessionList.end())
	{
		std::cout << "InitRIO failed. Session map is not initilazed" << std::endl;
		return false;
	}

	// For the purpose of obtaining the function table, any of the created sessions selected
	if (WSAIoctl((*itor)->sock, SIO_GET_MULTIPLE_EXTENSION_FUNCTION_POINTER, &functionTableId, sizeof(GUID)
		, reinterpret_cast<void**>(&rioFunctionTable), sizeof(rioFunctionTable), &bytes, NULL, NULL))
	{
		std::cout << "WSAIoctl_SIO_GET_MULTIPLE_EXTENSION_FUNCTION_POINTER" << std::endl;
		return false;
	}

	rioCQList = new RIO_CQ[numOfWorkerThread];
	if (rioCQList == nullptr)
	{
		return false;
	}

	for (auto& session : unusedSessionList)
	{
		if (not session->InitializeRIO(rioFunctionTable, rioCQList[session->sessionId % numOfWorkerThread], rioCQList[session->sessionId % numOfWorkerThread]))
		{
			return false;
		}
	}

	return true;
}

bool MultiSocketRUDPCore::RunAllThreads()
{
	if (not RunSessionBroker())
	{
		std::cout << "RunSessionBroker() failed" << std::endl;
		return false;
	}

	ioWorkerThreads.reserve(numOfWorkerThread);
	logicWorkerThreads.reserve(numOfWorkerThread);

	logicThreadEventStopHandle = CreateEvent(NULL, TRUE, FALSE, NULL);
	logicThreadEventHandles.reserve(numOfWorkerThread);

	for (unsigned char id = 0; id < numOfWorkerThread; ++id)
	{
		logicThreadEventHandles.emplace_back(CreateEvent(NULL, TRUE, FALSE, NULL));

		ioWorkerThreads.emplace_back([this, id]() { this->RunWorkerThread(static_cast<ThreadIdType>(id)); });
		logicWorkerThreads.emplace_back([this, id]() { this->RunLogicWorkerThread(static_cast<ThreadIdType>(id)); });
	}

	return true;
}

bool MultiSocketRUDPCore::RunSessionBroker()
{
#if USE_IOCP_SESSION_BROKER
	if (not sessionBroker.Start(sessionBrokerOptionFilePath))
	{
		CloseAllSockets();
		std::cout << "SessionBroker start falied" << std::endl;
		return false;
	}
#else
	sessionBrokerThread = std::thread([this]() { this->RunSessionBrokerThread(sessionBrokerPort, ip); });
#endif

	return true;
}

std::optional<SOCKET> MultiSocketRUDPCore::CreateRUDPSocket(unsigned short socketNumber)
{
	SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sock == INVALID_SOCKET)
	{
		std::cout << "Socket create failed " << WSAGetLastError() << std::endl;
		return std::nullopt;
	}

	sockaddr_in serverAddr;
	memset(&serverAddr, 0, sizeof(serverAddr));

	serverAddr.sin_family = AF_INET;
	serverAddr.sin_addr.S_un.S_addr = INADDR_ANY;
	serverAddr.sin_port = htons(portStartNumber + socketNumber);

	if (bind(sock, (SOCKADDR*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR)
	{
		std::cout << "Bind failed " << WSAGetLastError() << std::endl;
		closesocket(sock);
		return std::nullopt;
	}

	return sock;
}

void MultiSocketRUDPCore::CloseAllSessions()
{
	{
		std::unique_lock lock(usingSessionMapLock);
		usingSessionMap.clear();
	}
	{
		std::scoped_lock lock(unusedSessionListLock);
		unusedSessionList.clear();
	}
}

std::shared_ptr<RUDPSession> MultiSocketRUDPCore::AcquireSession()
{
	std::shared_ptr<RUDPSession> session = nullptr;
	{
		std::scoped_lock lock(unusedSessionListLock);
		
		if (unusedSessionList.empty() == true)
		{
			return nullptr;
		}

		session = unusedSessionList.front();
		unusedSessionList.pop_front();
	}

	session->isUsingSession = true;

	{
		std::unique_lock lock(usingSessionMapLock);
		usingSessionMap.insert({ session->sessionId, session });
	}

	return session;
}

std::shared_ptr<RUDPSession> MultiSocketRUDPCore::GetUsingSession(SessionIdType sessionId)
{
	std::unique_lock lock(usingSessionMapLock);
	auto itor = usingSessionMap.find(sessionId);
	if (itor == usingSessionMap.end())
	{
		return nullptr;
	}

	return itor->second;
}

void MultiSocketRUDPCore::ReleaseSession(std::shared_ptr<RUDPSession> session)
{
	if (session == nullptr)
	{
		std::cout << "ReleaseSession() : session is nullptr" << std::endl;
		return;
	}
	
	{
		std::unique_lock lock(usingSessionMapLock);
		usingSessionMap.erase(session->sessionId);
	}
	
	session->isUsingSession = false;

	{
		std::scoped_lock lock(unusedSessionListLock);
		unusedSessionList.push_back(session);
	}
}

void MultiSocketRUDPCore::RunWorkerThread(ThreadIdType threadId)
{
	RIORESULT rioResults[maxRIOResult];
	rioCQList[threadId] = rioFunctionTable.RIOCreateCompletionQueue(numOfSockets / numOfWorkerThread * maxSendBufferSize, nullptr);
	ULONG numOfResults = 0;

	TickSet tickSet;
	tickSet.nowTick = GetTickCount64();
	tickSet.beforeTick = tickSet.nowTick;

	while (not threadStopFlag)
	{
		ZeroMemory(rioResults, sizeof(rioResults));

		numOfResults = rioFunctionTable.RIODequeueCompletion(rioCQList[threadId], rioResults, maxRIOResult);
		for (ULONG i = 0; i < numOfResults; ++i)
		{
			auto contextResult = GetIOCompletedContext(rioResults[i]);
			if (contextResult == std::nullopt)
			{
				continue;
			}

			if (not IOCompleted(*contextResult->ioContext, rioResults[i].BytesTransferred, contextResult->session, threadId))
			{
				contextPool.Free(contextResult->ioContext);
				// error handling
				continue;
			}

			contextPool.Free(contextResult->ioContext);
		}

#if USE_IO_WORKER_THREAD_SLEEP_FOR_FRAME
		SleepRemainingFrameTime(tickSet);
#endif
	}

	std::cout << "worker thread stopped" << std::endl;
}

void MultiSocketRUDPCore::RunLogicWorkerThread(ThreadIdType threadId)
{
	TickSet tickSet;
	tickSet.nowTick = GetTickCount64();
	tickSet.beforeTick = tickSet.nowTick;

	HANDLE eventHandles[2] = { logicThreadEventHandles[threadId], logicThreadEventStopHandle };
	while (not threadStopFlag)
	{
		const auto waitResult = WaitForMultipleObjects(2, eventHandles, FALSE, INFINITE);
		switch (waitResult)
		{
		case WAIT_OBJECT_0:
		{
			// RecvFromClient();
		}
		break;
		case WAIT_OBJECT_0 + 1:
		{
			Sleep(logicThreadStopSleepTime);
			// RecvFromClient();
			std::cout << "Logic thread stop. ThreadId is " << threadId << std::endl;
			break;
		}
		break;
		default:
		{
			std::cout << "Invalid logic thread wait result. Error is " << WSAGetLastError() << std::endl;
			g_Dump.Crash();
		}
		break;
		}
	}
}

void MultiSocketRUDPCore::SleepRemainingFrameTime(OUT TickSet& tickSet)
{
	tickSet.nowTick = GetTickCount64();
	UINT64 deltaTick = tickSet.nowTick - tickSet.beforeTick;

	if (deltaTick < oneFrame && deltaTick > 0)
	{
		Sleep(oneFrame - static_cast<DWORD>(deltaTick));
	}

	tickSet.beforeTick = tickSet.nowTick;
}

std::optional<IOContextResult> MultiSocketRUDPCore::GetIOCompletedContext(RIORESULT& rioResult)
{
	IOContext* context = reinterpret_cast<IOContext*>(rioResult.RequestContext);
	if (context == nullptr)
	{
		return std::nullopt;
	}

	IOContextResult result;
	result.session = GetUsingSession(context->ownerSessionId);
	if (result.session == nullptr)
	{
		return std::nullopt;
	}

	if (rioResult.BytesTransferred == 0 || result.session->ioCancle == true)
	{
		contextPool.Free(context);
		return std::nullopt;
	}

	return result;
}

bool MultiSocketRUDPCore::IOCompleted(IOContext& context, ULONG transferred, std::shared_ptr<RUDPSession> session, BYTE threadId)
{
	switch (context.ioType)
	{
	case RIO_OPERATION_TYPE::OP_RECV:
	{

		return RecvIOCompleted(transferred, session, context.clientAddr, threadId);
	}
	break;
	case RIO_OPERATION_TYPE::OP_SEND:
	{
		return SendIOCompleted(transferred, session, threadId);
	}
	break;
	default:
	{
	}
	break;
	}

	return false;
}

bool MultiSocketRUDPCore::RecvIOCompleted(ULONG transferred, std::shared_ptr<RUDPSession> session, const sockaddr_in& clientAddr, BYTE threadId)
{
	auto& buffer = *NetBuffer::Alloc();
	do
	{
		if (memcpy_s(buffer.m_pSerializeBuffer, recvBufferSize, session->recvBuffer.buffer, transferred) != 0)
		{
			break;
		}

		if (not buffer.Decode())
		{
			break;
		}
		else if (buffer.GetUseSize() != GetPayloadLength(buffer))
		{
			break;
		}

		if (not ProcessByPacketType(session, clientAddr, buffer))
		{
			break;
		}

		NetBuffer::Free(&buffer);

		return DoRecv(session);
	} while (false);

	NetBuffer::Free(&buffer);
	return false;
}

bool MultiSocketRUDPCore::ProcessByPacketType(std::shared_ptr<RUDPSession> session, const sockaddr_in& clientAddr, NetBuffer& recvPacket)
{
	PACKET_TYPE packetType;
	recvPacket >> packetType;

	switch (packetType)
	{
	case PACKET_TYPE::ConnectType:
	{
		session->TryConnect(recvPacket);
		break;
	}
	break;
	case PACKET_TYPE::DisconnectType:
	{
		if (not session->CheckMyClient(clientAddr))
		{
			break;
		}

		if (session->TryDisconnect(recvPacket) == true)
		{
			ReleaseSession(session);
			return false;
		}

		break;
	}
	break;
	case PACKET_TYPE::SendType:
	{
		if (not session->CheckMyClient(clientAddr))
		{
			break;
		}

		session->OnRecvPacket(recvPacket);
		break;
	}
	break;
	case PACKET_TYPE::SendReplyType:
	{
		if (not session->CheckMyClient(clientAddr))
		{
			break;
		}

		break;
	}
	break;
	default:
		// TODO : Write log
		break;
	}

	return true;
}

bool MultiSocketRUDPCore::DoRecv(std::shared_ptr<RUDPSession> session)
{
	auto context = contextPool.Alloc();
	context->InitContext(session->sessionId, RIO_OPERATION_TYPE::OP_RECV);
	context->BufferId = session->recvBuffer.recvBufferId;
	context->Length = recvBufferSize;
	context->Offset = 0;

	if (rioFunctionTable.RIOReceiveEx(session->rioRQ, context, 1, nullptr, &context->addrBuffer, nullptr, nullptr, 0, nullptr) == false)
	{
		std::cout << "RIOReceiveEx() failed with " << WSAGetLastError() << std::endl;
		return false;
	}

	return true;
}

bool MultiSocketRUDPCore::SendIOCompleted(ULONG transferred, std::shared_ptr<RUDPSession> session, BYTE threadId)
{
	//InterlockedExchange((UINT*)&session->sendBuffer.ioMode, (UINT)IO_MODE::IO_NONE_SENDING);
	return true;
}

bool MultiSocketRUDPCore::DoSend(std::shared_ptr<RUDPSession> session)
{
	return true;
}

WORD MultiSocketRUDPCore::GetPayloadLength(OUT NetBuffer& buffer)
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

