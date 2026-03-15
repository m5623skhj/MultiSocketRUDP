#pragma once
#include "RUDPSession.h"

class ISessionDelegate;
class MultiSocketRUDPCore;
// ----------------------------------------
// @brief RUDPSession 객체를 생성하는 데 사용되는 팩토리 함수의 타입 정의입니다.
// MultiSocketRUDPCore 참조를 인자로 받아 RUDPSession 포인터를 반환합니다.
// ----------------------------------------
using SessionFactoryFunc = std::function<RUDPSession* (MultiSocketRUDPCore&)>;

// ----------------------------------------
// @brief RUDP 세션의 생성, 관리, 재사용을 담당하는 매니저 클래스입니다.
// 세션 풀을 통해 효율적인 세션 관리를 제공하고 연결된 세션 수를 추적합니다.
// ----------------------------------------
class RUDPSessionManager
{
public:
	// ----------------------------------------
	// @brief RUDPSessionManager 클래스의 생성자입니다.
	// 최대 세션 수와 RUDP 코어 참조를 받아 초기화합니다.
	// @param inMaxSessionSize 관리할 최대 세션의 수입니다.
	// @param inCore RUDP 코어 인스턴스에 대한 참조입니다.
	// ----------------------------------------
	explicit RUDPSessionManager(const unsigned short inMaxSessionSize, MultiSocketRUDPCore& inCore, ISessionDelegate& inSessionDelegate);
	// ----------------------------------------
	// @brief RUDPSessionManager 클래스의 소멸자입니다.
	// 관리 중인 모든 세션을 정리하고 리소스를 해제합니다.
	// ----------------------------------------
	~RUDPSessionManager();
	RUDPSessionManager(const RUDPSessionManager&) = delete;
	RUDPSessionManager& operator=(const RUDPSessionManager&) = delete;
	RUDPSessionManager(RUDPSessionManager&&) = delete;
	RUDPSessionManager& operator=(RUDPSessionManager&&) = delete;

public:
	// ----------------------------------------
	// @brief RUDPSessionManager를 초기화하고 세션 풀을 생성합니다.
	// 한 번 초기화된 후에는 다시 초기화되지 않습니다.
	// @param inNumOfWorkerThreads 워커 스레드의 수입니다. 각 세션에 스레드 ID를 할당하는 데 사용됩니다.
	// @param factory 세션을 생성하는 데 사용될 팩토리 함수입니다.
	// @return 초기화 성공 시 true, 실패 시 false를 반환합니다.
	// ----------------------------------------
	bool Initialize(const BYTE inNumOfWorkerThreads, SessionFactoryFunc&& factory);

	[[nodiscard]]
	RUDPSession* AcquireSession();
	// ----------------------------------------
	// @brief 지정된 세션 ID의 세션을 풀로 반환하여 재사용 가능하게 만듭니다.
	// 유효하지 않은 세션 ID이거나 이미 해제된 세션인 경우 오류를 기록합니다.
	// @param sessionId 해제할 세션의 고유 ID입니다.
	// ----------------------------------------
	bool ReleaseSession(SessionIdType sessionId);

public:
	// ----------------------------------------
	// @brief 지정된 세션 ID를 가진 사용 중인 세션을 가져옵니다.
	// 세션 ID가 유효하지 않거나 세션이 사용 중이 아닌 경우 nullptr을 반환합니다.
	// @param sessionId 검색할 세션의 고유 ID입니다.
	// @return 사용 중인 RUDPSession 포인터 또는 nullptr.
	// ----------------------------------------
	[[nodiscard]]
	RUDPSession* GetUsingSession(SessionIdType sessionId);
	// ----------------------------------------
	// @brief 지정된 세션 ID를 가진 사용 중인 세션을 const로 가져옵니다.
	// 세션 ID가 유효하지 않거나 세션이 사용 중이 아닌 경우 nullptr을 반환합니다.
	// @param sessionId 검색할 세션의 고유 ID입니다.
	// @return 사용 중인 const RUDPSession 포인터 또는 nullptr.
	// ----------------------------------------
	[[nodiscard]]
	const RUDPSession* GetUsingSession(SessionIdType sessionId) const;
	// ----------------------------------------
	// @brief 지정된 세션 ID를 가진 해제 중인 세션을 가져옵니다.
	// 세션 ID가 유효하지 않거나 세션이 해제 중이 아닌 경우 nullptr을 반환합니다.
	// @param sessionId 검색할 세션의 고유 ID입니다.
	// @return 해제 중인 RUDPSession 포인터 또는 nullptr.
	// ----------------------------------------
	[[nodiscard]]
	RUDPSession* GetReleasingSession(SessionIdType sessionId) const;

	// ----------------------------------------
	// @brief 현재 연결된 사용자의 수를 반환합니다.
	// ----------------------------------------
	[[nodiscard]]
	unsigned short GetConnectedCount() const { return connectedUserCount.load(); }
	// ----------------------------------------
	// @brief 매니저가 관리할 수 있는 최대 세션 수를 반환합니다.
	// ----------------------------------------
	[[nodiscard]]
	unsigned short GetMaxSessions() const { return maxSessionSize; }
	// ----------------------------------------
	// @brief 현재 사용 가능한(재사용 대기 중인) 세션의 수를 반환합니다.
	// ----------------------------------------
	[[nodiscard]]
	unsigned short GetUnusedSessionCount() const;
	// ----------------------------------------
	// @brief RUDPSessionManager가 초기화되었는지 여부를 반환합니다.
	// ----------------------------------------
	[[nodiscard]]
	bool IsInitialized() const { return isInitialized; }

public:
	// ----------------------------------------
	// @brief 모든 활성 세션을 닫습니다.
	// 모든 세션의 소켓을 닫고 연결된 사용자 수를 0으로 재설정합니다.
	// ----------------------------------------
	void CloseAllSessions();
	// ----------------------------------------
	// @brief 모든 세션 리소스를 해제하고 세션 풀을 비웁니다.
	// 관리 중인 모든 RUDPSession 객체를 삭제하고 초기화 상태를 해제합니다.
	// ----------------------------------------
	void ClearAllSessions();

public:
	// ----------------------------------------
	// @brief 연결된 사용자 수를 1 증가시킵니다.
	// ----------------------------------------
	void IncrementConnectedCount() { ++connectedUserCount; }
	// ----------------------------------------
	// @brief 연결된 사용자 수를 1 감소시킵니다.
	// ----------------------------------------
	void DecrementConnectedCount() { --connectedUserCount; }

public:
	// ----------------------------------------
	// @brief 모든 활성 세션에 대해 하트비트 검사를 수행합니다.
	// 일정 시간 동안 응답이 없는 세션이 감지되면 해당 세션의 연결 상태를 해제합니다.
	// ----------------------------------------
	void HeartbeatCheck(const unsigned long long now) const;

private:
	// ----------------------------------------
	// @brief 지정된 최대 세션 수만큼 세션 풀을 생성합니다.
	// 팩토리 함수를 사용하여 RUDPSession 객체를 만들고 풀에 추가합니다.
	// @return 세션 풀 생성 성공 시 true, 실패 시 false를 반환합니다.
	// ----------------------------------------
	[[nodiscard]]
	bool CreateSessionPool();
	// ----------------------------------------
	// @brief 특정 세션 ID가 현재 사용 가능한(재사용 대기 중인) 상태인지 확인합니다.
	// ----------------------------------------
	[[nodiscard]]
	bool IsUnusedSession(SessionIdType sessionId) const;

private:
	BYTE numOfWorkerThreads{};

	unsigned short maxSessionSize;
	SessionFactoryFunc sessionFactory;
	std::vector<RUDPSession*> sessionList;
	// 반드시 sessionListLock 안에서 sessionList와 같이 수정되어야 합니다.
	std::unordered_set<SessionIdType> unusedSessionIdSet;
	std::atomic_uint16_t connectedUserCount{};

	std::list<SessionIdType> unusedSessionIdList;
	mutable std::recursive_mutex unusedSessionIdListLock;

	bool isInitialized{};
	MultiSocketRUDPCore& core;
	ISessionDelegate& sessionDelegate;
};
