#pragma once

#include <string>
#include "NetServerSerializeBuffer.h"
#include "PacketManager.h"

////////////////////////////////////////////////////////////////////////////////////
// Packet id type
////////////////////////////////////////////////////////////////////////////////////

#define GET_PACKET_ID(packetId) virtual PacketId GetPacketId() const override { return static_cast<PacketId>(packetId); }

template<typename T>
void SetBufferToParameters(NetBuffer& recvBuffer, T& param)
{
	recvBuffer >> param;
}

template<typename T, typename... Args>
void SetBufferToParameters(NetBuffer& recvBuffer, T& param, Args&... argList)
{
	recvBuffer >> param;
	SetBufferToParameters(recvBuffer, argList...);
}

template<typename T>
void SetParametersToBuffer(NetBuffer& recvBuffer, T& param)
{
	recvBuffer << param;
}

template<typename T, typename... Args>
void SetParametersToBuffer(NetBuffer& recvBuffer, T& param, Args&... argList)
{
	recvBuffer << param;
	SetParametersToBuffer(recvBuffer, argList...);
}

#define SET_BUFFER_TO_PARAMETERS(...)\
virtual void BufferToPacket(OUT NetBuffer& recvBuffer) override { SetBufferToParameters(recvBuffer, __VA_ARGS__); }

#define SET_PARAMETERS_TO_BUFFER(...)\
virtual void PacketToBuffer(OUT NetBuffer& recvBuffer) override { SetParametersToBuffer(recvBuffer, __VA_ARGS__); }

// This function assembles the packet based on the order of the defined parameters
#define SET_PARAMETERS(...)\
	SET_BUFFER_TO_PARAMETERS(__VA_ARGS__)\
	SET_PARAMETERS_TO_BUFFER(__VA_ARGS__)


////////////////////////////////////////////////////////////////////////////////////
// Packet
////////////////////////////////////////////////////////////////////////////////////

// BEGIN GENERATED PACKET TYPES
struct GeneratedPosition
{
	float x{};
	float y{};
};

struct GeneratedUser
{
	std::uint64_t userId{};
	std::string nickname{};
	GeneratedPosition position{};
	std::vector<int> items{};
	std::set<int, std::greater<int>> achievements{};
	std::unordered_set<std::string> labels{};
};

struct GeneratedEmpty
{
};

template<>
struct NetBufferCodec<GeneratedPosition>
{
	static constexpr bool SUPPORTED = true;
	static constexpr size_t MIN_WIRE_SIZE = 0 + NetBuffer::MinimumValueSize<float>() + NetBuffer::MinimumValueSize<float>();
	static void Write(NetBuffer& buffer, const GeneratedPosition& value);
	static void Read(NetBuffer& buffer, GeneratedPosition& value);
};

template<>
struct NetBufferCodec<GeneratedUser>
{
	static constexpr bool SUPPORTED = true;
	static constexpr size_t MIN_WIRE_SIZE = 0 + NetBuffer::MinimumValueSize<std::uint64_t>() + NetBuffer::MinimumValueSize<std::string>() + NetBuffer::MinimumValueSize<GeneratedPosition>() + NetBuffer::MinimumValueSize<std::vector<int>>() + NetBuffer::MinimumValueSize<std::set<int, std::greater<int>>>() + NetBuffer::MinimumValueSize<std::unordered_set<std::string>>();
	static void Write(NetBuffer& buffer, const GeneratedUser& value);
	static void Read(NetBuffer& buffer, GeneratedUser& value);
};

template<>
struct NetBufferCodec<GeneratedEmpty>
{
	static constexpr bool SUPPORTED = true;
	static constexpr size_t MIN_WIRE_SIZE = 0;
	static void Write(NetBuffer& buffer, const GeneratedEmpty& value);
	static void Read(NetBuffer& buffer, GeneratedEmpty& value);
};

inline void NetBufferCodec<GeneratedPosition>::Write(NetBuffer& buffer, const GeneratedPosition& value)
{
	buffer.WriteValue(value.x);
	buffer.WriteValue(value.y);
}

inline void NetBufferCodec<GeneratedPosition>::Read(NetBuffer& buffer, GeneratedPosition& value)
{
	GeneratedPosition temporary{};
	buffer.ReadValue(temporary.x);
	buffer.ReadValue(temporary.y);
	value = std::move(temporary);
}

inline void NetBufferCodec<GeneratedUser>::Write(NetBuffer& buffer, const GeneratedUser& value)
{
	buffer.WriteValue(value.userId);
	buffer.WriteValue(value.nickname);
	buffer.WriteValue(value.position);
	buffer.WriteValue(value.items);
	buffer.WriteValue(value.achievements);
	buffer.WriteValue(value.labels);
}

inline void NetBufferCodec<GeneratedUser>::Read(NetBuffer& buffer, GeneratedUser& value)
{
	GeneratedUser temporary{};
	buffer.ReadValue(temporary.userId);
	buffer.ReadValue(temporary.nickname);
	buffer.ReadValue(temporary.position);
	buffer.ReadValue(temporary.items);
	buffer.ReadValue(temporary.achievements);
	buffer.ReadValue(temporary.labels);
	value = std::move(temporary);
}

inline void NetBufferCodec<GeneratedEmpty>::Write(NetBuffer& buffer, const GeneratedEmpty& value)
{
	(void)buffer; (void)value;
}

inline void NetBufferCodec<GeneratedEmpty>::Read(NetBuffer& buffer, GeneratedEmpty& value)
{
	GeneratedEmpty temporary{};
	(void)buffer;
	value = std::move(temporary);
}

class GeneratedEnvelopeReq final : public IPacket
{
public:
	GeneratedEnvelopeReq() = default;
	~GeneratedEnvelopeReq() override = default;

public:
	[[nodiscard]]
	PacketId GetPacketId() const override;
	void BufferToPacket(NetBuffer& buffer) override;
	void PacketToBuffer(NetBuffer& buffer) override;

public:
	int sequence{};
	GeneratedUser user{};
	std::vector<GeneratedUser> users{};
	std::map<int, GeneratedUser, std::greater<int>> byId{};
	std::unordered_map<int, GeneratedUser> lookup{};
	std::list<GeneratedUser> history{};
	std::vector<GeneratedEmpty> emptyValues{};
};

class GeneratedEmptyRes final : public IPacket
{
public:
	GeneratedEmptyRes() = default;
	~GeneratedEmptyRes() override = default;

public:
	[[nodiscard]]
	PacketId GetPacketId() const override;
};

// END GENERATED PACKET TYPES

////////////////////////////////////////////////////////////////////////////////////
// Packet Register
////////////////////////////////////////////////////////////////////////////////////

#pragma region PacketHandler
#define REGISTER_PACKET(PacketType){\
	PacketManager::GetInst().RegisterPacket<PacketType>();\
}

#pragma endregion PacketHandler
