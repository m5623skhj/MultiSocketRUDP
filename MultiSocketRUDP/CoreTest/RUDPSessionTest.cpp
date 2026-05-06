#include "PreCompile.h"
#include <gtest/gtest.h>

#include "../MultiSocketRUDPServer/SessionSocketContext.h"

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
