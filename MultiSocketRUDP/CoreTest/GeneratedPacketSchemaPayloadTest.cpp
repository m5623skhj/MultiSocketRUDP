// Generated from PacketDefine.yml / tests/Structs.yml.
#include "PreCompile.h"
#include <gtest/gtest.h>
#include <array>
#include "GeneratedPacketSchema/Protocol.h"

TEST(GeneratedPacketSchemaPayloadTest, GeneratedEnvelopeReq)
{
	GeneratedEnvelopeReq packet;
	packet.sequence = -1234567;
	packet.user = {81985529216486896ULL, "\x70\x61\x63\x6b\x65\x74\x2d\xed\x95\x9c\xea\xb8\x80\x2d\x32", {4.25f, 5.25f}, {-1234563, -1234562}, {-1234561, -1234562}, {"\x70\x61\x63\x6b\x65\x74\x2d\xed\x95\x9c\xea\xb8\x80\x2d\x36"}};
	packet.users = {{81985529216486897ULL, "\x70\x61\x63\x6b\x65\x74\x2d\xed\x95\x9c\xea\xb8\x80\x2d\x33", {5.25f, 6.25f}, {-1234562, -1234561}, {-1234560, -1234561}, {"\x70\x61\x63\x6b\x65\x74\x2d\xed\x95\x9c\xea\xb8\x80\x2d\x37"}}, {81985529216486898ULL, "\x70\x61\x63\x6b\x65\x74\x2d\xed\x95\x9c\xea\xb8\x80\x2d\x34", {6.25f, 7.25f}, {-1234561, -1234560}, {-1234559, -1234560}, {"\x70\x61\x63\x6b\x65\x74\x2d\xed\x95\x9c\xea\xb8\x80\x2d\x38"}}};
	packet.byId = {{-1234563, {81985529216486901ULL, "\x70\x61\x63\x6b\x65\x74\x2d\xed\x95\x9c\xea\xb8\x80\x2d\x37", {9.25f, 10.25f}, {-1234558, -1234557}, {-1234556, -1234557}, {"\x70\x61\x63\x6b\x65\x74\x2d\xed\x95\x9c\xea\xb8\x80\x2d\x31\x31"}}}, {-1234564, {81985529216486900ULL, "\x70\x61\x63\x6b\x65\x74\x2d\xed\x95\x9c\xea\xb8\x80\x2d\x36", {8.25f, 9.25f}, {-1234559, -1234558}, {-1234557, -1234558}, {"\x70\x61\x63\x6b\x65\x74\x2d\xed\x95\x9c\xea\xb8\x80\x2d\x31\x30"}}}};
	packet.lookup = {{-1234563, {81985529216486901ULL, "\x70\x61\x63\x6b\x65\x74\x2d\xed\x95\x9c\xea\xb8\x80\x2d\x37", {9.25f, 10.25f}, {-1234558, -1234557}, {-1234556, -1234557}, {"\x70\x61\x63\x6b\x65\x74\x2d\xed\x95\x9c\xea\xb8\x80\x2d\x31\x31"}}}};
	packet.history = {{81985529216486900ULL, "\x70\x61\x63\x6b\x65\x74\x2d\xed\x95\x9c\xea\xb8\x80\x2d\x36", {8.25f, 9.25f}, {-1234559, -1234558}, {-1234557, -1234558}, {"\x70\x61\x63\x6b\x65\x74\x2d\xed\x95\x9c\xea\xb8\x80\x2d\x31\x30"}}, {81985529216486901ULL, "\x70\x61\x63\x6b\x65\x74\x2d\xed\x95\x9c\xea\xb8\x80\x2d\x37", {9.25f, 10.25f}, {-1234558, -1234557}, {-1234556, -1234557}, {"\x70\x61\x63\x6b\x65\x74\x2d\xed\x95\x9c\xea\xb8\x80\x2d\x31\x31"}}};
	packet.emptyValues = {{}, {}};
	EXPECT_EQ(packet.GetPacketId(), 1u);
	NetBuffer buffer;
	packet.PacketToBuffer(buffer);
	const std::array<unsigned char, 678> expected = {121, 41, 237, 255, 240, 205, 171, 137, 103, 69, 35, 1, 15, 0, 112, 97, 99, 107, 101, 116, 45, 237, 149, 156, 234, 184, 128, 45, 50, 0, 0, 136, 64, 0, 0, 168, 64, 2, 0, 0, 0, 125, 41, 237, 255, 126, 41, 237, 255, 1, 2, 0, 0, 0, 127, 41, 237, 255, 126, 41, 237, 255, 1, 0, 0, 0, 15, 0, 112, 97, 99, 107, 101, 116, 45, 237, 149, 156, 234, 184, 128, 45, 54, 2, 0, 0, 0, 241, 205, 171, 137, 103, 69, 35, 1, 15, 0, 112, 97, 99, 107, 101, 116, 45, 237, 149, 156, 234, 184, 128, 45, 51, 0, 0, 168, 64, 0, 0, 200, 64, 2, 0, 0, 0, 126, 41, 237, 255, 127, 41, 237, 255, 1, 2, 0, 0, 0, 128, 41, 237, 255, 127, 41, 237, 255, 1, 0, 0, 0, 15, 0, 112, 97, 99, 107, 101, 116, 45, 237, 149, 156, 234, 184, 128, 45, 55, 242, 205, 171, 137, 103, 69, 35, 1, 15, 0, 112, 97, 99, 107, 101, 116, 45, 237, 149, 156, 234, 184, 128, 45, 52, 0, 0, 200, 64, 0, 0, 232, 64, 2, 0, 0, 0, 127, 41, 237, 255, 128, 41, 237, 255, 1, 2, 0, 0, 0, 129, 41, 237, 255, 128, 41, 237, 255, 1, 0, 0, 0, 15, 0, 112, 97, 99, 107, 101, 116, 45, 237, 149, 156, 234, 184, 128, 45, 56, 1, 2, 0, 0, 0, 125, 41, 237, 255, 245, 205, 171, 137, 103, 69, 35, 1, 15, 0, 112, 97, 99, 107, 101, 116, 45, 237, 149, 156, 234, 184, 128, 45, 55, 0, 0, 20, 65, 0, 0, 36, 65, 2, 0, 0, 0, 130, 41, 237, 255, 131, 41, 237, 255, 1, 2, 0, 0, 0, 132, 41, 237, 255, 131, 41, 237, 255, 1, 0, 0, 0, 16, 0, 112, 97, 99, 107, 101, 116, 45, 237, 149, 156, 234, 184, 128, 45, 49, 49, 124, 41, 237, 255, 244, 205, 171, 137, 103, 69, 35, 1, 15, 0, 112, 97, 99, 107, 101, 116, 45, 237, 149, 156, 234, 184, 128, 45, 54, 0, 0, 4, 65, 0, 0, 20, 65, 2, 0, 0, 0, 129, 41, 237, 255, 130, 41, 237, 255, 1, 2, 0, 0, 0, 131, 41, 237, 255, 130, 41, 237, 255, 1, 0, 0, 0, 16, 0, 112, 97, 99, 107, 101, 116, 45, 237, 149, 156, 234, 184, 128, 45, 49, 48, 1, 0, 0, 0, 125, 41, 237, 255, 245, 205, 171, 137, 103, 69, 35, 1, 15, 0, 112, 97, 99, 107, 101, 116, 45, 237, 149, 156, 234, 184, 128, 45, 55, 0, 0, 20, 65, 0, 0, 36, 65, 2, 0, 0, 0, 130, 41, 237, 255, 131, 41, 237, 255, 1, 2, 0, 0, 0, 132, 41, 237, 255, 131, 41, 237, 255, 1, 0, 0, 0, 16, 0, 112, 97, 99, 107, 101, 116, 45, 237, 149, 156, 234, 184, 128, 45, 49, 49, 2, 0, 0, 0, 0, 0, 0, 0, 244, 205, 171, 137, 103, 69, 35, 1, 15, 0, 112, 97, 99, 107, 101, 116, 45, 237, 149, 156, 234, 184, 128, 45, 54, 0, 0, 4, 65, 0, 0, 20, 65, 2, 0, 0, 0, 129, 41, 237, 255, 130, 41, 237, 255, 1, 2, 0, 0, 0, 131, 41, 237, 255, 130, 41, 237, 255, 1, 0, 0, 0, 16, 0, 112, 97, 99, 107, 101, 116, 45, 237, 149, 156, 234, 184, 128, 45, 49, 48, 245, 205, 171, 137, 103, 69, 35, 1, 15, 0, 112, 97, 99, 107, 101, 116, 45, 237, 149, 156, 234, 184, 128, 45, 55, 0, 0, 20, 65, 0, 0, 36, 65, 2, 0, 0, 0, 130, 41, 237, 255, 131, 41, 237, 255, 1, 2, 0, 0, 0, 132, 41, 237, 255, 131, 41, 237, 255, 1, 0, 0, 0, 16, 0, 112, 97, 99, 107, 101, 116, 45, 237, 149, 156, 234, 184, 128, 45, 49, 49, 2, 0, 0, 0};
	ASSERT_EQ(buffer.GetUseSize(), expected.size());
	for (size_t index = 0; index < expected.size(); ++index)
		EXPECT_EQ(static_cast<unsigned char>(buffer.GetReadBufferPtr()[index]), expected[index]);
	GeneratedEnvelopeReq decoded;
	decoded.BufferToPacket(buffer);
	EXPECT_EQ(buffer.GetUseSize(), 0);
	NetBuffer roundTrip;
	decoded.PacketToBuffer(roundTrip);
	ASSERT_EQ(roundTrip.GetUseSize(), expected.size());
	for (size_t index = 0; index < expected.size(); ++index)
		EXPECT_EQ(static_cast<unsigned char>(roundTrip.GetReadBufferPtr()[index]), expected[index]);
}

TEST(GeneratedPacketSchemaPayloadTest, GeneratedEmptyRes)
{
	GeneratedEmptyRes packet;
	EXPECT_EQ(packet.GetPacketId(), 2u);
	NetBuffer buffer;
	packet.PacketToBuffer(buffer);
	const std::array<unsigned char, 0> expected = {};
	ASSERT_EQ(buffer.GetUseSize(), expected.size());
	for (size_t index = 0; index < expected.size(); ++index)
		EXPECT_EQ(static_cast<unsigned char>(buffer.GetReadBufferPtr()[index]), expected[index]);
	GeneratedEmptyRes decoded;
	decoded.BufferToPacket(buffer);
	EXPECT_EQ(buffer.GetUseSize(), 0);
	NetBuffer roundTrip;
	decoded.PacketToBuffer(roundTrip);
	ASSERT_EQ(roundTrip.GetUseSize(), expected.size());
	for (size_t index = 0; index < expected.size(); ++index)
		EXPECT_EQ(static_cast<unsigned char>(roundTrip.GetReadBufferPtr()[index]), expected[index]);
}
