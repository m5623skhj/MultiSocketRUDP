#include "PreCompile.h"
#include "Protocol.h"
#include "PacketIdType.h"

#pragma region packet function
PacketId GeneratedEnvelopeReq::GetPacketId() const
{
	return static_cast<PacketId>(GeneratedSchemaPacketId::GENERATED_ENVELOPE_REQ);
}
void GeneratedEnvelopeReq::BufferToPacket(NetBuffer& buffer)
{
	GeneratedEnvelopeReq temporary{};
	buffer.ReadValue(temporary.sequence);
	buffer.ReadValue(temporary.user);
	buffer.ReadValue(temporary.users);
	buffer.ReadValue(temporary.byId);
	buffer.ReadValue(temporary.lookup);
	buffer.ReadValue(temporary.history);
	buffer.ReadValue(temporary.emptyValues);
	this->sequence = std::move(temporary.sequence);
	this->user = std::move(temporary.user);
	this->users = std::move(temporary.users);
	this->byId = std::move(temporary.byId);
	this->lookup = std::move(temporary.lookup);
	this->history = std::move(temporary.history);
	this->emptyValues = std::move(temporary.emptyValues);
}
void GeneratedEnvelopeReq::PacketToBuffer(NetBuffer& buffer)
{
	buffer.WriteValue(this->sequence);
	buffer.WriteValue(this->user);
	buffer.WriteValue(this->users);
	buffer.WriteValue(this->byId);
	buffer.WriteValue(this->lookup);
	buffer.WriteValue(this->history);
	buffer.WriteValue(this->emptyValues);
}
PacketId GeneratedEmptyRes::GetPacketId() const
{
	return static_cast<PacketId>(GeneratedSchemaPacketId::GENERATED_EMPTY_RES);
}
#pragma endregion packet function
