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
	[[nodiscard]]
	bool CanSend(PacketSequence nextSend, PacketSequence lastAck) noexcept
	{
		return flowController.CanSendPacket(nextSend, lastAck);
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
	BYTE GetCwnd() const noexcept
	{
		return flowController.GetCwnd();
	}

private:
	RUDPFlowController flowController;
	RUDPReceiveWindow receiveWindow;
};
