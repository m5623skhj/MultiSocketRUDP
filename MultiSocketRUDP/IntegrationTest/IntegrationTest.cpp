#include <gtest/gtest.h>

#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Windows.h>

#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "../Logger/Logger.h"
#include "../Common/TLS/TLSHelper.h"
#include "../ContentsServer/PacketIdType.h"
#include "../ContentsServer/Protocol.h"
#include "../MultiSocketRUDPServer/LogExtension.h"
#include "../MultiSocketRUDPServer/MultiSocketRUDPCore.h"
#include "../MultiSocketRUDPServer/PacketHandlerUtil.h"
#include "../MultiSocketRUDPServer/RUDPSession.h"

namespace
{
	using namespace std::chrono_literals;

	constexpr wchar_t TEST_CERT_FILE_NAME[] = L"TestCert.pfx";
	constexpr wchar_t TEST_CERT_PASSWORD[] = L"MultiSocketRUDPIntegrationTest!";

	std::optional<long> TryInitializeClientTls()
	{
		TLSHelper::TLSHelperClient tlsHelper;
		if (tlsHelper.Initialize())
		{
			return std::nullopt;
		}

		return tlsHelper.GetLastStatus();
	}

	std::wstring GetRootRelativePath(const wchar_t* relativePath)
	{
		std::array<wchar_t, MAX_PATH> modulePath{};
		GetModuleFileNameW(nullptr, modulePath.data(), static_cast<DWORD>(modulePath.size()));

		std::filesystem::path rootDir = std::filesystem::path(modulePath.data()).parent_path();
		rootDir = rootDir.parent_path();
		rootDir = rootDir.parent_path();

		return (rootDir / relativePath).wstring();
	}

	std::wstring GetTestOptionPath(const wchar_t* relativePath)
	{
		return GetRootRelativePath((std::wstring(L"IntegrationTest\\") + relativePath).c_str());
	}

	std::wstring GetTestCertificatePath()
	{
		return GetTestOptionPath(TEST_CERT_FILE_NAME);
	}

	std::filesystem::path GetTempOptionDirectory()
	{
		return std::filesystem::temp_directory_path() / L"MultiSocketRUDPIntegrationTest";
	}

	bool WriteUtf16TextFile(const std::filesystem::path& filePath, const std::wstring& contents)
	{
		std::error_code errorCode;
		std::filesystem::create_directories(filePath.parent_path(), errorCode);
		if (errorCode)
		{
			return false;
		}

		std::ofstream stream(filePath, std::ios::binary | std::ios::trunc);
		if (not stream.is_open())
		{
			return false;
		}

		constexpr wchar_t utf16Bom = 0xFEFF;
		stream.write(reinterpret_cast<const char*>(&utf16Bom), sizeof(utf16Bom));
		stream.write(reinterpret_cast<const char*>(contents.data()), static_cast<std::streamsize>(contents.size() * sizeof(wchar_t)));
		return stream.good();
	}

	unsigned short AcquireAvailableTcpPort()
	{
		const SOCKET tempSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (tempSocket == INVALID_SOCKET)
		{
			return 0;
		}

		sockaddr_in address{};
		address.sin_family = AF_INET;
		address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		address.sin_port = 0;

		if (bind(tempSocket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR)
		{
			closesocket(tempSocket);
			return 0;
		}

		int addressLength = sizeof(address);
		if (getsockname(tempSocket, reinterpret_cast<sockaddr*>(&address), &addressLength) == SOCKET_ERROR)
		{
			closesocket(tempSocket);
			return 0;
		}

		const unsigned short port = ntohs(address.sin_port);
		closesocket(tempSocket);
		return port;
	}

	bool HasTestCertificateFile()
	{
		std::error_code errorCode;
		return std::filesystem::exists(GetTestCertificatePath(), errorCode) && not errorCode;
	}

	struct TestOptionFiles
	{
		unsigned short brokerPort{};
		std::filesystem::path sessionBrokerOptionPath{};
		std::filesystem::path clientSessionGetterOptionPath{};
	};

	std::optional<TestOptionFiles> CreateTestOptionFiles()
	{
		const unsigned short brokerPort = AcquireAvailableTcpPort();
		if (brokerPort == 0)
		{
			return std::nullopt;
		}

		static std::atomic_uint32_t nextFileId{ 1 };
		const uint32_t fileId = nextFileId.fetch_add(1, std::memory_order_relaxed);
		const auto tempDirectory = GetTempOptionDirectory();

		TestOptionFiles optionFiles;
		optionFiles.brokerPort = brokerPort;
		optionFiles.sessionBrokerOptionPath = tempDirectory / (L"SessionBrokerOption_" + std::to_wstring(fileId) + L".txt");
		optionFiles.clientSessionGetterOptionPath = tempDirectory / (L"ClientSessionGetterOption_" + std::to_wstring(fileId) + L".txt");

		const std::wstring sessionBrokerContents =
			L":SESSION_BROKER\n"
			L"{\n"
			L"\tCORE_IP = \"127.0.0.1\"\n"
			L"\tSESSION_BROKER_PORT = " + std::to_wstring(brokerPort) + L"\n"
			L"}\n";

		const std::wstring clientSessionGetterContents =
			L":SESSION_BROKER\n"
			L"{\n"
			L"\tIP = \"127.0.0.1\"\n"
			L"\tPORT = " + std::to_wstring(brokerPort) + L"\n"
			L"}\n"
			L"\n"
			L":SERIALIZEBUF\n"
			L"{\n"
			L"\tPACKET_CODE\t= 119\n"
			L"\tPACKET_KEY\t= 50\n"
			L"}\n";

		if (not WriteUtf16TextFile(optionFiles.sessionBrokerOptionPath, sessionBrokerContents) ||
			not WriteUtf16TextFile(optionFiles.clientSessionGetterOptionPath, clientSessionGetterContents))
		{
			return std::nullopt;
		}

		return optionFiles;
	}

	struct SessionStats
	{
		std::atomic_int connectedCount{ 0 };
		std::atomic_int disconnectedCount{ 0 };
		std::atomic_int releasedCount{ 0 };
		std::atomic_int pingRequestCount{ 0 };
		std::atomic_int echoRequestCount{ 0 };
		std::mutex lastEchoMutex;
		std::string lastEchoRequest{};

		void Reset()
		{
			connectedCount.store(0, std::memory_order_relaxed);
			disconnectedCount.store(0, std::memory_order_relaxed);
			releasedCount.store(0, std::memory_order_relaxed);
			pingRequestCount.store(0, std::memory_order_relaxed);
			echoRequestCount.store(0, std::memory_order_relaxed);
			std::scoped_lock lock(lastEchoMutex);
			lastEchoRequest.clear();
		}
	};

	SessionStats& GetSessionStats()
	{
		static SessionStats stats;
		return stats;
	}

	void RegisterContentsPacketsOnce()
	{
		static std::once_flag once;
		std::call_once(once, []
		{
			PacketHandlerUtil::RegisterPacket<Ping>();
			PacketHandlerUtil::RegisterPacket<TestStringPacketReq>();
			PacketHandlerUtil::RegisterPacket<TestPacketReq>();
		});
	}

	class IntegrationSession final : public RUDPSession
	{
	public:
		explicit IntegrationSession(MultiSocketRUDPCore& inCore)
			: RUDPSession(inCore)
		{
			RegisterPacketHandler<IntegrationSession, Ping>(
				static_cast<PacketId>(PACKET_ID::PING),
				&IntegrationSession::OnPing
			);
			RegisterPacketHandler<IntegrationSession, TestStringPacketReq>(
				static_cast<PacketId>(PACKET_ID::TEST_STRING_PACKET_REQ),
				&IntegrationSession::OnTestStringPacketReq
			);
			RegisterPacketHandler<IntegrationSession, TestPacketReq>(
				static_cast<PacketId>(PACKET_ID::TEST_PACKET_REQ),
				&IntegrationSession::OnTestPacketReq
			);
		}

	private:
		void OnConnected() override
		{
			GetSessionStats().connectedCount.fetch_add(1, std::memory_order_relaxed);
		}

		void OnDisconnected() override
		{
			GetSessionStats().disconnectedCount.fetch_add(1, std::memory_order_relaxed);
		}

		void OnReleased() override
		{
			GetSessionStats().releasedCount.fetch_add(1, std::memory_order_relaxed);
		}

		void OnPing(const Ping&)
		{
			GetSessionStats().pingRequestCount.fetch_add(1, std::memory_order_relaxed);
			Pong pong;
			EXPECT_TRUE(SendPacket(pong));
		}

		void OnTestStringPacketReq(const TestStringPacketReq& packet)
		{
			GetSessionStats().echoRequestCount.fetch_add(1, std::memory_order_relaxed);
			{
				std::scoped_lock lock(GetSessionStats().lastEchoMutex);
				GetSessionStats().lastEchoRequest = packet.testString;
			}

			TestStringPacketRes response;
			response.echoString = packet.testString;
			EXPECT_TRUE(SendPacket(response));
		}

		void OnTestPacketReq(const TestPacketReq& packet)
		{
			TestPacketRes response;
			response.order = packet.order;
			EXPECT_TRUE(SendPacket(response));
		}
	};

	class IntegrationServerHarness
	{
	public:
		explicit IntegrationServerHarness(std::wstring inSessionBrokerOptionPath)
			: serverCore(TLSHelper::ServerCertificateConfig::FromPfxFile(GetTestCertificatePath(), TEST_CERT_PASSWORD))
			, sessionBrokerOptionPath(std::move(inSessionBrokerOptionPath))
		{
		}

		bool Start()
		{
			RegisterContentsPacketsOnce();
			auto factory = [](MultiSocketRUDPCore& inCore) -> RUDPSession*
			{
				return new IntegrationSession(inCore);
			};

			return serverCore.StartServer(
				GetTestOptionPath(L"TestOptions\\CoreOption.txt"),
				sessionBrokerOptionPath,
				std::move(factory),
				false
			);
		}

		void Stop()
		{
			serverCore.StopServer();
		}

		[[nodiscard]]
		unsigned short GetConnectedSessionCount() const
		{
			return serverCore.GetNowSessionCount();
		}

		[[nodiscard]]
		unsigned short GetUnusedSessionCount() const
		{
			return serverCore.GetUnusedSessionCount();
		}

		[[nodiscard]]
		unsigned int GetAllConnectedCount() const
		{
			return serverCore.GetAllConnectedCount();
		}

		[[nodiscard]]
		unsigned int GetAllDisconnectedCount() const
		{
			return serverCore.GetAllDisconnectedCount();
		}

		[[nodiscard]]
		unsigned int GetAllDisconnectedByRetransmissionCount() const
		{
			return serverCore.GetAllDisconnectedByRetransmissionCount();
		}

	private:
		MultiSocketRUDPCore serverCore;
		std::wstring sessionBrokerOptionPath;
	};

	struct ClientProcessResult
	{
		bool completed{ false };
		DWORD exitCode{ static_cast<DWORD>(-1) };
		std::string output{};
	};

	class ClientHarnessProcess
	{
	public:
		~ClientHarnessProcess()
		{
			Close();
		}

		bool Start(const std::vector<std::wstring>& args)
		{
			SECURITY_ATTRIBUTES securityAttributes{};
			securityAttributes.nLength = sizeof(securityAttributes);
			securityAttributes.bInheritHandle = TRUE;

			HANDLE writeHandle = nullptr;
			if (not CreatePipe(&readHandle, &writeHandle, &securityAttributes, 0))
			{
				return false;
			}

			SetHandleInformation(readHandle, HANDLE_FLAG_INHERIT, 0);

			STARTUPINFOW startupInfo{};
			startupInfo.cb = sizeof(startupInfo);
			startupInfo.dwFlags = STARTF_USESTDHANDLES;
			startupInfo.hStdOutput = writeHandle;
			startupInfo.hStdError = writeHandle;
			startupInfo.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

			std::wstring commandLine = L"\"" + GetHarnessPath() + L"\"";
			for (const auto& arg : args)
			{
				commandLine += L" \"" + arg + L"\"";
			}

			std::vector<wchar_t> mutableCommandLine(commandLine.begin(), commandLine.end());
			mutableCommandLine.push_back(L'\0');

			const bool created = CreateProcessW(
				nullptr,
				mutableCommandLine.data(),
				nullptr,
				nullptr,
				TRUE,
				CREATE_NO_WINDOW,
				nullptr,
				nullptr,
				&startupInfo,
				&processInfo
			) == TRUE;

			CloseHandle(writeHandle);
			if (not created)
			{
				Close();
			}

			return created;
		}

		ClientProcessResult Wait(const std::chrono::milliseconds timeout)
		{
			ClientProcessResult result;
			if (processInfo.hProcess == nullptr)
			{
				return result;
			}

			const DWORD waitResult = WaitForSingleObject(processInfo.hProcess, static_cast<DWORD>(timeout.count()));
			if (waitResult == WAIT_OBJECT_0)
			{
				result.completed = true;
				GetExitCodeProcess(processInfo.hProcess, &result.exitCode);
			}
			else if (waitResult == WAIT_TIMEOUT)
			{
				TerminateProcess(processInfo.hProcess, 1);
				WaitForSingleObject(processInfo.hProcess, 5000);
				result.completed = false;
				GetExitCodeProcess(processInfo.hProcess, &result.exitCode);
			}

			result.output = ReadRemainingOutput();
			Close();
			return result;
		}

	private:
		static std::wstring GetHarnessPath()
		{
			std::array<wchar_t, MAX_PATH> modulePath{};
			GetModuleFileNameW(nullptr, modulePath.data(), static_cast<DWORD>(modulePath.size()));
			return (std::filesystem::path(modulePath.data()).parent_path() / L"IntegrationClientHarness.exe").wstring();
		}

		std::string ReadRemainingOutput()
		{
			std::string output;
			if (readHandle == nullptr)
			{
				return output;
			}

			char buffer[512];
			DWORD bytesRead = 0;
			while (ReadFile(readHandle, buffer, sizeof(buffer), &bytesRead, nullptr) && bytesRead > 0)
			{
				output.append(buffer, buffer + bytesRead);
			}

			return output;
		}

		void Close()
		{
			if (processInfo.hThread != nullptr)
			{
				CloseHandle(processInfo.hThread);
				processInfo.hThread = nullptr;
			}

			if (processInfo.hProcess != nullptr)
			{
				CloseHandle(processInfo.hProcess);
				processInfo.hProcess = nullptr;
			}

			if (readHandle != nullptr)
			{
				CloseHandle(readHandle);
				readHandle = nullptr;
			}
		}

	private:
		PROCESS_INFORMATION processInfo{};
		HANDLE readHandle{ nullptr };
	};

	class IntegrationFixture : public ::testing::Test
	{
	protected:
		static void SetUpTestSuite()
		{
			WSADATA wsaData{};
			ASSERT_EQ(WSAStartup(MAKEWORD(2, 2), &wsaData), 0);
		}

		static void TearDownTestSuite()
		{
			WSACleanup();
		}

		void SetUp() override
		{
			ASSERT_TRUE(HasTestCertificateFile())
				<< "Missing IntegrationTest\\\\" << TEST_CERT_FILE_NAME << ". Run Tool\\\\ForTLS\\\\CreateDevTLSPfx.bat first.";
			const auto clientTlsStatus = TryInitializeClientTls();
			ASSERT_FALSE(clientTlsStatus.has_value())
				<< "TLS client credential initialization failed. SECURITY_STATUS=0x"
				<< std::hex << clientTlsStatus.value_or(0);

			GetSessionStats().Reset();

			optionFiles = CreateTestOptionFiles();
			ASSERT_TRUE(optionFiles.has_value());

			server = std::make_unique<IntegrationServerHarness>(optionFiles->sessionBrokerOptionPath.wstring());
			serverStarted = server->Start();
			ASSERT_TRUE(serverStarted);
		}

		void TearDown() override
		{
			if (server != nullptr && serverStarted)
			{
				server->Stop();
			}

			if (optionFiles.has_value())
			{
				std::error_code errorCode;
				std::filesystem::remove(optionFiles->sessionBrokerOptionPath, errorCode);
				std::filesystem::remove(optionFiles->clientSessionGetterOptionPath, errorCode);
			}

#if _DEBUG
			RUDPSession::SetReservedSessionTimeoutMsForTest(30000);
#endif
			if (not serverStarted)
			{
				Logger::GetInstance().StopLoggerThread();
			}
		}

		ClientProcessResult RunClientScenario(const std::vector<std::wstring>& args, const std::chrono::milliseconds timeout)
		{
			ClientHarnessProcess process;
			const auto fullArgs = BuildClientArgs(args);
			EXPECT_TRUE(process.Start(fullArgs));
			return process.Wait(timeout);
		}

		std::vector<std::wstring> BuildClientArgs(const std::vector<std::wstring>& args) const
		{
			std::vector<std::wstring> fullArgs = args;
			fullArgs.emplace_back(L"--client-session-getter-option");
			fullArgs.emplace_back(optionFiles->clientSessionGetterOptionPath.wstring());
			return fullArgs;
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

	protected:
		std::unique_ptr<IntegrationServerHarness> server;
		std::optional<TestOptionFiles> optionFiles;
		bool serverStarted{ false };
	};

	TEST_F(IntegrationFixture, ConnectHandshakeCompletesAndSessionCountsUpdate)
	{
		ClientHarnessProcess process;
		ASSERT_TRUE(process.Start(BuildClientArgs({ L"--scenario", L"connect" })));

		EXPECT_TRUE(WaitUntil(5s, [this]()
		{
			return server->GetConnectedSessionCount() == 1 &&
				server->GetAllConnectedCount() == 1 &&
				GetSessionStats().connectedCount.load(std::memory_order_relaxed) == 1;
		}));

		const auto result = process.Wait(10s);
		EXPECT_TRUE(result.completed);
		EXPECT_EQ(result.exitCode, 0u) << result.output;
	}

	TEST_F(IntegrationFixture, ReservedSessionReturnsToPoolAfterTimeout)
	{
#if _DEBUG
		RUDPSession::SetReservedSessionTimeoutMsForTest(300);
		const unsigned short unusedBefore = server->GetUnusedSessionCount();

		ClientHarnessProcess process;
		ASSERT_TRUE(process.Start(BuildClientArgs({ L"--scenario", L"reserve-timeout" })));

		EXPECT_TRUE(WaitUntil(2s, [this, unusedBefore]()
		{
			return server->GetUnusedSessionCount() == unusedBefore - 1;
		}));

		EXPECT_TRUE(WaitUntil(5s, [this, unusedBefore]()
		{
			return server->GetUnusedSessionCount() == unusedBefore;
		}));

		const auto result = process.Wait(10s);
		EXPECT_TRUE(result.completed);
		EXPECT_EQ(result.exitCode, 0u) << result.output;
		EXPECT_EQ(server->GetAllConnectedCount(), 0u);
		EXPECT_EQ(server->GetAllDisconnectedCount(), 0u);
#else
		GTEST_SKIP() << "Reserved session timeout override is available only in Debug builds.";
#endif
	}

	TEST_F(IntegrationFixture, RequestResponseRoundTripReturnsExpectedPayload)
	{
		ClientHarnessProcess process;
		ASSERT_TRUE(process.Start(BuildClientArgs({ L"--scenario", L"echo", L"integration-echo" })));

		EXPECT_TRUE(WaitUntil(5s, []()
		{
			return GetSessionStats().echoRequestCount.load(std::memory_order_relaxed) == 1;
		}));

		const auto result = process.Wait(10s);
		EXPECT_TRUE(result.completed);
		EXPECT_EQ(result.exitCode, 0u) << result.output;

		std::scoped_lock lock(GetSessionStats().lastEchoMutex);
		EXPECT_EQ(GetSessionStats().lastEchoRequest, "integration-echo");
	}

	TEST_F(IntegrationFixture, MissingReplyAckTriggersServerRetransmissionDisconnect)
	{
		// Drop SEND_REPLY ACKs so the server exercises the real dead-heap
		// retransmission path, including dynamic RTO backoff and final disconnect.
		ClientHarnessProcess process;
		ASSERT_TRUE(process.Start(BuildClientArgs({ L"--scenario", L"drop-ack" })));

		EXPECT_TRUE(WaitUntil(30s, [this]()
		{
			return server->GetAllDisconnectedByRetransmissionCount() == 1;
		}));

		const auto result = process.Wait(35s);
		EXPECT_TRUE(result.completed);
		EXPECT_EQ(result.exitCode, 0u) << result.output;
	}
}
