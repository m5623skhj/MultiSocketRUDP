#pragma once
#include "../Common/etc/CoreType.h"
#include <thread>
#include <functional>

// ----------------------------------------
// @brief 일정 주기마다 수신 시퀀스 진행 여부를 확인하여 서버 생존 상태를 감시합니다.
// 두 검사 사이에 시퀀스가 진행하지 않으면 등록된 코어 종료 함수를 감시 스레드에서 호출합니다.
// Stop은 감시 스레드 자신에게서 호출될 수 있으므로 self-join을 피하도록 구현되어 있습니다.
// ----------------------------------------
class ServerAliveChecker
{
public:
	ServerAliveChecker() = delete;
	explicit ServerAliveChecker(const std::function<void()>& inCoreStopFunction, const std::function<PacketSequence()>& inGetNextRecvSequenceFunction);
	~ServerAliveChecker() = default;
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
	// @brief 직전 검사 이후 수신 시퀀스가 진행했는지 확인하고 기준값을 갱신합니다.
	// ----------------------------------------
	[[nodiscard]]
	bool IsServerAlive(PacketSequence nowPacketSequence);

private:
	void RunServerAliveCheckerThread();

private:
	std::atomic_bool isStopped{ false };

private:
	unsigned int checkIntervalMs{ 0 };
	PacketSequence beforeCheckSequence{ 0 };
	std::jthread serverAliveCheckThread;

	std::function<void()> coreStopFunction{};
	std::function<PacketSequence()> getNextRecvSequenceFunction{};
};
