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
	~Ping() override = default;

public:
	[[nodiscard]]
	PacketId GetPacketId() const override;
};

class Pong : public IPacket
{
public:
	Pong() = default;
	~Pong() override = default;

public:
	[[nodiscard]]
	PacketId GetPacketId() const override;
};

class TestStringPacketReq : public IPacket
{
public:
	TestStringPacketReq() = default;
	~TestStringPacketReq() override = default;

public:
	[[nodiscard]]
	PacketId GetPacketId() const override;
	void BufferToPacket(NetBuffer& buffer) override;
	void PacketToBuffer(NetBuffer& buffer) override;

public:
	std::string testString;
};

class TestStringPacketRes : public IPacket
{
public:
	TestStringPacketRes() = default;
	~TestStringPacketRes() override = default;

public:
	[[nodiscard]]
	PacketId GetPacketId() const override;
	void BufferToPacket(NetBuffer& buffer) override;
	void PacketToBuffer(NetBuffer& buffer) override;

public:
	std::string echoString;
};

class TestPacketReq : public IPacket
{
public:
	TestPacketReq() = default;
	~TestPacketReq() override = default;

public:
	[[nodiscard]]
	PacketId GetPacketId() const override;
	void BufferToPacket(NetBuffer& buffer) override;
	void PacketToBuffer(NetBuffer& buffer) override;

public:
	int order;
};

class TestPacketRes : public IPacket
{
public:
	TestPacketRes() = default;
	~TestPacketRes() override = default;

public:
	[[nodiscard]]
	PacketId GetPacketId() const override;
	void BufferToPacket(NetBuffer& buffer) override;
	void PacketToBuffer(NetBuffer& buffer) override;

public:
	int order;
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

#pragma endregion PacketHandler