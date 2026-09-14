// Generated from PacketDefine.yml / tests/Structs.yml.
#include "PreCompile.h"
#include <gtest/gtest.h>
#include <array>
#include "../ContentsServer/Protocol.h"

TEST(GeneratedPacketPayloadTest, Ping)
{
	Ping packet;
	EXPECT_EQ(packet.GetPacketId(), 1u);
	NetBuffer buffer;
	packet.PacketToBuffer(buffer);
	const std::array<unsigned char, 0> expected = {};
	ASSERT_EQ(buffer.GetUseSize(), expected.size());
	for (size_t index = 0; index < expected.size(); ++index)
		EXPECT_EQ(static_cast<unsigned char>(buffer.GetReadBufferPtr()[index]), expected[index]);
	Ping decoded;
	decoded.BufferToPacket(buffer);
	EXPECT_EQ(buffer.GetUseSize(), 0);
	NetBuffer roundTrip;
	decoded.PacketToBuffer(roundTrip);
	ASSERT_EQ(roundTrip.GetUseSize(), expected.size());
	for (size_t index = 0; index < expected.size(); ++index)
		EXPECT_EQ(static_cast<unsigned char>(roundTrip.GetReadBufferPtr()[index]), expected[index]);
}

TEST(GeneratedPacketPayloadTest, Pong)
{
	Pong packet;
	EXPECT_EQ(packet.GetPacketId(), 2u);
	NetBuffer buffer;
	packet.PacketToBuffer(buffer);
	const std::array<unsigned char, 0> expected = {};
	ASSERT_EQ(buffer.GetUseSize(), expected.size());
	for (size_t index = 0; index < expected.size(); ++index)
		EXPECT_EQ(static_cast<unsigned char>(buffer.GetReadBufferPtr()[index]), expected[index]);
	Pong decoded;
	decoded.BufferToPacket(buffer);
	EXPECT_EQ(buffer.GetUseSize(), 0);
	NetBuffer roundTrip;
	decoded.PacketToBuffer(roundTrip);
	ASSERT_EQ(roundTrip.GetUseSize(), expected.size());
	for (size_t index = 0; index < expected.size(); ++index)
		EXPECT_EQ(static_cast<unsigned char>(roundTrip.GetReadBufferPtr()[index]), expected[index]);
}

TEST(GeneratedPacketPayloadTest, TestStringPacketReq)
{
	TestStringPacketReq packet;
	packet.testString = "\x70\x61\x63\x6b\x65\x74\x2d\xed\x95\x9c\xea\xb8\x80\x2d\x30";
	EXPECT_EQ(packet.GetPacketId(), 3u);
	NetBuffer buffer;
	packet.PacketToBuffer(buffer);
	const std::array<unsigned char, 17> expected = {15, 0, 112, 97, 99, 107, 101, 116, 45, 237, 149, 156, 234, 184, 128, 45, 48};
	ASSERT_EQ(buffer.GetUseSize(), expected.size());
	for (size_t index = 0; index < expected.size(); ++index)
		EXPECT_EQ(static_cast<unsigned char>(buffer.GetReadBufferPtr()[index]), expected[index]);
	TestStringPacketReq decoded;
	decoded.BufferToPacket(buffer);
	EXPECT_EQ(buffer.GetUseSize(), 0);
	NetBuffer roundTrip;
	decoded.PacketToBuffer(roundTrip);
	ASSERT_EQ(roundTrip.GetUseSize(), expected.size());
	for (size_t index = 0; index < expected.size(); ++index)
		EXPECT_EQ(static_cast<unsigned char>(roundTrip.GetReadBufferPtr()[index]), expected[index]);
}

TEST(GeneratedPacketPayloadTest, TestStringPacketRes)
{
	TestStringPacketRes packet;
	packet.echoString = "\x70\x61\x63\x6b\x65\x74\x2d\xed\x95\x9c\xea\xb8\x80\x2d\x30";
	EXPECT_EQ(packet.GetPacketId(), 4u);
	NetBuffer buffer;
	packet.PacketToBuffer(buffer);
	const std::array<unsigned char, 17> expected = {15, 0, 112, 97, 99, 107, 101, 116, 45, 237, 149, 156, 234, 184, 128, 45, 48};
	ASSERT_EQ(buffer.GetUseSize(), expected.size());
	for (size_t index = 0; index < expected.size(); ++index)
		EXPECT_EQ(static_cast<unsigned char>(buffer.GetReadBufferPtr()[index]), expected[index]);
	TestStringPacketRes decoded;
	decoded.BufferToPacket(buffer);
	EXPECT_EQ(buffer.GetUseSize(), 0);
	NetBuffer roundTrip;
	decoded.PacketToBuffer(roundTrip);
	ASSERT_EQ(roundTrip.GetUseSize(), expected.size());
	for (size_t index = 0; index < expected.size(); ++index)
		EXPECT_EQ(static_cast<unsigned char>(roundTrip.GetReadBufferPtr()[index]), expected[index]);
}

TEST(GeneratedPacketPayloadTest, TestPacketReq)
{
	TestPacketReq packet;
	packet.order = -1234567;
	EXPECT_EQ(packet.GetPacketId(), 5u);
	NetBuffer buffer;
	packet.PacketToBuffer(buffer);
	const std::array<unsigned char, 4> expected = {121, 41, 237, 255};
	ASSERT_EQ(buffer.GetUseSize(), expected.size());
	for (size_t index = 0; index < expected.size(); ++index)
		EXPECT_EQ(static_cast<unsigned char>(buffer.GetReadBufferPtr()[index]), expected[index]);
	TestPacketReq decoded;
	decoded.BufferToPacket(buffer);
	EXPECT_EQ(buffer.GetUseSize(), 0);
	NetBuffer roundTrip;
	decoded.PacketToBuffer(roundTrip);
	ASSERT_EQ(roundTrip.GetUseSize(), expected.size());
	for (size_t index = 0; index < expected.size(); ++index)
		EXPECT_EQ(static_cast<unsigned char>(roundTrip.GetReadBufferPtr()[index]), expected[index]);
}

TEST(GeneratedPacketPayloadTest, TestPacketRes)
{
	TestPacketRes packet;
	packet.order = -1234567;
	EXPECT_EQ(packet.GetPacketId(), 6u);
	NetBuffer buffer;
	packet.PacketToBuffer(buffer);
	const std::array<unsigned char, 4> expected = {121, 41, 237, 255};
	ASSERT_EQ(buffer.GetUseSize(), expected.size());
	for (size_t index = 0; index < expected.size(); ++index)
		EXPECT_EQ(static_cast<unsigned char>(buffer.GetReadBufferPtr()[index]), expected[index]);
	TestPacketRes decoded;
	decoded.BufferToPacket(buffer);
	EXPECT_EQ(buffer.GetUseSize(), 0);
	NetBuffer roundTrip;
	decoded.PacketToBuffer(roundTrip);
	ASSERT_EQ(roundTrip.GetUseSize(), expected.size());
	for (size_t index = 0; index < expected.size(); ++index)
		EXPECT_EQ(static_cast<unsigned char>(roundTrip.GetReadBufferPtr()[index]), expected[index]);
}

TEST(GeneratedPacketPayloadTest, ChannelEchoReq)
{
	ChannelEchoReq packet;
	packet.requestId = 81985529216486895ULL;
	packet.unreliable = 174;
	EXPECT_EQ(packet.GetPacketId(), 7u);
	NetBuffer buffer;
	packet.PacketToBuffer(buffer);
	const std::array<unsigned char, 9> expected = {239, 205, 171, 137, 103, 69, 35, 1, 174};
	ASSERT_EQ(buffer.GetUseSize(), expected.size());
	for (size_t index = 0; index < expected.size(); ++index)
		EXPECT_EQ(static_cast<unsigned char>(buffer.GetReadBufferPtr()[index]), expected[index]);
	ChannelEchoReq decoded;
	decoded.BufferToPacket(buffer);
	EXPECT_EQ(buffer.GetUseSize(), 0);
	NetBuffer roundTrip;
	decoded.PacketToBuffer(roundTrip);
	ASSERT_EQ(roundTrip.GetUseSize(), expected.size());
	for (size_t index = 0; index < expected.size(); ++index)
		EXPECT_EQ(static_cast<unsigned char>(roundTrip.GetReadBufferPtr()[index]), expected[index]);
}

TEST(GeneratedPacketPayloadTest, ChannelEchoRes)
{
	ChannelEchoRes packet;
	packet.requestId = 81985529216486895ULL;
	packet.unreliable = 174;
	EXPECT_EQ(packet.GetPacketId(), 8u);
	NetBuffer buffer;
	packet.PacketToBuffer(buffer);
	const std::array<unsigned char, 9> expected = {239, 205, 171, 137, 103, 69, 35, 1, 174};
	ASSERT_EQ(buffer.GetUseSize(), expected.size());
	for (size_t index = 0; index < expected.size(); ++index)
		EXPECT_EQ(static_cast<unsigned char>(buffer.GetReadBufferPtr()[index]), expected[index]);
	ChannelEchoRes decoded;
	decoded.BufferToPacket(buffer);
	EXPECT_EQ(buffer.GetUseSize(), 0);
	NetBuffer roundTrip;
	decoded.PacketToBuffer(roundTrip);
	ASSERT_EQ(roundTrip.GetUseSize(), expected.size());
	for (size_t index = 0; index < expected.size(); ++index)
		EXPECT_EQ(static_cast<unsigned char>(roundTrip.GetReadBufferPtr()[index]), expected[index]);
}
