#pragma once
#include "ISessionDelegate.h"
#include <functional>

class MockSessionDelegate final : public ISessionDelegate
{
public:
    [[nodiscard]]
    bool InitializeSessionRIO(RUDPSession&, const RIO_EXTENSION_FUNCTION_TABLE&,
        const RIO_CQ&, const RIO_CQ&) override
    {
        ++initializeSessionRIOCount;
        return initializeSessionRIOReturn;
    }

    void SetSessionId(RUDPSession&, SessionIdType id) override { lastSetSessionId = id; }
    void SetThreadId(RUDPSession&, ThreadIdType id)  override { lastSetThreadId = id; }

    void CloseSocket(RUDPSession&) override { ++closeSocketCount; }
    [[nodiscard]]
    SOCKET GetSocket(const RUDPSession&) override { return getSocketReturn; }
    [[nodiscard]]
    std::shared_mutex& GetSocketMutex(const RUDPSession&) override { return dummyMutex; }

    void RecvContextReset(RUDPSession&) override { ++recvContextResetCount; }
    [[nodiscard]]
    std::shared_ptr<IOContext> GetRecvBufferContext(const RUDPSession&) override { return recvBufferContextReturn; }
    [[nodiscard]]
    RecvBuffer& GetRecvBuffer(RUDPSession& session) override;   // 별도 정의
    void EnqueueToRecvBufferList(RUDPSession&, NetBuffer* buf) override { enqueueBuffer = buf; }
    [[nodiscard]]
    RIO_RQ GetRecvRIORQ(const RUDPSession&) override { return recvRIORQReturn; }

    [[nodiscard]]
    IO_MODE& GetSendIOMode(RUDPSession&) override { return dummyIOMode; }
    [[nodiscard]]
    bool IsNothingToSend(RUDPSession&) override { return isNothingToSendReturn; }
    [[nodiscard]]
    bool IsSendPacketInfoQueueEmpty(RUDPSession&) override { return isSendPacketInfoQueueEmpty; }
    [[nodiscard]]
    SendPacketInfo* TryGetFrontAndPop(RUDPSession&) override { return tryGetFrontReturn; }
    [[nodiscard]]
    SendPacketInfo* GetReservedSendPacketInfo(const RUDPSession&) override { return reservedSendReturn; }
    void SetReservedSendPacketInfo(RUDPSession&, SendPacketInfo* info) override { reservedSendReturn = info; }
    [[nodiscard]]
    size_t GetSendPacketInfoQueueSize(RUDPSession&) override { return sendPacketInfoQueueSizeRet; }
    [[nodiscard]]
    char* GetRIOSendBuffer(RUDPSession&) override { return dummySendBuffer; }
    [[nodiscard]]
    RIO_BUFFERID GetSendBufferId(const RUDPSession&) override { return sendBufferIdReturn; }
    [[nodiscard]]
    RIO_RQ GetSendRIORQ(const RUDPSession&) override { return sendRIORQReturn; }
    [[nodiscard]]
    std::set<MultiSocketRUDP::PacketSequenceSetKey>& GetCachedSequenceSet(RUDPSession&) override { return dummySeqSet; }
    [[nodiscard]]
    std::mutex& GetCachedSequenceSetMutex(RUDPSession&) override { return dummySeqMutex; }

    [[nodiscard]]
    bool TryConnect(RUDPSession&, NetBuffer&, const sockaddr_in&) override
    {
        ++tryConnectCount; return tryConnectReturn;
    }

    [[nodiscard]]
    bool CanProcessPacket(const RUDPSession&, const sockaddr_in&) override { return canProcessReturn; }

    [[nodiscard]]
    bool OnRecvPacket(RUDPSession&, NetBuffer&) override
    {
        ++onRecvPacketCount; return onRecvPacketReturn;
    }

    void OnSendReply(RUDPSession&, NetBuffer&) override { ++onSendReplyCount; }
    void Disconnect(RUDPSession&, NetBuffer&) override { ++disconnectCount; }

    void SendHeartbeatPacket(RUDPSession&) override { ++sendHeartbeatCount; }
    [[nodiscard]]
    bool CheckReservedSessionTimeout(const RUDPSession&, unsigned long long) override
    {
        return checkReservedTimeoutReturn;
    }
    void AbortReservedSession(RUDPSession&) override { ++abortReservedCount; }
    void SetSessionReservedTime(RUDPSession&, unsigned long long now) override
    {
        lastReservedTime = now;
    }

    [[nodiscard]]
    const unsigned char* GetSessionKey(const RUDPSession&) override { return dummyKey; }
    void SetSessionKey(RUDPSession&, const unsigned char* k) override
    {
        std::copy_n(k, sizeof(dummyKey), dummyKey);
    }
    [[nodiscard]]
    const unsigned char* GetSessionSalt(const RUDPSession&) override { return dummySalt; }
    void SetSessionSalt(RUDPSession&, const unsigned char* s) override
    {
        std::copy_n(s, sizeof(dummySalt), dummySalt);
    }
    [[nodiscard]]
    const BCRYPT_KEY_HANDLE& GetSessionKeyHandle(const RUDPSession&) override { return dummyKeyHandle; }
    void SetSessionKeyHandle(RUDPSession&, const BCRYPT_KEY_HANDLE& h) override { dummyKeyHandle = h; }
    [[nodiscard]]
	unsigned char* GetSessionKeyObjectBuffer(const RUDPSession&) override { return dummyKeyObjBuf; }
    void SetSessionKeyObjectBuffer(RUDPSession&, unsigned char*) override {}

    void GetServerPortAndSessionId(const RUDPSession&, PortType& outPort, SessionIdType& outId) override
    {
        outPort = portReturn; outId = sessionIdReturn;
    }

    void ResetCounts()
    {
        initializeSessionRIOCount = closeSocketCount = recvContextResetCount
            = tryConnectCount = onRecvPacketCount = onSendReplyCount
            = disconnectCount = sendHeartbeatCount = abortReservedCount = 0;
    }

    bool initializeSessionRIOReturn = true;
    int  initializeSessionRIOCount = 0;

    bool tryConnectReturn = false;
    int tryConnectCount = 0;
    bool canProcessReturn = true;
    bool onRecvPacketReturn = true;
    int onRecvPacketCount = 0;
    int onSendReplyCount = 0;
    int disconnectCount = 0;

    PortType portReturn = 0;
    SessionIdType sessionIdReturn = INVALID_SESSION_ID;

    IO_MODE dummyIOMode{};
    bool isNothingToSendReturn = true;
    bool isSendPacketInfoQueueEmpty = true;
    SendPacketInfo* tryGetFrontReturn = nullptr;
    SendPacketInfo* reservedSendReturn = nullptr;
    size_t sendPacketInfoQueueSizeRet = 0;
    char dummySendBuffer[65536]{};
    RIO_BUFFERID sendBufferIdReturn = RIO_INVALID_BUFFERID;
    RIO_RQ sendRIORQReturn = RIO_INVALID_RQ;
    std::set<MultiSocketRUDP::PacketSequenceSetKey> dummySeqSet;
    mutable std::mutex dummySeqMutex;

    unsigned char dummyKey[32]{};
    unsigned char dummySalt[16]{};
    BCRYPT_KEY_HANDLE dummyKeyHandle = nullptr;
    unsigned char dummyKeyObjBuf[256]{};

    int sendHeartbeatCount = 0;
    bool checkReservedTimeoutReturn = false;
    int abortReservedCount = 0;
    unsigned long long lastReservedTime = 0;

    SessionIdType lastSetSessionId = INVALID_SESSION_ID;
    ThreadIdType  lastSetThreadId = 0;

    int    closeSocketCount = 0;
    SOCKET getSocketReturn = INVALID_SOCKET;
    mutable std::shared_mutex dummyMutex;

    int recvContextResetCount = 0;
    std::shared_ptr<IOContext> recvBufferContextReturn;
    NetBuffer* enqueueBuffer = nullptr;
    RIO_RQ recvRIORQReturn = RIO_INVALID_RQ;
};
