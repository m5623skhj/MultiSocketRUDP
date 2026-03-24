#include "PreCompile.h"
#include "RUDPSessionManager.h"
#include "Logger.h"
#include "LogExtension.h"
#include "RUDPSessionFunctionDelegate.h"

RUDPSessionManager::RUDPSessionManager(const unsigned short inMaxSessionSize, MultiSocketRUDPCore& inCore, ISessionDelegate& inSessionDelegate)
    : maxSessionSize(inMaxSessionSize)
    , core(inCore)
	, sessionDelegate(inSessionDelegate)
{
}

RUDPSessionManager::~RUDPSessionManager()
{
    ClearAllSessions();
}

bool RUDPSessionManager::Initialize(const BYTE inNumOfWorkerThreads, SessionFactoryFunc&& factory)
{
    if (isInitialized)
    {
		return true;
    }

	numOfWorkerThreads = inNumOfWorkerThreads;

    if (factory == nullptr)
    {
		LOG_ERROR("Session factory function is not set");
		return false;
    }
	sessionFactory = std::move(factory);

    if (not CreateSessionPool())
    {
		LOG_ERROR("CreateSessionPool failed");
		return false;
    }

	isInitialized = true;
	return true;
}

RUDPSession* RUDPSessionManager::AcquireSession()
{
	if (not isInitialized)
	{
		LOG_ERROR("RUDPSessionManager is not initialized");
		return nullptr;
	}

	{
		std::scoped_lock lock(unusedSessionIdListLock);
		if (unusedSessionIdList.empty() == true)
		{
			return nullptr;
		}

		const SessionIdType sessionId = unusedSessionIdList.front();
		unusedSessionIdList.pop_front();
		unusedSessionIdSet.erase(sessionId);

		RUDPSession* session = sessionList[sessionId];
		if (session == nullptr)
		{
			LOG_ERROR("Acquired session is nullptr");
		}

		return session;
	}
}

bool RUDPSessionManager::ReleaseSession(SessionIdType sessionId)
{
	if (sessionId >= sessionList.size())
	{
		LOG_ERROR("Invalid sessionId in ReleaseSession");
		return false;
	}

	if (sessionList[sessionId]->GetSessionState() != SESSION_STATE::RELEASING)
	{
		LOG_ERROR("Session is not in RELEASING state in ReleaseSession");
		return false;
	}
	const auto disconnectedReason = sessionList[sessionId]->GetDisconnectedReason();
	sessionDelegate.InitializeSession(*sessionList[sessionId]);

	{
		std::scoped_lock lock(unusedSessionIdListLock);
		if (unusedSessionIdSet.contains(sessionId))
		{
			LOG_ERROR("Session already released in ReleaseSession");
			return false;
		}

		unusedSessionIdList.emplace_back(sessionId);
		unusedSessionIdSet.emplace(sessionId);
	}

	DecrementConnectedCount(disconnectedReason);
	return true;
}

RUDPSession* RUDPSessionManager::GetUsingSession(const SessionIdType sessionId)
{
	if (sessionId >= sessionList.size() ||
		sessionList[sessionId] == nullptr ||
		not sessionList[sessionId]->IsUsingSession())
	{
		return nullptr;
	}

	return sessionList[sessionId];
}

const RUDPSession* RUDPSessionManager::GetUsingSession(const SessionIdType sessionId) const
{
	if (sessionId >= sessionList.size() || 
		sessionList[sessionId] == nullptr ||
		not sessionList[sessionId]->IsUsingSession())
	{
		return nullptr;
	}

	return sessionList[sessionId];
}

RUDPSession* RUDPSessionManager::GetReleasingSession(const SessionIdType sessionId) const
{
	if (sessionId >= sessionList.size() ||
		sessionList[sessionId] == nullptr ||
		not sessionList[sessionId]->IsReleasing())
	{
		return nullptr;
	}

	return sessionList[sessionId];
}

unsigned short RUDPSessionManager::GetUnusedSessionCount() const
{
	std::scoped_lock lock(unusedSessionIdListLock);
	return static_cast<unsigned short>(unusedSessionIdList.size());
}

void RUDPSessionManager::CloseAllSessions()
{
	for (auto* session : sessionList)
	{
		if (session == nullptr)
		{
			continue;
		}

		sessionDelegate.CloseSocket(*session);
	}

	connectedUserCount.store(0);

	const auto log = Logger::MakeLogObject<ServerLog>();
	log->logString = "All sessions closed";
	Logger::GetInstance().WriteLog(log);
}

void RUDPSessionManager::ClearAllSessions()
{
	{
		std::scoped_lock lock(unusedSessionIdListLock);
		unusedSessionIdList.clear();
		unusedSessionIdSet.clear();
	}

	for (auto* session : sessionList)
	{
		if (session == nullptr)
		{
			continue;
		}

		sessionDelegate.RecvContextReset(*session);
		delete session;
	}

	sessionList.clear();
	connectedUserCount.store(0);
	isInitialized = {};

	const auto log = Logger::MakeLogObject<ServerLog>();
	log->logString = "All sessions cleared";
	Logger::GetInstance().WriteLog(log);
}

void RUDPSessionManager::IncrementConnectedCount()
{
	++connectedUserCount;
	allConnectedCount.fetch_add(1, std::memory_order_relaxed);
}

void RUDPSessionManager::DecrementConnectedCount(const DISCONNECT_REASON disconnectedReason)
{
	switch (disconnectedReason)
	{
	case DISCONNECT_REASON::BY_ABORT_RESERVED:
	{
		return;
	}
	case DISCONNECT_REASON::BY_RETRANSMISSION:
	{
		allDisconnectedByRetransmissionCount.fetch_add(1, std::memory_order_relaxed);
		break;
	}
	case DISCONNECT_REASON::NORMAL:
	case DISCONNECT_REASON::BY_ERROR:
	{
		break;
	}
	case DISCONNECT_REASON::NOT_DISCONNECTED:
	{
		LOG_ERROR(std::format("Call DecrementConnectedCount with NOT_DISCONNECTED"));
		return;
	}
	default:
	{
		LOG_ERROR(std::format("Invalid type in DecrementConnectedCount {}", static_cast<uint8_t>(disconnectedReason)));
		return;
	}
	}

	--connectedUserCount;
	allDisconnectedCount.fetch_add(1, std::memory_order_relaxed);
}

void RUDPSessionManager::HeartbeatCheck(const unsigned long long now) const
{
	for (auto* session : sessionList)
	{
		if (session == nullptr || not session->IsUsingSession())
		{
			continue;
		}

		if (session->IsConnected() == true)
		{
			sessionDelegate.SendHeartbeatPacket(*session);
		}
		else if (session->IsReserved() == true)
		{
			// Waiting 30 seconds
			if (sessionDelegate.CheckReservedSessionTimeout(*session, now) == true)
			{
				// if not connected within the time, disconnect the session
				sessionDelegate.AbortReservedSession(*session);
			}
		}
	}
}

bool RUDPSessionManager::CreateSessionPool()
{
	try
	{
		sessionList.reserve(maxSessionSize);

		for (size_t sessionIndex = 0; sessionIndex < maxSessionSize; ++sessionIndex)
		{
			RUDPSession* session = sessionFactory(core);
			if (session == nullptr)
			{
				LOG_ERROR(std::format("Failed to create session {}", sessionIndex));
				return false;
			}

			sessionDelegate.SetSessionId(*session, static_cast<SessionIdType>(sessionIndex));
			sessionDelegate.SetThreadId(*session, sessionIndex % numOfWorkerThreads);
			sessionList.emplace_back(session);
			unusedSessionIdList.emplace_back(static_cast<SessionIdType>(sessionIndex));
			unusedSessionIdSet.emplace(static_cast<SessionIdType>(sessionIndex));
		}

		return true;
	}
	catch (const std::exception& e)
	{
		LOG_ERROR(std::format("Exception during session pool creation: {}", e.what()));
		return false;
	}
}

bool RUDPSessionManager::IsUnusedSession(const SessionIdType sessionId) const
{
	std::scoped_lock lock(unusedSessionIdListLock);

	const auto it = std::ranges::find(unusedSessionIdList, sessionId);
	return it != unusedSessionIdList.end();
}
