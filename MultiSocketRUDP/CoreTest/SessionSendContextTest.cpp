#include "PreCompile.h"
#include <gtest/gtest.h>

#include "SessionSendContext.h"
#include "SendPacketInfo.h"

namespace
{
	struct SendContextRIOState
	{
		int registerCount{};
		int deregisterCount{};
		RIO_BUFFERID registerResult{ reinterpret_cast<RIO_BUFFERID>(1) };
	};

	SendContextRIOState* gSendContextRIOState{};

	RIO_BUFFERID WINAPI RegisterSendContextBuffer(PCHAR, DWORD)
	{
		++gSendContextRIOState->registerCount;
		return gSendContextRIOState->registerResult;
	}

	void WINAPI DeregisterSendContextBuffer(RIO_BUFFERID)
	{
		++gSendContextRIOState->deregisterCount;
	}

	RIO_EXTENSION_FUNCTION_TABLE MakeSendContextRIOTable(SendContextRIOState& state)
	{
		gSendContextRIOState = &state;
		RIO_EXTENSION_FUNCTION_TABLE table{};
		table.RIORegisterBuffer = RegisterSendContextBuffer;
		table.RIODeregisterBuffer = DeregisterSendContextBuffer;
		return table;
	}

	SendPacketInfo* MakeSendPacketInfo(const PacketSequence sequence)
	{
		NetBuffer* buffer = NetBuffer::Alloc();
		SendPacketInfo* info = sendPacketInfoPool->Alloc();
		if (buffer == nullptr || info == nullptr)
		{
			NetBuffer::Free(buffer);
			return nullptr;
		}

		info->Initialize(nullptr, 0, buffer, sequence, false);
		return info;
	}
}

// ------------------------------------------------------------
// 새 송신 컨텍스트에 큐, 예약 패킷, 보류 패킷과 송신 시퀀스가 없는지 확인합니다.
// ------------------------------------------------------------
TEST(SessionSendContextTest, InitialStateHasNoQueuedReservedOrPendingPackets)
{
	SessionSendContext context;

	EXPECT_TRUE(context.IsSendPacketInfoQueueEmpty());
	EXPECT_EQ(context.TryGetFrontAndPop(), nullptr);
	EXPECT_EQ(context.GetReservedSendPacketInfo(), nullptr);
	EXPECT_EQ(context.TakeReservedSendPacketInfo(), nullptr);
	EXPECT_TRUE(context.IsPendingQueueEmpty());
	EXPECT_EQ(context.GetLastSendPacketSequence(), 0);
}

// ------------------------------------------------------------
// 초기화와 반복 정리가 RIO 송신 버퍼를 정확히 한 번 등록하고 해제하는지 확인합니다.
// ------------------------------------------------------------
TEST(SessionSendContextTest, InitializeAndCleanupManageRegisteredBufferIdempotently)
{
	SendContextRIOState state;
	const auto table = MakeSendContextRIOTable(state);
	SessionSendContext context;

	ASSERT_TRUE(context.Initialize(table, 3));
	EXPECT_EQ(state.registerCount, 1);
	EXPECT_NE(context.GetSendBufferId(), RIO_INVALID_BUFFERID);

	context.Cleanup(table);
	context.Cleanup(table);
	EXPECT_EQ(state.deregisterCount, 1);
	EXPECT_EQ(context.GetSendBufferId(), RIO_INVALID_BUFFERID);
}

// ------------------------------------------------------------
// RIO 버퍼 등록 실패 후 송신 버퍼 ID가 무효 상태로 유지되고 중복 해제되지 않는지 확인합니다.
// ------------------------------------------------------------
TEST(SessionSendContextTest, InitializeFailureLeavesInvalidBufferId)
{
	SendContextRIOState state;
	state.registerResult = RIO_INVALID_BUFFERID;
	const auto table = MakeSendContextRIOTable(state);
	SessionSendContext context;

	EXPECT_FALSE(context.Initialize(table, 1));
	EXPECT_EQ(context.GetSendBufferId(), RIO_INVALID_BUFFERID);
	context.Cleanup(table);
	EXPECT_EQ(state.deregisterCount, 0);
}

TEST(SessionSendContextTest, QueueAndReservedPacketOwnershipTransitionsAreConsistent)
{
	SessionSendContext context;
	auto* first = reinterpret_cast<SendPacketInfo*>(1);
	auto* second = reinterpret_cast<SendPacketInfo*>(2);

	EXPECT_TRUE(context.IsNothingToSend());
	context.PushSendPacketInfo(first);
	context.SetReservedSendPacketInfo(second);
	EXPECT_EQ(context.GetSendPacketInfoQueueSize(), 1);
	EXPECT_EQ(context.TryGetFrontAndPop(), first);
	EXPECT_EQ(context.TakeReservedSendPacketInfo(), second);
	EXPECT_TRUE(context.IsNothingToSend());
}

TEST(SessionSendContextTest, SendPacketMapFindAndErasePreservesCallerReference)
{
	SessionSendContext context;
	NetBuffer* buffer = NetBuffer::Alloc();
	SendPacketInfo* info = sendPacketInfoPool->Alloc();
	ASSERT_NE(buffer, nullptr);
	ASSERT_NE(info, nullptr);
	info->Initialize(nullptr, 0, buffer, 11, false);

	context.InsertSendPacketInfo(11, info);
	EXPECT_EQ(context.FindSendPacketInfo(11), info);
	EXPECT_EQ(context.FindAndEraseSendPacketInfo(11), info);
	EXPECT_EQ(context.FindSendPacketInfo(11), nullptr);

	SendPacketInfo::Free(info);
	SendPacketInfo::Free(info);
}

TEST(SessionSendContextTest, PendingQueueHonorsCapacityAndOrder)
{
	SessionSendContext context;
	context.InitializePendingQueue(2);
	NetBuffer* first = NetBuffer::Alloc();
	NetBuffer* second = NetBuffer::Alloc();
	NetBuffer* extra = NetBuffer::Alloc();

	ASSERT_TRUE(context.PushToPendingQueue(1, first));
	ASSERT_TRUE(context.PushToPendingQueue(2, second));
	EXPECT_TRUE(context.IsPendingQueueFull());
	EXPECT_FALSE(context.PushToPendingQueue(3, extra));

	std::pair<PacketSequence, NetBuffer*> item;
	ASSERT_TRUE(context.PopFromPendingQueue(item));
	EXPECT_EQ(item, std::make_pair(PacketSequence{ 1 }, first));
	NetBuffer::Free(item.second);
	ASSERT_TRUE(context.PopFromPendingQueue(item));
	EXPECT_EQ(item, std::make_pair(PacketSequence{ 2 }, second));
	NetBuffer::Free(item.second);
	NetBuffer::Free(extra);
}

// ------------------------------------------------------------
// 송신 패킷 map의 삭제와 전체 정리가 저장 항목과 소유권 콜백을 올바르게 처리하는지 확인합니다.
// ------------------------------------------------------------
TEST(SessionSendContextTest, PacketMapEraseAndClearInvokeExpectedOwnershipCallbacks)
{
	SessionSendContext context;
	SendPacketInfo* first = MakeSendPacketInfo(10);
	SendPacketInfo* second = MakeSendPacketInfo(20);
	ASSERT_NE(first, nullptr);
	ASSERT_NE(second, nullptr);

	context.InsertSendPacketInfo(10, first);
	context.InsertSendPacketInfo(20, second);
	context.EraseSendPacketInfo(999);

	std::vector<SendPacketInfo*> visited;
	context.ForEachAndClearSendPacketInfoMap([&visited](SendPacketInfo* info) { visited.push_back(info); });

	EXPECT_EQ(visited.size(), 2u);
	EXPECT_EQ(context.FindSendPacketInfo(10), nullptr);
	EXPECT_EQ(context.FindSendPacketInfo(20), nullptr);

	for (SendPacketInfo* info : visited)
	{
		SendPacketInfo::Free(info);
	}
	SendPacketInfo::Free(first);
	SendPacketInfo::Free(second);
}

// ------------------------------------------------------------
// reset이 소유 큐와 보류 버퍼를 해제하고 시퀀스·버퍼 ID·IO 모드를 초기화하는지 확인합니다.
// ------------------------------------------------------------
TEST(SessionSendContextTest, ResetClearsOwnedQueuesPendingBuffersAndSequenceState)
{
	SessionSendContext context;
	SendPacketInfo* queued = MakeSendPacketInfo(1);
	SendPacketInfo* reserved = MakeSendPacketInfo(2);
	NetBuffer* pending = NetBuffer::Alloc();
	ASSERT_NE(queued, nullptr);
	ASSERT_NE(reserved, nullptr);
	ASSERT_NE(pending, nullptr);

	context.PushSendPacketInfo(queued);
	context.SetReservedSendPacketInfo(reserved);
	context.InitializePendingQueue(1);
	ASSERT_TRUE(context.PushToPendingQueue(3, pending));
	context.SetSendRIOBufferId(reinterpret_cast<RIO_BUFFERID>(9));
	context.GetIOMode().store(IO_MODE::IO_SENDING);
	EXPECT_EQ(context.IncrementLastSendPacketSequence(), 1);

	context.Reset();

	EXPECT_TRUE(context.IsNothingToSend());
	EXPECT_TRUE(context.IsPendingQueueEmpty());
	EXPECT_EQ(context.GetLastSendPacketSequence(), 0);
	EXPECT_EQ(context.GetSendBufferId(), RIO_INVALID_BUFFERID);
	EXPECT_EQ(context.GetIOMode().load(), IO_MODE::IO_NONE_SENDING);
}

// ------------------------------------------------------------
// 용량이 0인 보류 큐가 가득 찬 상태를 보고하고 패킷 삽입을 거부하는지 확인합니다.
// ------------------------------------------------------------
TEST(SessionSendContextTest, ZeroCapacityPendingQueueRejectsPackets)
{
	SessionSendContext context;
	context.InitializePendingQueue(0);
	NetBuffer* buffer = NetBuffer::Alloc();
	ASSERT_NE(buffer, nullptr);

	EXPECT_TRUE(context.IsPendingQueueFull());
	EXPECT_FALSE(context.PushToPendingQueue(1, buffer));
	NetBuffer::Free(buffer);
}
