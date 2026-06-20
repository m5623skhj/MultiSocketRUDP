#include "PreCompile.h"
#include <gtest/gtest.h>

#include "SessionSendContext.h"
#include "SendPacketInfo.h"

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
