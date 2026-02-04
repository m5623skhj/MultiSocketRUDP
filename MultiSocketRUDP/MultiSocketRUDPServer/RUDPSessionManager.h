#pragma once
#include "RUDPSession.h"

class MultiSocketRUDPCore;
using SessionFactoryFunc = std::function<RUDPSession* (MultiSocketRUDPCore&)>;

class RUDPSessionManager
{
public:
	explicit RUDPSessionManager(const unsigned short inMaxSessionSize, MultiSocketRUDPCore& inCore);
	~RUDPSessionManager();
	RUDPSessionManager(const RUDPSessionManager&) = delete;
	RUDPSessionManager& operator=(const RUDPSessionManager&) = delete;
	RUDPSessionManager(RUDPSessionManager&&) = delete;
	RUDPSessionManager& operator=(RUDPSessionManager&&) = delete;

public:
	bool Initialize(const BYTE inNumOfWorkerThreads, SessionFactoryFunc&& factory);

	[[nodiscard]]
	RUDPSession* AcquireSession();
	void ReleaseSession(SessionIdType sessionId);

public:
	[[nodiscard]]
	RUDPSession* GetUsingSession(SessionIdType sessionId);
	[[nodiscard]]
	const RUDPSession* GetUsingSession(SessionIdType sessionId) const;
	[[nodiscard]]
	RUDPSession* GetReleasingSession(SessionIdType sessionId) const;

	[[nodiscard]]
	unsigned short GetConnectedCount() const { return connectedUserCount.load(); }
	[[nodiscard]]
	unsigned short GetMaxSessions() const { return maxSessionSize; }
	[[nodiscard]]
	unsigned short GetUnusedSessionCount() const;
	[[nodiscard]]
	bool IsInitialized() const { return isInitialized; }

public:
	void CloseAllSessions();
	void ClearAllSessions();

public:
	void IncrementConnectedCount() { ++connectedUserCount; }
	void DecrementConnectedCount() { --connectedUserCount; }

private:
	[[nodiscard]]
	bool CreateSessionPool();
	[[nodiscard]]
	bool IsUnusedSession(SessionIdType sessionId) const;

private:
	BYTE numOfWorkerThreads{};

	unsigned short maxSessionSize;
	SessionFactoryFunc sessionFactory;
	std::vector<RUDPSession*> sessionList;
	std::atomic_uint16_t connectedUserCount{};

	std::list<SessionIdType> unusedSessionIdList;
	mutable std::recursive_mutex unusedSessionIdListLock;

	bool isInitialized{};
	MultiSocketRUDPCore& core;
};
