#pragma once

#include <chrono>
#include <memory>
#include <string>
#include <string_view>

#include "../ContentsServer/PacketIdType.h"

struct ReceivedAppPacket
{
	PACKET_ID packetId{ PACKET_ID::INVALID_PACKET_ID };
	std::string echoText{};
};

class TestableRUDPClient
{
public:
	TestableRUDPClient();
	~TestableRUDPClient();

	bool StartClient(const std::wstring& clientCoreOptionPath, const std::wstring& sessionGetterOptionPath, bool shouldAutoConnect);
	void StopClient();

	void SetAutoReplyDataPackets(bool shouldAutoReply);
	void SendPingPacket();
	void SendEchoRequestPacket(const std::string& text);
	void DisconnectClient();

	bool WaitForConnected(std::chrono::milliseconds timeout);
	bool WaitForPong(std::chrono::milliseconds timeout);
	bool WaitForEcho(std::string_view expectedText, std::chrono::milliseconds timeout);

private:
	class Impl;
	std::unique_ptr<Impl> impl;
};
