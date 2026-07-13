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

namespace
{
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
		for (ULONG i = 0; i < numOfResults; ++i)
		{
			const auto context = GetIOCompletedContext(rioResults[i]);
			if (context == nullptr)
			{
				continue;
			}

			if (not ioHandler->IOCompleted(context, rioResults[i].BytesTransferred, threadId))
			{
				LOG_ERROR(std::format("IOCompleted() failed with io type {}", static_cast<INT8>(context->ioType)));
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
		default:
			LOG_ERROR(std::format("Invalid logic thread wait result. Error is {}", WSAGetLastError()));
			break;
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
	static unsigned long long constexpr FORCE_CHANGE_SENDING_MODE_MS_IN_RELEASE_THREAD = 10000;
	const HANDLE eventHandles[2] = { sessionReleaseEventHandle, sessionReleaseStopEventHandle };
	while (not stopToken.stop_requested())
	{
		const auto now = GetTickCount64();

		switch (WaitForMultipleObjects(2, eventHandles, FALSE, INFINITE))
		{
		case WAIT_OBJECT_0:
		{
			std::vector<SessionIdType> copyList;
			{
				std::scoped_lock lock(releaseSessionIdListLock);
				copyList.assign(releaseSessionIdList.begin(), releaseSessionIdList.end());
				releaseSessionIdList.clear();
			}

			std::vector<SessionIdType> remainList;
			for (const auto releaseSessionId : copyList)
			{
				const auto releaseSession = GetReleasingSession(releaseSessionId);
				if (releaseSession == nullptr)
				{
					continue;
				}

				if (releaseSession->GetSendContext().GetIOMode().load(std::memory_order_seq_cst) == IO_MODE::IO_SENDING ||
					releaseSession->nowInProcessingRecvPacket.load(std::memory_order_seq_cst))
				{
					if ((releaseSession->onSessionReleaseTime + FORCE_CHANGE_SENDING_MODE_MS_IN_RELEASE_THREAD) > now)
					{
						remainList.emplace_back(releaseSessionId);
						continue;
					}

					releaseSession->GetSendContext().GetIOMode().store(IO_MODE::IO_NONE_SENDING, std::memory_order_seq_cst);
				}

				releaseSession->Disconnect();
			}

			if (not remainList.empty())
			{
				std::scoped_lock lock(releaseSessionIdListLock);
				for (const auto remainId : remainList)
				{
					releaseSessionIdList.emplace_back(remainId);
				}
				SetEvent(sessionReleaseEventHandle);
			}
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
