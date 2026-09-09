#include "PreCompile.h"
#include <gtest/gtest.h>
#include <semaphore>
#include <thread>

#include "../MultiSocketRUDPServer/SessionSocketContext.h"
#include "../MultiSocketRUDPServer/RUDPSession.h"
#include "../MultiSocketRUDPServer/MultiSocketRUDPCore.h"
#include "../MultiSocketRUDPServer/SendPacketInfo.h"
#include "MultiSocketRUDPCoreTestAccess.h"
#include "RUDPSessionTestAccess.h"
#include "../Common/Crypto/CryptoHelper.h"
#ifndef LOG_ERROR
#define LOG_ERROR(...) ((void)0)
#endif
#include "../Common/PacketCrypto/PacketCryptoHelper.h"

namespace
{
	class SessionBehaviorTestSession final : public RUDPSession
	{
	public:
		explicit SessionBehaviorTestSession(MultiSocketRUDPCore& inCore) : RUDPSession(inCore) {}
	};

	class LifecycleContentPacket final : public IPacket
	{
	public:
		static constexpr PacketId PACKET_ID = 9010;
		PacketId GetPacketId() const override { return PACKET_ID; }
		void BufferToPacket(NetBuffer& buffer) override { buffer >> value; }
		unsigned int value{};
	};

	class LifecycleHookTestSession final : public RUDPSession
	{
	public:
		explicit LifecycleHookTestSession(MultiSocketRUDPCore& inCore) : RUDPSession(inCore) {}

		void RegisterContentHandler()
		{
			PacketManager::RegisterPacket<LifecycleContentPacket>();
			RegisterPacketHandler<LifecycleHookTestSession, LifecycleContentPacket>(
				LifecycleContentPacket::PACKET_ID, &LifecycleHookTestSession::HandleContent);
		}

		void HandleContent(const LifecycleContentPacket& packet)
		{
			if (contentCallback)
			{
				contentCallback(packet.value);
			}
		}

		void OnConnected() override
		{
			if (connectedCallback)
			{
				connectedCallback();
			}
		}

		void OnDisconnected() override
		{
			++disconnectedCount;
		}

		int disconnectedCount{};
		std::function<void()> connectedCallback;
		std::function<void(unsigned int)> contentCallback;
	};

	class NoOpPacket final : public IPacket
	{
	public:
		PacketId GetPacketId() const override { return 1; }
	};

	class CallbackPacket final : public IPacket
	{
	public:
		explicit CallbackPacket(std::function<void(NetBuffer&)> inSerialize)
			: serialize(std::move(inSerialize)) {}
		PacketId GetPacketId() const override { return 1; }
		void PacketToBuffer(NetBuffer& buffer) override { serialize(buffer); }

	private:
		std::function<void(NetBuffer&)> serialize;
	};
}

class SessionSendLifecycleTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		releaseInitialized = MultiSocketRUDPCoreTestAccess::InitializeSessionRelease(core);
		ASSERT_TRUE(releaseInitialized);
		MultiSocketRUDPCoreTestAccess::SetTimingOptions(core, 100, 250, 100, 1000);
		RUDPSessionBehaviorAccess::SetMaximumPacketHoldingQueueSize(4);
		RUDPSessionBehaviorAccess::InitializeSession(session);
		RUDPSessionBehaviorAccess::GetSendContext(session).InitializePendingQueue(4);
		RUDPSessionBehaviorAccess::SetConnected(session);
		auto& crypto = RUDPSessionBehaviorAccess::GetCryptoContext(session);
		auto& helper = CryptoHelper::GetTLSInstance();
		auto keyObject = std::make_unique<unsigned char[]>(helper.GetKeyObjectSize());
		unsigned char key[SESSION_KEY_SIZE]{};
		const auto keyHandle = helper.GetSymmetricKeyHandle(keyObject.get(), key);
		ASSERT_NE(keyHandle, nullptr);
		crypto.SetSessionKeyHandle(keyHandle);
		crypto.SetKeyObjectBuffer(keyObject.release());
	}

	void TearDown() override
	{
		RUDPSessionBehaviorAccess::GetSendContext(session).Reset();
		if (releaseInitialized)
		{
			MultiSocketRUDPCoreTestAccess::CleanupSessionRelease(core);
		}
	}

	MultiSocketRUDPCore core{ L"", L"" };
	LifecycleHookTestSession session{ core };
	bool releaseInitialized{};
};

// Stop a real public send before encryption; release must wait even without posted send I/O.
TEST_F(SessionSendLifecycleTest, DisconnectWaitsForPausedSerializationAndRemainingIo)
{
	auto& crypto = RUDPSessionBehaviorAccess::GetCryptoContext(session);
	const auto keyHandle = crypto.GetSessionKeyHandle();
	const auto generation = session.GetSessionGeneration();

	std::binary_semaphore entered{ 0 };
	std::binary_semaphore resume{ 0 };
	CallbackPacket packet([&](NetBuffer& buffer)
		{
			buffer << BYTE{ 42 };
			entered.release();
			resume.acquire();
		});
	bool sendResult = true;
	std::jthread sender([&]() { sendResult = session.SendPacket(packet); });
	entered.acquire();

	session.DoDisconnect(DISCONNECT_REASON::NORMAL);
	EXPECT_FALSE(RUDPSessionBehaviorAccess::CanFinalizeIO(session));
	RUDPSessionBehaviorAccess::BeginIOShutdown(session);
	EXPECT_EQ(session.disconnectedCount, 0);
	EXPECT_EQ(crypto.GetSessionKeyHandle(), keyHandle);
	EXPECT_EQ(session.GetSessionGeneration(), generation);
	NoOpPacket rejectedPacket;
	EXPECT_FALSE(session.SendPacket(rejectedPacket));

	auto& recvBuffer = RUDPSessionBehaviorAccess::GetRecvBuffer(session);
	recvBuffer.BeginRecvIo();
	resume.release();
	sender.join();
	EXPECT_FALSE(sendResult);
	EXPECT_FALSE(RUDPSessionBehaviorAccess::CanFinalizeIO(session));
	recvBuffer.CompleteRecvIo();
	EXPECT_TRUE(RUDPSessionBehaviorAccess::CanFinalizeIO(session));
	RUDPSessionBehaviorAccess::BeginIOShutdown(session);
	RUDPSessionBehaviorAccess::BeginIOShutdown(session);
	EXPECT_EQ(session.disconnectedCount, 1);

	RUDPSessionBehaviorAccess::InitializeSession(session);
	EXPECT_EQ(crypto.GetSessionKeyHandle(), nullptr);
	EXPECT_EQ(crypto.GetKeyObjectBuffer(), nullptr);
	EXPECT_EQ(session.GetSessionGeneration(), generation + 1);
}

// Disconnect called from user serialization must not deadlock or leak the active-send barrier.
TEST_F(SessionSendLifecycleTest, DisconnectInsideSerializationDrainsAfterSendFailure)
{
	CallbackPacket packet([&](NetBuffer& buffer)
		{
			buffer << BYTE{ 42 };
			session.DoDisconnect(DISCONNECT_REASON::BY_ERROR);
			EXPECT_FALSE(RUDPSessionBehaviorAccess::CanFinalizeIO(session));
			RUDPSessionBehaviorAccess::BeginIOShutdown(session);
			EXPECT_EQ(session.disconnectedCount, 0);
		});

	EXPECT_FALSE(session.SendPacket(packet));
	EXPECT_EQ(session.GetDisconnectedReason(), DISCONNECT_REASON::BY_ERROR);
	EXPECT_TRUE(RUDPSessionBehaviorAccess::CanFinalizeIO(session));
	EXPECT_EQ(RUDPSessionBehaviorAccess::GetSendContext(session).FindSendPacketInfo(1), nullptr);
	RUDPSessionBehaviorAccess::BeginIOShutdown(session);
	EXPECT_EQ(session.disconnectedCount, 1);
}

// Pending success and heartbeat early returns must release their guards; shutdown rejects every producer.
TEST_F(SessionSendLifecycleTest, PendingSendAndHeartbeatReturnWithoutHoldingReleaseBarrier)
{
	RUDPSessionBehaviorAccess::SendHeartbeatPacket(session, 0);
	EXPECT_TRUE(RUDPSessionBehaviorAccess::CanFinalizeIO(session));
	auto& sendContext = RUDPSessionBehaviorAccess::GetSendContext(session);
	for (int i = 0; i < 4; ++i)
	{
		std::ignore = sendContext.IncrementLastSendPacketSequence();
	}
	NoOpPacket packet;
	EXPECT_TRUE(session.SendPacket(packet));
	EXPECT_FALSE(sendContext.IsPendingQueueEmpty());
	EXPECT_TRUE(RUDPSessionBehaviorAccess::CanFinalizeIO(session));
	RUDPSessionBehaviorAccess::SendHeartbeatPacket(session, 100);
	EXPECT_TRUE(RUDPSessionBehaviorAccess::CanFinalizeIO(session));

	session.DoDisconnect(DISCONNECT_REASON::NORMAL);
	const auto sequence = sendContext.GetLastSendPacketSequence();
	EXPECT_FALSE(session.SendPacket(packet));
	RUDPSessionBehaviorAccess::SendHeartbeatPacket(session, 200);
	RUDPSessionBehaviorAccess::SendReplyToClient(session, 1);
	RUDPSessionBehaviorAccess::TryFlushPendingQueue(session);
	EXPECT_EQ(sendContext.GetLastSendPacketSequence(), sequence);
	EXPECT_EQ(sendContext.GetSendPacketInfoQueueSize(), 0u);
	EXPECT_FALSE(sendContext.IsPendingQueueEmpty());
	EXPECT_TRUE(RUDPSessionBehaviorAccess::CanFinalizeIO(session));
}

TEST_F(SessionSendLifecycleTest, AbortedReservationRejectsSendWithoutDisconnectedHook)
{
	RUDPSessionBehaviorAccess::SetReserved(session);
	RUDPSessionBehaviorAccess::AbortReservedSession(session);
	NoOpPacket packet;
	EXPECT_FALSE(session.SendPacket(packet));
	RUDPSessionBehaviorAccess::SendHeartbeatPacket(session, 100);
	RUDPSessionBehaviorAccess::SendReplyToClient(session, 1);
	RUDPSessionBehaviorAccess::TryFlushPendingQueue(session);
	EXPECT_TRUE(RUDPSessionBehaviorAccess::CanFinalizeIO(session));
	RUDPSessionBehaviorAccess::BeginIOShutdown(session);
	EXPECT_EQ(session.disconnectedCount, 0);
}

// The real receive queue must retain its count while a connection callback is running.
TEST_F(SessionSendLifecycleTest, RecvLogicCountProtectsQueuedAndExecutingCallback)
{
	ASSERT_TRUE(MultiSocketRUDPCoreTestAccess::InitializeRecvLogic(core));
	RUDPSessionBehaviorAccess::SetReserved(session);
	auto& recvBuffer = RUDPSessionBehaviorAccess::GetRecvBuffer(session);
	auto& crypto = RUDPSessionBehaviorAccess::GetCryptoContext(session);
	NetBuffer* packet = NetBuffer::Alloc();
	ASSERT_NE(packet, nullptr);
	auto packetType = PACKET_TYPE::CONNECT_TYPE;
	*packet << packetType << PacketSequence{ LOGIN_PACKET_SEQUENCE } << session.GetSessionId();
	PacketCryptoHelper::EncodePacket(*packet, LOGIN_PACKET_SEQUENCE, PACKET_DIRECTION::CLIENT_TO_SERVER,
		crypto.GetSessionSalt(), SESSION_SALT_SIZE, crypto.GetSessionKeyHandle(), true);
	// Encoding rewinds the cursor; a received buffer starts immediately after the header.
	char header[df_HEADER_SIZE];
	packet->ReadBuffer(header, sizeof(header));
	IOContext context{};
	context.session = &session;
	context.ownerRecvBuffer = &recvBuffer;
	context.ownerSessionGeneration = session.GetSessionGeneration();
	ASSERT_TRUE(MultiSocketRUDPCoreTestAccess::EnqueueRecv(core, context, packet));
	EXPECT_EQ(recvBuffer.pendingRecvLogic.load(), 1u);
	EXPECT_FALSE(RUDPSessionBehaviorAccess::CanFinalizeIO(session));

	std::binary_semaphore entered{ 0 };
	std::binary_semaphore resume{ 0 };
	session.connectedCallback = [&]()
		{
			entered.release();
			resume.acquire();
		};
	std::jthread receiver([&]() { MultiSocketRUDPCoreTestAccess::ProcessRecvQueue(core); });
	if (not entered.try_acquire_for(std::chrono::seconds(5)))
	{
		resume.release();
		receiver.join();
		FAIL() << "The queued CONNECT packet did not reach its callback";
	}

	session.DoDisconnect(DISCONNECT_REASON::NORMAL);
	EXPECT_EQ(recvBuffer.pendingRecvLogic.load(), 1u);
	EXPECT_FALSE(RUDPSessionBehaviorAccess::CanFinalizeIO(session));
	RUDPSessionBehaviorAccess::BeginIOShutdown(session);
	EXPECT_EQ(session.disconnectedCount, 0);
	resume.release();
	receiver.join();

	EXPECT_EQ(recvBuffer.pendingRecvLogic.load(), 0u);
	EXPECT_TRUE(RUDPSessionBehaviorAccess::CanFinalizeIO(session));
	RUDPSessionBehaviorAccess::BeginIOShutdown(session);
	RUDPSessionBehaviorAccess::BeginIOShutdown(session);
	EXPECT_EQ(session.disconnectedCount, 1);
}

// Rejected dispatch still owns a queued receive count and must return it on every path.
TEST_F(SessionSendLifecycleTest, ReleasingSessionDrainsQueuedPacketWithoutDispatch)
{
	ASSERT_TRUE(MultiSocketRUDPCoreTestAccess::InitializeRecvLogic(core));
	auto& recvBuffer = RUDPSessionBehaviorAccess::GetRecvBuffer(session);
	IOContext context{};
	context.session = &session;
	context.ownerRecvBuffer = &recvBuffer;
	context.ownerSessionGeneration = session.GetSessionGeneration();
	NetBuffer* packet = NetBuffer::Alloc();
	ASSERT_NE(packet, nullptr);
	ASSERT_TRUE(MultiSocketRUDPCoreTestAccess::EnqueueRecv(core, context, packet));
	session.DoDisconnect(DISCONNECT_REASON::NORMAL);
	EXPECT_FALSE(RUDPSessionBehaviorAccess::CanFinalizeIO(session));
	MultiSocketRUDPCoreTestAccess::ProcessRecvQueue(core);
	EXPECT_EQ(recvBuffer.pendingRecvLogic.load(), 0u);
	EXPECT_TRUE(RUDPSessionBehaviorAccess::CanFinalizeIO(session));
}

// Publishing via the release queue preserves the winning reason without a separate release flag.
TEST_F(SessionSendLifecycleTest, ReleaseQueuePublishesReasonOnceAndStateResetsForReuse)
{
	std::vector<SessionIdType> releaseIds;
	DISCONNECT_REASON observedReason = DISCONNECT_REASON::NOT_DISCONNECTED;
	std::jthread releaser([&]()
		{
			releaseIds = MultiSocketRUDPCoreTestAccess::WaitAndTakeReleaseSessionIds(core);
			if (not releaseIds.empty())
			{
				observedReason = session.GetDisconnectedReason();
			}
		});
	session.DoDisconnect(DISCONNECT_REASON::BY_ERROR);
	session.DoDisconnect(DISCONNECT_REASON::NORMAL);
	releaser.join();
	EXPECT_EQ(releaseIds, std::vector<SessionIdType>{ session.GetSessionId() });
	EXPECT_EQ(observedReason, DISCONNECT_REASON::BY_ERROR);
	EXPECT_TRUE(MultiSocketRUDPCoreTestAccess::TakeReleaseSessionIds(core).empty());
	EXPECT_TRUE(session.IsReleasing());
	EXPECT_EQ(session.GetSessionState(), SESSION_STATE::RELEASING);
	RUDPSessionBehaviorAccess::BeginIOShutdown(session);
	RUDPSessionBehaviorAccess::BeginIOShutdown(session);
	EXPECT_EQ(session.disconnectedCount, 1);

	RUDPSessionBehaviorAccess::InitializeSession(session);
	EXPECT_FALSE(session.IsReleasing());
	RUDPSessionBehaviorAccess::SetConnected(session);
	session.DoDisconnect(DISCONNECT_REASON::NORMAL);
	RUDPSessionBehaviorAccess::BeginIOShutdown(session);
	EXPECT_EQ(session.disconnectedCount, 2);
}

namespace
{
	// Keep real retransmission decisions while controlling only the final send result.
	class RetransmissionTestCore final : public MultiSocketRUDPCore
	{
	public:
		RetransmissionTestCore() : MultiSocketRUDPCore(L"", L"") {}

		bool SendPacket(SendPacketInfo*) const override
		{
			++sendCalls;
			return sendCallback ? sendCallback() : true;
		}

		mutable int sendCalls{};
		std::function<bool()> sendCallback;
	};
}

class RetransmissionLifecycleTest : public ::testing::Test
{
protected:
	using PacketPtr = std::unique_ptr<SendPacketInfo, decltype(&SendPacketInfo::Free)>;

	void SetUp() override
	{
		releaseInitialized = MultiSocketRUDPCoreTestAccess::InitializeSessionRelease(core);
		ASSERT_TRUE(releaseInitialized);
		MultiSocketRUDPCoreTestAccess::InitializeRetransmission(core, 2);
		MultiSocketRUDPCoreTestAccess::SetTimingOptions(core, 100, 250, 100, 1000);
		RUDPSessionBehaviorAccess::SetSessionId(session, 0);
		RUDPSessionBehaviorAccess::InitializeSession(session);
		RUDPSessionBehaviorAccess::SetConnected(session);
	}

	void TearDown() override
	{
		if (releaseInitialized)
		{
			MultiSocketRUDPCoreTestAccess::CleanupSessionRelease(core);
		}
	}

	PacketPtr MakePacket(const uint32_t generation)
	{
		NetBuffer* buffer = NetBuffer::Alloc();
		if (buffer == nullptr)
		{
			return PacketPtr(nullptr, &SendPacketInfo::Free);
		}
		SendPacketInfo* info = sendPacketInfoPool->Alloc();
		if (info == nullptr)
		{
			NetBuffer::Free(buffer);
			return PacketPtr(nullptr, &SendPacketInfo::Free);
		}
		info->Initialize(&session, generation, buffer, 1, false);
		return PacketPtr(info, &SendPacketInfo::Free);
	}

	void Process(SendPacketInfo* info)
	{
		// The worker consumes its own reference; the test retains one for assertions.
		info->AddRefCount();
		MultiSocketRUDPCoreTestAccess::ProcessRetransmission(core, info);
	}

	RetransmissionTestCore core;
	LifecycleHookTestSession session{ core };
	bool releaseInitialized{};
};

TEST_F(RetransmissionLifecycleTest, PreviousGenerationCannotDisconnectOrSendOnReusedSession)
{
	const auto oldGeneration = session.GetSessionGeneration();
	session.DoDisconnect(DISCONNECT_REASON::NORMAL);
	RUDPSessionBehaviorAccess::BeginIOShutdown(session);
	ASSERT_TRUE(RUDPSessionBehaviorAccess::CanFinalizeIO(session));
	std::ignore = MultiSocketRUDPCoreTestAccess::TakeReleaseSessionIds(core);
	RUDPSessionBehaviorAccess::InitializeSession(session);
	RUDPSessionBehaviorAccess::SetConnected(session);
	const auto newRto = session.GetRetransmissionTimeoutMs();

	for (const PacketRetransmissionCount limit : { 1, 2 })
	{
		MultiSocketRUDPCoreTestAccess::SetRetransmissionLimit(core, limit);
		auto packet = MakePacket(oldGeneration);
		ASSERT_NE(packet, nullptr);
		Process(packet.get());
		EXPECT_EQ(packet->refCount.load(), 1);
		EXPECT_TRUE(session.IsConnected());
		EXPECT_EQ(session.GetDisconnectedReason(), DISCONNECT_REASON::NOT_DISCONNECTED);
		EXPECT_EQ(session.GetRetransmissionTimeoutMs(), newRto);
		EXPECT_EQ(core.sendCalls, 0);
		EXPECT_TRUE(RUDPSessionBehaviorAccess::GetSendContext(session).IsSendPacketInfoQueueEmpty());
		EXPECT_TRUE(MultiSocketRUDPCoreTestAccess::TakeReleaseSessionIds(core).empty());
		EXPECT_TRUE(RUDPSessionBehaviorAccess::CanFinalizeIO(session));
	}
}

TEST_F(RetransmissionLifecycleTest, CurrentGenerationAtLimitDisconnectsOnceAndReturnsCount)
{
	MultiSocketRUDPCoreTestAccess::SetRetransmissionLimit(core, 1);
	auto packet = MakePacket(session.GetSessionGeneration());
	ASSERT_NE(packet, nullptr);
	Process(packet.get());
	EXPECT_TRUE(session.IsReleasing());
	EXPECT_EQ(session.GetDisconnectedReason(), DISCONNECT_REASON::BY_RETRANSMISSION);
	EXPECT_EQ(core.sendCalls, 0);
	EXPECT_EQ(packet->refCount.load(), 1);
	EXPECT_TRUE(RUDPSessionBehaviorAccess::CanFinalizeIO(session));
	EXPECT_EQ(MultiSocketRUDPCoreTestAccess::TakeReleaseSessionIds(core),
		std::vector<SessionIdType>{ session.GetSessionId() });

	Process(packet.get());
	EXPECT_TRUE(MultiSocketRUDPCoreTestAccess::TakeReleaseSessionIds(core).empty());
	EXPECT_EQ(packet->refCount.load(), 1);
	RUDPSessionBehaviorAccess::BeginIOShutdown(session);
	EXPECT_EQ(session.disconnectedCount, 1);
}

TEST_F(RetransmissionLifecycleTest, ReleasingSessionRejectsTimeoutAndResend)
{
	auto packet = MakePacket(session.GetSessionGeneration());
	ASSERT_NE(packet, nullptr);
	session.DoDisconnect(DISCONNECT_REASON::NORMAL);
	const auto rto = session.GetRetransmissionTimeoutMs();
	std::ignore = MultiSocketRUDPCoreTestAccess::TakeReleaseSessionIds(core);
	Process(packet.get());
	EXPECT_EQ(core.sendCalls, 0);
	EXPECT_EQ(session.GetRetransmissionTimeoutMs(), rto);
	EXPECT_EQ(session.GetDisconnectedReason(), DISCONNECT_REASON::NORMAL);
	EXPECT_TRUE(MultiSocketRUDPCoreTestAccess::TakeReleaseSessionIds(core).empty());
	EXPECT_TRUE(RUDPSessionBehaviorAccess::CanFinalizeIO(session));
	EXPECT_EQ(packet->refCount.load(), 1);
}

TEST_F(RetransmissionLifecycleTest, ActiveResendBlocksReleaseThroughFailureCleanup)
{
	auto packet = MakePacket(session.GetSessionGeneration());
	ASSERT_NE(packet, nullptr);
	const auto generation = session.GetSessionGeneration();
	std::binary_semaphore entered{ 0 };
	std::binary_semaphore resume{ 0 };
	core.sendCallback = [&]()
		{
			entered.release();
			resume.acquire();
			return false;
		};
	std::jthread retransmitter([&]() { Process(packet.get()); });
	if (not entered.try_acquire_for(std::chrono::seconds(5)))
	{
		resume.release();
		retransmitter.join();
		FAIL() << "Retransmission did not reach the send callback";
	}

	session.DoDisconnect(DISCONNECT_REASON::NORMAL);
	EXPECT_FALSE(RUDPSessionBehaviorAccess::CanFinalizeIO(session));
	RUDPSessionBehaviorAccess::BeginIOShutdown(session);
	EXPECT_EQ(session.disconnectedCount, 0);
	EXPECT_EQ(session.GetSessionGeneration(), generation);
	resume.release();
	retransmitter.join();

	EXPECT_EQ(session.GetDisconnectedReason(), DISCONNECT_REASON::NORMAL);
	EXPECT_EQ(packet->refCount.load(), 1);
	EXPECT_TRUE(RUDPSessionBehaviorAccess::CanFinalizeIO(session));
	RUDPSessionBehaviorAccess::BeginIOShutdown(session);
	EXPECT_EQ(session.disconnectedCount, 1);
}

TEST_F(RetransmissionLifecycleTest, SuccessfulResendReturnsCount)
{
	auto packet = MakePacket(session.GetSessionGeneration());
	ASSERT_NE(packet, nullptr);
	Process(packet.get());
	EXPECT_EQ(core.sendCalls, 1);
	EXPECT_TRUE(session.IsConnected());
	EXPECT_EQ(packet->refCount.load(), 1);
	EXPECT_TRUE(RUDPSessionBehaviorAccess::CanFinalizeIO(session));
}

TEST_F(RetransmissionLifecycleTest, FailedResendDisconnectsAndReturnsCount)
{
	auto packet = MakePacket(session.GetSessionGeneration());
	ASSERT_NE(packet, nullptr);
	core.sendCallback = []() { return false; };
	Process(packet.get());
	EXPECT_EQ(core.sendCalls, 1);
	EXPECT_EQ(session.GetDisconnectedReason(), DISCONNECT_REASON::BY_ERROR);
	EXPECT_TRUE(session.IsReleasing());
	EXPECT_EQ(packet->refCount.load(), 1);
	EXPECT_TRUE(RUDPSessionBehaviorAccess::CanFinalizeIO(session));
}

class SessionReceiveLifecycleTest : public SessionSendLifecycleTest
{
protected:
	using BufferPtr = std::unique_ptr<NetBuffer, decltype(&NetBuffer::Free)>;

	void SetUp() override
	{
		SessionSendLifecycleTest::SetUp();
		session.RegisterContentHandler();
	}

	BufferPtr MakeContent(const PacketSequence sequence)
	{
		BufferPtr buffer(NetBuffer::Alloc(), &NetBuffer::Free);
		if (buffer)
		{
			*buffer << sequence << LifecycleContentPacket::PACKET_ID << static_cast<unsigned int>(sequence);
		}
		return buffer;
	}
};

TEST_F(SessionReceiveLifecycleTest, DisconnectInsideHandlerStopsHeldPacketsAndPreservesReason)
{
	const auto buffersBefore = NetBuffer::GetUsingSerializeBufNodeCount();
	auto first = MakeContent(0);
	auto second = MakeContent(1);
	auto third = MakeContent(2);
	ASSERT_TRUE(first && second && third);
	ASSERT_TRUE(RUDPSessionBehaviorAccess::OnRecvPacket(session, *second));
	ASSERT_TRUE(RUDPSessionBehaviorAccess::OnRecvPacket(session, *third));
	std::vector<unsigned int> handled;
	session.contentCallback = [&](const unsigned int value)
		{
			handled.push_back(value);
			if (value == 0)
			{
				session.DoDisconnect(DISCONNECT_REASON::NORMAL);
			}
		};
	const auto windowEnd = RUDPSessionBehaviorAccess::GetReceiveWindowEnd(session);

	EXPECT_FALSE(RUDPSessionBehaviorAccess::OnRecvPacket(session, *first));
	EXPECT_EQ(handled, std::vector<unsigned int>{ 0 });
	EXPECT_EQ(RUDPSessionBehaviorAccess::GetReceiveWindowEnd(session), windowEnd + 1);
	// The packet processor requests BY_ERROR when OnRecvPacket returns false.
	session.DoDisconnect(DISCONNECT_REASON::BY_ERROR);
	EXPECT_EQ(session.GetDisconnectedReason(), DISCONNECT_REASON::NORMAL);
	EXPECT_EQ(MultiSocketRUDPCoreTestAccess::TakeReleaseSessionIds(core).size(), 1u);

	RUDPSessionBehaviorAccess::BeginIOShutdown(session);
	ASSERT_TRUE(RUDPSessionBehaviorAccess::CanFinalizeIO(session));
	RUDPSessionBehaviorAccess::InitializeSession(session);
	first.reset();
	second.reset();
	third.reset();
	EXPECT_EQ(NetBuffer::GetUsingSerializeBufNodeCount(), buffersBefore);
}

TEST_F(SessionReceiveLifecycleTest, ReleasingSessionDoesNotEnterContentHandler)
{
	auto packet = MakeContent(0);
	ASSERT_NE(packet, nullptr);
	int handled = 0;
	session.contentCallback = [&](unsigned int) { ++handled; };
	session.DoDisconnect(DISCONNECT_REASON::NORMAL);
	const auto windowEnd = RUDPSessionBehaviorAccess::GetReceiveWindowEnd(session);
	EXPECT_FALSE(RUDPSessionBehaviorAccess::OnRecvPacket(session, *packet));
	EXPECT_EQ(handled, 0);
	EXPECT_EQ(RUDPSessionBehaviorAccess::GetReceiveWindowEnd(session), windowEnd);
}

TEST_F(SessionReceiveLifecycleTest, ConcurrentDisconnectStopsHeldPacketsAfterRunningHandlerReturns)
{
	auto first = MakeContent(0);
	auto second = MakeContent(1);
	ASSERT_TRUE(first && second);
	ASSERT_TRUE(RUDPSessionBehaviorAccess::OnRecvPacket(session, *second));
	std::binary_semaphore entered{ 0 };
	std::binary_semaphore resume{ 0 };
	std::vector<unsigned int> handled;
	session.contentCallback = [&](const unsigned int value)
		{
			handled.push_back(value);
			if (value == 0)
			{
				entered.release();
				resume.acquire();
			}
		};
	bool processed = true;
	std::jthread receiver([&]() { processed = RUDPSessionBehaviorAccess::OnRecvPacket(session, *first); });
	if (not entered.try_acquire_for(std::chrono::seconds(5)))
	{
		resume.release();
		receiver.join();
		FAIL() << "Content packet did not reach its handler";
	}
	session.DoDisconnect(DISCONNECT_REASON::NORMAL);
	resume.release();
	receiver.join();
	EXPECT_FALSE(processed);
	EXPECT_EQ(handled, std::vector<unsigned int>{ 0 });
	EXPECT_EQ(session.GetDisconnectedReason(), DISCONNECT_REASON::NORMAL);
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

TEST(RUDPSessionBehaviorTest, OnRecvPacketUnknownPacketIdReturnsFalse)
{
	MultiSocketRUDPCore core{ L"", L"" };
	RUDPSessionBehaviorAccess::SetMaximumPacketHoldingQueueSize(4);
	SessionBehaviorTestSession session{ core };
	RUDPSessionBehaviorAccess::InitializeSession(session);
	NetBuffer* buffer = NetBuffer::Alloc();
	ASSERT_NE(buffer, nullptr);

	*buffer << PacketSequence{ 0 } << PacketId{ 9999 };

	EXPECT_FALSE(RUDPSessionBehaviorAccess::OnRecvPacket(session, *buffer));

	NetBuffer::Free(buffer);
}

// ------------------------------------------------------------
// Verifies that receive I/O, receive logic, send, and completion work each block finalization.
// ------------------------------------------------------------
TEST(RUDPSessionBehaviorTest, FinalizeBarrierWaitsForReceiveIoLogicAndSendCompletion)
{
	MultiSocketRUDPCore core{ L"", L"" };
	SessionBehaviorTestSession session{ core };
	auto& recvBuffer = RUDPSessionBehaviorAccess::GetRecvBuffer(session);
	auto& sendMode = RUDPSessionBehaviorAccess::GetSendContext(session).GetIOMode();

	EXPECT_TRUE(RUDPSessionBehaviorAccess::CanFinalizeIO(session));
	recvBuffer.BeginRecvIo();
	EXPECT_FALSE(RUDPSessionBehaviorAccess::CanFinalizeIO(session));
	recvBuffer.BeginRecvLogic();
	recvBuffer.CompleteRecvIo();
	EXPECT_FALSE(RUDPSessionBehaviorAccess::CanFinalizeIO(session));
	recvBuffer.CompleteRecvLogic();
	EXPECT_TRUE(RUDPSessionBehaviorAccess::CanFinalizeIO(session));
	sendMode.store(IO_MODE::IO_SENDING);
	EXPECT_FALSE(RUDPSessionBehaviorAccess::CanFinalizeIO(session));
	sendMode.store(IO_MODE::IO_NONE_SENDING);
	EXPECT_TRUE(RUDPSessionBehaviorAccess::CanFinalizeIO(session));
	RUDPSessionBehaviorAccess::BeginIOCompletion(session);
	EXPECT_FALSE(RUDPSessionBehaviorAccess::CanFinalizeIO(session));
	RUDPSessionBehaviorAccess::CompleteIOCompletion(session);
	EXPECT_TRUE(RUDPSessionBehaviorAccess::CanFinalizeIO(session));
}

// ------------------------------------------------------------
// Verifies that I/O shutdown waits for receive logic drain and calls OnDisconnected exactly once.
// ------------------------------------------------------------
TEST(RUDPSessionBehaviorTest, IOShutdownWaitsForRecvLogicBeforeCallingDisconnectedHook)
{
	MultiSocketRUDPCore core{ L"", L"" };
	LifecycleHookTestSession session{ core };
	RUDPSessionBehaviorAccess::SetReleasing(session);
	auto& recvBuffer = RUDPSessionBehaviorAccess::GetRecvBuffer(session);
	recvBuffer.BeginRecvLogic();

	RUDPSessionBehaviorAccess::BeginIOShutdown(session);
	EXPECT_EQ(session.disconnectedCount, 0);

	recvBuffer.CompleteRecvLogic();
	RUDPSessionBehaviorAccess::BeginIOShutdown(session);
	RUDPSessionBehaviorAccess::BeginIOShutdown(session);
	EXPECT_EQ(session.disconnectedCount, 1);
}

TEST(RUDPSessionBehaviorTest, ReceiveOrderComparisonUsesFullPacketSequenceWidth)
{
	constexpr PacketSequence twoToTheThirtySecond = PacketSequence{ 1 } << 32;
	constexpr PacketSequence maxSequence = ~PacketSequence{ 0 };

	EXPECT_FALSE(RUDPSessionBehaviorAccess::IsOlderRecvSequence(twoToTheThirtySecond, 0));
	EXPECT_TRUE(RUDPSessionBehaviorAccess::IsOlderRecvSequence(0, twoToTheThirtySecond));
	EXPECT_FALSE(RUDPSessionBehaviorAccess::IsOlderRecvSequence(0, maxSequence));
	EXPECT_TRUE(RUDPSessionBehaviorAccess::IsOlderRecvSequence(maxSequence, 0));
}

TEST(RUDPSessionBehaviorTest, OnSendReplyErasesTrackedSendPacketInfo)
{
	MultiSocketRUDPCore core{ L"", L"" };
	SessionBehaviorTestSession session{ core };
	NetBuffer* sendBuffer = NetBuffer::Alloc();
	SendPacketInfo* info = sendPacketInfoPool->Alloc();
	ASSERT_NE(sendBuffer, nullptr);
	ASSERT_NE(info, nullptr);

	constexpr PacketSequence sequence = 0;
	info->Initialize(&session, session.GetSessionGeneration(), sendBuffer, sequence, false);
	RUDPSessionBehaviorAccess::GetSendContext(session).InsertSendPacketInfo(sequence, info);
	ASSERT_EQ(RUDPSessionBehaviorAccess::GetSendContext(session).FindSendPacketInfo(sequence), info);

	NetBuffer reply;
	reply << sequence << BYTE{ 1 };
	RUDPSessionBehaviorAccess::OnSendReply(session, reply);

	EXPECT_EQ(RUDPSessionBehaviorAccess::GetSendContext(session).FindSendPacketInfo(sequence), nullptr);
	EXPECT_TRUE(info->isErasedPacketInfo.load(std::memory_order_acquire));

	SendPacketInfo::Free(info);
}

TEST(RUDPSessionBehaviorTest, InitializeSessionResetsReusableStateAndUsesCoreRtoOptions)
{
	MultiSocketRUDPCore core{ L"", L"" };
	MultiSocketRUDPCoreTestAccess::SetTimingOptions(core, 100, 250, 100, 1000);
	RUDPSessionBehaviorAccess::SetMaximumPacketHoldingQueueSize(4);
	SessionBehaviorTestSession session{ core };
	RUDPSessionBehaviorAccess::SetConnected(session);
	RUDPSessionBehaviorAccess::SetReleasing(session);
	RUDPSessionBehaviorAccess::SetDisconnectedReason(session, DISCONNECT_REASON::BY_ERROR);
	RUDPSessionBehaviorAccess::RefreshLastReceivedPacketTime(session, 500);
	std::ignore = RUDPSessionBehaviorAccess::GetSendContext(session).IncrementLastSendPacketSequence();
	sockaddr_in clientAddress{};
	clientAddress.sin_family = AF_INET;
	clientAddress.sin_port = htons(12000);
	clientAddress.sin_addr.S_un.S_addr = htonl(INADDR_LOOPBACK);
	RUDPSessionBehaviorAccess::SetClientAddress(session, clientAddress);
	const uint32_t generationBefore = session.GetSessionGeneration();

	RUDPSessionBehaviorAccess::InitializeSession(session);

	EXPECT_EQ(session.GetSessionGeneration(), generationBefore + 1);
	EXPECT_EQ(session.GetSessionState(), SESSION_STATE::DISCONNECTED);
	EXPECT_FALSE(session.IsReleasing());
	EXPECT_EQ(session.GetDisconnectedReason(), DISCONNECT_REASON::NOT_DISCONNECTED);
	EXPECT_EQ(session.GetSocketAddress().sin_port, 0);
	EXPECT_EQ(RUDPSessionBehaviorAccess::GetSendContext(session).GetLastSendPacketSequence(), 0);
	EXPECT_EQ(session.GetRetransmissionTimeoutMs(), 250u);
}

TEST(RUDPSessionBehaviorTest, HeartbeatGuardHonorsStateReleaseAndThreshold)
{
	MultiSocketRUDPCore core{ L"", L"" };
	MultiSocketRUDPCoreTestAccess::SetTimingOptions(core, 100, 250, 100, 1000);
	SessionBehaviorTestSession session{ core };
	RUDPSessionBehaviorAccess::InitializeSession(session);
	RUDPSessionBehaviorAccess::RefreshLastReceivedPacketTime(session, 1000);

	EXPECT_FALSE(RUDPSessionBehaviorAccess::NeedToSendHeartbeat(session, 1100));

	RUDPSessionBehaviorAccess::SetConnected(session);
	EXPECT_FALSE(RUDPSessionBehaviorAccess::NeedToSendHeartbeat(session, 1099));
	EXPECT_TRUE(RUDPSessionBehaviorAccess::NeedToSendHeartbeat(session, 1100));

	RUDPSessionBehaviorAccess::SetReleasing(session);
	EXPECT_FALSE(RUDPSessionBehaviorAccess::NeedToSendHeartbeat(session, 1200));
}

TEST(RUDPSessionBehaviorTest, ReservedSessionTimeoutUsesInclusiveBoundary)
{
	MultiSocketRUDPCore core{ L"", L"" };
	SessionBehaviorTestSession session{ core };
	RUDPSessionBehaviorAccess::SetReservedSessionTimeoutMs(100);
	RUDPSessionBehaviorAccess::SetSessionReservedTime(session, 1000);

	EXPECT_FALSE(RUDPSessionBehaviorAccess::CheckReservedSessionTimeout(session, 1100));
	RUDPSessionBehaviorAccess::SetReserved(session);
	EXPECT_FALSE(RUDPSessionBehaviorAccess::CheckReservedSessionTimeout(session, 1099));
	EXPECT_TRUE(RUDPSessionBehaviorAccess::CheckReservedSessionTimeout(session, 1100));

	RUDPSessionBehaviorAccess::SetReservedSessionTimeoutMs(30000);
}

TEST(RUDPSessionBehaviorTest, CanProcessPacketRequiresMatchingAddressAndNonReleasingSession)
{
	MultiSocketRUDPCore core{ L"", L"" };
	SessionBehaviorTestSession session{ core };
	sockaddr_in clientAddress{};
	clientAddress.sin_family = AF_INET;
	clientAddress.sin_port = htons(12000);
	clientAddress.sin_addr.S_un.S_addr = htonl(INADDR_LOOPBACK);
	RUDPSessionBehaviorAccess::SetClientAddress(session, clientAddress);

	EXPECT_TRUE(RUDPSessionBehaviorAccess::CanProcessPacket(session, clientAddress));

	sockaddr_in wrongPort = clientAddress;
	wrongPort.sin_port = htons(12001);
	EXPECT_FALSE(RUDPSessionBehaviorAccess::CanProcessPacket(session, wrongPort));

	sockaddr_in wrongAddress = clientAddress;
	wrongAddress.sin_addr.S_un.S_addr = htonl(0x7F000002);
	EXPECT_FALSE(RUDPSessionBehaviorAccess::CanProcessPacket(session, wrongAddress));

	RUDPSessionBehaviorAccess::SetReleasing(session);
	EXPECT_FALSE(RUDPSessionBehaviorAccess::CanProcessPacket(session, clientAddress));
}

TEST(RUDPSessionBehaviorTest, TryConnectRejectsInvalidSequenceSessionIdAndStateWithoutMutation)
{
	MultiSocketRUDPCore core{ L"", L"" };
	SessionBehaviorTestSession session{ core };
	RUDPSessionBehaviorAccess::SetSessionId(session, 7);
	RUDPSessionBehaviorAccess::SetReserved(session);
	sockaddr_in clientAddress{};
	clientAddress.sin_family = AF_INET;
	clientAddress.sin_port = htons(12000);
	clientAddress.sin_addr.S_un.S_addr = htonl(INADDR_LOOPBACK);

	NetBuffer invalidSequence;
	invalidSequence << PacketSequence{ LOGIN_PACKET_SEQUENCE + 1 } << SessionIdType{ 7 };
	EXPECT_FALSE(RUDPSessionBehaviorAccess::TryConnect(session, invalidSequence, clientAddress));
	EXPECT_TRUE(session.IsReserved());

	NetBuffer invalidSessionId;
	invalidSessionId << PacketSequence{ LOGIN_PACKET_SEQUENCE } << SessionIdType{ 8 };
	EXPECT_FALSE(RUDPSessionBehaviorAccess::TryConnect(session, invalidSessionId, clientAddress));
	EXPECT_TRUE(session.IsReserved());

	RUDPSessionBehaviorAccess::InitializeSession(session);
	NetBuffer invalidState;
	invalidState << PacketSequence{ LOGIN_PACKET_SEQUENCE } << SessionIdType{ 7 };
	EXPECT_FALSE(RUDPSessionBehaviorAccess::TryConnect(session, invalidState, clientAddress));
	EXPECT_EQ(session.GetSessionState(), SESSION_STATE::DISCONNECTED);
	EXPECT_EQ(session.GetSocketAddress().sin_port, 0);
}

TEST(RUDPSessionBehaviorTest, OnSendReplyIgnoresFutureAndUnknownSequences)
{
	MultiSocketRUDPCore core{ L"", L"" };
	SessionBehaviorTestSession session{ core };
	NetBuffer* sendBuffer = NetBuffer::Alloc();
	SendPacketInfo* info = sendPacketInfoPool->Alloc();
	ASSERT_NE(sendBuffer, nullptr);
	ASSERT_NE(info, nullptr);
	info->Initialize(&session, session.GetSessionGeneration(), sendBuffer, 1, false);
	RUDPSessionBehaviorAccess::GetSendContext(session).InsertSendPacketInfo(1, info);

	NetBuffer futureReply;
	futureReply << PacketSequence{ 2 } << BYTE{ 1 };
	RUDPSessionBehaviorAccess::OnSendReply(session, futureReply);
	EXPECT_EQ(RUDPSessionBehaviorAccess::GetSendContext(session).FindSendPacketInfo(1), info);

	NetBuffer unknownReply;
	unknownReply << PacketSequence{ 0 } << BYTE{ 1 };
	RUDPSessionBehaviorAccess::OnSendReply(session, unknownReply);
	EXPECT_EQ(RUDPSessionBehaviorAccess::GetSendContext(session).FindSendPacketInfo(1), info);

	EXPECT_EQ(RUDPSessionBehaviorAccess::GetSendContext(session).FindAndEraseSendPacketInfo(1), info);
	SendPacketInfo::Free(info);
	SendPacketInfo::Free(info);
}
