#pragma once
#include "../Common/etc/CoreType.h"
#include <thread>
#include <functional>

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
	void StartServerAliveCheck(unsigned int inCheckIntervalMs);
	void StopServerAliveCheck();
	[[nodiscard]]
	bool IsServerAlive(PacketSequence nowPacketSequence);

private:
	void RunServerAliveCheckerThread();

private:
	bool isStopped{ false };

private:
	unsigned int checkIntervalMs{ 0 };
	PacketSequence beforeCheckSequence{ 0 };
	std::jthread serverAliveCheckThread;

	std::function<void()> coreStopFunction{};
	std::function<PacketSequence()> getNextRecvSequenceFunction{};
};
