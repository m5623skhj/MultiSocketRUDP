#include "PreCompile.h"
#include "MultiSocketRUDPCore.h"
#include "BuildConfig.h"
#include "EssentialHandler.h"

void IOContext::InitContext(SessionIdType inOwnerSessionId, RIO_OPERATION_TYPE inIOType)
{
	ownerSessionId = inOwnerSessionId;
	ioType = inIOType;
}

MultiSocketRUDPCore::MultiSocketRUDPCore()
	: contextPool(2, false)
{
}

bool MultiSocketRUDPCore::StartServer(const std::wstring& coreOptionFilePath, const std::wstring& sessionBrokerOptionFilePath)
{
	if (not ReadOptionFile(coreOptionFilePath, sessionBrokerOptionFilePath))
	{
		std::cout << "Option file read failed" << std::endl;
		return false;
	}

	if (not EssentialHandlerManager::GetInst().IsRegisteredAllEssentialHandler())
	{
		std::cout << "Required handler not registered" << std::endl;
		EssentialHandlerManager::GetInst().PrintUnregisteredEssentialHandler();
		return false;
	}

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
	delete[] rioCQList;

	isServerStopped = true;
	std::cout << "Server stop" << std::endl;
}

bool MultiSocketRUDPCore::IsServerStopped()
{
	return isServerStopped;
}

bool MultiSocketRUDPCore::SendPacket(SendPacketInfo* sendPacketInfo)
{
	auto buffer = sendPacketInfo->GetBuffer();

	if (buffer->m_bIsEncoded == false)
	{
		buffer->m_iWriteLast = buffer->m_iWrite;
		buffer->m_iWrite = 0;
		buffer->m_iRead = 0;
		buffer->Encode();
	}

	sendPacketInfo->owner->sendBuffer.sendPacketInfoQueue.Enqueue(sendPacketInfo);

	if (not DoSend(*sendPacketInfo->owner, sendPacketInfo->owner->threadId))
	{
		NetBuffer::Free(buffer);
		sendPacketInfoPool->Free(sendPacketInfo);
		
		return false;
	}

	return true;
}

void MultiSocketRUDPCore::DisconnectSession(const SessionIdType disconnectTargetSessionId)
{
	if (disconnectTargetSessionId >= sessionArray.size() || not sessionArray[disconnectTargetSessionId]->isUsingSession)
	{
		return;
	}

	{
		std::scoped_lock lock(unusedSessionIdListLock);
		unusedSessionIdList.emplace_back(disconnectTargetSessionId);
	}
	sessionArray[disconnectTargetSessionId]->isUsingSession = false;

	std::cout << "Session id " << disconnectTargetSessionId << " is disconnected" << std::endl;
}

void MultiSocketRUDPCore::EraseSendPacketInfo(OUT SendPacketInfo* eraseTarget, ThreadIdType threadId)
{
	if (eraseTarget == nullptr)
	{
		return;
	}

	do
	{
		if (eraseTarget->owner == nullptr)
		{
			break;
		}

		{
			std::scoped_lock lock(*sendedPacketInfoListLock[threadId]);
			sendedPacketInfoList[threadId].erase(eraseTarget->listItor);
		}
	} while (false);

	sendPacketInfoPool->Free(eraseTarget);
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

	sessionArray.reserve(numOfSockets);
	for (auto socketNumber = 0; socketNumber < numOfSockets; ++socketNumber)
	{
		auto optSocket = CreateRUDPSocket(socketNumber);
		if (not optSocket.has_value())
		{
			WSACleanup();
			return false;
		}

		sessionArray.emplace_back(RUDPSession::Create(optSocket.value(), static_cast<PortType>(portStartNumber + socketNumber), *this));
		sessionArray[socketNumber]->sessionId = socketNumber;
	}

	return true;
}

bool MultiSocketRUDPCore::InitRIO()
{
	GUID guid = WSAID_MULTIPLE_RIO;
	DWORD bytes = 0;

	// For the purpose of obtaining the function table, any of the created sessions selected
	if (WSAIoctl((sessionArray[0])->sock, SIO_GET_MULTIPLE_EXTENSION_FUNCTION_POINTER, &guid, sizeof(GUID)
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
	for (int rioCQIndex = 0; rioCQIndex < numOfWorkerThread; ++rioCQIndex)
	{
		rioCQList[rioCQIndex] = rioFunctionTable.RIOCreateCompletionQueue(numOfSockets / numOfWorkerThread * maxSendBufferSize, nullptr);
	}

	for (auto& session : sessionArray)
	{
		session->threadId = session->sessionId % numOfWorkerThread;
		if (not session->InitializeRIO(rioFunctionTable, rioCQList[session->threadId], rioCQList[session->threadId]))
		{
			return false;
		}
	}

	return true;
}

RIO_BUFFERID MultiSocketRUDPCore::RegisterRIOBuffer(char* targetBuffer, unsigned int targetBuffersize)
{
	if (targetBuffer == nullptr)
	{
		return RIO_INVALID_BUFFERID;
	}

	RIO_BUFFERID clientAddrBufferId = rioFunctionTable.RIORegisterBuffer(targetBuffer, targetBuffersize);
	if (clientAddrBufferId == RIO_INVALID_BUFFERID)
	{
		std::cout << "Send RIORegisterBuffer failed with error code " << WSAGetLastError() << std::endl;
	}

	return clientAddrBufferId;
}

bool MultiSocketRUDPCore::RunAllThreads()
{
	if (not RunSessionBroker())
	{
		std::cout << "RunSessionBroker() failed" << std::endl;
		return false;
	}

	sendedPacketInfoList.reserve(numOfWorkerThread);
	sendedPacketInfoListLock.reserve(numOfWorkerThread);
	ioWorkerThreads.reserve(numOfWorkerThread);
	recvLogicWorkerThreads.reserve(numOfWorkerThread);
	retransmissionThread.reserve(numOfWorkerThread);

	logicThreadEventStopHandle = CreateEvent(NULL, TRUE, FALSE, NULL);
	recvLogicThreadEventHandles.reserve(numOfWorkerThread);
	ioCompletedContexts.reserve(numOfWorkerThread);

	for (unsigned char id = 0; id < numOfWorkerThread; ++id)
	{
		recvLogicThreadEventHandles.emplace_back(CreateEvent(NULL, FALSE, FALSE, NULL));
		sendedPacketInfoList.emplace_back();
		sendedPacketInfoListLock.push_back(std::make_unique<std::mutex>());

		ioWorkerThreads.emplace_back([this, id]() { this->RunWorkerThread(static_cast<ThreadIdType>(id)); });
		recvLogicWorkerThreads.emplace_back([this, id]() { this->RunRecvLogicWorkerThread(static_cast<ThreadIdType>(id)); });
		retransmissionThread.emplace_back([this, id]() { this->RunRetransmissionThread(static_cast<ThreadIdType>(id)); });
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
	sessionBrokerThread = std::thread([this]() { this->RunSessionBrokerThread(sessionBrokerPort, coreServerIp); });
#endif

	return true;
}

std::optional<SOCKET> MultiSocketRUDPCore::CreateRUDPSocket(unsigned short socketNumber)
{
	SOCKET sock = WSASocket(AF_INET, SOCK_DGRAM, IPPROTO_UDP, NULL, 0, WSA_FLAG_REGISTERED_IO);
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
		std::scoped_lock lock(unusedSessionIdListLock);
		unusedSessionIdList.clear();
	}

	{
		for (auto& session : sessionArray)
		{
			closesocket(session->sock);
		}

		// need wait?
		sessionArray.clear();
	}
}

std::shared_ptr<RUDPSession> MultiSocketRUDPCore::AcquireSession()
{
	std::shared_ptr<RUDPSession> session = nullptr;
	SessionIdType sessionId{};
	{
		std::scoped_lock lock(unusedSessionIdListLock);

		if (unusedSessionIdList.empty() == true)
		{
			return nullptr;
		}

		sessionId = unusedSessionIdList.front();
		unusedSessionIdList.pop_front();
	}

	session = sessionArray[sessionId];
	if (session->isUsingSession == true)
	{
		return nullptr;
	}
	session->isUsingSession = true;

	return session;
}

std::shared_ptr<RUDPSession> MultiSocketRUDPCore::GetUsingSession(SessionIdType sessionId)
{
	if (sessionArray.size() <= sessionId || not sessionArray[sessionId]->isUsingSession)
	{
		return nullptr;
	}

	return sessionArray[sessionId];
}

void MultiSocketRUDPCore::RunWorkerThread(ThreadIdType threadId)
{
	RIORESULT rioResults[maxRIOResult];
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
			auto context = GetIOCompletedContext(rioResults[i]);
			if (context == nullptr)
			{
				continue;
			}

			if (not IOCompleted(context, rioResults[i].BytesTransferred, threadId))
			{
				contextPool.Free(context);
				// error handling
				continue;
			}
		}

#if USE_IO_WORKER_THREAD_SLEEP_FOR_FRAME
		SleepRemainingFrameTime(tickSet, workerThreadOneFrameMs);
#endif
	}

	std::cout << "worker thread stopped" << std::endl;
}

void MultiSocketRUDPCore::RunRecvLogicWorkerThread(ThreadIdType threadId)
{
	HANDLE eventHandles[2] = { recvLogicThreadEventHandles[threadId], logicThreadEventStopHandle };
	while (not threadStopFlag)
	{
		const auto waitResult = WaitForMultipleObjects(2, eventHandles, FALSE, INFINITE);
		switch (waitResult)
		{
		case WAIT_OBJECT_0:
		{
			OnRecvPacket(threadId);
		}
		break;
		case WAIT_OBJECT_0 + 1:
		{
			Sleep(logicThreadStopSleepTime);
			OnRecvPacket(threadId);
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

void MultiSocketRUDPCore::RunRetransmissionThread(ThreadIdType threadId)
{
	TickSet tickSet;
	tickSet.nowTick = GetTickCount64();
	tickSet.beforeTick = tickSet.nowTick;

	auto& thisThreadSendedPacketInfoList = sendedPacketInfoList[threadId];
	auto& thisThreadSendedPacketInfoListLock = *sendedPacketInfoListLock[threadId];

	std::list<SendPacketInfo*> copyList;

	while (not threadStopFlag)
	{
		{
			std::scoped_lock lock(thisThreadSendedPacketInfoListLock);
			copyList.assign(thisThreadSendedPacketInfoList.begin(), thisThreadSendedPacketInfoList.end());
		}
		
		for (auto& sendedPacketInfo : copyList)
		{
			if (sendedPacketInfo->sendTimeStamp < tickSet.nowTick)
			{
				continue;
			}

			if (++sendedPacketInfo->retransmissionCount >= maxPacketRetransmissionCount)
			{
				sendedPacketInfo->owner->Disconnect();
				continue;
			}

			SendPacket(sendedPacketInfo);
		}

		SleepRemainingFrameTime(tickSet, retransmissionThreadSleepMs);
	}
}

void MultiSocketRUDPCore::SleepRemainingFrameTime(OUT TickSet& tickSet, unsigned int intervalMs)
{
	tickSet.nowTick = GetTickCount64();
	UINT64 deltaTick = tickSet.nowTick - tickSet.beforeTick;

	if (deltaTick < intervalMs && deltaTick > 0)
	{
		Sleep(intervalMs - static_cast<DWORD>(deltaTick));
	}

	tickSet.beforeTick = tickSet.nowTick;
}

IOContext* MultiSocketRUDPCore::GetIOCompletedContext(RIORESULT& rioResult)
{
	IOContext* context = reinterpret_cast<IOContext*>(rioResult.RequestContext);
	if (context == nullptr)
	{
		return nullptr;
	}

	context->session = GetUsingSession(context->ownerSessionId);
	if (context->session == nullptr)
	{
		contextPool.Free(context);
		return nullptr;
	}

	if (rioResult.BytesTransferred == 0 || context->session->ioCancle == true)
	{
		contextPool.Free(context);
		return nullptr;
	}

	return context;
}

bool MultiSocketRUDPCore::IOCompleted(OUT IOContext* contextResult, ULONG transferred, BYTE threadId)
{
	if (contextResult == nullptr)
	{
		return false;
	}

	switch (contextResult->ioType)
	{
	case RIO_OPERATION_TYPE::OP_RECV:
	{
		if (RecvIOCompleted(contextResult, transferred, threadId))
		{
			// Release session?
		}
		return true;
	}
	break;
	case RIO_OPERATION_TYPE::OP_SEND:
	{
		return SendIOCompleted(transferred, contextResult->session, threadId);
	}
	break;
	default:
	{
		std::cout << "Invalid rio operation type. Type is " << static_cast<unsigned char>(contextResult->ioType) << std::endl;
		g_Dump.Crash();
	}
	break;
	}

	return false;
}

bool MultiSocketRUDPCore::RecvIOCompleted(OUT IOContext* contextResult, ULONG transferred, BYTE threadId)
{
	auto buffer = NetBuffer::Alloc();
	if (memcpy_s(buffer->m_pSerializeBuffer, recvBufferSize, contextResult->session->recvBuffer.buffer, transferred) != 0)
	{
		return false;
	}

	contextResult->session->recvBuffer.recvBufferList.Enqueue(buffer);
	ioCompletedContexts[threadId].Enqueue(contextResult);
	SetEvent(recvLogicThreadEventHandles[threadId]);

	return DoRecv(contextResult->session);
}

bool MultiSocketRUDPCore::SendIOCompleted(ULONG transferred, std::shared_ptr<RUDPSession> session, BYTE threadId)
{
	InterlockedExchange((UINT*)&session->sendBuffer.ioMode, (UINT)IO_MODE::IO_NONE_SENDING);
	return DoSend(*session, threadId);
}

void MultiSocketRUDPCore::OnRecvPacket(BYTE threadId)
{
	while (ioCompletedContexts[threadId].GetRestSize() > 0)
	{
		IOContext* context = nullptr;
		if (ioCompletedContexts[threadId].Dequeue(&context) == false || context == nullptr)
		{
			continue;
		}

		NetBuffer* buffer = nullptr;
		do
		{
			if (context->session->recvBuffer.recvBufferList.Dequeue(&buffer) == false || buffer == nullptr)
			{
				break;
			}

			if (not buffer->Decode() || buffer->GetUseSize() != GetPayloadLength(*buffer))
			{
				break;
			}

			sockaddr_in clientAddr;
			std::ignore = memcpy_s(&clientAddr, sizeof(clientAddr), context->clientAddrBuffer, sizeof(context->clientAddrBuffer));
			ProcessByPacketType(context->session, clientAddr, *buffer);
		} while (false);

		if (buffer != nullptr)
		{
			NetBuffer::Free(buffer);
		}
		contextPool.Free(context);
	}
}

bool MultiSocketRUDPCore::DoRecv(std::shared_ptr<RUDPSession> session)
{
	auto context = contextPool.Alloc();
	context->InitContext(session->sessionId, RIO_OPERATION_TYPE::OP_RECV);
	context->BufferId = session->recvBuffer.recvBufferId;
	context->Length = recvBufferSize;
	context->Offset = 0;

	if (context->clientAddrBufferId == RIO_INVALID_BUFFERID &&
		(context->clientAddrBufferId = RegisterRIOBuffer(context->clientAddrBuffer, sizeof(sockaddr_in))) == RIO_INVALID_BUFFERID)
	{
		std::cout << "DoRecv() : clientAddrBufferId is RIO_INVALID_BUFFERID" << std::endl;
		return false;
	}

	RIO_BUF clientAddrBuffer;
	clientAddrBuffer.BufferId = context->clientAddrBufferId;
	clientAddrBuffer.Length = sizeof(sockaddr_in);
	clientAddrBuffer.Offset = 0;

	if (rioFunctionTable.RIOReceiveEx(session->rioRQ, context, 1, nullptr, &clientAddrBuffer, nullptr, nullptr, 0, nullptr) == false)
	{
		std::cout << "RIOReceiveEx() failed with " << WSAGetLastError() << std::endl;
		return false;
	}

	return true;
}

bool MultiSocketRUDPCore::DoSend(OUT RUDPSession& session, ThreadIdType threadId)
{
	while (1)
	{
		if (InterlockedCompareExchange((UINT*)&session.sendBuffer.ioMode, (UINT)IO_MODE::IO_SENDING, (UINT)IO_MODE::IO_NONE_SENDING))
		{
			break;
		}

		if (session.sendBuffer.sendPacketInfoQueue.GetRestSize() == 0 &&
			session.sendBuffer.reservedSendPacketInfo == nullptr)
		{
			InterlockedExchange((UINT*)&session.sendBuffer.ioMode, (UINT)IO_MODE::IO_NONE_SENDING);
			if (session.sendBuffer.sendPacketInfoQueue.GetRestSize() > 0)
			{
				continue;
			}
			break;
		}

		int contextCount = 1;
		IOContext* context = contextPool.Alloc();
		context->InitContext(session.sessionId, RIO_OPERATION_TYPE::OP_SEND);
		context->BufferId = session.sendBuffer.sendBufferId;
		context->Offset = 0;
		context->Length = MakeSendStream(session, context, threadId);

		if (context->clientAddrBufferId == RIO_INVALID_BUFFERID &&
			(context->clientAddrBufferId = RegisterRIOBuffer(context->clientAddrBuffer, sizeof(sockaddr_in))) == RIO_INVALID_BUFFERID)
		{
			std::cout << "DoSend() : clientAddrBufferId is RIO_INVALID_BUFFERID" << std::endl;
			return false;
		}

		RIO_BUF clientAddrBuffer;
		clientAddrBuffer.BufferId = context->clientAddrBufferId;
		clientAddrBuffer.Length = sizeof(sockaddr_in);
		clientAddrBuffer.Offset = 0;

		if (rioFunctionTable.RIOSendEx(session.rioRQ, static_cast<PRIO_BUF>(context), 1, nullptr, &clientAddrBuffer, nullptr, nullptr, 0, nullptr) == false)
		{
			std::cout << "RIOSendEx() failed with " << WSAGetLastError() << std::endl;
			return false;
		}

		break;
	}

	return true;
}

int MultiSocketRUDPCore::MakeSendStream(OUT RUDPSession& session, OUT IOContext* context, ThreadIdType threadId)
{
	int totalSendSize = 0;
	int bufferCount = session.sendBuffer.sendPacketInfoQueue.GetRestSize();
	char* bufferPositionPointer = session.sendBuffer.rioSendBuffer;

	if (session.sendBuffer.reservedSendPacketInfo != nullptr)
	{
		int useSize = session.sendBuffer.reservedSendPacketInfo->buffer->GetAllUseSize();
		if (useSize < maxSendBufferSize)
		{
			std::cout << "MakeSendStream() : useSize over with " << maxSendBufferSize << std::endl;
			// call g_Dump.Crash() ?
			return 0;
		}

		memcpy_s(bufferPositionPointer, maxSendBufferSize
			, session.sendBuffer.reservedSendPacketInfo->buffer->GetBufferPtr(), useSize);

		totalSendSize += useSize;
		bufferPositionPointer += totalSendSize;
		session.sendBuffer.reservedSendPacketInfo = nullptr;
	}

	SendPacketInfo* sendPacketInfo;
	for (int i = 0; i < bufferCount; ++i)
	{
		session.sendBuffer.sendPacketInfoQueue.Dequeue(&sendPacketInfo);

		int useSize = sendPacketInfo->buffer->GetAllUseSize();
		if (useSize < maxSendBufferSize)
		{
			std::cout << "MakeSendStream() : useSize over with " << maxSendBufferSize << std::endl;
			// call g_Dump.Crash() ?
			return 0;
		}

		totalSendSize += useSize;
		if (totalSendSize >= maxSendBufferSize)
		{
			session.sendBuffer.reservedSendPacketInfo = sendPacketInfo;
			break;
		}

		sendPacketInfo->sendTimeStamp = GetTickCount64() + retransmissionMs;
		{
			std::scoped_lock lock(*sendedPacketInfoListLock[threadId]);
			auto itor = sendedPacketInfoList[threadId].emplace(sendedPacketInfoList[threadId].end(), sendPacketInfo);
			sendPacketInfo->listItor = itor;
		}
		memcpy_s(&session.sendBuffer.rioSendBuffer[totalSendSize - useSize], maxSendBufferSize - totalSendSize - useSize
			, sendPacketInfo->buffer->GetBufferPtr(), useSize);
	}

	return totalSendSize;
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

void MultiSocketRUDPCore::EncodePacket(OUT NetBuffer& packet)
{
	if (packet.m_bIsEncoded == false)
	{
		packet.m_iWriteLast = packet.m_iWrite;
		packet.m_iWrite = 0;
		packet.m_iRead = 0;
		packet.Encode();
	}
}
