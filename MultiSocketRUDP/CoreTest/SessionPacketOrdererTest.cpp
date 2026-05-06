#include "PreCompile.h"
#include "gtest/gtest.h"
#include "SessionPacketOrderer.h"

// ============================================================
// SessionPacketOrderer ���� �׽�Ʈ
//
// �ٽ� ����:
//  - seq < nextExpected  �� DUPLICATED_RECV
//  - seq == nextExpected �� ó�� �� ������ ��Ŷ�� ���� ó�� �� PROCESSED / ERROR_OCCURED
//  - seq >  nextExpected �� maxHoldingQueueSize �̸��̸� Ȧ�� �� PACKET_HELD
//  - ���� �̷� ������ �ߺ� ���� �� �� ��°�� ���� (recvHoldingPacketSequences.contains)
//  - Reset(startSeq)	: Ȧ�� ��Ŷ ���� Free, nextExpected �缳��
//  - Initialize(size)   : Reset(0) + maxSize �缳��
// ============================================================

static bool successCb(NetBuffer&, PacketSequence) { return true; }
static bool failCb(NetBuffer&, PacketSequence) { return false; }

static SessionPacketOrderer::PacketProcessCallback makeRecorder(std::vector<PacketSequence>& out)
{
	return [&out](NetBuffer&, PacketSequence seq) -> bool
		{
			out.push_back(seq);
			return true;
		};
}

class SessionPacketOrdererTest : public ::testing::Test
{
protected:
	static constexpr BYTE MAX_QUEUE = 8;
	static constexpr PacketSequence START = 1;

	SessionPacketOrderer orderer{ MAX_QUEUE };

	struct AutoBuf
	{
		NetBuffer* p = NetBuffer::Alloc();
		~AutoBuf() { if (p) NetBuffer::Free(p); }
		NetBuffer& get() const { return *p; }
	};

	void SetUp() override { orderer.Reset(START); }
};

// ------------------------------------------------------------
// 1. �ʱ� ����
// ------------------------------------------------------------
TEST_F(SessionPacketOrdererTest, InitialState_NextExpectedEqualsStart)
{
	EXPECT_EQ(orderer.GetNextExpected(), START);
}

// ------------------------------------------------------------
// 2. ������� �����ϴ� ��Ŷ
// ------------------------------------------------------------
TEST_F(SessionPacketOrdererTest, InOrder_ReturnsProcessed)
{
	AutoBuf buf;
	const auto result = orderer.OnReceive(START, buf.get(), successCb);

	EXPECT_EQ(result, ON_RECV_RESULT::PROCESSED);
	EXPECT_EQ(orderer.GetNextExpected(), START + 1);
}

TEST_F(SessionPacketOrdererTest, InOrder_SequenceAdvancesPerPacket)
{
	std::vector<PacketSequence> processed;
	auto cb = makeRecorder(processed);

	for (PacketSequence s = START; s < START + 5; ++s)
	{
		AutoBuf buf;
		EXPECT_EQ(orderer.OnReceive(s, buf.get(), cb), ON_RECV_RESULT::PROCESSED);
	}

	ASSERT_EQ(processed.size(), 5u);
	for (size_t i = 0; i < processed.size(); ++i)
		EXPECT_EQ(processed[i], static_cast<PacketSequence>(START + i));

	EXPECT_EQ(orderer.GetNextExpected(), START + 5);
}

// ------------------------------------------------------------
// 3. �ߺ� ���� (���� ������)
// ------------------------------------------------------------
TEST_F(SessionPacketOrdererTest, PastSeq_ReturnsDuplicatedRecv)
{
	AutoBuf b1, b2;
	std::ignore = orderer.OnReceive(START, b1.get(), successCb);

	const auto result = orderer.OnReceive(START, b2.get(), successCb);
	EXPECT_EQ(result, ON_RECV_RESULT::DUPLICATED_RECV);
}

TEST_F(SessionPacketOrdererTest, FarPastSeq_ReturnsDuplicatedRecv)
{
	AutoBuf b1, b2, b3;
	std::ignore = orderer.OnReceive(START, b1.get(), successCb);
	std::ignore = orderer.OnReceive(START + 1, b2.get(), successCb);

	EXPECT_EQ(orderer.OnReceive(START, b3.get(), successCb), ON_RECV_RESULT::DUPLICATED_RECV);
	EXPECT_EQ(orderer.GetNextExpected(), START + 2);
}

// ------------------------------------------------------------
// 4. �̷� ������ Ȧ��
// ------------------------------------------------------------
TEST_F(SessionPacketOrdererTest, FutureSeq_ReturnsPacketHeld)
{
	AutoBuf buf;
	EXPECT_EQ(orderer.OnReceive(START + 1, buf.get(), successCb), ON_RECV_RESULT::PACKET_HELD);
	EXPECT_EQ(orderer.GetNextExpected(), START);
}

TEST_F(SessionPacketOrdererTest, HeldPacket_ReleasedWhenExpectedArrives)
{
	std::vector<PacketSequence> processed;
	auto cb = makeRecorder(processed);

	AutoBuf b2;
	EXPECT_EQ(orderer.OnReceive(START + 1, b2.get(), cb), ON_RECV_RESULT::PACKET_HELD);
	EXPECT_TRUE(processed.empty());

	AutoBuf b1;
	EXPECT_EQ(orderer.OnReceive(START, b1.get(), cb), ON_RECV_RESULT::PROCESSED);

	ASSERT_EQ(processed.size(), 2u);
	EXPECT_EQ(processed[0], START);
	EXPECT_EQ(processed[1], START + 1);
	EXPECT_EQ(orderer.GetNextExpected(), START + 2);
}

TEST_F(SessionPacketOrdererTest, MultipleHeldPackets_ProcessedInOrder)
{
	std::vector<PacketSequence> processed;
	auto cb = makeRecorder(processed);

	AutoBuf b4, b3, b2;
	std::ignore = orderer.OnReceive(START + 3, b4.get(), cb);
	std::ignore = orderer.OnReceive(START + 1, b2.get(), cb);
	std::ignore = orderer.OnReceive(START + 2, b3.get(), cb);
	EXPECT_TRUE(processed.empty());

	AutoBuf b1;
	EXPECT_EQ(orderer.OnReceive(START, b1.get(), cb), ON_RECV_RESULT::PROCESSED);

	ASSERT_EQ(processed.size(), 4u);
	for (size_t i = 0; i < processed.size(); ++i)
		EXPECT_EQ(processed[i], static_cast<PacketSequence>(START + i));
}

// ------------------------------------------------------------
// 5. �ߺ� Ȧ�� ������: �� �� �޾Ƶ� �� ���� ó��
// ------------------------------------------------------------
TEST_F(SessionPacketOrdererTest, DuplicateFutureSeq_HeldOnlyOnce)
{
	AutoBuf b2a, b2b;
	std::ignore = orderer.OnReceive(START + 1, b2a.get(), successCb);
	std::ignore = orderer.OnReceive(START + 1, b2b.get(), successCb);

	std::vector<PacketSequence> processed;
	auto cb = makeRecorder(processed);

	AutoBuf b1;
	std::ignore = orderer.OnReceive(START, b1.get(), cb);

	EXPECT_EQ(processed.size(), 2u);
}

// ------------------------------------------------------------
// 6. ť �뷮 �ʰ�
// ------------------------------------------------------------
TEST_F(SessionPacketOrdererTest, QueueFull_ReturnsErrorOccured)
{
	std::vector<AutoBuf> bufs(MAX_QUEUE);
	for (BYTE i = 0; i < MAX_QUEUE; ++i)
	{
		EXPECT_EQ(orderer.OnReceive(START + 1 + i, bufs[i].get(), successCb),
			ON_RECV_RESULT::PACKET_HELD);
	}

	AutoBuf extra;
	EXPECT_EQ(orderer.OnReceive(START + 1 + MAX_QUEUE, extra.get(), successCb),
		ON_RECV_RESULT::ERROR_OCCURED);
}

// ------------------------------------------------------------
// 7. �ݹ� ���� ó��
// ------------------------------------------------------------
TEST_F(SessionPacketOrdererTest, CallbackFails_ReturnsErrorOccured)
{
	AutoBuf buf;
	EXPECT_EQ(orderer.OnReceive(START, buf.get(), failCb), ON_RECV_RESULT::ERROR_OCCURED);
}

TEST_F(SessionPacketOrdererTest, HeldPacketCallbackFails_ReturnsErrorOccured)
{
	AutoBuf b2;
	std::ignore = orderer.OnReceive(START + 1, b2.get(), successCb);

	int callCount = 0;
	auto partialFail = [&callCount](NetBuffer&, PacketSequence) -> bool
		{
			return ++callCount == 1;
		};

	AutoBuf b1;
	EXPECT_EQ(orderer.OnReceive(START, b1.get(), partialFail), ON_RECV_RESULT::ERROR_OCCURED);
}

// ------------------------------------------------------------
// 8. Reset
// ------------------------------------------------------------
TEST_F(SessionPacketOrdererTest, Reset_SetsNewSequenceAndClearsHolding)
{
	AutoBuf b2;
	std::ignore = orderer.OnReceive(START + 1, b2.get(), successCb);

	constexpr PacketSequence newStart = 100;
	orderer.Reset(newStart);

	EXPECT_EQ(orderer.GetNextExpected(), newStart);

	AutoBuf buf;
	EXPECT_EQ(orderer.OnReceive(newStart, buf.get(), successCb), ON_RECV_RESULT::PROCESSED);
	EXPECT_EQ(orderer.GetNextExpected(), newStart + 1);
}

TEST_F(SessionPacketOrdererTest, Reset_OldHeldPacketsDiscarded)
{
	AutoBuf b2, b3;
	std::ignore = orderer.OnReceive(START + 1, b2.get(), successCb);
	std::ignore = orderer.OnReceive(START + 2, b3.get(), successCb);

	orderer.Reset(START);

	std::vector<PacketSequence> processed;
	auto cb = makeRecorder(processed);

	AutoBuf b1;
	std::ignore = orderer.OnReceive(START, b1.get(), cb);

	EXPECT_EQ(processed.size(), 1u);
	EXPECT_EQ(processed[0], START);
}

// ------------------------------------------------------------
// 9. Initialize
// ------------------------------------------------------------
TEST_F(SessionPacketOrdererTest, Initialize_ResetsToZeroAndChangesMaxSize)
{
	AutoBuf b2;
	std::ignore = orderer.OnReceive(START + 1, b2.get(), successCb);

	orderer.Initialize(4);

	EXPECT_EQ(orderer.GetNextExpected(), 0u);
}

// ------------------------------------------------------------
// 10. ��(gap) �ó�����
// ------------------------------------------------------------
TEST_F(SessionPacketOrdererTest, GapScenario_ProcessesInChunks)
{
	std::vector<PacketSequence> processed;
	auto cb = makeRecorder(processed);

	AutoBuf b1, b3, b5;
	EXPECT_EQ(orderer.OnReceive(START, b1.get(), cb), ON_RECV_RESULT::PROCESSED);
	EXPECT_EQ(orderer.OnReceive(START + 2, b3.get(), cb), ON_RECV_RESULT::PACKET_HELD);
	EXPECT_EQ(orderer.OnReceive(START + 4, b5.get(), cb), ON_RECV_RESULT::PACKET_HELD);

	EXPECT_EQ(processed.size(), 1u);
	EXPECT_EQ(orderer.GetNextExpected(), START + 1);

	AutoBuf b2;
	EXPECT_EQ(orderer.OnReceive(START + 1, b2.get(), cb), ON_RECV_RESULT::PROCESSED);
	EXPECT_EQ(processed.size(), 3u);
	EXPECT_EQ(orderer.GetNextExpected(), START + 3);

	AutoBuf b4;
	EXPECT_EQ(orderer.OnReceive(START + 3, b4.get(), cb), ON_RECV_RESULT::PROCESSED);
	EXPECT_EQ(processed.size(), 5u);
	EXPECT_EQ(orderer.GetNextExpected(), START + 5);

	for (size_t i = 0; i < processed.size(); ++i)
	{
		EXPECT_EQ(processed[i], START + i);
	}
}
