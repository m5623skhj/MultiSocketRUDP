#include "PreCompile.h"
#include "MultiSocketRUDPCore.h"
#include "LogExtension.h"
#include "Logger.h"
#include "RUDPIOHandler.h"
#include "RIOManager.h"

namespace
{
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
