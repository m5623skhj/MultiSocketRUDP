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

    bool CanAccept(PacketSequence seq) const noexcept
    {
        return receiveWindow.CanReceive(seq);
    }

    void MarkReceived(PacketSequence seq) noexcept
    {
        receiveWindow.MarkReceived(seq);
    }

    PacketSequence GetReceiveWindowEnd(PacketSequence nextRecv) const noexcept
    {
        return receiveWindow.GetWindowEnd(nextRecv);
    }

    unsigned int GetCwnd() const noexcept
    {
        return flowController.GetCwnd();
    }

private:
    RUDPFlowController flowController;
    RUDPReceiveWindow receiveWindow;
};
