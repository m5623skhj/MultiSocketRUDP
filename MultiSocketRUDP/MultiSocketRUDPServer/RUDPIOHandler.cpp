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
#include "SendPacketInfo.h"

RUDPIOHandler::RUDPIOHandler(IRIOManager& inRioManager
	, ISessionDelegate& inSessionDelegate
	, CTLSMemoryPool<IOContext>& contextPool
	, std::vector<std::list<SendPacketInfo*>>& sendPacketInfoList
	, std::vector<std::unique_ptr<std::mutex>>& sendPacketInfoListLock
	, const BYTE inMaxHoldingPacketQueueSize
	, const unsigned int inRetransmissionMs)
	: rioManager(inRioManager)
	, sessionDelegate(inSessionDelegate)
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
	const auto context = sessionDelegate.GetRecvBufferContext(session);
	if (context == nullptr)
	{
		LOG_ERROR("DoRecv context is nullptr");
		return false;
	}

	{
		std::shared_lock lock(sessionDelegate.GetSocketMutex(session));
		if (sessionDelegate.GetSocket(session) == INVALID_SOCKET)
		{
			return false;
		}

		if (not rioManager.RIOReceiveEx(sessionDelegate.GetRecvRIORQ(session)
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
	if (session.IsReleasing())
	{
		return true;
	}

	const auto releaseIOSending = [&]()
	{
		InterlockedExchange(
			reinterpret_cast<UINT*>(&sessionDelegate.GetSendIOMode(session)),
			static_cast<UINT>(IO_MODE::IO_NONE_SENDING));
	};

	while (true)
	{
		if (InterlockedCompareExchange(reinterpret_cast<UINT*>(&sessionDelegate.GetSendIOMode(session))
			, static_cast<UINT>(IO_MODE::IO_SENDING)
			, static_cast<UINT>(IO_MODE::IO_NONE_SENDING)))
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
	if (memcpy_s(buffer->m_pSerializeBuffer, RECV_BUFFER_SIZE, sessionDelegate.GetRecvBuffer(*contextResult->session).buffer, transferred) != 0)
	{
		NetBuffer::Free(buffer);
		return false;
	}
	buffer->m_iWrite = static_cast<WORD>(transferred);

	sessionDelegate.EnqueueToRecvBufferList(*contextResult->session, buffer);
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

	InterlockedExchange(reinterpret_cast<UINT*>(&sessionDelegate.GetSendIOMode(*context->session)), static_cast<UINT>(IO_MODE::IO_NONE_SENDING));

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
		InterlockedExchange(
			reinterpret_cast<UINT*>(&sessionDelegate.GetSendIOMode(session)),
			static_cast<UINT>(IO_MODE::IO_NONE_SENDING));
	};

	context->session = &session;

	{
		std::shared_lock lock(sessionDelegate.GetSocketMutex(session));
		if (sessionDelegate.GetSocket(session) == INVALID_SOCKET)
		{
			releaseIOSending();
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
	std::scoped_lock cachedSequenceLock(sessionDelegate.GetCachedSequenceSetMutex(session));
	auto& packetSequenceSet = sessionDelegate.GetCachedSequenceSet(session);
	packetSequenceSet.clear();
	
	unsigned int totalSendSize = 0;
	const size_t bufferCount = sessionDelegate.GetSendPacketInfoQueueSize(session);
	if (sessionDelegate.GetReservedSendPacketInfo(session) != nullptr)
	{
		if (ReservedSendPacketInfoToStream(session, packetSequenceSet, totalSendSize, threadId) == SEND_PACKET_INFO_TO_STREAM_RETURN::OCCURED_ERROR)
		{
			return { false, 0 };
		}
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
	SendPacketInfo* sendPacketInfo = sessionDelegate.GetReservedSendPacketInfo(session);
	const unsigned int useSize = sendPacketInfo->buffer->GetAllUseSize();
	if (useSize >= MAX_SEND_BUFFER_SIZE)
	{
		LOG_ERROR(std::format("MakeSendStream() : useSize is less than MAX_SEND_BUFFER_SIZE. useSize: {}, MAX_SEND_BUFFER_SIZE: {}", useSize, MAX_SEND_BUFFER_SIZE));
		session.DoDisconnect();
		SendPacketInfo::Free(sendPacketInfo);
		sessionDelegate.SetReservedSendPacketInfo(session, nullptr);
		return SEND_PACKET_INFO_TO_STREAM_RETURN::OCCURED_ERROR;
	}

	if (not RefreshRetransmissionSendPacketInfo(sendPacketInfo, threadId))
	{
		SendPacketInfo::Free(sendPacketInfo);
		sessionDelegate.SetReservedSendPacketInfo(session, nullptr);
		return SEND_PACKET_INFO_TO_STREAM_RETURN::IS_ERASED_PACKET;
	}

	char* bufferPositionPointer = sessionDelegate.GetRIOSendBuffer(session);
	memcpy_s(bufferPositionPointer, MAX_SEND_BUFFER_SIZE, sendPacketInfo->buffer->GetBufferPtr(), useSize);
	packetSequenceSet.insert(MultiSocketRUDP::PacketSequenceSetKey{ sendPacketInfo->isReplyType, sendPacketInfo->sendPacketSequence });

	totalSendSize += static_cast<int>(useSize);

	SendPacketInfo::Free(sessionDelegate.GetReservedSendPacketInfo(session));
	sessionDelegate.SetReservedSendPacketInfo(session, nullptr);

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
		session.DoDisconnect();
		SendPacketInfo::Free(sendPacketInfo);
		return SEND_PACKET_INFO_TO_STREAM_RETURN::OCCURED_ERROR;
	}

	const unsigned int beforeSendSize = totalSendSize;
	totalSendSize += useSize;
	if (totalSendSize >= MAX_SEND_BUFFER_SIZE)
	{
		sessionDelegate.SetReservedSendPacketInfo(session, sendPacketInfo);
		return SEND_PACKET_INFO_TO_STREAM_RETURN::STREAM_IS_FULL;
	}

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
