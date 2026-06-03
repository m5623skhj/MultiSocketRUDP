#include <Windows.h>

#include <array>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

#include "../Logger/Logger.h"
#include "../IntegrationTest/TestableRUDPClient.h"

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
		auto* client = new TestableRUDPClient();
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
		return true;
	}

	bool RunReserveOnlyScenario(const std::wstring& clientCoreOptionPath, const std::wstring& sessionGetterOptionPath)
	{
		auto* client = new TestableRUDPClient();
		if (not client->StartClient(clientCoreOptionPath, sessionGetterOptionPath, false))
		{
			std::cout << "client reserve-only start failed\n";
			Logger::GetInstance().StopLoggerThread();
			return false;
		}

		Sleep(1000);
		return true;
	}

	bool RunEchoScenario(const std::wstring& clientCoreOptionPath, const std::wstring& sessionGetterOptionPath, const std::string& message)
	{
		auto* client = new TestableRUDPClient();
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

		return true;
	}

	bool RunDropAckScenario(const std::wstring& clientCoreOptionPath, const std::wstring& sessionGetterOptionPath)
	{
		auto* client = new TestableRUDPClient();
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
		return true;
	}
}

int wmain(const int argc, wchar_t* argv[])
{
	std::cout.setf(std::ios::unitbuf);

	int exitCode = 2;
	if (argc < 3 || std::wstring_view(argv[1]) != L"--scenario")
	{
		std::cout << "usage: --scenario <connect|reserve-timeout|echo|drop-ack> [message]\n";
		ExitProcess(2);
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
	else if (scenario == L"drop-ack")
	{
		exitCode = RunDropAckScenario(clientCoreOptionPath, sessionGetterOptionPath) ? 0 : 1;
	}
	else
	{
		std::cout << "unknown scenario\n";
		exitCode = 2;
	}

	ExitProcess(static_cast<UINT>(exitCode));
}
