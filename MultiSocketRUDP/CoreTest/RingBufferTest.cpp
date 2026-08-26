#include "PreCompile.h"
#include <gtest/gtest.h>

#include "../Common/etc/RingBuffer.h"

// 내부 인덱스가 순환해도 Push와 Pop이 FIFO 순서를 유지하는지 확인합니다.
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

// 용량이 0인 버퍼가 가득 찬 상태로 취급되어 Push를 거부하는지 확인합니다.
TEST(RingBufferTest, PushFailsWhenCapacityIsZero)
{
	RingBuffer<int> buffer{ 0 };

	EXPECT_TRUE(buffer.IsEmpty());
	EXPECT_TRUE(buffer.IsFull());
	EXPECT_FALSE(buffer.Push(1));
}

// 크기 변경이 기존 항목을 비우고 새 용량을 적용하는지 확인합니다.
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

// Clear가 저장된 항목을 제거하고 버퍼를 다시 사용할 수 있게 하는지 확인합니다.
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

// 빈 버퍼의 Pop이 출력값을 변경하지 않고 실패하는지 확인합니다.
TEST(RingBufferTest, PopFromEmptyBufferReturnsFalse)
{
	RingBuffer<int> buffer{ 2 };
	int value = 17;

	EXPECT_FALSE(buffer.Pop(value));
	EXPECT_EQ(value, 17);
}

// 용량 1인 버퍼가 반복적인 Push와 Pop 순환을 올바르게 처리하는지 확인합니다.
TEST(RingBufferTest, CapacityOneSupportsRepeatedWraparound)
{
	RingBuffer<int> buffer{ 1 };
	int value{};

	for (int expected = 1; expected <= 3; ++expected)
	{
		ASSERT_TRUE(buffer.Push(expected));
		EXPECT_TRUE(buffer.IsFull());
		ASSERT_TRUE(buffer.Pop(value));
		EXPECT_EQ(value, expected);
		EXPECT_TRUE(buffer.IsEmpty());
	}
}

// 용량을 0으로 변경하면 기존 항목을 제거하고 이후 Push를 거부하는지 확인합니다.
TEST(RingBufferTest, ResizeToZeroClearsItemsAndRejectsNewPush)
{
	RingBuffer<int> buffer{ 2 };
	ASSERT_TRUE(buffer.Push(1));

	buffer.Resize(0);

	EXPECT_TRUE(buffer.IsEmpty());
	EXPECT_TRUE(buffer.IsFull());
	EXPECT_FALSE(buffer.Push(2));
	int value{};
	EXPECT_FALSE(buffer.Pop(value));
}
