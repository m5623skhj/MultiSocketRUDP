#include "PreCompile.h"
#include <gtest/gtest.h>

#include "../Common/etc/RingBuffer.h"

TEST(RingBufferTest, PushPopPreservesFifoOrderAcrossWraparound)
{
	RingBuffer<int> buffer{ 3 };
	int value{};

	ASSERT_TRUE(buffer.Push(1));
	ASSERT_TRUE(buffer.Push(2));
	ASSERT_TRUE(buffer.Push(3));
	EXPECT_TRUE(buffer.IsFull());

	ASSERT_TRUE(buffer.Pop(value));
	EXPECT_EQ(value, 1);
	ASSERT_TRUE(buffer.Push(4));

	ASSERT_TRUE(buffer.Pop(value));
	EXPECT_EQ(value, 2);
	ASSERT_TRUE(buffer.Pop(value));
	EXPECT_EQ(value, 3);
	ASSERT_TRUE(buffer.Pop(value));
	EXPECT_EQ(value, 4);
	EXPECT_TRUE(buffer.IsEmpty());
}

TEST(RingBufferTest, PushFailsWhenCapacityIsZero)
{
	RingBuffer<int> buffer{ 0 };

	EXPECT_TRUE(buffer.IsEmpty());
	EXPECT_TRUE(buffer.IsFull());
	EXPECT_FALSE(buffer.Push(1));
}

TEST(RingBufferTest, ResizeClearsExistingItemsAndUsesNewCapacity)
{
	RingBuffer<int> buffer{ 2 };
	ASSERT_TRUE(buffer.Push(1));
	ASSERT_TRUE(buffer.Push(2));

	buffer.Resize(1);

	EXPECT_TRUE(buffer.IsEmpty());
	EXPECT_FALSE(buffer.IsFull());
	ASSERT_TRUE(buffer.Push(3));
	EXPECT_TRUE(buffer.IsFull());

	int value{};
	ASSERT_TRUE(buffer.Pop(value));
	EXPECT_EQ(value, 3);
}

TEST(RingBufferTest, ClearDropsItemsAndAllowsReuse)
{
	RingBuffer<int> buffer{ 2 };
	ASSERT_TRUE(buffer.Push(1));
	ASSERT_TRUE(buffer.Push(2));

	buffer.Clear();

	EXPECT_TRUE(buffer.IsEmpty());
	ASSERT_TRUE(buffer.Push(9));

	int value{};
	ASSERT_TRUE(buffer.Pop(value));
	EXPECT_EQ(value, 9);
}
