#include <WinSock2.h>
#include <atomic>
#include <string>
#include <algorithm>
#include <functional>
#include <future>
#include <filesystem>
#include <fstream>
#include <iostream>
#include "../MultiSocketRUDPClient/RUDPClientCore.h"
#include "../ContentsServer/Protocol.h"
#include "../ContentsServer/PacketIdType.h"
#include "../Common/Crypto/CryptoHelper.h"
#include <format>
#ifndef LOG_ERROR
#define LOG_ERROR(...) ((void)0)
#endif
#include "../Common/PacketCrypto/PacketCryptoHelper.h"

// Client and server SendPacketInfo have distinct layouts, so client-only checks
// run in the client harness rather than linking both implementations into CoreTest.
class RUDPClientCoreTestAccess
{
	class CallbackPacket final : public IPacket
	{
	public:
		explicit CallbackPacket(std::function<void()> inCallback) : callback(std::move(inCallback)) {}
		PacketId GetPacketId() const override { return 77; }
		void PacketToBuffer(NetBuffer&) override { callback(); }
	private:
		std::function<void()> callback;
	};
public:
	static bool RunLifecycle(const std::wstring& corePath, const std::wstring& brokerPath, bool timeout)
	{
		RUDPClientCore client;
		if (timeout)
		{
			// Reserve a real TLS session but direct CONNECT to a silent local UDP sink.
			class ReservedClient final : public RUDPClientCore
			{
				bool ShouldSendConnectPacketOnStart() const override { return false; }
			} reserved;
			if (not reserved.Start(corePath, brokerPath, true)) return false;
			const SOCKET sink = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
			sockaddr_in sinkAddress{};
			sinkAddress.sin_family = AF_INET;
			sinkAddress.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
			if (sink == INVALID_SOCKET || ::bind(sink, reinterpret_cast<sockaddr*>(&sinkAddress), sizeof(sinkAddress)) != 0)
			{
				if (sink != INVALID_SOCKET) closesocket(sink);
				return false;
			}
			int size = sizeof(sinkAddress);
			getsockname(sink, reinterpret_cast<sockaddr*>(&sinkAddress), &size);
			{
				std::scoped_lock lock(reserved.lifecycleLock);
				reserved.serverAddr = sinkAddress;
				reserved.SendConnectPacket();
			}
			const bool stopped = WaitUntil([&] { return reserved.IsStopped(); });
			closesocket(sink);
			reserved.Stop();
			return stopped && not reserved.IsConnected() && reserved.sendPacketInfoMap.empty() &&
				reserved.sendEventHandles[0] == nullptr && reserved.rudpSocket == INVALID_SOCKET;
		}

		if (not client.Start(corePath, brokerPath, true) ||
			not WaitUntil([&] { return client.IsConnected(); })) return false;
		bool restarted = false;
		// Stop and restart while an old reliable call is still serializing user data.
		CallbackPacket pausedSerializer([&]
		{
			client.Stop();
			restarted = client.Start(corePath, brokerPath, true) &&
				WaitUntil([&] { return client.IsConnected(); });
		});
		client.SendPacket(pausedSerializer);
		if (not restarted || client.lastSendPacketSequence != 0) return false;

		for (int cycle = 0; cycle < 2; ++cycle)
		{
			TestStringPacketReq request;
			request.testString = "lifecycle-restart";
			client.SendPacket(request);
			if (client.lastSendPacketSequence != 1) return false;
			bool echo = false;
			if (not WaitUntil([&]
			{
				auto* buffer = client.GetReceivedPacket();
				if (buffer == nullptr) return false;
				PacketId id{};
				*buffer >> id;
				if (id == static_cast<PacketId>(PACKET_ID::TEST_STRING_PACKET_RES))
				{
					TestStringPacketRes reply;
					reply.BufferToPacket(*buffer);
					echo = reply.echoString == request.testString;
				}
				NetBuffer::Free(buffer);
				return echo;
			})) return false;
			std::promise<void> gate;
			auto ready = gate.get_future().share();
			const auto stop = [&]
			{
				ready.wait();
				client.Stop();
				return client.IsStopped() && not client.IsConnected();
			};
			auto first = std::async(std::launch::async, stop);
			auto second = std::async(std::launch::async, stop);
			gate.set_value();
			if (not first.get() || not second.get() || client.sendEventHandles[0] != nullptr ||
				not client.sendPacketInfoMap.empty() || not client.pendingPacketQueue.empty()) return false;
			if (cycle == 0 && (not client.Start(corePath, brokerPath, true) ||
				not WaitUntil([&] { return client.IsConnected(); }))) return false;
		}
		return true;
	}

	static bool RunOptionChecks()
	{
		RUDPClientCore client;
		const auto path = std::filesystem::temp_directory_path() /
			(L"RudpClientOptions-" + std::to_wstring(GetCurrentProcessId()) + L".txt");
		const auto write = [&](const std::wstring& text)
		{
			std::ofstream stream(path, std::ios::binary | std::ios::trunc);
			const wchar_t bom = 0xfeff;
			stream.write(reinterpret_cast<const char*>(&bom), sizeof(bom));
			stream.write(reinterpret_cast<const char*>(text.data()), text.size() * sizeof(wchar_t));
			return stream.good();
		};
		bool passed = not client.ReadClientCoreOptionFile(path.wstring() + L".missing") &&
			not client.ReadSessionGetterOptionFile(path.wstring() + L".missing");
		for (const auto* value : { L"0", L"-1", L"2147483648", L"999999999999999999999", L"abc", L"" })
		{
			passed &= write(L":CORE\n{\nMAX_PACKET_RETRANSMISSION_COUNT = 3\nRETRANSMISSION_MS = " +
				std::wstring(value) + L"\nSERVER_ALIVE_CHECK_MS = 15000\n}\n");
			passed &= not client.ReadClientCoreOptionFile(path.wstring());
		}
		passed &= write(L":CORE\n{\nMAX_PACKET_RETRANSMISSION_COUNT = 65535\nRETRANSMISSION_MS = 50\nSERVER_ALIVE_CHECK_MS = 15000\n}\n");
		passed &= client.ReadClientCoreOptionFile(path.wstring());
		passed &= client.maxPacketRetransmissionCount == 65535;
		for (const auto* value : { L"0", L"-1", L"65536" })
		{
			passed &= write(L":CORE\n{\nMAX_PACKET_RETRANSMISSION_COUNT = " + std::wstring(value) +
				L"\nRETRANSMISSION_MS = 50\nSERVER_ALIVE_CHECK_MS = 15000\n}\n");
			passed &= not client.ReadClientCoreOptionFile(path.wstring());
			passed &= write(L":SESSION_BROKER\n{\nIP = \"127.0.0.1\"\nPORT = " + std::wstring(value) +
				L"\n}\n:SERIALIZEBUF\n{\nPACKET_CODE = 119\nPACKET_KEY = 50\n}\n");
			passed &= not client.ReadSessionGetterOptionFile(path.wstring());
		}
		passed &= write(std::wstring(2048, L' '));
		passed &= not client.ReadClientCoreOptionFile(path.wstring());
		passed &= write(L"");
		passed &= not client.ReadSessionGetterOptionFile(path.wstring());
		std::filesystem::remove(path);
		return passed;
	}

	static bool Run()
	{
		RUDPClientCore client;
		client.unreliableQueueCapacity = 3;
		client.isStopped = false;
		client.isConnected = true;
		client.sendEventHandles[0] = CreateSemaphore(nullptr, 0, LONG_MAX, nullptr);
		std::fill_n(client.sessionKey, SESSION_KEY_SIZE, 0);
		std::fill_n(client.sessionSalt, SESSION_SALT_SIZE, 0);
		auto& crypto = CryptoHelper::GetTLSInstance();
		client.keyObjectBuffer = new unsigned char[crypto.GetKeyObjectSize()];
		client.sessionKeyHandle = crypto.GetSymmetricKeyHandle(client.keyObjectBuffer, client.sessionKey);
		if (client.sessionKeyHandle == nullptr || client.sendEventHandles[0] == nullptr)
		{
			client.Stop();
			return false;
		}
		bool passed = true;
		TestStringPacketReq request;
		request.testString = "queue-test";
		for (int index = 0; index < 4; ++index)
		{
			passed &= client.SendUnreliablePacket(request);
		}
		passed &= client.unreliableSendQueue.size() == 3 && client.sendPacketInfoMap.empty() &&
			client.lastSendPacketSequence == 0 && client.pendingPacketQueue.empty();
		PacketSequence expected = 2;
		for (auto* buffer : client.unreliableSendQueue)
		{
			PacketSequence sequence{};
			memcpy(&sequence, buffer->GetReadBufferPtr() + df_HEADER_SIZE + sizeof(PACKET_TYPE), sizeof(sequence));
			passed &= sequence == expected++;
		}

		// A forged high sequence must not advance the authenticated receive state.
		Receive(client, 100, true);
		passed &= client.authenticatedReceiveCount.load() == 0;
		for (const PacketSequence sequence : { 0, 5, 3, 5, 7, 9 }) Receive(client, sequence, false);
		passed &= client.authenticatedReceiveCount.load() == 6;
		passed &= client.serverAliveChecker.IsServerAlive(client.authenticatedReceiveCount.load());
		Receive(client, 200, true);
		passed &= not client.serverAliveChecker.IsServerAlive(client.authenticatedReceiveCount.load());
		passed &= client.unreliableReceivedPackets.size() == 3 && client.nextRecvPacketSequence == 1 &&
			client.sendBufferQueue.GetRestSize() == 0;
		for (const PacketSequence sequence : { 5, 7, 9 })
		{
			auto* buffer = client.GetReceivedUnreliablePacket();
			if (buffer == nullptr) { passed = false; break; }
			PacketId packetId{};
			PacketSequence value{};
			*buffer >> packetId >> value;
			passed &= packetId == 77 && value == sequence;
			NetBuffer::Free(buffer);
		}
		Receive(client, 10, false);
		Receive(client, 1, false, PACKET_TYPE::HEARTBEAT_TYPE);
		Receive(client, 1, false, PACKET_TYPE::HEARTBEAT_TYPE);
		passed &= client.recvPacketHoldingQueue.empty() && client.nextRecvPacketSequence == 2;
		// A heartbeat behind a gap must wait; content remains available to the application.
		Receive(client, 3, false, PACKET_TYPE::HEARTBEAT_TYPE);
		passed &= client.nextRecvPacketSequence == 2 && client.recvPacketHoldingQueue.size() == 1;
		Receive(client, 2, false, PACKET_TYPE::SEND_TYPE);
		passed &= client.nextRecvPacketSequence == 2 && client.recvPacketHoldingQueue.size() == 2;
		auto* content = client.GetReceivedPacket();
		if (content != nullptr)
		{
			PacketId id{};
			PacketSequence value{};
			*content >> id >> value;
			passed &= id == 77 && value == 2;
			NetBuffer::Free(content);
		}
		else passed = false;
		passed &= client.recvPacketHoldingQueue.empty() && client.nextRecvPacketSequence == 4;
		Receive(client, 5, false, PACKET_TYPE::HEARTBEAT_TYPE);
		Receive(client, 6, false, PACKET_TYPE::SEND_TYPE);
		// This harness has no send worker; release the ACKs generated by the injected packets.
		NetBuffer* ack{};
		while (client.sendBufferQueue.Dequeue(&ack)) NetBuffer::Free(ack);
		client.Stop();
		passed &= client.recvPacketHoldingQueue.empty() && client.nextRecvPacketSequence == 1;
		passed &= client.unreliableSendQueue.empty() && client.unreliableReceivedPackets.empty() &&
			client.lastUnreliableSendSequence == 0 && client.unreliableReceiveState.Accept(1);
		passed &= not client.SendUnreliablePacket(request);
		// Model Stop + Start while an earlier call is still serializing user data.
		client.isStopped = false;
		client.threadStopFlag = false;
		client.isConnected = true;
		CallbackPacket pausedSerializer([&]()
		{
			client.Stop();
			client.isStopped = false;
			client.threadStopFlag = false;
			client.isConnected = true;
			client.sendEventHandles[0] = CreateSemaphore(nullptr, 0, LONG_MAX, nullptr);
		});
		passed &= not client.SendUnreliablePacket(pausedSerializer);
		passed &= client.lastUnreliableSendSequence == 0 && client.unreliableSendQueue.empty();
		client.Stop();
		return passed;
	}

private:
	template <typename Predicate>
	static bool WaitUntil(Predicate predicate)
	{
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
		while (std::chrono::steady_clock::now() < deadline)
		{
			if (predicate()) return true;
			Sleep(5);
		}
		return predicate();
	}
	static void Receive(RUDPClientCore& client, const PacketSequence sequence, const bool tampered,
		PACKET_TYPE type = PACKET_TYPE::UNRELIABLE_SEND_TYPE)
	{
		auto* buffer = NetBuffer::Alloc();
		const bool isCore = type == PACKET_TYPE::HEARTBEAT_TYPE;
		*buffer << type << sequence;
		if (not isCore) *buffer << PacketId{ 77 } << sequence;
		const auto direction = type == PACKET_TYPE::UNRELIABLE_SEND_TYPE ?
			PACKET_DIRECTION::SERVER_TO_CLIENT_UNREL : PACKET_DIRECTION::SERVER_TO_CLIENT;
		PacketCryptoHelper::EncodePacket(*buffer, sequence, direction,
			client.sessionSalt, SESSION_SALT_SIZE, client.sessionKeyHandle, isCore);
		if (tampered) buffer->GetReadBufferPtr()[buffer->GetUseSize() - 1] ^= 1;
		char header[df_HEADER_SIZE];
		buffer->ReadBuffer(header, sizeof(header));
		client.ProcessRecvPacket(*buffer);
		NetBuffer::Free(buffer);
	}
};

bool RunUnreliableClientChecks()
{
	return RUDPClientCoreTestAccess::Run();
}

bool RunClientLifecycleChecks(const std::wstring& corePath, const std::wstring& brokerPath, bool timeout)
{
	return RUDPClientCoreTestAccess::RunOptionChecks() &&
		RUDPClientCoreTestAccess::RunLifecycle(corePath, brokerPath, timeout);
}
