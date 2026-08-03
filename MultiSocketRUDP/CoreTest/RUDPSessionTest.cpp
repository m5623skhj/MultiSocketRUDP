#include "PreCompile.h"
#include <gtest/gtest.h>

#include "../MultiSocketRUDPServer/SessionSocketContext.h"
#include "../MultiSocketRUDPServer/RUDPSession.h"
#include "../MultiSocketRUDPServer/MultiSocketRUDPCore.h"
#include "../MultiSocketRUDPServer/SendPacketInfo.h"
#include "MultiSocketRUDPCoreTestAccess.h"
#include "RUDPSessionTestAccess.h"

namespace
{
	class SessionBehaviorTestSession final : public RUDPSession
	{
	public:
		explicit SessionBehaviorTestSession(MultiSocketRUDPCore& inCore) : RUDPSession(inCore) {}
	};

	class LifecycleHookTestSession final : public RUDPSession
	{
	public:
		explicit LifecycleHookTestSession(MultiSocketRUDPCore& inCore) : RUDPSession(inCore) {}

		void OnDisconnected() override
		{
			++disconnectedCount;
		}

		int disconnectedCount{};
	};

	class NoOpPacket final : public IPacket
	{
	public:
		PacketId GetPacketId() const override { return 1; }
	};
}

class SessionSocketContextTest : public ::testing::Test
{
protected:
	SessionSocketContext context;
};

TEST_F(SessionSocketContextTest, InitialState_HasInvalidSocket)
{
	EXPECT_EQ(context.GetSocket(), INVALID_SOCKET);
}

TEST_F(SessionSocketContextTest, InitialState_HasInvalidPort)
{
	EXPECT_EQ(context.GetServerPort(), INVALID_PORT_NUMBER);
}

TEST_F(SessionSocketContextTest, SetSocket_StoresSocketValue)
{
	const SOCKET expectedSocket = static_cast<SOCKET>(42);

	context.SetSocket(expectedSocket);

	EXPECT_EQ(context.GetSocket(), expectedSocket);
}

TEST_F(SessionSocketContextTest, SetServerPort_StoresPortValue)
{
	constexpr PortType expectedPort = 54321;

	context.SetServerPort(expectedPort);

	EXPECT_EQ(context.GetServerPort(), expectedPort);
}

TEST_F(SessionSocketContextTest, CloseSocket_OnInvalidSocket_DoesNotCrash)
{
	EXPECT_NO_FATAL_FAILURE(context.CloseSocket());
	EXPECT_EQ(context.GetSocket(), INVALID_SOCKET);
}

TEST_F(SessionSocketContextTest, GetSocketMutex_ReturnsStableReference)
{
	auto& first = context.GetSocketMutex();
	auto& second = context.GetSocketMutex();

	EXPECT_EQ(&first, &second);
}

TEST(RUDPSessionBehaviorTest, DisconnectedSessionRejectsSendAndDisconnectTransition)
{
	MultiSocketRUDPCore core{ L"", L"" };
	SessionBehaviorTestSession session{ core };
	NoOpPacket packet;

	EXPECT_FALSE(session.SendPacket(packet));
	session.DoDisconnect(DISCONNECT_REASON::NORMAL);
	EXPECT_EQ(session.GetSessionState(), SESSION_STATE::DISCONNECTED);
	EXPECT_FALSE(session.IsReleasing());
}

TEST(RUDPSessionBehaviorTest, OnRecvPacketUnknownPacketIdReturnsFalse)
{
	MultiSocketRUDPCore core{ L"", L"" };
	RUDPSessionBehaviorAccess::SetMaximumPacketHoldingQueueSize(4);
	SessionBehaviorTestSession session{ core };
	RUDPSessionBehaviorAccess::InitializeSession(session);
	NetBuffer* buffer = NetBuffer::Alloc();
	ASSERT_NE(buffer, nullptr);

	*buffer << PacketSequence{ 0 } << PacketId{ 9999 };

	EXPECT_FALSE(RUDPSessionBehaviorAccess::OnRecvPacket(session, *buffer));

	NetBuffer::Free(buffer);
}

// ------------------------------------------------------------
// Verifies that receive I/O, receive logic, send, and completion work each block finalization.
// ------------------------------------------------------------
TEST(RUDPSessionBehaviorTest, FinalizeBarrierWaitsForReceiveIoLogicAndSendCompletion)
{
	MultiSocketRUDPCore core{ L"", L"" };
	SessionBehaviorTestSession session{ core };
	auto& recvBuffer = RUDPSessionBehaviorAccess::GetRecvBuffer(session);
	auto& sendMode = RUDPSessionBehaviorAccess::GetSendContext(session).GetIOMode();

	EXPECT_TRUE(RUDPSessionBehaviorAccess::CanFinalizeIO(session));
	recvBuffer.BeginRecvIo();
	EXPECT_FALSE(RUDPSessionBehaviorAccess::CanFinalizeIO(session));
	recvBuffer.BeginRecvLogic();
	recvBuffer.CompleteRecvIo();
	EXPECT_FALSE(RUDPSessionBehaviorAccess::CanFinalizeIO(session));
	recvBuffer.CompleteRecvLogic();
	EXPECT_TRUE(RUDPSessionBehaviorAccess::CanFinalizeIO(session));
	sendMode.store(IO_MODE::IO_SENDING);
	EXPECT_FALSE(RUDPSessionBehaviorAccess::CanFinalizeIO(session));
	sendMode.store(IO_MODE::IO_NONE_SENDING);
	EXPECT_TRUE(RUDPSessionBehaviorAccess::CanFinalizeIO(session));
	RUDPSessionBehaviorAccess::BeginIOCompletion(session);
	EXPECT_FALSE(RUDPSessionBehaviorAccess::CanFinalizeIO(session));
	RUDPSessionBehaviorAccess::CompleteIOCompletion(session);
	EXPECT_TRUE(RUDPSessionBehaviorAccess::CanFinalizeIO(session));
}

// ------------------------------------------------------------
// Verifies that I/O shutdown waits for receive logic drain and calls OnDisconnected exactly once.
// ------------------------------------------------------------
TEST(RUDPSessionBehaviorTest, IOShutdownWaitsForRecvLogicBeforeCallingDisconnectedHook)
{
	MultiSocketRUDPCore core{ L"", L"" };
	LifecycleHookTestSession session{ core };
	RUDPSessionBehaviorAccess::SetReleasing(session);
	auto& recvBuffer = RUDPSessionBehaviorAccess::GetRecvBuffer(session);
	recvBuffer.BeginRecvLogic();

	RUDPSessionBehaviorAccess::BeginIOShutdown(session);
	EXPECT_EQ(session.disconnectedCount, 0);

	recvBuffer.CompleteRecvLogic();
	RUDPSessionBehaviorAccess::BeginIOShutdown(session);
	RUDPSessionBehaviorAccess::BeginIOShutdown(session);
	EXPECT_EQ(session.disconnectedCount, 1);
}

TEST(RUDPSessionBehaviorTest, ReceiveOrderComparisonUsesFullPacketSequenceWidth)
{
	constexpr PacketSequence twoToTheThirtySecond = PacketSequence{ 1 } << 32;
	constexpr PacketSequence maxSequence = ~PacketSequence{ 0 };

	EXPECT_FALSE(RUDPSessionBehaviorAccess::IsOlderRecvSequence(twoToTheThirtySecond, 0));
	EXPECT_TRUE(RUDPSessionBehaviorAccess::IsOlderRecvSequence(0, twoToTheThirtySecond));
	EXPECT_FALSE(RUDPSessionBehaviorAccess::IsOlderRecvSequence(0, maxSequence));
	EXPECT_TRUE(RUDPSessionBehaviorAccess::IsOlderRecvSequence(maxSequence, 0));
}

TEST(RUDPSessionBehaviorTest, OnSendReplyErasesTrackedSendPacketInfo)
{
	MultiSocketRUDPCore core{ L"", L"" };
	SessionBehaviorTestSession session{ core };
	NetBuffer* sendBuffer = NetBuffer::Alloc();
	SendPacketInfo* info = sendPacketInfoPool->Alloc();
	ASSERT_NE(sendBuffer, nullptr);
	ASSERT_NE(info, nullptr);

	constexpr PacketSequence sequence = 0;
	info->Initialize(&session, session.GetSessionGeneration(), sendBuffer, sequence, false);
	RUDPSessionBehaviorAccess::GetSendContext(session).InsertSendPacketInfo(sequence, info);
	ASSERT_EQ(RUDPSessionBehaviorAccess::GetSendContext(session).FindSendPacketInfo(sequence), info);

	NetBuffer reply;
	reply << sequence << BYTE{ 1 };
	RUDPSessionBehaviorAccess::OnSendReply(session, reply);

	EXPECT_EQ(RUDPSessionBehaviorAccess::GetSendContext(session).FindSendPacketInfo(sequence), nullptr);
	EXPECT_TRUE(info->isErasedPacketInfo.load(std::memory_order_acquire));

	SendPacketInfo::Free(info);
}

TEST(RUDPSessionBehaviorTest, InitializeSessionResetsReusableStateAndUsesCoreRtoOptions)
{
	MultiSocketRUDPCore core{ L"", L"" };
	MultiSocketRUDPCoreTestAccess::SetTimingOptions(core, 100, 250, 100, 1000);
	RUDPSessionBehaviorAccess::SetMaximumPacketHoldingQueueSize(4);
	SessionBehaviorTestSession session{ core };
	RUDPSessionBehaviorAccess::SetConnected(session);
	RUDPSessionBehaviorAccess::SetNowInReleaseThread(session, true);
	RUDPSessionBehaviorAccess::SetDisconnectedReason(session, DISCONNECT_REASON::BY_ERROR);
	RUDPSessionBehaviorAccess::RefreshLastReceivedPacketTime(session, 500);
	std::ignore = RUDPSessionBehaviorAccess::GetSendContext(session).IncrementLastSendPacketSequence();
	sockaddr_in clientAddress{};
	clientAddress.sin_family = AF_INET;
	clientAddress.sin_port = htons(12000);
	clientAddress.sin_addr.S_un.S_addr = htonl(INADDR_LOOPBACK);
	RUDPSessionBehaviorAccess::SetClientAddress(session, clientAddress);
	const uint32_t generationBefore = session.GetSessionGeneration();

	RUDPSessionBehaviorAccess::InitializeSession(session);

	EXPECT_EQ(session.GetSessionGeneration(), generationBefore + 1);
	EXPECT_EQ(session.GetSessionState(), SESSION_STATE::DISCONNECTED);
	EXPECT_FALSE(session.IsReleasing());
	EXPECT_EQ(session.GetDisconnectedReason(), DISCONNECT_REASON::NOT_DISCONNECTED);
	EXPECT_EQ(session.GetSocketAddress().sin_port, 0);
	EXPECT_EQ(RUDPSessionBehaviorAccess::GetSendContext(session).GetLastSendPacketSequence(), 0);
	EXPECT_EQ(session.GetRetransmissionTimeoutMs(), 250u);
}

TEST(RUDPSessionBehaviorTest, HeartbeatGuardHonorsStateReleaseAndThreshold)
{
	MultiSocketRUDPCore core{ L"", L"" };
	MultiSocketRUDPCoreTestAccess::SetTimingOptions(core, 100, 250, 100, 1000);
	SessionBehaviorTestSession session{ core };
	RUDPSessionBehaviorAccess::InitializeSession(session);
	RUDPSessionBehaviorAccess::RefreshLastReceivedPacketTime(session, 1000);

	EXPECT_FALSE(RUDPSessionBehaviorAccess::NeedToSendHeartbeat(session, 1100));

	RUDPSessionBehaviorAccess::SetConnected(session);
	EXPECT_FALSE(RUDPSessionBehaviorAccess::NeedToSendHeartbeat(session, 1099));
	EXPECT_TRUE(RUDPSessionBehaviorAccess::NeedToSendHeartbeat(session, 1100));

	RUDPSessionBehaviorAccess::SetNowInReleaseThread(session, true);
	EXPECT_FALSE(RUDPSessionBehaviorAccess::NeedToSendHeartbeat(session, 1200));
}

TEST(RUDPSessionBehaviorTest, ReservedSessionTimeoutUsesInclusiveBoundary)
{
	MultiSocketRUDPCore core{ L"", L"" };
	SessionBehaviorTestSession session{ core };
	RUDPSessionBehaviorAccess::SetReservedSessionTimeoutMs(100);
	RUDPSessionBehaviorAccess::SetSessionReservedTime(session, 1000);

	EXPECT_FALSE(RUDPSessionBehaviorAccess::CheckReservedSessionTimeout(session, 1100));
	RUDPSessionBehaviorAccess::SetReserved(session);
	EXPECT_FALSE(RUDPSessionBehaviorAccess::CheckReservedSessionTimeout(session, 1099));
	EXPECT_TRUE(RUDPSessionBehaviorAccess::CheckReservedSessionTimeout(session, 1100));

	RUDPSessionBehaviorAccess::SetReservedSessionTimeoutMs(30000);
}

TEST(RUDPSessionBehaviorTest, CanProcessPacketRequiresMatchingAddressAndNonReleasingSession)
{
	MultiSocketRUDPCore core{ L"", L"" };
	SessionBehaviorTestSession session{ core };
	sockaddr_in clientAddress{};
	clientAddress.sin_family = AF_INET;
	clientAddress.sin_port = htons(12000);
	clientAddress.sin_addr.S_un.S_addr = htonl(INADDR_LOOPBACK);
	RUDPSessionBehaviorAccess::SetClientAddress(session, clientAddress);

	EXPECT_TRUE(RUDPSessionBehaviorAccess::CanProcessPacket(session, clientAddress));

	sockaddr_in wrongPort = clientAddress;
	wrongPort.sin_port = htons(12001);
	EXPECT_FALSE(RUDPSessionBehaviorAccess::CanProcessPacket(session, wrongPort));

	sockaddr_in wrongAddress = clientAddress;
	wrongAddress.sin_addr.S_un.S_addr = htonl(0x7F000002);
	EXPECT_FALSE(RUDPSessionBehaviorAccess::CanProcessPacket(session, wrongAddress));

	RUDPSessionBehaviorAccess::SetNowInReleaseThread(session, true);
	EXPECT_FALSE(RUDPSessionBehaviorAccess::CanProcessPacket(session, clientAddress));
}

TEST(RUDPSessionBehaviorTest, TryConnectRejectsInvalidSequenceSessionIdAndStateWithoutMutation)
{
	MultiSocketRUDPCore core{ L"", L"" };
	SessionBehaviorTestSession session{ core };
	RUDPSessionBehaviorAccess::SetSessionId(session, 7);
	RUDPSessionBehaviorAccess::SetReserved(session);
	sockaddr_in clientAddress{};
	clientAddress.sin_family = AF_INET;
	clientAddress.sin_port = htons(12000);
	clientAddress.sin_addr.S_un.S_addr = htonl(INADDR_LOOPBACK);

	NetBuffer invalidSequence;
	invalidSequence << PacketSequence{ LOGIN_PACKET_SEQUENCE + 1 } << SessionIdType{ 7 };
	EXPECT_FALSE(RUDPSessionBehaviorAccess::TryConnect(session, invalidSequence, clientAddress));
	EXPECT_TRUE(session.IsReserved());

	NetBuffer invalidSessionId;
	invalidSessionId << PacketSequence{ LOGIN_PACKET_SEQUENCE } << SessionIdType{ 8 };
	EXPECT_FALSE(RUDPSessionBehaviorAccess::TryConnect(session, invalidSessionId, clientAddress));
	EXPECT_TRUE(session.IsReserved());

	RUDPSessionBehaviorAccess::InitializeSession(session);
	NetBuffer invalidState;
	invalidState << PacketSequence{ LOGIN_PACKET_SEQUENCE } << SessionIdType{ 7 };
	EXPECT_FALSE(RUDPSessionBehaviorAccess::TryConnect(session, invalidState, clientAddress));
	EXPECT_EQ(session.GetSessionState(), SESSION_STATE::DISCONNECTED);
	EXPECT_EQ(session.GetSocketAddress().sin_port, 0);
}

TEST(RUDPSessionBehaviorTest, OnSendReplyIgnoresFutureAndUnknownSequences)
{
	MultiSocketRUDPCore core{ L"", L"" };
	SessionBehaviorTestSession session{ core };
	NetBuffer* sendBuffer = NetBuffer::Alloc();
	SendPacketInfo* info = sendPacketInfoPool->Alloc();
	ASSERT_NE(sendBuffer, nullptr);
	ASSERT_NE(info, nullptr);
	info->Initialize(&session, session.GetSessionGeneration(), sendBuffer, 1, false);
	RUDPSessionBehaviorAccess::GetSendContext(session).InsertSendPacketInfo(1, info);

	NetBuffer futureReply;
	futureReply << PacketSequence{ 2 } << BYTE{ 1 };
	RUDPSessionBehaviorAccess::OnSendReply(session, futureReply);
	EXPECT_EQ(RUDPSessionBehaviorAccess::GetSendContext(session).FindSendPacketInfo(1), info);

	NetBuffer unknownReply;
	unknownReply << PacketSequence{ 0 } << BYTE{ 1 };
	RUDPSessionBehaviorAccess::OnSendReply(session, unknownReply);
	EXPECT_EQ(RUDPSessionBehaviorAccess::GetSendContext(session).FindSendPacketInfo(1), info);

	EXPECT_EQ(RUDPSessionBehaviorAccess::GetSendContext(session).FindAndEraseSendPacketInfo(1), info);
	SendPacketInfo::Free(info);
	SendPacketInfo::Free(info);
}
