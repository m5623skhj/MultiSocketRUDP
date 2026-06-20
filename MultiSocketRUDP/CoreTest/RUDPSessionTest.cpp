#include "PreCompile.h"
#include <gtest/gtest.h>

#include "../MultiSocketRUDPServer/SessionSocketContext.h"
#include "../MultiSocketRUDPServer/RUDPSession.h"
#include "../MultiSocketRUDPServer/MultiSocketRUDPCore.h"

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
