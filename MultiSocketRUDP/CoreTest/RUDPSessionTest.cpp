#include "PreCompile.h"
#include <gtest/gtest.h>

#include "../MultiSocketRUDPServer/SessionSocketContext.h"
#include "../MultiSocketRUDPServer/RUDPSession.h"
#include "../MultiSocketRUDPServer/MultiSocketRUDPCore.h"
#include "../MultiSocketRUDPServer/SendPacketInfo.h"

namespace
{
	class SessionBehaviorTestSession final : public RUDPSession
	{
	public:
		explicit SessionBehaviorTestSession(MultiSocketRUDPCore& inCore) : RUDPSession(inCore) {}
	};

	class NoOpPacket final : public IPacket
	{
	public:
		PacketId GetPacketId() const override { return 1; }
	};
}

class RUDPSessionBehaviorAccess
{
public:
	static bool OnRecvPacket(RUDPSession& session, NetBuffer& recvPacket)
	{
		return session.OnRecvPacket(recvPacket);
	}

	static void OnSendReply(RUDPSession& session, NetBuffer& recvPacket)
	{
		session.OnSendReply(recvPacket);
	}

	static SessionSendContext& GetSendContext(RUDPSession& session)
	{
		return session.GetSendContext();
	}

	static void InitializeSession(RUDPSession& session)
	{
		session.InitializeSession();
	}

	static void SetMaximumPacketHoldingQueueSize(const BYTE size)
	{
		RUDPSession::SetMaximumPacketHoldingQueueSize(size);
	}
};

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
