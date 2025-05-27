#include "PreCompile.h"
#include "Protocol.h"

#pragma region packet function
PacketId Ping::GetPacketId() const
{
	return static_cast<PacketId>(PACKET_ID::PING);
}
PacketId Pong::GetPacketId() const
{
	return static_cast<PacketId>(PACKET_ID::PONG);
}
PacketId TestStringPacketReq::GetPacketId() const
{
	return static_cast<PacketId>(PACKET_ID::TEST_STRING_PACKET_REQ);
}
void TestStringPacketReq::BufferToPacket(NetBuffer& buffer)
{
	SetBufferToParameters(buffer, testString);
}
void TestStringPacketReq::PacketToBuffer(NetBuffer& buffer)
{
	SetParametersToBuffer(buffer, testString);
}
PacketId TestStringPacketRes::GetPacketId() const
{
	return static_cast<PacketId>(PACKET_ID::TEST_STRING_PACKET_RES);
}
void TestStringPacketRes::BufferToPacket(NetBuffer& buffer)
{
	SetBufferToParameters(buffer, echoString);
}
void TestStringPacketRes::PacketToBuffer(NetBuffer& buffer)
{
	SetParametersToBuffer(buffer, echoString);
}
PacketId TestPacketReq::GetPacketId() const
{
	return static_cast<PacketId>(PACKET_ID::TEST_PACKET_REQ);
}
void TestPacketReq::BufferToPacket(NetBuffer& buffer)
{
	SetBufferToParameters(buffer, order);
}
void TestPacketReq::PacketToBuffer(NetBuffer& buffer)
{
	SetParametersToBuffer(buffer, order);
}
PacketId TestPacketRes::GetPacketId() const
{
	return static_cast<PacketId>(PACKET_ID::TEST_PACKET_RES);
}
void TestPacketRes::BufferToPacket(NetBuffer& buffer)
{
	SetBufferToParameters(buffer, order);
}
void TestPacketRes::PacketToBuffer(NetBuffer& buffer)
{
	SetParametersToBuffer(buffer, order);
}
#pragma endregion packet function