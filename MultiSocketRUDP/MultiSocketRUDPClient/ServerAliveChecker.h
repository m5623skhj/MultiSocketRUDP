#pragma once
#include "../Common/etc/CoreType.h"
#include <thread>
#include <functional>
#include <mutex>
#include <condition_variable>

// ----------------------------------------
// @brief 일정 주기마다 인증된 수신 횟수를 확인하여 서버 생존 상태를 감시합니다.
// 두 검사 사이에 인증된 수신이 없으면 감시 스레드에서 종료 요청 콜백을 호출합니다.
// 콜백은 감시기를 직접 join하지 않습니다. 외부 종료 경로에서 Stop을 호출합니다.
// ----------------------------------------
class ServerAliveChecker
{
public:
	ServerAliveChecker() = delete;
	explicit ServerAliveChecker(const std::function<void()>& inCoreStopFunction, const std::function<uint64_t()>& inGetReceiveCountFunction);
	~ServerAliveChecker() { StopServerAliveCheck(); }
	ServerAliveChecker(const ServerAliveChecker&) = delete;
	ServerAliveChecker& operator=(const ServerAliveChecker&) = delete;
	ServerAliveChecker(ServerAliveChecker&&) = delete;
	ServerAliveChecker& operator=(ServerAliveChecker&&) = delete;

public:
	// ----------------------------------------
	// @brief 지정한 검사 주기로 서버 생존 감시 스레드를 시작합니다.
	// ----------------------------------------
	void StartServerAliveCheck(unsigned int inCheckIntervalMs);
	// ----------------------------------------
	// @brief 감시 중단을 원자적으로 표시하고 스레드를 안전하게 정리합니다.
	// ----------------------------------------
	void StopServerAliveCheck();
	// ----------------------------------------
	// @brief 직전 검사 이후 인증된 수신 횟수가 증가했는지 확인하고 기준값을 갱신합니다.
	// ----------------------------------------
	[[nodiscard]]
	bool IsServerAlive(uint64_t receiveCount);

private:
	void RunServerAliveCheckerThread();

private:
	std::atomic_bool isStopped{ false };
	std::mutex threadLock;
	std::mutex waitLock;
	std::condition_variable wake;

private:
	unsigned int checkIntervalMs{ 0 };
	uint64_t beforeCheckReceiveCount{};
	std::jthread serverAliveCheckThread;

	std::function<void()> coreStopFunction{};
	std::function<uint64_t()> getReceiveCountFunction{};
};
