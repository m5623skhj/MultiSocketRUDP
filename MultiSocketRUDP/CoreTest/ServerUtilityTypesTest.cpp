#include "PreCompile.h"
#include <gtest/gtest.h>

#include "../Common/etc/CoreType.h"
#include "../Common/etc/EnumTypes.h"
#include "IOContext.h"
#include "PacketSequenceSetKey.h"
#include "RecvBuffer.h"
#include "MockSessionDelegate.h"
#include "RIOManager.h"
#include "RUDPIOHandler.h"

// ------------------------------------------------------------
// IOContext 초기화가 소유 세션 ID와 작업 종류만 설정하고 RIO 버퍼 필드는 보존하는지 확인합니다.
// ------------------------------------------------------------
TEST(IOContextTest, InitContextStoresOwnerAndOperationWithoutChangingBufferFields)
{
	IOContext context;
	context.Length = 77;
	context.InitContext(12, RIO_OPERATION_TYPE::OP_SEND);

	EXPECT_EQ(context.ownerSessionId, 12);
	EXPECT_EQ(context.ioType, RIO_OPERATION_TYPE::OP_SEND);
	EXPECT_EQ(context.Length, 77u);
}

// ------------------------------------------------------------
// 수신 컨텍스트 자유 큐가 FIFO 순서를 유지하고 소진 후 빈 상태를 올바르게 보고하는지 확인합니다.
// ------------------------------------------------------------
TEST(RecvBufferTest, FreeContextQueuePreservesFifoAndEmptyState)
{
	RecvBuffer buffer;
	IOContext first;
	IOContext second;

	EXPECT_EQ(buffer.AcquireFreeRecvContext(), nullptr);
	buffer.ReleaseRecvContext(&first);
	buffer.ReleaseRecvContext(&second);
	EXPECT_EQ(buffer.AcquireFreeRecvContext(), &first);
	EXPECT_EQ(buffer.AcquireFreeRecvContext(), &second);
	EXPECT_EQ(buffer.AcquireFreeRecvContext(), nullptr);
}

// ------------------------------------------------------------
// 자유 수신 컨텍스트 큐를 비우면 대기 중인 모든 컨텍스트가 제거되는지 확인합니다.
// ------------------------------------------------------------
TEST(RecvBufferTest, ClearFreeRecvContextsDrainsQueuedContexts)
{
	RecvBuffer buffer;
	IOContext context;
	buffer.ReleaseRecvContext(&context);

	buffer.ClearFreeRecvContexts();

	EXPECT_EQ(buffer.AcquireFreeRecvContext(), nullptr);
}

// ------------------------------------------------------------
// 패킷 정렬 키가 일반 데이터를 응답보다 우선하고 같은 종류에서는 시퀀스 순으로 정렬하는지 확인합니다.
// ------------------------------------------------------------
TEST(PacketSequenceSetKeyTest, OrdersDataBeforeReplyThenBySequence)
{
	std::set<MultiSocketRUDP::PacketSequenceSetKey> keys;
	keys.emplace(true, 1);
	keys.emplace(false, 9);
	keys.emplace(false, 3);
	keys.emplace(true, 1);

	ASSERT_EQ(keys.size(), 3u);
	auto it = keys.begin();
	EXPECT_FALSE(it->isReplyType);
	EXPECT_EQ(it++->packetSequence, 3);
	EXPECT_FALSE(it->isReplyType);
	EXPECT_EQ(it++->packetSequence, 9);
	EXPECT_TRUE(it->isReplyType);
	EXPECT_EQ(it->packetSequence, 1);
}

// ------------------------------------------------------------
// 패킷 손실률 경계값 0%와 100%에서 손실 판정이 결정적으로 동작하는지 확인합니다.
// ------------------------------------------------------------
TEST(DatagramLossSimulatorTest, BoundaryRatesAreDeterministic)
{
	DatagramLossSimulator neverDrop(0, 123);
	DatagramLossSimulator alwaysDrop(100, 123);

	for (int i = 0; i < 64; ++i)
	{
		EXPECT_FALSE(neverDrop.ShouldDropReceivedDatagram());
		EXPECT_FALSE(neverDrop.ShouldDropSendingDatagram());
		EXPECT_TRUE(alwaysDrop.ShouldDropReceivedDatagram());
		EXPECT_TRUE(alwaysDrop.ShouldDropSendingDatagram());
	}
}

// ------------------------------------------------------------
// 동일한 시드가 송신과 수신 손실 스트림을 각각 동일하게 재현하는지 확인합니다.
// ------------------------------------------------------------
TEST(DatagramLossSimulatorTest, SameSeedReproducesEachDirection)
{
	DatagramLossSimulator first(37, 456);
	DatagramLossSimulator second(37, 456);

	for (int i = 0; i < 128; ++i)
	{
		EXPECT_EQ(first.ShouldDropReceivedDatagram(), second.ShouldDropReceivedDatagram());
		EXPECT_EQ(first.ShouldDropSendingDatagram(), second.ShouldDropSendingDatagram());
	}
}

// ------------------------------------------------------------
// RIOManager 초기화 전 호출이 RIO API에 접근하지 않고 안전한 실패값을 반환하는지 확인합니다.
// ------------------------------------------------------------
TEST(RIOManagerTest, OperationsBeforeInitializeReturnSafeGuardValues)
{
	MockSessionDelegate delegate;
	RIOManager manager(delegate);
	char buffer[8]{};
	RIORESULT result{};
	IOContext context;

	EXPECT_EQ(manager.RegisterRIOBuffer(buffer, sizeof(buffer)), RIO_INVALID_BUFFERID);
	EXPECT_EQ(manager.RegisterRIOBuffer(nullptr, sizeof(buffer)), RIO_INVALID_BUFFERID);
	EXPECT_EQ(manager.DequeueCompletions(0, &result, 1), 0u);
	EXPECT_FALSE(manager.RIOReceiveEx(RIO_INVALID_RQ, &context, 1, nullptr, nullptr, nullptr, nullptr, 0, &context));
	EXPECT_FALSE(manager.RIOSendEx(RIO_INVALID_RQ, &context, 1, nullptr, nullptr, nullptr, nullptr, 0, &context));
	manager.DeregisterBuffer(reinterpret_cast<RIO_BUFFERID>(1));
	manager.Shutdown();
	manager.Shutdown();
}
