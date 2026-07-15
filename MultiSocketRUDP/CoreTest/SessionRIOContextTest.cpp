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
		SessionIdType requestQueueSessionId{ INVALID_SESSION_ID };
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

	RIO_RQ WINAPI TestCreateRequestQueue(SOCKET, ULONG, ULONG, ULONG, ULONG, RIO_CQ, RIO_CQ, PVOID requestContext)
	{
		++gRioState->createRequestQueueCallCount;
		gRioState->requestQueueSessionId = *static_cast<SessionIdType*>(requestContext);
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

// ------------------------------------------------------------
// 수신 컨텍스트 초기화가 모든 RIO 버퍼와 메타데이터를 구성하고 자유 큐에 등록하는지 확인합니다.
// ------------------------------------------------------------
TEST(SessionRecvContextTest, InitializePopulatesEveryContextAndFreeQueue)
{
	TestRIOState state;
	const auto table = MakeTestRioTable(state);
	SessionRecvContext context;
	constexpr SessionIdType sessionId = 23;

	ASSERT_TRUE(context.Initialize(table, sessionId, nullptr));
	auto& recvBuffer = context.GetRecvBuffer();
	std::set<IOContext*> acquired;
	for (size_t i = 0; i < RECV_OUTSTANDING_COUNT; ++i)
	{
		IOContext* ioContext = recvBuffer.AcquireFreeRecvContext();
		ASSERT_NE(ioContext, nullptr);
		acquired.insert(ioContext);
		EXPECT_EQ(ioContext->ownerSessionId, sessionId);
		EXPECT_EQ(ioContext->ioType, RIO_OPERATION_TYPE::OP_RECV);
		EXPECT_EQ(ioContext->Length, RECV_BUFFER_SIZE);
		EXPECT_EQ(ioContext->Offset, 0u);
		EXPECT_EQ(ioContext->clientAddrRIOBuffer.Length, sizeof(SOCKADDR_INET));
		EXPECT_EQ(ioContext->localAddrRIOBuffer.Length, sizeof(SOCKADDR_INET));
	}
	EXPECT_EQ(acquired.size(), RECV_OUTSTANDING_COUNT);
	EXPECT_EQ(recvBuffer.AcquireFreeRecvContext(), nullptr);

	context.Cleanup(table);
}

// ------------------------------------------------------------
// 수신 패킷 enqueue와 컨텍스트 reset 이후 버퍼 목록 및 자유 큐 상태가 올바른지 확인합니다.
// ------------------------------------------------------------
TEST(SessionRecvContextTest, EnqueueAndResetExposeExpectedBufferState)
{
	TestRIOState state;
	const auto table = MakeTestRioTable(state);
	SessionRecvContext context;
	ASSERT_TRUE(context.Initialize(table, 3, nullptr));
	NetBuffer* packet = NetBuffer::Alloc();
	ASSERT_NE(packet, nullptr);

	context.EnqueueToRecvBufferList(packet);
	NetBuffer* dequeued = nullptr;
	ASSERT_TRUE(context.GetRecvBuffer().recvBufferList.Dequeue(&dequeued));
	EXPECT_EQ(dequeued, packet);
	NetBuffer::Free(dequeued);

	context.Cleanup(table);
	context.RecvContextReset();
	EXPECT_EQ(context.GetRecvBufferContext(), nullptr);
	EXPECT_EQ(context.GetRecvBuffer().AcquireFreeRecvContext(), nullptr);
}

TEST(SessionRIOContextTest, InitializeCreatesRequestQueueAfterRecvAndSendBuffers)
{
	TestRIOState state;
	const auto table = MakeTestRioTable(state);
	SessionRIOContext context;

	ASSERT_TRUE(context.Initialize(table, RIO_INVALID_CQ, RIO_INVALID_CQ, INVALID_SOCKET, 11, nullptr, 2));

	EXPECT_EQ(state.registerCallCount, RECV_OUTSTANDING_COUNT * 3 + 1);
	EXPECT_EQ(state.createRequestQueueCallCount, 1);
	EXPECT_EQ(state.requestQueueSessionId, 11);
	EXPECT_EQ(context.GetRIORQ(), state.createRequestQueueReturn);

	context.Cleanup(table);
	EXPECT_EQ(state.deregisterCallCount, state.registerCallCount);
}

// ------------------------------------------------------------
// 송신 버퍼 등록 실패 시 먼저 등록된 모든 수신 버퍼가 해제되는지 확인합니다.
// ------------------------------------------------------------
TEST(SessionRIOContextTest, SendBufferRegistrationFailureRollsBackRecvBuffers)
{
	TestRIOState state;
	state.failRegisterCallIndex = RECV_OUTSTANDING_COUNT * 3 + 1;
	const auto table = MakeTestRioTable(state);
	SessionRIOContext context;

	EXPECT_FALSE(context.Initialize(table, RIO_INVALID_CQ, RIO_INVALID_CQ, INVALID_SOCKET, 17, nullptr, 2));
	EXPECT_EQ(state.createRequestQueueCallCount, 0);
	EXPECT_EQ(state.registerCallCount, RECV_OUTSTANDING_COUNT * 3 + 1);
	EXPECT_EQ(state.deregisterCallCount, RECV_OUTSTANDING_COUNT * 3);
}

// ------------------------------------------------------------
// SessionRIOContext의 래퍼 함수가 수신 및 송신 하위 컨텍스트로 작업을 전달하는지 확인합니다.
// ------------------------------------------------------------
TEST(SessionRIOContextTest, WrapperMethodsForwardToRecvAndSendContexts)
{
	SessionRIOContext context;
	context.GetSendContext().InitializePendingQueue(1);
	NetBuffer* pending = NetBuffer::Alloc();
	NetBuffer* received = NetBuffer::Alloc();
	ASSERT_NE(pending, nullptr);
	ASSERT_NE(received, nullptr);

	EXPECT_TRUE(context.GetSendContext().PushToPendingQueue(8, pending));
	context.EnqueueToRecvBufferList(received);
	NetBuffer* dequeued = nullptr;
	ASSERT_TRUE(context.GetRecvBuffer().recvBufferList.Dequeue(&dequeued));
	EXPECT_EQ(dequeued, received);
	NetBuffer::Free(dequeued);

	context.GetSendContext().ClearPendingQueue();
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
