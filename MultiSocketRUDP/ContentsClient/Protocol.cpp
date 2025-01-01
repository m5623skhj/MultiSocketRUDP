#include "PreCompile.h"
#include "Protocol.h"

#pragma region packet function
PacketId Ping::GetPacketId() const
{
	return static_cast<PacketId>(PACKET_ID::Ping);
}
PacketId Pong::GetPacketId() const
{
	return static_cast<PacketId>(PACKET_ID::Pong);
}
PacketId TestStringPacket::GetPacketId() const
{
	return static_cast<PacketId>(PACKET_ID::TestStringPacket);
}
void TestStringPacket::BufferToPacket(NetBuffer& buffer)
{
	SetBufferToParameters(buffer, testString);
}
void TestStringPacket::PacketToBuffer(NetBuffer& buffer)
{
	SetParametersToBuffer(buffer, testString);
}
PacketId TestPacketReq::GetPacketId() const
{
	return static_cast<PacketId>(PACKET_ID::TestPacketReq);
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
	return static_cast<PacketId>(PACKET_ID::TestPacketRes);
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