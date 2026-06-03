#include "TestableRUDPClient.h"

#include <WinSock2.h>

#include <atomic>

#include "../ContentsServer/Protocol.h"
#include "../MultiSocketRUDPClient/RUDPClientCore.h"

class TestableRUDPClient::Impl final : public RUDPClientCore
{
public:
	bool StartClient(const std::wstring& clientCoreOptionPath, const std::wstring& sessionGetterOptionPath, const bool shouldAutoConnect)
	{
		shouldSendConnectPacketOnStart.store(shouldAutoConnect, std::memory_order_relaxed);
		shouldAutoReplyDataPackets.store(true, std::memory_order_relaxed);
		return Start(clientCoreOptionPath, sessionGetterOptionPath, true);
	}

	void StopClient()
	{
		Stop();
	}

	void SetAutoReplyDataPackets(const bool shouldAutoReply)
	{
		shouldAutoReplyDataPackets.store(shouldAutoReply, std::memory_order_relaxed);
	}

	void SendPingPacket()
	{
		Ping ping;
		SendPacket(ping);
	}

	void SendEchoRequestPacket(const std::string& text)
	{
		TestStringPacketReq request;
		request.testString = text;
		SendPacket(request);
	}

	void DisconnectClient()
	{
		Disconnect();
	}

	bool WaitForConnected(const std::chrono::milliseconds timeout)
	{
		return WaitUntil(timeout, [this]()
		{
			return IsConnected();
		});
	}

	bool WaitForPong(const std::chrono::milliseconds timeout)
	{
		return WaitForPacket(timeout, [](const ReceivedAppPacket& packet)
		{
			return packet.packetId == PACKET_ID::PONG;
		});
	}

	bool WaitForEcho(const std::string_view expectedText, const std::chrono::milliseconds timeout)
	{
		return WaitForPacket(timeout, [expectedText](const ReceivedAppPacket& packet)
		{
			return packet.packetId == PACKET_ID::TEST_STRING_PACKET_RES &&
				packet.echoText == expectedText;
		});
	}

protected:
	[[nodiscard]]
	bool ShouldSendConnectPacketOnStart() const override
	{
		return shouldSendConnectPacketOnStart.load(std::memory_order_relaxed);
	}

	[[nodiscard]]
	bool ShouldSendReplyToServer(const PacketSequence, const unsigned int inPacketId) const override
	{
		if (inPacketId == 0)
		{
			return true;
		}

		return shouldAutoReplyDataPackets.load(std::memory_order_relaxed);
	}

private:
	bool WaitForPacket(const std::chrono::milliseconds timeout, const auto& predicate)
	{
		const auto deadline = std::chrono::steady_clock::now() + timeout;
		while (std::chrono::steady_clock::now() < deadline)
		{
			ReceivedAppPacket packet;
			if (TryPopReceivedPacket(packet) && predicate(packet))
			{
				return true;
			}

			Sleep(10);
		}

		ReceivedAppPacket packet;
		return TryPopReceivedPacket(packet) && predicate(packet);
	}

	bool TryPopReceivedPacket(ReceivedAppPacket& outPacket)
	{
		NetBuffer* receivedBuffer = GetReceivedPacket();
		if (receivedBuffer == nullptr)
		{
			return false;
		}

		PacketId packetId = 0;
		*receivedBuffer >> packetId;
		outPacket.packetId = static_cast<PACKET_ID>(packetId);
		outPacket.echoText.clear();

		if (outPacket.packetId == PACKET_ID::TEST_STRING_PACKET_RES)
		{
			TestStringPacketRes response;
			response.BufferToPacket(*receivedBuffer);
			outPacket.echoText = response.echoString;
		}

		NetBuffer::Free(receivedBuffer);
		return true;
	}

	static bool WaitUntil(const std::chrono::milliseconds timeout, const auto& predicate)
	{
		const auto deadline = std::chrono::steady_clock::now() + timeout;
		while (std::chrono::steady_clock::now() < deadline)
		{
			if (predicate())
			{
				return true;
			}

			Sleep(10);
		}

		return predicate();
	}

private:
	std::atomic_bool shouldSendConnectPacketOnStart{ true };
	std::atomic_bool shouldAutoReplyDataPackets{ true };
};

TestableRUDPClient::TestableRUDPClient()
	: impl(std::make_unique<Impl>())
{
}

TestableRUDPClient::~TestableRUDPClient() = default;

bool TestableRUDPClient::StartClient(const std::wstring& clientCoreOptionPath, const std::wstring& sessionGetterOptionPath, const bool shouldAutoConnect)
{
	return impl->StartClient(clientCoreOptionPath, sessionGetterOptionPath, shouldAutoConnect);
}

void TestableRUDPClient::StopClient()
{
	impl->StopClient();
}

void TestableRUDPClient::SetAutoReplyDataPackets(const bool shouldAutoReply)
{
	impl->SetAutoReplyDataPackets(shouldAutoReply);
}

void TestableRUDPClient::SendPingPacket()
{
	impl->SendPingPacket();
}

void TestableRUDPClient::SendEchoRequestPacket(const std::string& text)
{
	impl->SendEchoRequestPacket(text);
}

void TestableRUDPClient::DisconnectClient()
{
	impl->DisconnectClient();
}

bool TestableRUDPClient::WaitForConnected(const std::chrono::milliseconds timeout)
{
	return impl->WaitForConnected(timeout);
}

bool TestableRUDPClient::WaitForPong(const std::chrono::milliseconds timeout)
{
	return impl->WaitForPong(timeout);
}

bool TestableRUDPClient::WaitForEcho(const std::string_view expectedText, const std::chrono::milliseconds timeout)
{
	return impl->WaitForEcho(expectedText, timeout);
}
