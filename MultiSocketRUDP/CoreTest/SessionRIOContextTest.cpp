#include "PreCompile.h"
#include <gtest/gtest.h>

#include "SessionRecvContext.h"
#include "SessionRIOContext.h"

namespace
{
	struct TestRIOState
	{
		int registerCallCount{};
		int deregisterCallCount{};
		int createRequestQueueCallCount{};
		int failRegisterCallIndex{};
		RIO_RQ createRequestQueueReturn{ reinterpret_cast<RIO_RQ>(1) };
	};

	TestRIOState* gRioState = nullptr;

	RIO_BUFFERID WINAPI TestRegisterBuffer(PCHAR, DWORD)
	{
		++gRioState->registerCallCount;
		if (gRioState->failRegisterCallIndex == gRioState->registerCallCount)
		{
			return RIO_INVALID_BUFFERID;
		}

		return reinterpret_cast<RIO_BUFFERID>(static_cast<intptr_t>(gRioState->registerCallCount));
	}

	void WINAPI TestDeregisterBuffer(RIO_BUFFERID)
	{
		++gRioState->deregisterCallCount;
	}

	RIO_RQ WINAPI TestCreateRequestQueue(SOCKET, ULONG, ULONG, ULONG, ULONG, RIO_CQ, RIO_CQ, PVOID)
	{
		++gRioState->createRequestQueueCallCount;
		return gRioState->createRequestQueueReturn;
	}

	RIO_EXTENSION_FUNCTION_TABLE MakeTestRioTable(TestRIOState& state)
	{
		gRioState = &state;
		RIO_EXTENSION_FUNCTION_TABLE table{};
		table.RIORegisterBuffer = TestRegisterBuffer;
		table.RIODeregisterBuffer = TestDeregisterBuffer;
		table.RIOCreateRequestQueue = TestCreateRequestQueue;
		return table;
	}
}

TEST(SessionRecvContextTest, InitializeRegistersEveryRecvBufferAndCleanupDeregistersThem)
{
	TestRIOState state;
	const auto table = MakeTestRioTable(state);
	SessionRecvContext context;

	ASSERT_TRUE(context.Initialize(table, 7, nullptr));
	EXPECT_EQ(state.registerCallCount, RECV_OUTSTANDING_COUNT * 3);

	context.Cleanup(table);
	EXPECT_EQ(state.deregisterCallCount, state.registerCallCount);
}

TEST(SessionRecvContextTest, InitializeFailureDeregistersBuffersRegisteredBeforeFailure)
{
	TestRIOState state;
	state.failRegisterCallIndex = 2;
	const auto table = MakeTestRioTable(state);
	SessionRecvContext context;

	EXPECT_FALSE(context.Initialize(table, 7, nullptr));
	EXPECT_EQ(state.registerCallCount, 3);
	EXPECT_EQ(state.deregisterCallCount, 2);
}

TEST(SessionRIOContextTest, InitializeCreatesRequestQueueAfterRecvAndSendBuffers)
{
	TestRIOState state;
	const auto table = MakeTestRioTable(state);
	SessionRIOContext context;

	ASSERT_TRUE(context.Initialize(table, RIO_INVALID_CQ, RIO_INVALID_CQ, INVALID_SOCKET, 11, nullptr, 2));

	EXPECT_EQ(state.registerCallCount, RECV_OUTSTANDING_COUNT * 3 + 1);
	EXPECT_EQ(state.createRequestQueueCallCount, 1);

	context.Cleanup(table);
	EXPECT_EQ(state.deregisterCallCount, state.registerCallCount);
}

TEST(SessionRIOContextTest, RequestQueueFailureCleansRegisteredRecvAndSendBuffers)
{
	TestRIOState state;
	state.createRequestQueueReturn = RIO_INVALID_RQ;
	const auto table = MakeTestRioTable(state);
	SessionRIOContext context;

	EXPECT_FALSE(context.Initialize(table, RIO_INVALID_CQ, RIO_INVALID_CQ, INVALID_SOCKET, 11, nullptr, 2));

	EXPECT_EQ(state.registerCallCount, RECV_OUTSTANDING_COUNT * 3 + 1);
	EXPECT_EQ(state.createRequestQueueCallCount, 1);
	EXPECT_EQ(state.deregisterCallCount, state.registerCallCount);
}
