#include "PreCompile.h"
#include "Protocol.h"

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
	return static_cast<PacketId>(PACKET_ID::Pong);
}

void TestStringPacket::BufferToPacket(NetBuffer& buffer) 
{
	SetBufferToParameters(buffer, testString);
}

void TestStringPacket::PacketToBuffer(NetBuffer& buffer)
{
	SetParametersToBuffer(buffer, testString);
};