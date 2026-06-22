#include "PreCompile.h"
#include <gtest/gtest.h>
#include "../MultiSocketRUDPServer/PacketManager.h"

constexpr PacketId TEST_PACKET_ID_A = 9001;
constexpr PacketId TEST_PACKET_ID_B = 9002;

class TestPacketA : public IPacket
{
public:
	PacketId GetPacketId() const override { return TEST_PACKET_ID_A; }
};

class TestPacketB : public IPacket
{
public:
	PacketId GetPacketId() const override { return TEST_PACKET_ID_B; }
};

class PacketManagerTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		PacketManager::RegisterPacket<TestPacketA>();
		PacketManager::RegisterPacket<TestPacketB>();
	}
};

// ------------------------------------------------------------
// 등록한 패킷은 MakePacket으로 생성할 수 있어야 한다
// ------------------------------------------------------------
TEST_F(PacketManagerTest, MakePacket_ReturnsNonNull_WhenRegistered)
{
	const auto packetA = PacketManager::MakePacket(TEST_PACKET_ID_A);
	const auto packetB = PacketManager::MakePacket(TEST_PACKET_ID_B);

	EXPECT_NE(packetA, nullptr);
	EXPECT_NE(packetB, nullptr);
}

// ------------------------------------------------------------
// MakePacket이 반환한 패킷의 실제 타입이 올바른지 확인
// ------------------------------------------------------------
TEST_F(PacketManagerTest, MakePacket_ReturnsCorrectType)
{
	const auto packetA = PacketManager::MakePacket(TEST_PACKET_ID_A);
	const auto packetB = PacketManager::MakePacket(TEST_PACKET_ID_B);

	ASSERT_NE(packetA, nullptr);
	ASSERT_NE(packetB, nullptr);

	EXPECT_NE(dynamic_cast<TestPacketA*>(packetA.get()), nullptr);
	EXPECT_NE(dynamic_cast<TestPacketB*>(packetB.get()), nullptr);
}

// ------------------------------------------------------------
// MakePacket이 반환하는 패킷의 GetPacketId가 올바른지 확인
// ------------------------------------------------------------
TEST_F(PacketManagerTest, MakePacket_ReturnsPacketWithCorrectId)
{
	const auto packetA = PacketManager::MakePacket(TEST_PACKET_ID_A);
	ASSERT_NE(packetA, nullptr);
	EXPECT_EQ(packetA->GetPacketId(), TEST_PACKET_ID_A);

	const auto packetB = PacketManager::MakePacket(TEST_PACKET_ID_B);
	ASSERT_NE(packetB, nullptr);
	EXPECT_EQ(packetB->GetPacketId(), TEST_PACKET_ID_B);
}

// ------------------------------------------------------------
// MakePacket은 호출마다 새로운 인스턴스를 반환해야 한다
// ------------------------------------------------------------
TEST_F(PacketManagerTest, MakePacket_ReturnsNewInstanceEachCall)
{
	const auto packet1 = PacketManager::MakePacket(TEST_PACKET_ID_A);
	const auto packet2 = PacketManager::MakePacket(TEST_PACKET_ID_A);

	ASSERT_NE(packet1, nullptr);
	ASSERT_NE(packet2, nullptr);

	EXPECT_NE(packet1.get(), packet2.get());
}

// ------------------------------------------------------------
// 등록되지 않은 ID로 MakePacket 호출 시 nullptr을 반환해야 한다
// ------------------------------------------------------------
TEST_F(PacketManagerTest, MakePacket_ReturnsNull_WhenNotRegistered)
{
	constexpr PacketId unregisteredId = 9999;
	const auto packet = PacketManager::MakePacket(unregisteredId);

	EXPECT_EQ(packet, nullptr);
}

// ------------------------------------------------------------
// 같은 ID로 다른 타입을 재등록하면 새 타입으로 덮어써져야 한다
// ------------------------------------------------------------
TEST_F(PacketManagerTest, RegisterPacket_Overwrite_UpdatesFactory)
{
	struct OverridePacket : IPacket
	{
		[[nodiscard]]
		PacketId GetPacketId() const override { return TEST_PACKET_ID_A; }
	};

	PacketManager::RegisterPacket<OverridePacket>();

	const auto packet = PacketManager::MakePacket(TEST_PACKET_ID_A);
	ASSERT_NE(packet, nullptr);

	EXPECT_EQ(dynamic_cast<TestPacketA*>(packet.get()), nullptr);
	EXPECT_NE(dynamic_cast<OverridePacket*>(packet.get()), nullptr);

	PacketManager::RegisterPacket<TestPacketA>();
}
