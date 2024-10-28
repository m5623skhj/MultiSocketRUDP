#pragma once

#include <string>
#include <functional>
#include "CoreType.h"
#include "NetServerSerializeBuffer.h"

using PacketId = unsigned int;

#define GET_PACKET_ID(packetId) virtual PacketId GetPacketId() const override { return static_cast<PacketId>(packetId); }

template<typename T>
void SetBufferToParameters(OUT NetBuffer& buffer, T& param)
{
	buffer >> param;
}

template<typename T, typename... Args>
void SetBufferToParameters(OUT NetBuffer& buffer, T& param, Args&... argList)
{
	buffer >> param;
	SetBufferToParameters(buffer, argList...);
}

template<typename T>
void SetParametersToBuffer(OUT NetBuffer& buffer, T& param)
{
	buffer << param;
}

template<typename T, typename... Args>
void SetParametersToBuffer(OUT NetBuffer& buffer, T& param, Args&... argList)
{
	buffer << param;
	SetParametersToBuffer(buffer, argList...);
}

#define SET_BUFFER_TO_PARAMETERS(...)\
virtual void BufferToPacket(OUT NetBuffer& buffer) override { SetBufferToParameters(buffer, __VA_ARGS__); }

#define SET_PARAMETERS_TO_BUFFER(...)\
virtual void PacketToBuffer(OUT NetBuffer& buffer) override { SetParametersToBuffer(buffer, __VA_ARGS__); }

// This function assembles the packet based on the order of the defined parameters
#define SET_PARAMETERS(...)\
	SET_BUFFER_TO_PARAMETERS(__VA_ARGS__)\
	SET_PARAMETERS_TO_BUFFER(__VA_ARGS__)


////////////////////////////////////////////////////////////////////////////////////
// Packet
////////////////////////////////////////////////////////////////////////////////////

#pragma pack(push, 1)

#pragma pack(pop)

////////////////////////////////////////////////////////////////////////////////////
// Packet Register
////////////////////////////////////////////////////////////////////////////////////

#pragma region PacketHandler
#define REGISTER_PACKET(PacketType){\
	RegisterPacket<PacketType>();\
	RegisterPacketHandler<PacketType>();\
}

#define DECLARE_HANDLE_PACKET(PacketType)\
	static bool HandlePacket(RUDPSession& session, PacketType& packet);\

#define DECLARE_ALL_HANDLER()\

#define REGISTER_PACKET_LIST(){\
}

#pragma endregion PacketHandler