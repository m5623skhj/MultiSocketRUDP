#pragma once
#include "RUDPFlowController.h"
#include "RUDPReceiveWindow.h"

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
		return flowController.CanSendPacket(nextSend, flowController.GetLastAckedSequence());
	}

	void OnAckReceived(PacketSequence replySeq) noexcept
	{
		flowController.OnReplyReceived(replySeq);
	}

	void OnTimeout() noexcept
	{
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
		return flowController.GetCwnd();
	}

	void Initialize(const BYTE recvWindowSize) noexcept
	{
		Reset(0);
		ResizeRecvWindowSize(recvWindowSize);
	}

	void Reset(const PacketSequence recvStartSequence) noexcept
	{
		flowController.Reset();
		receiveWindow.Reset(recvStartSequence);
	}

private:
	RUDPFlowController flowController;
	RUDPReceiveWindow receiveWindow;
};
