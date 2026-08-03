#include "PreCompile.h"
#include "MultiSocketRUDPCore.h"
#include "LogExtension.h"
#include "Logger.h"
#include "RUDPIOHandler.h"
#include "RIOManager.h"
#include "RUDPSession.h"
#include "RUDPSessionManager.h"
#include "SendPacketInfo.h"
#include <chrono>
#include "BuildConfig.h"

namespace
{
	/**
	* @brief 재전송 타이머를 설정합니다. 지정된 기한까지 타이머를 무장시키거나,
	*        기한이 현재 시간보다 이르다면 즉시 신호를 발생시키도록 설정합니다.
	* @param timerHandle 설정할 타이머 객체의 핸들입니다.
	* @param deadline 타이머가 만료될 절대 시간입니다.
	* @return 타이머 설정 성공 여부를 반환합니다.
	* @retval true 타이머가 성공적으로 설정되었습니다.
	* @retval false SetWaitableTimer 호출이 실패했습니다. (오류 로깅)
	*
	* @note 타이머 만료 시간(dueTime)은 100나노초 단위로 계산됩니다.
	*       deadline이 현재 시간보다 이전이거나 같으면 타이머는 즉시 만료되도록 설정됩니다.
	*       타이머 설정 실패 시 `GetLastError()`를 통해 오류 코드가 로깅됩니다.
	*       타이머 설정 실패는 WaitableTimer가 작동하지 않거나 예상치 못한 동작을 야기할 수 있습니다.
	*/
	[[nodiscard]]
	bool ArmRetransmissionTimer(const HANDLE timerHandle, const std::chrono::steady_clock::time_point deadline)
	{
		const auto now = std::chrono::steady_clock::now();

		LARGE_INTEGER dueTime;
		if (deadline <= now)
		{
			dueTime.QuadPart = -1;
		}
		else
		{
			using HundredNanoseconds = std::chrono::duration<long long, std::ratio<1, 10'000'000>>;
			const long long remaining = std::chrono::duration_cast<HundredNanoseconds>(deadline - now).count();
			dueTime.QuadPart = -(remaining > 0 ? remaining : 1);
		}

		if (SetWaitableTimer(timerHandle, &dueTime, 0, nullptr, nullptr, FALSE) == FALSE)
		{
			LOG_ERROR(std::format("SetWaitableTimer failed. error is {}", GetLastError()));
			return false;
		}

		return true;
	}

	FORCEINLINE void SleepRemainingFrameTime(OUT TickSet& tickSet, const unsigned int intervalMs)
	{
		const UINT64 now = GetTickCount64();
		if (const UINT64 delta = now - tickSet.nowTick; delta < intervalMs)
		{
			Sleep(static_cast<DWORD>(intervalMs - delta));
		}

		tickSet.nowTick = GetTickCount64();
	}
}

void MultiSocketRUDPCore::RunIOWorkerThread(const std::stop_token& stopToken, const ThreadIdType threadId)
{
	TickSet tickSet;
	tickSet.nowTick = GetTickCount64();

	while (not stopToken.stop_requested())
	{
		RIORESULT rioResults[MAX_RIO_RESULT];

		const ULONG numOfResults = rioManager->DequeueCompletions(threadId, rioResults, MAX_RIO_RESULT);
		if (numOfResults == RIO_CORRUPT_CQ)
		{
			LOG_ERROR(std::format(
				"Fatal RIO completion queue corruption detected on worker {}. "
				"I/O processing cannot continue safely; server restart is required.",
				threadId));
			ReportFatalError({ SERVER_FATAL_ERROR_CODE::RIO_COMPLETION_QUEUE_CORRUPT, threadId, 0 });
			return;
		}
		for (ULONG i = 0; i < numOfResults; ++i)
		{
			const auto context = reinterpret_cast<IOContext*>(rioResults[i].RequestContext);
			if (context == nullptr)
			{
				continue;
			}

			const auto ioType = context->ioType;
			if (not ioHandler->IOCompleted(context, rioResults[i].BytesTransferred, threadId, rioResults[i].Status))
			{
				LOG_ERROR(std::format("IOCompleted() failed with io type {}", static_cast<INT8>(ioType)));
			}
		}

#if USE_IO_WORKER_THREAD_SLEEP_FOR_FRAME == USE_WORKER_THREAD_SLEEP_FOR_FRAME
		SleepRemainingFrameTime(tickSet, workerThreadOneFrameMs);
#elif USE_IO_WORKER_THREAD_SLEEP_FOR_FRAME == USE_WORKER_THREAD_SLEEP_ZERO
		Sleep(0);
#endif
	}

	const auto log = Logger::MakeLogObject<ServerLog>();
	log->logString = "Worker thread stopped";
	Logger::GetInstance().WriteLog(log);
}

void MultiSocketRUDPCore::RunRecvLogicWorkerThread(const std::stop_token& stopToken, const ThreadIdType threadId)
{
	const HANDLE eventHandles[2] = { recvLogicThreadEventHandles[threadId], recvLogicThreadEventStopHandle };
	while (not stopToken.stop_requested())
	{
		switch (WaitForMultipleObjects(2, eventHandles, FALSE, INFINITE))
		{
		case WAIT_OBJECT_0:
			OnRecvPacket(threadId);
			break;
		case WAIT_OBJECT_0 + 1:
			Sleep(LOGIC_THREAD_STOP_SLEEP_TIME);
			OnRecvPacket(threadId);
			{
				const auto log = Logger::MakeLogObject<ServerLog>();
				log->logString = std::format("Logic thread stop. ThreadId is {}", threadId);
				Logger::GetInstance().WriteLog(log);
			}
			return;
		case WAIT_FAILED:
		{
			const unsigned long errorCode = GetLastError();
			LOG_ERROR(std::format("Recv logic wait failed. error is {}", errorCode));
			ReportFatalError({ SERVER_FATAL_ERROR_CODE::RECV_LOGIC_WAIT_FAILED, threadId, errorCode });
			return;
		}
		default:
		{
			LOG_ERROR("Recv logic wait returned an unexpected result");
			return;
		}
		}
	}
}

void MultiSocketRUDPCore::RunHeartbeatThread(const std::stop_token& stopToken) const
{
	TickSet tickSet;
	tickSet.nowTick = GetTickCount64();

	while (not stopToken.stop_requested())
	{
		sessionManager->HeartbeatCheck(GetTickCount64());
		SleepRemainingFrameTime(tickSet, heartbeatThreadSleepMs);
	}
}

void MultiSocketRUDPCore::RunRetransmissionThread(const std::stop_token& stopToken, const ThreadIdType threadId)
{
	auto& scheduler = *retransmissionSchedulers[threadId];
	const HANDLE waitHandles[3] = { scheduler.timerHandle, scheduler.wakeEventHandle, retransmissionStopEventHandle };
	const HANDLE emptyWaitHandles[2] = { scheduler.wakeEventHandle, retransmissionStopEventHandle };

	std::vector<SendPacketInfo*> dueList;
	while (not stopToken.stop_requested())
	{
		dueList.clear();
		bool hasNext = false;
		std::chrono::steady_clock::time_point nextDeadline{};
		{
			std::scoped_lock lock(scheduler.lock);
			const auto now = std::chrono::steady_clock::now();
			while (not scheduler.heap.empty())
			{
				const RetransmissionHeapEntry& top = scheduler.heap.top();
				if (top.info->isErasedPacketInfo.load(std::memory_order_acquire) || top.version != top.info->scheduleVersion)
				{
					SendPacketInfo* staleInfo = top.info;
					scheduler.heap.pop();
					SendPacketInfo::Free(staleInfo);
					continue;
				}

				if (top.deadline > now)
				{
					hasNext = true;
					nextDeadline = top.deadline;
					break;
				}

				top.info->InvalidateRttSample();
				dueList.push_back(top.info);
				scheduler.heap.pop();
			}
		}

		for (auto* sendPacketInfo : dueList)
		{
			ProcessRetransmission(sendPacketInfo, threadId);
		}

		if (not dueList.empty())
		{
			continue;
		}

		if (hasNext)
		{
			if (not ArmRetransmissionTimer(scheduler.timerHandle, nextDeadline))
			{
				Sleep(1);
				continue;
			}

			const DWORD waitResult = WaitForMultipleObjects(3, waitHandles, FALSE, INFINITE);
			if (waitResult == WAIT_OBJECT_0 + 2)
			{
				break;
			}
			if (waitResult == WAIT_FAILED)
			{
				LOG_ERROR(std::format("Retransmission wait failed. error is {}", GetLastError()));
				break;
			}
			if (waitResult != WAIT_OBJECT_0 && waitResult != WAIT_OBJECT_0 + 1)
			{
				LOG_ERROR(std::format("Retransmission wait returned unexpected result {}", waitResult));
				break;
			}
		}
		else
		{
			const DWORD waitResult = WaitForMultipleObjects(2, emptyWaitHandles, FALSE, INFINITE);
			if (waitResult == WAIT_OBJECT_0 + 1)
			{
				break;
			}
			if (waitResult == WAIT_FAILED)
			{
				LOG_ERROR(std::format("Empty retransmission wait failed. error is {}", GetLastError()));
				break;
			}
			if (waitResult != WAIT_OBJECT_0)
			{
				LOG_ERROR(std::format("Empty retransmission wait returned unexpected result {}", waitResult));
				break;
			}
		}
	}

	{
		std::scoped_lock lock(scheduler.lock);
		while (not scheduler.heap.empty())
		{
			SendPacketInfo* info = scheduler.heap.top().info;
			scheduler.heap.pop();
			SendPacketInfo::Free(info);
		}
	}
}

void MultiSocketRUDPCore::ProcessRetransmission(SendPacketInfo* sendPacketInfo, const ThreadIdType threadId)
{
	auto& scheduler = *retransmissionSchedulers[threadId];

	bool shouldDisconnect = false;
	{
		std::scoped_lock lock(scheduler.lock);
		if (sendPacketInfo->isErasedPacketInfo.load(std::memory_order_acquire))
		{
			SendPacketInfo::Free(sendPacketInfo);
			return;
		}

		if (++sendPacketInfo->retransmissionCount >= maxPacketRetransmissionCount)
		{
			shouldDisconnect = true;
		}
	}

	if (shouldDisconnect)
	{
		if (sendPacketInfo->IsOwnerValid())
		{
			sendPacketInfo->owner->DoDisconnect(DISCONNECT_REASON::BY_RETRANSMISSION);
		}

		SendPacketInfo::Free(sendPacketInfo);
		return;
	}

	if (sendPacketInfo->IsOwnerValid())
	{
		sendPacketInfo->owner->OnRetransmissionTimeout();
	}

	if (not SendPacket(sendPacketInfo) && sendPacketInfo->IsOwnerValid())
	{
		sendPacketInfo->owner->DoDisconnect(DISCONNECT_REASON::BY_ERROR);
	}

	SendPacketInfo::Free(sendPacketInfo);
}

void MultiSocketRUDPCore::RunSessionReleaseThread(const std::stop_token& stopToken)
{
	const HANDLE eventHandles[2] = { sessionReleaseEventHandle, sessionReleaseStopEventHandle };
	while (not stopToken.stop_requested())
	{
		const auto now = GetTickCount64();

		switch (WaitForMultipleObjects(2, eventHandles, FALSE, INFINITE))
		{
		case WAIT_OBJECT_0:
		{
			std::vector<SessionIdType> remainList;
			for (const auto releaseSessionId : TakeReleaseSessionIds())
			{
				if (not TryFinalizeSessionRelease(releaseSessionId, now))
				{
					remainList.emplace_back(releaseSessionId);
				}
			}
			RequeueReleaseSessionIds(remainList);
		}
		break;
		case WAIT_OBJECT_0 + 1:
		{
			const auto log = Logger::MakeLogObject<ServerLog>();
			log->logString = "Session release thread stop.";
			Logger::GetInstance().WriteLog(log);
			return;
		}
		default:
		{
			LOG_ERROR(std::format("RunSessionReleaseThread() : Invalid session release thread wait result. Error is {}", WSAGetLastError()));
		}
		break;
		}
	}
}

std::vector<SessionIdType> MultiSocketRUDPCore::TakeReleaseSessionIds()
{
	std::scoped_lock lock(releaseSessionIdListLock);
	std::vector<SessionIdType> sessionIds(releaseSessionIdList.begin(), releaseSessionIdList.end());
	releaseSessionIdList.clear();
	return sessionIds;
}

bool MultiSocketRUDPCore::TryFinalizeSessionRelease(const SessionIdType sessionId, const unsigned long long now)
{
	const auto releaseSession = GetReleasingSession(sessionId);
	if (releaseSession == nullptr)
	{
		return true;
	}

	releaseSession->BeginIOShutdown();
	if (releaseSession->CanFinalizeIO())
	{
		releaseSession->Disconnect();
		return true;
	}

	LogSessionReleaseStall(*releaseSession, sessionId, now);
	return false;
}

void MultiSocketRUDPCore::LogSessionReleaseStall(
	RUDPSession& session,
	const SessionIdType sessionId,
	const unsigned long long now)
{
	static unsigned long long constexpr RELEASE_STALL_WARNING_MS = 10000;
	if ((session.onSessionReleaseTime + RELEASE_STALL_WARNING_MS) > now)
	{
		return;
	}

	const auto& recvBuffer = session.rioContext.GetRecvBuffer();
	LOG_ERROR(std::format(
		"Session {} release is waiting for RIO drain. recvIo={}, recvLogic={}, sendMode={}",
		sessionId,
		recvBuffer.outstandingRecvIo.load(std::memory_order_acquire),
		recvBuffer.pendingRecvLogic.load(std::memory_order_acquire),
		static_cast<unsigned int>(session.GetSendContext().GetIOMode().load(std::memory_order_acquire))));
	session.onSessionReleaseTime = now;
}

void MultiSocketRUDPCore::RequeueReleaseSessionIds(const std::vector<SessionIdType>& sessionIds)
{
	if (sessionIds.empty())
	{
		return;
	}

	Sleep(1);
	std::scoped_lock lock(releaseSessionIdListLock);
	for (const auto sessionId : sessionIds)
	{
		releaseSessionIdList.emplace_back(sessionId);
	}
	SetEvent(sessionReleaseEventHandle);
}
