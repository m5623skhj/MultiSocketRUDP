#pragma once
#include "RUDPFlowController.h"
#include "RUDPReceiveWindow.h"
#include <mutex>

class RUDPFlowManager
{
public:
	RUDPFlowManager(BYTE recvWindowSize)
		: flowController()
		, receiveWindow(recvWindowSize)
	{
	}

public:
	// ----------------------------------------
	// @brief 수신 윈도우 크기를 동적으로 조절합니다.
	// @param recvWindowSize 새로운 수신 윈도우 크기
	// ----------------------------------------
	void ResizeRecvWindowSize(BYTE recvWindowSize)
	{
		receiveWindow.ResizeRecvWindowSize(recvWindowSize);
	}

	[[nodiscard]]
	bool CanSend(PacketSequence nextSend) noexcept
	{
		std::scoped_lock lock(sendFlowMutex);
		return flowController.CanSendPacket(nextSend, flowController.GetLastAckedSequence());
	}

	void OnAckReceived(PacketSequence replySeq) noexcept
	{
		std::scoped_lock lock(sendFlowMutex);
		flowController.OnReplyReceived(replySeq);
	}

	void OnTimeout() noexcept
	{
		std::scoped_lock lock(sendFlowMutex);
		flowController.OnTimeout();
	}

	[[nodiscard]]
	bool CanAccept(PacketSequence seq) const noexcept
	{
		return receiveWindow.CanReceive(seq);
	}

	void MarkReceived(PacketSequence seq) noexcept
	{
		receiveWindow.MarkReceived(seq);
	}

	[[nodiscard]]
	PacketSequence GetReceiveWindowEnd() const noexcept
	{
		return receiveWindow.GetWindowEnd();
	}

	[[nodiscard]]
	uint16_t GetCwnd() const noexcept
	{
		std::scoped_lock lock(sendFlowMutex);
		return flowController.GetCwnd();
	}

	void Initialize(const BYTE recvWindowSize) noexcept
	{
		Reset(0);
		ResizeRecvWindowSize(recvWindowSize);
	}

	void Reset(const PacketSequence recvStartSequence) noexcept
	{
		{
			std::scoped_lock lock(sendFlowMutex);
			flowController.Reset();
		}
		receiveWindow.Reset(recvStartSequence);
	}

	[[nodiscard]]
	BYTE GetAdvertisableWindow() const noexcept
	{
		return receiveWindow.GetAdvertiseWindow();
	}

private:
	// Protect the entire send-flow operation; CanSend does not reserve send capacity.
	mutable std::mutex sendFlowMutex;
	RUDPFlowController flowController;
	// Receive-window access and reset require receive-worker ownership or a drained session.
	RUDPReceiveWindow receiveWindow;
};
