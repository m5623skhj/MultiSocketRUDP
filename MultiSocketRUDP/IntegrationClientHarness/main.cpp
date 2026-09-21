#include <Windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <future>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "../Logger/Logger.h"
#include "../IntegrationTest/TestableRUDPClient.h"

bool RunUnreliableClientChecks();
bool RunClientLifecycleChecks(const std::wstring& corePath, const std::wstring& brokerPath, bool timeout);

namespace
{
	using namespace std::chrono_literals;

	std::wstring GetOptionPath(const wchar_t* relativePath)
	{
		std::array<wchar_t, MAX_PATH> modulePath{};
		GetModuleFileNameW(nullptr, modulePath.data(), static_cast<DWORD>(modulePath.size()));

		std::filesystem::path rootDir = std::filesystem::path(modulePath.data()).parent_path();
		rootDir = rootDir.parent_path();
		rootDir = rootDir.parent_path();

		return (rootDir / L"IntegrationTest" / relativePath).wstring();
	}

	std::optional<std::wstring> GetArgumentValue(const int argc, wchar_t* argv[], const std::wstring_view optionName)
	{
		for (int i = 1; i + 1 < argc; ++i)
		{
			if (std::wstring_view(argv[i]) == optionName)
			{
				return argv[i + 1];
			}
		}

		return std::nullopt;
	}

	bool RunConnectScenario(const std::wstring& clientCoreOptionPath, const std::wstring& sessionGetterOptionPath)
	{
		auto client = std::make_unique<TestableRUDPClient>();
		if (not client->StartClient(clientCoreOptionPath, sessionGetterOptionPath, true))
		{
			std::cout << "client start failed\n";
			Logger::GetInstance().StopLoggerThread();
			return false;
		}

		if (not client->WaitForConnected(8s))
		{
			std::cout << "client connect wait failed\n";
			return false;
		}

		Sleep(1000);
		client->StopClient();
		return true;
	}

	bool RunReserveOnlyScenario(const std::wstring& clientCoreOptionPath, const std::wstring& sessionGetterOptionPath)
	{
		auto client = std::make_unique<TestableRUDPClient>();
		if (not client->StartClient(clientCoreOptionPath, sessionGetterOptionPath, false))
		{
			std::cout << "client reserve-only start failed\n";
			Logger::GetInstance().StopLoggerThread();
			return false;
		}

		Sleep(1000);
		client->StopClient();
		return true;
	}

	bool RunEchoScenario(const std::wstring& clientCoreOptionPath, const std::wstring& sessionGetterOptionPath, const std::string& message)
	{
		auto client = std::make_unique<TestableRUDPClient>();
		if (not client->StartClient(clientCoreOptionPath, sessionGetterOptionPath, true))
		{
			std::cout << "client start failed\n";
			Logger::GetInstance().StopLoggerThread();
			return false;
		}

		if (not client->WaitForConnected(8s))
		{
			std::cout << "client connect wait failed\n";
			return false;
		}

		client->SendEchoRequestPacket(message);
		if (not client->WaitForEcho(message, 3s))
		{
			std::cout << "echo response wait failed\n";
			return false;
		}

		client->StopClient();
		return true;
	}

	bool RunUnreliableOnlyScenario(const std::wstring& clientCoreOptionPath, const std::wstring& sessionGetterOptionPath)
	{
		TestableRUDPClient client;
		if (not client.StartClient(clientCoreOptionPath, sessionGetterOptionPath, true) ||
			not client.WaitForConnected(8s)) return false;
		// The test config uses a 500 ms alive check. Never consume the reliable queue.
		// Stay idle across several heartbeat periods before starting unreliable traffic.
		Sleep(2000);
		if (client.GetPendingReliablePacketCount() != 0) return false;
		const auto deadline = std::chrono::steady_clock::now() + 3s;
		int index = 0;
		while (std::chrono::steady_clock::now() < deadline)
		{
			const auto message = "unreliable:alive:" + std::to_string(index++);
			if (not client.SendUnreliableEchoRequestPacket(message) ||
				not client.WaitForUnreliableEcho(message, 1s)) return false;
			Sleep(10);
		}
		client.StopClient();
		return true;
	}

	bool RunMixedChannelsScenario(const std::wstring& clientCoreOptionPath, const std::wstring& sessionGetterOptionPath)
	{
		TestableRUDPClient client;
		if (not client.StartClient(clientCoreOptionPath, sessionGetterOptionPath, true) ||
			not client.WaitForConnected(8s)) return false;
		for (int index = 0; index < 2; ++index)
		{
			const auto reliable = "reliable:" + std::to_string(index);
			const auto unreliable = "unreliable:" + std::to_string(index);
			client.SendEchoRequestPacket(reliable);
			if (not client.SendUnreliableEchoRequestPacket(unreliable)) return false;
			if (not client.WaitForEcho(reliable, 3s) ||
				not client.WaitForUnreliableEcho(unreliable, 3s)) return false;
		}
		client.StopClient();
		return true;
	}

	bool RunPingScenario(const std::wstring& clientCoreOptionPath, const std::wstring& sessionGetterOptionPath)
	{
		auto client = std::make_unique<TestableRUDPClient>();
		if (not client->StartClient(clientCoreOptionPath, sessionGetterOptionPath, true))
		{
			std::cout << "client start failed\n";
			Logger::GetInstance().StopLoggerThread();
			return false;
		}

		if (not client->WaitForConnected(8s))
		{
			std::cout << "client connect wait failed\n";
			return false;
		}

		client->SendPingPacket();
		if (not client->WaitForPong(3s))
		{
			std::cout << "pong wait failed\n";
			return false;
		}

		client->StopClient();
		return true;
	}

	bool RunDropAckScenario(const std::wstring& clientCoreOptionPath, const std::wstring& sessionGetterOptionPath)
	{
		auto client = std::make_unique<TestableRUDPClient>();
		if (not client->StartClient(clientCoreOptionPath, sessionGetterOptionPath, true))
		{
			std::cout << "client start failed\n";
			Logger::GetInstance().StopLoggerThread();
			return false;
		}

		if (not client->WaitForConnected(8s))
		{
			std::cout << "client connect wait failed\n";
			return false;
		}

		client->SetAutoReplyDataPackets(false);
		client->SendPingPacket();
		if (not client->WaitForPong(3s))
		{
			std::cout << "pong wait failed\n";
			return false;
		}

		Sleep(4000);
		client->StopClient();
		return true;
	}

	bool RunDisconnectScenario(const std::wstring& clientCoreOptionPath, const std::wstring& sessionGetterOptionPath)
	{
		auto client = std::make_unique<TestableRUDPClient>();
		if (not client->StartClient(clientCoreOptionPath, sessionGetterOptionPath, true))
		{
			std::cout << "client start failed\n";
			Logger::GetInstance().StopLoggerThread();
			return false;
		}

		if (not client->WaitForConnected(8s))
		{
			std::cout << "client connect wait failed\n";
			return false;
		}

		client->DisconnectClient();
		Sleep(1000);
		client->StopClient();
		return true;
	}

	bool RunStopScenario(const std::wstring& clientCoreOptionPath, const std::wstring& sessionGetterOptionPath)
	{
		auto client = std::make_unique<TestableRUDPClient>();
		if (not client->StartClient(clientCoreOptionPath, sessionGetterOptionPath, true))
		{
			std::cout << "client start failed\n";
			Logger::GetInstance().StopLoggerThread();
			return false;
		}

		if (not client->WaitForConnected(8s))
		{
			std::cout << "client connect wait failed\n";
			return false;
		}

		client->StopClient();
		return true;
	}

	bool RunMultiEchoScenario(const std::wstring& clientCoreOptionPath, const std::wstring& sessionGetterOptionPath, const int clientCount)
	{
		std::vector<std::unique_ptr<TestableRUDPClient>> clients;
		clients.reserve(clientCount);
		for (int i = 0; i < clientCount; ++i)
		{
			auto client = std::make_unique<TestableRUDPClient>();
			if (not client->StartClient(clientCoreOptionPath, sessionGetterOptionPath, true))
			{
				std::cout << "client start failed\n";
				return false;
			}
			clients.emplace_back(std::move(client));
		}

		for (const auto& client : clients)
		{
			if (not client->WaitForConnected(8s))
			{
				std::cout << "client connect wait failed\n";
				return false;
			}
		}

		for (int i = 0; i < clientCount; ++i)
		{
			const std::string message = "multi-echo-" + std::to_string(i);
			clients[i]->SendEchoRequestPacket(message);
			if (not clients[i]->WaitForEcho(message, 3s))
			{
				std::cout << "multi echo response wait failed\n";
				return false;
			}
		}

		for (const auto& client : clients)
		{
			client->StopClient();
		}
		return true;
	}

	// Each worker owns one client; traffic and disconnects overlap across clients.
	bool RunConcurrentEchoDisconnectScenario(const std::wstring& clientCoreOptionPath, const std::wstring& sessionGetterOptionPath)
	{
		constexpr int CLIENT_COUNT = 4;
		constexpr int MESSAGE_COUNT = 8;
		std::vector<std::unique_ptr<TestableRUDPClient>> clients;
		for (int index = 0; index < CLIENT_COUNT; ++index)
		{
			auto client = std::make_unique<TestableRUDPClient>();
			if (not client->StartClient(clientCoreOptionPath, sessionGetterOptionPath, true) ||
				not client->WaitForConnected(8s))
			{
				return false;
			}
			clients.emplace_back(std::move(client));
		}

		std::vector<std::future<bool>> workers;
		for (int index = 0; index < CLIENT_COUNT; ++index)
		{
			workers.emplace_back(std::async(std::launch::async, [client = clients[index].get(), index]()
			{
				bool succeeded = true;
				for (int messageIndex = 0; messageIndex < MESSAGE_COUNT; ++messageIndex)
				{
					const auto message = "concurrent-" + std::to_string(index) + "-" + std::to_string(messageIndex);
					client->SendEchoRequestPacket(message);
					if (not client->WaitForEcho(message, 3s))
					{
						succeeded = false;
						break;
					}
				}
				client->DisconnectClient();
				Sleep(1000);
				client->StopClient();
				return succeeded;
			}));
		}
		bool succeeded = true;
		for (auto& worker : workers)
		{
			// Always join every worker before destroying its client, even after a failure.
			succeeded = worker.get() && succeeded;
		}
		return succeeded;
	}

	bool RunOrderedBurstScenario(const std::wstring& clientCoreOptionPath, const std::wstring& sessionGetterOptionPath)
	{
		auto client = std::make_unique<TestableRUDPClient>();
		if (not client->StartClient(clientCoreOptionPath, sessionGetterOptionPath, true))
		{
			std::cout << "client start failed\n";
			Logger::GetInstance().StopLoggerThread();
			return false;
		}

		if (not client->WaitForConnected(8s))
		{
			std::cout << "client connect wait failed\n";
			return false;
		}

		for (int order = 1; order <= 5; ++order)
		{
			client->SendOrderedPacket(order);
		}

		for (int order = 1; order <= 5; ++order)
		{
			if (not client->WaitForOrderedResponse(order, 3s))
			{
				std::cout << "ordered response wait failed\n";
				return false;
			}
		}

		client->StopClient();
		return true;
	}
}

int wmain(const int argc, wchar_t* argv[])
{
	std::cout.setf(std::ios::unitbuf);

	int exitCode = 2;
	if (argc < 3 || std::wstring_view(argv[1]) != L"--scenario")
	{
		std::cout << "usage: --scenario <connect|reserve-timeout|echo|ping|drop-ack|disconnect|stop|multi-echo|ordered-burst> [value]\n";
		return 2;
	}

	const std::wstring clientCoreOptionPath = GetArgumentValue(argc, argv, L"--client-core-option")
		.value_or(GetOptionPath(L"TestOptions\\ClientCoreOption.txt"));
	const std::wstring sessionGetterOptionPath = GetArgumentValue(argc, argv, L"--client-session-getter-option")
		.value_or(GetOptionPath(L"TestOptions\\ClientSessionGetterOption.txt"));

	const std::wstring_view scenario = argv[2];
	if (scenario == L"connect")
	{
		exitCode = RunConnectScenario(clientCoreOptionPath, sessionGetterOptionPath) ? 0 : 1;
	}
	else if (scenario == L"reserve-timeout")
	{
		exitCode = RunReserveOnlyScenario(clientCoreOptionPath, sessionGetterOptionPath) ? 0 : 1;
	}
	else if (scenario == L"echo")
	{
		const std::string message = argc >= 4 ? std::filesystem::path(argv[3]).string() : "integration-echo";
		exitCode = RunEchoScenario(clientCoreOptionPath, sessionGetterOptionPath, message) ? 0 : 1;
	}
	else if (scenario == L"unreliable-only")
	{
		exitCode = RunUnreliableOnlyScenario(clientCoreOptionPath, sessionGetterOptionPath) ? 0 : 1;
	}
	else if (scenario == L"mixed-channels")
	{
		exitCode = RunUnreliableClientChecks() &&
			RunMixedChannelsScenario(clientCoreOptionPath, sessionGetterOptionPath) ? 0 : 1;
	}
	else if (scenario == L"unreliable-checks")
	{
		exitCode = RunUnreliableClientChecks() ? 0 : 1;
	}
	else if (scenario == L"ping")
	{
		exitCode = RunPingScenario(clientCoreOptionPath, sessionGetterOptionPath) ? 0 : 1;
	}
	else if (scenario == L"drop-ack")
	{
		exitCode = RunDropAckScenario(clientCoreOptionPath, sessionGetterOptionPath) ? 0 : 1;
	}
	else if (scenario == L"disconnect")
	{
		exitCode = RunDisconnectScenario(clientCoreOptionPath, sessionGetterOptionPath) ? 0 : 1;
	}
	else if (scenario == L"lifecycle-restart" || scenario == L"connect-timeout")
	{
		exitCode = RunClientLifecycleChecks(clientCoreOptionPath, sessionGetterOptionPath,
			scenario == L"connect-timeout") ? 0 : 1;
	}
	else if (scenario == L"stop")
	{
		exitCode = RunStopScenario(clientCoreOptionPath, sessionGetterOptionPath) ? 0 : 1;
	}
	else if (scenario == L"multi-echo")
	{
		const int clientCount = argc >= 4 ? (std::max)(1, _wtoi(argv[3])) : 3;
		exitCode = RunMultiEchoScenario(clientCoreOptionPath, sessionGetterOptionPath, clientCount) ? 0 : 1;
	}
	else if (scenario == L"concurrent-echo-disconnect")
	{
		exitCode = RunConcurrentEchoDisconnectScenario(clientCoreOptionPath, sessionGetterOptionPath) ? 0 : 1;
	}
	else if (scenario == L"ordered-burst")
	{
		exitCode = RunOrderedBurstScenario(clientCoreOptionPath, sessionGetterOptionPath) ? 0 : 1;
	}
	else
	{
		std::cout << "unknown scenario\n";
		exitCode = 2;
	}

	ExitProcess(exitCode);
	return exitCode;
}
