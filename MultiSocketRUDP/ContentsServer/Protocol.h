#pragma once

#include <string>
#include <functional>
#include "NetServerSerializeBuffer.h"
#include "../MultiSocketRUDPServer/PacketManager.h"
#include "PacketIdType.h"

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

#pragma pack(push, 1)
class Ping : public IPacket
{
public:
	Ping() = default;
	virtual ~Ping() override = default;

public:
	virtual PacketId GetPacketId() const override;
	//GET_PACKET_ID(PACKET_ID::Ping)
};

class Pong : public IPacket
{
public:
	Pong() = default;
	virtual ~Pong() override = default;

public:
	virtual PacketId GetPacketId() const override;
	//GET_PACKET_ID(PACKET_ID::Pong);
};

class TestStringPacket : public IPacket
{
public:
	TestStringPacket() = default;
	~TestStringPacket() = default;

public:
	virtual PacketId GetPacketId() const override;
	virtual void BufferToPacket(NetBuffer& buffer) override;
	virtual void PacketToBuffer(NetBuffer& buffer) override;

public:
	std::string testString;
};

#pragma pack(pop)

////////////////////////////////////////////////////////////////////////////////////
// Packet Register
////////////////////////////////////////////////////////////////////////////////////

#pragma region PacketHandler
#define REGISTER_PACKET(PacketType, Handler){\
	PacketManager::GetInst().RegisterPacket<PacketType>();\
	PacketManager::GetInst().RegisterPacketHandler<PacketType>(Handler);\
	PacketManager::GetInst().RegisterBufferToPacketType<PacketType>();\
}

// deprecated
/*
#define DECLARE_HANDLE_PACKET(PacketType)\
	static bool HandlePacket(RUDPSession& session, PacketType& packet);\

#define DECLARE_ALL_HANDLER()\
	DECLARE_HANDLE_PACKET(Ping)\

#define REGISTER_PACKET_LIST(Handler){\
	REGISTER_PACKET(Ping, Handler)\
}
*/

#pragma endregion PacketHandler