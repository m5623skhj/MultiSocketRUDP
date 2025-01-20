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
PacketId TestStringPacketReq::GetPacketId() const
{
	return static_cast<PacketId>(PACKET_ID::TestStringPacketReq);
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
	return static_cast<PacketId>(PACKET_ID::TestStringPacketRes);
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