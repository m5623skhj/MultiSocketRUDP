#include "PreCompile.h"
#include "Protocol.h"
#include "PacketIdType.h"

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
	TestStringPacketReq temporary{};
	buffer.ReadValue(temporary.testString);
	this->testString = std::move(temporary.testString);
}
void TestStringPacketReq::PacketToBuffer(NetBuffer& buffer)
{
	buffer.WriteValue(this->testString);
}
PacketId TestStringPacketRes::GetPacketId() const
{
	return static_cast<PacketId>(PACKET_ID::TEST_STRING_PACKET_RES);
}
void TestStringPacketRes::BufferToPacket(NetBuffer& buffer)
{
	TestStringPacketRes temporary{};
	buffer.ReadValue(temporary.echoString);
	this->echoString = std::move(temporary.echoString);
}
void TestStringPacketRes::PacketToBuffer(NetBuffer& buffer)
{
	buffer.WriteValue(this->echoString);
}
PacketId TestPacketReq::GetPacketId() const
{
	return static_cast<PacketId>(PACKET_ID::TEST_PACKET_REQ);
}
void TestPacketReq::BufferToPacket(NetBuffer& buffer)
{
	TestPacketReq temporary{};
	buffer.ReadValue(temporary.order);
	this->order = std::move(temporary.order);
}
void TestPacketReq::PacketToBuffer(NetBuffer& buffer)
{
	buffer.WriteValue(this->order);
}
PacketId TestPacketRes::GetPacketId() const
{
	return static_cast<PacketId>(PACKET_ID::TEST_PACKET_RES);
}
void TestPacketRes::BufferToPacket(NetBuffer& buffer)
{
	TestPacketRes temporary{};
	buffer.ReadValue(temporary.order);
	this->order = std::move(temporary.order);
}
void TestPacketRes::PacketToBuffer(NetBuffer& buffer)
{
	buffer.WriteValue(this->order);
}
PacketId ChannelEchoReq::GetPacketId() const
{
	return static_cast<PacketId>(PACKET_ID::CHANNEL_ECHO_REQ);
}
void ChannelEchoReq::BufferToPacket(NetBuffer& buffer)
{
	ChannelEchoReq temporary{};
	buffer.ReadValue(temporary.requestId);
	buffer.ReadValue(temporary.unreliable);
	this->requestId = std::move(temporary.requestId);
	this->unreliable = std::move(temporary.unreliable);
}
void ChannelEchoReq::PacketToBuffer(NetBuffer& buffer)
{
	buffer.WriteValue(this->requestId);
	buffer.WriteValue(this->unreliable);
}
PacketId ChannelEchoRes::GetPacketId() const
{
	return static_cast<PacketId>(PACKET_ID::CHANNEL_ECHO_RES);
}
void ChannelEchoRes::BufferToPacket(NetBuffer& buffer)
{
	ChannelEchoRes temporary{};
	buffer.ReadValue(temporary.requestId);
	buffer.ReadValue(temporary.unreliable);
	this->requestId = std::move(temporary.requestId);
	this->unreliable = std::move(temporary.unreliable);
}
void ChannelEchoRes::PacketToBuffer(NetBuffer& buffer)
{
	buffer.WriteValue(this->requestId);
	buffer.WriteValue(this->unreliable);
}
#pragma endregion packet function
