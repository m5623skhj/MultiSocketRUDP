#include "PreCompile.h"
#include "RUDPIOHandler.h"
#include "RIOManager.h"
#include "RUDPSession.h"
#include "Logger.h"
#include "RUDPSessionFunctionDelegate.h"
#include "LogExtension.h"
#include "IOContext.h"
#include "MultiSocketRUDPCore.h"
#include "MultiSocketRUDPCoreFunctionDelegate.h"
#include "SendPacketInfo.h"
#include "../Common/etc/UtilFunc.h"

RUDPIOHandler::RUDPIOHandler(IRIOManager& inRioManager
	, ISessionDelegate& inSessionDelegate
	, CTLSMemoryPool<IOContext>& contextPool
	, std::vector<std::unique_ptr<RetransmissionScheduler>>& retransmissionSchedulers
	, const BYTE inMaxHoldingPacketQueueSize
	, const unsigned int inRetransmissionMs
	, const unsigned int inSimulatedPacketLossPercent
	, const int inSimulatedPacketLossSeed)
	: rioManager(inRioManager)
	, sessionDelegate(inSessionDelegate)
	, contextPool(contextPool)
	, retransmissionSchedulers(retransmissionSchedulers)
	, retransmissionMs(inRetransmissionMs)
{
	if (inSimulatedPacketLossPercent > 0)
	{
		lossSimulator = std::make_unique<DatagramLossSimulator>(inSimulatedPacketLossPercent, inSimulatedPacketLossSeed);
	}
}

bool RUDPIOHandler::IOCompleted(IOContext* context, const ULONG transferred, const BYTE threadId, const LONG status) const
{
	if (context == nullptr)
	{
		LOG_ERROR("IOCompleted context is nullptr");
		return false;
	}
	if (context->session == nullptr)
	{
		LOG_ERROR("IOCompleted context session is nullptr");
		return false;
	}

	RUDPSession* session = context->session;
	session->BeginIOCompletion();

	// 모든 generation 및 status 분기는 동일한 session drain barrier를 해제해야 합니다.
	auto completionGuard = Util::MakeScopeExit([session]()
		{
			session->CompleteIOCompletion();
		});
	const bool isCurrentGeneration =
		context->ownerSessionId == session->GetSessionId() &&
		context->ownerSessionGeneration == session->GetSessionGeneration();

	if (not isCurrentGeneration)
	{
		return HandleStaleCompletion(context);
	}

	if (status != 0)
	{
		return HandleFailedCompletion(context, *session, status);
	}

	return DispatchSuccessfulCompletion(context, *session, transferred, threadId);
}

bool RUDPIOHandler::HandleStaleCompletion(IOContext* context) const
{
	LOG_ERROR(std::format("Discarding stale RIO completion for session {} generation {}",
		context->ownerSessionId,
		context->ownerSessionGeneration));

	if (context->ioType == RIO_OPERATION_TYPE::OP_RECV)
	{
		ReleaseRecvContext(context);
		return true;
	}

	if (context->ioType == RIO_OPERATION_TYPE::OP_SEND)
	{
		contextPool.Free(context);
		return true;
	}

	return false;
}

bool RUDPIOHandler::HandleFailedCompletion(IOContext* context, RUDPSession& session, const LONG status) const
{
	if (context->ioType == RIO_OPERATION_TYPE::OP_RECV)
	{
		ReleaseRecvContext(context);
	}
	else if (context->ioType == RIO_OPERATION_TYPE::OP_SEND)
	{
		sessionDelegate.GetSendIOMode(session).store(IO_MODE::IO_NONE_SENDING, std::memory_order_release);
		contextPool.Free(context);
	}
	else
	{
		return false;
	}

	const bool isExpectedCancellation =
		status == WSA_OPERATION_ABORTED && session.IsReleasing();
	if (isExpectedCancellation)
	{
		return true;
	}

	const DISCONNECT_REASON reason = status == WSAECONNRESET ?
		DISCONNECT_REASON::NORMAL : DISCONNECT_REASON::BY_ERROR;
	session.DoDisconnect(reason);
	if (reason == DISCONNECT_REASON::BY_ERROR)
	{
		LOG_ERROR(std::format("RIO operation failed with error code {}", status));
	}
	return true;
}

bool RUDPIOHandler::DispatchSuccessfulCompletion(
	IOContext* context,
	RUDPSession& session,
	const ULONG transferred,
	const BYTE threadId) const
{
	switch (context->ioType)
	{
	case RIO_OPERATION_TYPE::OP_RECV:
	{
		if (not RecvIOCompleted(context, transferred, threadId))
		{
			session.DoDisconnect(DISCONNECT_REASON::BY_ERROR);
			break;
		}

		return true;
	}
	case RIO_OPERATION_TYPE::OP_SEND:
	{
		if (not SendIOCompleted(context, threadId))
		{
			session.DoDisconnect(DISCONNECT_REASON::BY_ERROR);
			break;
		}

		return true;
	}
	default:
	{
		LOG_ERROR("IOCompleted invalid ioType");
		return false;
	}
	}

	return false;
}

bool RUDPIOHandler::DoRecv(RUDPSession& session) const
{
	std::shared_lock lock(sessionDelegate.GetSocketMutex(session));
	if (session.IsReleasing())
	{
		return true;
	}
	if (sessionDelegate.GetSocket(session) == INVALID_SOCKET)
	{
		return false;
	}

	auto& recvBuffer = sessionDelegate.GetRecvBuffer(session);
	while (IOContext* context = recvBuffer.AcquireFreeRecvContext())
	{
		context->ownerSessionGeneration = session.GetSessionGeneration();
		recvBuffer.BeginRecvIo();
		if (not rioManager.RIOReceiveEx(sessionDelegate.GetRecvRIORQ(session)
			, context
			, 1
			, &context->localAddrRIOBuffer
			, &context->clientAddrRIOBuffer
			, nullptr
			, nullptr
			, 0
			, context))
		{
			recvBuffer.CompleteRecvIo();
			recvBuffer.ReleaseRecvContext(context);
			return false;
		}
	}

	return true;
}

bool RUDPIOHandler::DoSend(RUDPSession& session, const ThreadIdType threadId) const
{
	if (session.IsReleasing())
	{
		return true;
	}

	const auto releaseIOSending = [&]()
	{
		sessionDelegate.GetSendIOMode(session).store(IO_MODE::IO_NONE_SENDING);
	};

	while (true)
	{
		IO_MODE expected = IO_MODE::IO_NONE_SENDING;
		if (not sessionDelegate.GetSendIOMode(session).compare_exchange_strong(expected, IO_MODE::IO_SENDING))
		{
			break;
		}

		if (session.IsReleasing())
		{
			releaseIOSending();
			return true;
		}
		
		if (sessionDelegate.IsNothingToSend(session))
		{
			releaseIOSending();
			if (not sessionDelegate.IsNothingToSend(session))
			{
				continue;
			}

			break;
		}

		const auto [succeeded, sendContext] = MakeSendContext(session, threadId);
		if (sendContext == nullptr)
		{
			releaseIOSending();
			return succeeded;
		}

		if (lossSimulator != nullptr && lossSimulator->ShouldDropSendingDatagram())
		{
			contextPool.Free(sendContext);
			releaseIOSending();
			continue;
		}

		return TryRIOSend(session, sendContext);
	}

	return true;
}

bool RUDPIOHandler::RecvIOCompleted(OUT IOContext* contextResult, const ULONG transferred, const BYTE threadId) const
{
	if (contextResult == nullptr || contextResult->session == nullptr)
	{
		LOG_ERROR("HandleRecvCompleted context or context->session is nullptr");
		return false;
	}

	if (lossSimulator != nullptr && lossSimulator->ShouldDropReceivedDatagram())
	{
		ReleaseRecvContext(contextResult);
		return DoRecv(*contextResult->session);
	}
	
	const auto buffer = NetBuffer::Alloc();
	if (buffer == nullptr)
	{
		LOG_ERROR("RecvIOCompleted NetBuffer::Allock() failed");
		ReleaseRecvContext(contextResult);
		
		return false;
	}
	
	if (memcpy_s(buffer->m_pSerializeBuffer, RECV_BUFFER_SIZE, contextResult->recvDataBuffer, transferred) != 0)
	{
		NetBuffer::Free(buffer);
		ReleaseRecvContext(contextResult);

		return false;
	}
	buffer->m_iWrite = static_cast<WORD>(transferred);

	if (not MultiSocketRUDPCoreFunctionDelegate::EnqueueContextResult(contextResult, buffer, threadId))
	{
		NetBuffer::Free(buffer);
		ReleaseRecvContext(contextResult);
		return false;
	}

	ReleaseRecvContext(contextResult);
	return DoRecv(*contextResult->session);
}

void RUDPIOHandler::ReleaseRecvContext(IOContext* context) const
{
	assert(context != nullptr);
	assert(context->ownerRecvBuffer != nullptr);

	RecvBuffer* recvBuffer = context->ownerRecvBuffer;
	recvBuffer->ReleaseRecvContext(context);
	recvBuffer->CompleteRecvIo();
}

bool RUDPIOHandler::SendIOCompleted(IOContext* context, const BYTE threadId) const
{
	if (context == nullptr || context->session == nullptr)
	{
		LOG_ERROR("HandleSendCompleted context or context->session is nullptr");
		return false;
	}

	sessionDelegate.GetSendIOMode(*context->session).store(IO_MODE::IO_NONE_SENDING);
	if (context->session->IsReleasing())
	{
		contextPool.Free(context);
		return true;
	}

	const bool result = DoSend(*context->session, threadId);
	contextPool.Free(context);
	return result;
}

bool RUDPIOHandler::TryRIOSend(RUDPSession& session, IOContext* context) const
{
	const auto releaseIOSending = [&]()
	{
		sessionDelegate.GetSendIOMode(session).store(IO_MODE::IO_NONE_SENDING);
	};

	context->session = &session;
	context->ownerSessionGeneration = session.GetSessionGeneration();

	{
		std::shared_lock lock(sessionDelegate.GetSocketMutex(session));
		if (sessionDelegate.GetSocket(session) == INVALID_SOCKET)
		{
			releaseIOSending();
			contextPool.Free(context);
			return false;
		}

		if (not rioManager.RIOSendEx(sessionDelegate.GetSendRIORQ(session)
			, context
			, 1
			, nullptr
			, &context->clientAddrRIOBuffer
			, nullptr
			, nullptr
			, 0
			, context))
		{
			LOG_ERROR(std::format("RIOSendEx() failed with error code {}", WSAGetLastError()));
			releaseIOSending();
			contextPool.Free(context);
			return false;
		}
	}

	return true;
}

std::pair<bool, IOContext*> RUDPIOHandler::MakeSendContext(RUDPSession& session, const ThreadIdType threadId) const
{
	IOContext* context = contextPool.Alloc();
	if (context == nullptr)
	{
		LOG_ERROR("MakeSendContext contextPool.Alloc() failed");
		return { false, nullptr };
	}

	context->InitContext(session.GetSessionId(), RIO_OPERATION_TYPE::OP_SEND);
	context->BufferId = sessionDelegate.GetSendBufferId(session);
	context->Offset = 0;
	const auto [succeeded, length] = MakeSendStream(session, threadId);
	if (not succeeded)
	{
		contextPool.Free(context);
		return { false, nullptr };
	}

	if (length == 0)
	{
		contextPool.Free(context);
		return { true, nullptr };
	}
	context->Length = length;

	if (context->clientAddrRIOBuffer.BufferId == RIO_INVALID_BUFFERID)
	{
		if (context->clientAddrRIOBuffer.BufferId = rioManager.RegisterRIOBuffer(context->clientAddrBuffer, sizeof(SOCKADDR_INET)); context->clientAddrRIOBuffer.BufferId == RIO_INVALID_BUFFERID)
		{
			LOG_ERROR("MakeSendContext clientAddrBufferId is RIO_INVALID_BUFFERID");
			contextPool.Free(context);
			return { false, nullptr };
		}
	}

	if (memcpy_s(context->clientAddrBuffer, sizeof(context->clientAddrBuffer), &session.GetSocketAddressInetRef(), sizeof(SOCKADDR_INET)) != NOERROR)
	{
		LOG_ERROR("MakeSendContext memcpy_s failed");
		contextPool.Free(context);
		return { false, nullptr };
	}

	context->clientAddrRIOBuffer.Length = sizeof(context->clientAddrBuffer);
	context->clientAddrRIOBuffer.Offset = 0;

	return { true, context };
}

std::pair<bool, unsigned int> RUDPIOHandler::MakeSendStream(RUDPSession& session, const ThreadIdType threadId) const
{
	auto& packetSequenceSet = sessionDelegate.GetCachedSequenceSet(session);
	packetSequenceSet.clear();
	
	unsigned int totalSendSize = 0;
	const size_t bufferCount = sessionDelegate.GetSendPacketInfoQueueSize(session);
	if (ReservedSendPacketInfoToStream(session, packetSequenceSet, totalSendSize, threadId) == SEND_PACKET_INFO_TO_STREAM_RETURN::OCCURED_ERROR)
	{
		return { false, 0 };
	}

	for (size_t i = 0; i < bufferCount; ++i)
	{
		switch (StoredSendPacketInfoToStream(session, packetSequenceSet, totalSendSize, threadId))
		{
		case SEND_PACKET_INFO_TO_STREAM_RETURN::OCCURED_ERROR:
		{
			return { false, 0 };
		}
		case SEND_PACKET_INFO_TO_STREAM_RETURN::STREAM_IS_FULL:
		{
			return { true, totalSendSize };
		}
		default:
			break;
		}
	}

	return { true, totalSendSize };
}

SEND_PACKET_INFO_TO_STREAM_RETURN RUDPIOHandler::ReservedSendPacketInfoToStream(RUDPSession& session, std::set<MultiSocketRUDP::PacketSequenceSetKey>& packetSequenceSet, unsigned int& totalSendSize, ThreadIdType threadId) const
{
	SendPacketInfo* sendPacketInfo = sessionDelegate.TakeReservedSendPacketInfo(session);
	if (sendPacketInfo == nullptr)
	{
		return SEND_PACKET_INFO_TO_STREAM_RETURN::SUCCESS;
	}

	const unsigned int useSize = sendPacketInfo->buffer->GetAllUseSize();
	if (useSize >= MAX_SEND_BUFFER_SIZE)
	{
		LOG_ERROR(std::format("MakeSendStream() : useSize must be less than MAX_SEND_BUFFER_SIZE. useSize: {}, MAX_SEND_BUFFER_SIZE: {}", useSize, MAX_SEND_BUFFER_SIZE));
		session.DoDisconnect(DISCONNECT_REASON::BY_ERROR);
		SendPacketInfo::Free(sendPacketInfo);
		return SEND_PACKET_INFO_TO_STREAM_RETURN::OCCURED_ERROR;
	}

	if (not RefreshRetransmissionSendPacketInfo(sendPacketInfo, threadId))
	{
		SendPacketInfo::Free(sendPacketInfo);
		return SEND_PACKET_INFO_TO_STREAM_RETURN::IS_ERASED_PACKET;
	}

	char* bufferPositionPointer = sessionDelegate.GetRIOSendBuffer(session);
	memcpy_s(bufferPositionPointer, MAX_SEND_BUFFER_SIZE, sendPacketInfo->buffer->GetBufferPtr(), useSize);
	packetSequenceSet.insert(MultiSocketRUDP::PacketSequenceSetKey{ sendPacketInfo->isReplyType, sendPacketInfo->sendPacketSequence });

	totalSendSize += static_cast<int>(useSize);

	SendPacketInfo::Free(sendPacketInfo);

	return SEND_PACKET_INFO_TO_STREAM_RETURN::SUCCESS;
}

SEND_PACKET_INFO_TO_STREAM_RETURN RUDPIOHandler::StoredSendPacketInfoToStream(RUDPSession& session, std::set<MultiSocketRUDP::PacketSequenceSetKey>& packetSequenceSet, unsigned int& totalSendSize, ThreadIdType threadId) const
{
	SendPacketInfo* sendPacketInfo = sessionDelegate.TryGetFrontAndPop(session);
	if (sendPacketInfo == nullptr)
	{
		return SEND_PACKET_INFO_TO_STREAM_RETURN::SUCCESS;
	}

	const MultiSocketRUDP::PacketSequenceSetKey key{ sendPacketInfo->isReplyType, sendPacketInfo->sendPacketSequence };
	if (packetSequenceSet.contains(key) == true)
	{
		SendPacketInfo::Free(sendPacketInfo);
		return SEND_PACKET_INFO_TO_STREAM_RETURN::IS_SENT;
	}

	const unsigned int useSize = sendPacketInfo->buffer->GetAllUseSize();
	if (useSize > MAX_SEND_BUFFER_SIZE || useSize == 0)
	{
		LOG_ERROR(std::format("MakeSendStream() : useSize is invalid. useSize: {}, MAX_SEND_BUFFER_SIZE: {}", useSize, MAX_SEND_BUFFER_SIZE));
		session.DoDisconnect(DISCONNECT_REASON::BY_ERROR);
		SendPacketInfo::Free(sendPacketInfo);
		return SEND_PACKET_INFO_TO_STREAM_RETURN::OCCURED_ERROR;
	}

	const unsigned int beforeSendSize = totalSendSize;
	if (beforeSendSize + useSize > MAX_SEND_BUFFER_SIZE)
	{
		sessionDelegate.SetReservedSendPacketInfo(session, sendPacketInfo);
		return SEND_PACKET_INFO_TO_STREAM_RETURN::STREAM_IS_FULL;
	}

	totalSendSize += useSize;
	if (not RefreshRetransmissionSendPacketInfo(sendPacketInfo, threadId))
	{
		SendPacketInfo::Free(sendPacketInfo);
		return SEND_PACKET_INFO_TO_STREAM_RETURN::IS_ERASED_PACKET;
	}

	packetSequenceSet.insert(key);
	memcpy_s(&sessionDelegate.GetRIOSendBuffer(session)[beforeSendSize]
		, MAX_SEND_BUFFER_SIZE - beforeSendSize
		, sendPacketInfo->buffer->GetBufferPtr()
		, useSize);

	SendPacketInfo::Free(sendPacketInfo);

	return SEND_PACKET_INFO_TO_STREAM_RETURN::SUCCESS;
}

bool RUDPIOHandler::RefreshRetransmissionSendPacketInfo(SendPacketInfo* sendPacketInfo, ThreadIdType threadId) const
{
	if (sendPacketInfo->isErasedPacketInfo.load(std::memory_order_acquire))
	{
		return false;
	}

	if (sendPacketInfo->isReplyType == true)
	{
		return true;
	}

	auto& scheduler = *retransmissionSchedulers[threadId];
	const auto now = std::chrono::steady_clock::now();
	const unsigned int rtoMs = sendPacketInfo->IsOwnerValid()
		? sendPacketInfo->owner->GetRetransmissionTimeoutMs()
		: retransmissionMs;
	const auto deadline = now + std::chrono::milliseconds(rtoMs);
	sendPacketInfo->MarkSentForRttSample(now);
	{
		std::scoped_lock lock(scheduler.lock);
		if (sendPacketInfo->isErasedPacketInfo.load(std::memory_order_acquire))
		{
			return false;
		}

		PushRetransmissionSchedule(scheduler, *sendPacketInfo, deadline);
	}

	if (not SignalRetransmissionWakeEvent(scheduler))
	{
		LOG_ERROR(std::format("Retransmission wake event signal failed. error is {}", GetLastError()));
	}

	return true;
}
