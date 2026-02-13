#include "PreCompile.h"
#include "RUDPIOHandler.h"
#include "RIOManager.h"
#include "RUDPSession.h"
#include "Logger.h"
#include "RUDPSessionFunctionDelegate.h"
#include "LogExtension.h"
#include "IOContext.h"
#include "MultiSocketRUDPCore.h"
#include "MultiSocketRUDPCoreFuntionDeletage.h"

RUDPIOHandler::RUDPIOHandler(RIOManager& rioManager
                             , CTLSMemoryPool<IOContext>& contextPool
                             , std::vector<std::list<SendPacketInfo*>>& sendPacketInfoList
                             , std::vector<std::unique_ptr<std::mutex>>& sendPacketInfoListLock
                             , const BYTE inMaxHoldingPacketQueueSize
                             , const unsigned int inRetransmissionMs)
	: rioManager(rioManager)
	, contextPool(contextPool)
	, sendPacketInfoList(sendPacketInfoList)
	, sendPacketInfoListLock(sendPacketInfoListLock)
	, maximumHoldingPacketQueueSize(inMaxHoldingPacketQueueSize)
	, retransmissionMs(inRetransmissionMs)
{
}

bool RUDPIOHandler::IOCompleted(IOContext* context, const ULONG transferred, const BYTE threadId) const
{
	if (context == nullptr)
	{
		LOG_ERROR("IOCompleted context is nullptr");
		return false;
	}

	switch (context->ioType)
	{
	case RIO_OPERATION_TYPE::OP_RECV:
	{
		if (not RecvIOCompleted(context, transferred, threadId))
		{
			context->session->DoDisconnect();
			break;
		}

		return true;
	}
	case RIO_OPERATION_TYPE::OP_SEND:
	{
		if (not SendIOCompleted(context, threadId))
		{
			context->session->DoDisconnect();
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

bool RUDPIOHandler::DoRecv(const RUDPSession& session) const
{
	const auto context = RUDPSessionFunctionDelegate::GetRecvBufferContext(session);
	if (context == nullptr)
	{
		LOG_ERROR("DoRecv context is nullptr");
		return false;
	}

	{
		std::shared_lock lock(RUDPSessionFunctionDelegate::GetSocketMutex(session));
		if (RUDPSessionFunctionDelegate::GetSocket(session) == INVALID_SOCKET)
		{
			return false;
		}

		if (not rioManager.RIOReceiveEx(RUDPSessionFunctionDelegate::GetRecvRIORQ(session)
			, context.get()
			, 1
			, &context->localAddrRIOBuffer
			, &context->clientAddrRIOBuffer
			, nullptr
			, nullptr
			, 0
			, context.get()))
		{
			return false;
		}
	}

	return true;
}

bool RUDPIOHandler::DoSend(RUDPSession& session, const ThreadIdType threadId) const
{
	while (true)
	{
		if (InterlockedCompareExchange(reinterpret_cast<UINT*>(&RUDPSessionFunctionDelegate::GetSendIOMode(session))
			, static_cast<UINT>(IO_MODE::IO_SENDING)
			, static_cast<UINT>(IO_MODE::IO_NONE_SENDING)))
		{
			break;
		}

		{
			std::scoped_lock lock(RUDPSessionFunctionDelegate::AcquireSendPacketInfoQueueLock(session));
			if (RUDPSessionFunctionDelegate::IsSendPacketInfoQueueEmpty(session) &&
				RUDPSessionFunctionDelegate::GetReservedSendPacketInfo(session) == nullptr)
			{
				InterlockedExchange(reinterpret_cast<UINT*>(&RUDPSessionFunctionDelegate::GetSendIOMode(session)), static_cast<UINT>(IO_MODE::IO_NONE_SENDING));
				if (not RUDPSessionFunctionDelegate::IsSendPacketInfoQueueEmpty(session))
				{
					continue;
				}
				break;
			}
		}

		IOContext* sendContext = MakeSendContext(session, threadId);
		if (sendContext == nullptr)
		{
			return false;
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

	const auto buffer = NetBuffer::Alloc();
	if (memcpy_s(buffer->m_pSerializeBuffer, RECV_BUFFER_SIZE, RUDPSessionFunctionDelegate::GetRecvBuffer(*contextResult->session).buffer, transferred) != 0)
	{
		NetBuffer::Free(buffer);
		return false;
	}
	buffer->m_iWrite = static_cast<WORD>(transferred);

	RUDPSessionFunctionDelegate::EnqueueToRecvBufferList(*contextResult->session, buffer);
	MultiSocketRUDPCoreFunctionDelegate::EnqueueContextResult(contextResult, threadId);
	return DoRecv(*contextResult->session);
}

bool RUDPIOHandler::SendIOCompleted(IOContext* context, const BYTE threadId) const
{
	if (context == nullptr || context->session == nullptr)
	{
		LOG_ERROR("HandleSendCompleted context or context->session is nullptr");
		return false;
	}

	InterlockedExchange(reinterpret_cast<UINT*>(&RUDPSessionFunctionDelegate::GetSendIOMode(*context->session)), static_cast<UINT>(IO_MODE::IO_NONE_SENDING));

	const bool result = DoSend(*context->session, threadId);
	contextPool.Free(context);
	return result;
}

bool RUDPIOHandler::TryRIOSend(RUDPSession& session, IOContext* context) const
{
	context->session = &session;

	{
		std::shared_lock lock(RUDPSessionFunctionDelegate::GetSocketMutex(session));
		if (RUDPSessionFunctionDelegate::GetSocket(session) == INVALID_SOCKET)
		{
			return false;
		}

		if (not rioManager.RIOSendEx(RUDPSessionFunctionDelegate::GetSendRIORQ(session)
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
			contextPool.Free(context);
			return false;
		}
	}

	return true;
}

IOContext* RUDPIOHandler::MakeSendContext(RUDPSession& session, const ThreadIdType threadId) const
{
	IOContext* context = contextPool.Alloc();
	if (context == nullptr)
	{
		LOG_ERROR("MakeSendContext contextPool.Alloc() failed");
		return nullptr;
	}

	context->InitContext(session.GetSessionId(), RIO_OPERATION_TYPE::OP_SEND);
	context->BufferId = RUDPSessionFunctionDelegate::GetSendBufferId(session);
	context->Offset = 0;
	context->Length = MakeSendStream(session, context, threadId);
	if (context->Length == 0)
	{
		contextPool.Free(context);
		return nullptr;
	}

	if (context->clientAddrRIOBuffer.BufferId == RIO_INVALID_BUFFERID)
	{
		if (context->clientAddrRIOBuffer.BufferId = rioManager.RegisterRIOBuffer(context->clientAddrBuffer, sizeof(SOCKADDR_INET)); context->clientAddrRIOBuffer.BufferId == RIO_INVALID_BUFFERID)
		{
			LOG_ERROR("MakeSendContext clientAddrBufferId is RIO_INVALID_BUFFERID");
			contextPool.Free(context);
			return nullptr;
		}
	}

	if (memcpy_s(context->clientAddrBuffer, sizeof(context->clientAddrBuffer), &session.GetSocketAddressInetRef(), sizeof(SOCKADDR_INET)) != NOERROR)
	{
		LOG_ERROR("MakeSendContext memcpy_s failed");
		contextPool.Free(context);
		return nullptr;
	}

	context->clientAddrRIOBuffer.Length = sizeof(context->clientAddrBuffer);
	context->clientAddrRIOBuffer.Offset = 0;

	return context;
}

unsigned int RUDPIOHandler::MakeSendStream(RUDPSession& session, IOContext* context, const ThreadIdType threadId) const
{
	std::scoped_lock lock(RUDPSessionFunctionDelegate::GetCachedSequenceSetMutex(session));
	auto& packetSequenceSet = RUDPSessionFunctionDelegate::GetCachedSequenceSet(session);
	packetSequenceSet.clear();
	
	unsigned int totalSendSize = 0;
	size_t bufferCount;
	{
		std::scoped_lock lock(RUDPSessionFunctionDelegate::AcquireSendPacketInfoQueueLock(session));
		bufferCount = RUDPSessionFunctionDelegate::GetSendPacketInfoQueueSize(session);
	}

	if (RUDPSessionFunctionDelegate::GetReservedSendPacketInfo(session) != nullptr)
	{
		if (ReservedSendPacketInfoToStream(session, packetSequenceSet, totalSendSize, threadId) == SEND_PACKET_INFO_TO_STREAM_RETURN::OCCURED_ERROR)
		{
			return 0;
		}
	}

	for (size_t i = 0; i < bufferCount; ++i)
	{
		switch (StoredSendPacketInfoToStream(session, packetSequenceSet, totalSendSize, threadId))
		{
		case SEND_PACKET_INFO_TO_STREAM_RETURN::OCCURED_ERROR:
		{
			return 0;
		}
		case SEND_PACKET_INFO_TO_STREAM_RETURN::STREAM_IS_FULL:
		{
			return totalSendSize;
		}
		default:
			break;
		}
	}

	return totalSendSize;
}

SEND_PACKET_INFO_TO_STREAM_RETURN RUDPIOHandler::ReservedSendPacketInfoToStream(RUDPSession& session, std::set<MultiSocketRUDP::PacketSequenceSetKey>& packetSequenceSet, unsigned int& totalSendSize, ThreadIdType threadId) const
{
	SendPacketInfo* sendPacketInfo = RUDPSessionFunctionDelegate::GetReservedSendPacketInfo(session);
	const unsigned int useSize = sendPacketInfo->buffer->GetAllUseSize();
	if (useSize >= MAX_SEND_BUFFER_SIZE)
	{
		LOG_ERROR(std::format("MakeSendStream() : useSize is less than MAX_SEND_BUFFER_SIZE. useSize: {}, MAX_SEND_BUFFER_SIZE: {}", useSize, MAX_SEND_BUFFER_SIZE));
		session.DoDisconnect();

		return SEND_PACKET_INFO_TO_STREAM_RETURN::OCCURED_ERROR;
	}

	if (not RefreshRetransmissionSendPacketInfo(sendPacketInfo, threadId))
	{
		return SEND_PACKET_INFO_TO_STREAM_RETURN::IS_ERASED_PACKET;
	}

	char* bufferPositionPointer = RUDPSessionFunctionDelegate::GetRIOSendBuffer(session);
	memcpy_s(bufferPositionPointer, MAX_SEND_BUFFER_SIZE, sendPacketInfo->buffer->GetBufferPtr(), useSize);
	packetSequenceSet.insert(MultiSocketRUDP::PacketSequenceSetKey{ sendPacketInfo->isReplyType, sendPacketInfo->sendPacketSequence });

	totalSendSize += static_cast<int>(useSize);
	if (sendPacketInfo->isReplyType == true)
	{
		SendPacketInfo::Free(RUDPSessionFunctionDelegate::GetReservedSendPacketInfo(session));
	}
	RUDPSessionFunctionDelegate::SetReservedSendPacketInfo(session, nullptr);

	return SEND_PACKET_INFO_TO_STREAM_RETURN::SUCCESS;
}

SEND_PACKET_INFO_TO_STREAM_RETURN RUDPIOHandler::StoredSendPacketInfoToStream(RUDPSession& session, std::set<MultiSocketRUDP::PacketSequenceSetKey>& packetSequenceSet, unsigned int& totalSendSize, ThreadIdType threadId) const
{
	SendPacketInfo* sendPacketInfo;
	{
		std::scoped_lock lock(RUDPSessionFunctionDelegate::AcquireSendPacketInfoQueueLock(session));
		if (RUDPSessionFunctionDelegate::IsSendPacketInfoQueueEmpty(session) == true)
		{
			return SEND_PACKET_INFO_TO_STREAM_RETURN::SUCCESS;
		}

		sendPacketInfo = RUDPSessionFunctionDelegate::GetSendPacketInfoQueueFrontAndPop(session);
	}

	const MultiSocketRUDP::PacketSequenceSetKey key{ sendPacketInfo->isReplyType, sendPacketInfo->sendPacketSequence };
	if (packetSequenceSet.contains(key) == true)
	{
		return SEND_PACKET_INFO_TO_STREAM_RETURN::IS_SENT;
	}

	const unsigned int useSize = sendPacketInfo->buffer->GetAllUseSize();
	if (useSize > MAX_SEND_BUFFER_SIZE || useSize == 0)
	{
		LOG_ERROR(std::format("MakeSendStream() : useSize is invalid. useSize: {}, MAX_SEND_BUFFER_SIZE: {}", useSize, MAX_SEND_BUFFER_SIZE));
		session.DoDisconnect();

		return SEND_PACKET_INFO_TO_STREAM_RETURN::OCCURED_ERROR;
	}

	const unsigned int beforeSendSize = totalSendSize;
	totalSendSize += useSize;
	if (totalSendSize >= MAX_SEND_BUFFER_SIZE)
	{
		RUDPSessionFunctionDelegate::SetReservedSendPacketInfo(session, sendPacketInfo);
		return SEND_PACKET_INFO_TO_STREAM_RETURN::STREAM_IS_FULL;
	}

	if (not RefreshRetransmissionSendPacketInfo(sendPacketInfo, threadId))
	{
		return SEND_PACKET_INFO_TO_STREAM_RETURN::IS_ERASED_PACKET;
	}

	packetSequenceSet.insert(key);
	memcpy_s(&RUDPSessionFunctionDelegate::GetRIOSendBuffer(session)[beforeSendSize]
		, MAX_SEND_BUFFER_SIZE - beforeSendSize
		, sendPacketInfo->buffer->GetBufferPtr()
		, useSize);

	if (sendPacketInfo->isReplyType == true)
	{
		SendPacketInfo::Free(sendPacketInfo);
	}

	return SEND_PACKET_INFO_TO_STREAM_RETURN::SUCCESS;
}

bool RUDPIOHandler::RefreshRetransmissionSendPacketInfo(SendPacketInfo* sendPacketInfo, ThreadIdType threadId) const
{
	if (sendPacketInfo->isReplyType == true)
	{
		return true;
	}

	sendPacketInfo->retransmissionTimeStamp = GetTickCount64() + retransmissionMs;
	{
		std::scoped_lock lock(*sendPacketInfoListLock[threadId]);
		if (sendPacketInfo->isErasedPacketInfo == true)
		{
			sendPacketInfo = nullptr;
			return false;
		}

		if (sendPacketInfo->retransmissionCount > 0)
		{
			sendPacketInfoList[threadId].erase(sendPacketInfo->listItor);
		}
		sendPacketInfo->listItor = sendPacketInfoList[threadId].emplace(sendPacketInfoList[threadId].end(), sendPacketInfo);
	}

	return true;
}
