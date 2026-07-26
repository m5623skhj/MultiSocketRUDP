#include "PreCompile.h"
#include <gtest/gtest.h>

#include "MultiSocketRUDPCoreTestAccess.h"

#include <array>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <optional>
#include <tuple>

namespace
{
	std::wstring MakeCoreOptions(
		const std::optional<unsigned int> minRtoMs = 16,
		const std::optional<unsigned int> maxRtoMs = 100,
		const bool includeLossOptions = true)
	{
		std::wstring options =
			L":CORE\n"
			L"{\n"
			L"\tTHREAD_COUNT = 2\n"
			L"\tNUM_OF_SOCKET = 8\n"
			L"\tMAX_PACKET_RETRANSMISSION_COUNT = 3\n"
			L"\tWORKER_THREAD_ONE_FRAME_MS = 1\n"
			L"\tRETRANSMISSION_MS = 30\n";
		if (minRtoMs.has_value())
		{
			options += L"\tMIN_RETRANSMISSION_MS = " + std::to_wstring(*minRtoMs) + L"\n";
		}
		if (maxRtoMs.has_value())
		{
			options += L"\tMAX_RETRANSMISSION_MS = " + std::to_wstring(*maxRtoMs) + L"\n";
		}
		options +=
			L"\tHEARTBEAT_THREAD_SLEEP_MS = 100\n"
			L"\tTIMER_TICK_MS = 20\n"
			L"\tMAX_HOLDING_PACKET_QUEUE_SIZE = 16\n";
		if (includeLossOptions)
		{
			options +=
				L"\tSIMULATED_PACKET_LOSS_PERCENT = 25\n"
				L"\tSIMULATED_PACKET_LOSS_SEED = 12345\n";
		}
		options +=
			L"}\n"
			L"\n"
			L":SERIALIZEBUF\n"
			L"{\n"
			L"\tPACKET_CODE = 119\n"
			L"\tPACKET_KEY = 50\n"
			L"}\n";
		return options;
	}

	std::wstring MakeBrokerOptions(const std::wstring& ip = L"127.0.0.1")
	{
		return
			L":SESSION_BROKER\n"
			L"{\n"
			L"\tCORE_IP = \"" + ip + L"\"\n"
			L"\tSESSION_BROKER_PORT = 12011\n"
			L"}\n";
	}

	void RemoveLineContaining(std::wstring& text, const std::wstring_view token)
	{
		const size_t tokenPosition = text.find(token);
		if (tokenPosition == std::wstring::npos)
		{
			return;
		}

		const size_t lineStartPosition = text.rfind(L'\n', tokenPosition);
		const size_t eraseStart = lineStartPosition == std::wstring::npos ? 0 : lineStartPosition + 1;
		const size_t lineEndPosition = text.find(L'\n', tokenPosition);
		const size_t eraseEnd = lineEndPosition == std::wstring::npos ? text.size() : lineEndPosition + 1;
		text.erase(eraseStart, eraseEnd - eraseStart);
	}

	bool WriteUtf16File(const std::filesystem::path& path, const std::wstring& contents)
	{
		std::ofstream stream(path, std::ios::binary | std::ios::trunc);
		if (not stream.is_open())
		{
			return false;
		}

		constexpr wchar_t utf16Bom = 0xFEFF;
		stream.write(reinterpret_cast<const char*>(&utf16Bom), sizeof(utf16Bom));
		stream.write(
			reinterpret_cast<const char*>(contents.data()),
			static_cast<std::streamsize>(contents.size() * sizeof(wchar_t)));
		return stream.good();
	}
}

class CoreOptionParserTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		static std::atomic_uint32_t nextDirectoryId{ 1 };
		const uint32_t directoryId = nextDirectoryId.fetch_add(1, std::memory_order_relaxed);
		tempDirectory = std::filesystem::temp_directory_path() /
			(L"MultiSocketRUDPCoreOptionTest_" + std::to_wstring(GetCurrentProcessId()) + L"_" + std::to_wstring(directoryId));
		std::filesystem::create_directories(tempDirectory);
		coreOptionPath = tempDirectory / L"CoreOption.txt";
		brokerOptionPath = tempDirectory / L"BrokerOption.txt";
		originalHeaderCode = MultiSocketRUDPCoreTestAccess::GetPacketHeaderCode();
		originalXorCode = MultiSocketRUDPCoreTestAccess::GetPacketXorCode();
	}

	void TearDown() override
	{
		MultiSocketRUDPCoreTestAccess::SetPacketCodes(originalHeaderCode, originalXorCode);
		std::error_code errorCode;
		std::filesystem::remove_all(tempDirectory, errorCode);
	}

	bool Parse(
		MultiSocketRUDPCore& core,
		const std::wstring& coreOptions,
		const std::wstring& brokerOptions)
	{
		if (not WriteUtf16File(coreOptionPath, coreOptions) ||
			not WriteUtf16File(brokerOptionPath, brokerOptions))
		{
			return false;
		}

		return MultiSocketRUDPCoreTestAccess::ReadOptionFile(
			core,
			coreOptionPath.wstring(),
			brokerOptionPath.wstring());
	}

	std::filesystem::path tempDirectory;
	std::filesystem::path coreOptionPath;
	std::filesystem::path brokerOptionPath;
	BYTE originalHeaderCode{};
	BYTE originalXorCode{};
};

TEST_F(CoreOptionParserTest, ValidOptionsPopulateEveryServerSetting)
{
	MultiSocketRUDPCore core{ L"", L"" };

	ASSERT_TRUE(Parse(core, MakeCoreOptions(), MakeBrokerOptions()));

	EXPECT_EQ(MultiSocketRUDPCoreTestAccess::GetWorkerThreadCount(core), 2);
	EXPECT_EQ(MultiSocketRUDPCoreTestAccess::GetSocketCount(core), 8);
	EXPECT_EQ(MultiSocketRUDPCoreTestAccess::GetMaxRetransmissionCount(core), 3);
	EXPECT_EQ(MultiSocketRUDPCoreTestAccess::GetWorkerFrameMs(core), 1u);
	EXPECT_EQ(core.GetInitialRetransmissionMs(), 30u);
	EXPECT_EQ(core.GetMinRetransmissionMs(), 16u);
	EXPECT_EQ(core.GetMaxRetransmissionMs(), 100u);
	EXPECT_EQ(core.GetHeartbeatThreadSleepMs(), 100u);
	EXPECT_EQ(MultiSocketRUDPCoreTestAccess::GetTimerTickMs(core), 20u);
	EXPECT_EQ(MultiSocketRUDPCoreTestAccess::GetMaximumHoldingQueueSize(core), 16);
	EXPECT_EQ(MultiSocketRUDPCoreTestAccess::GetSimulatedPacketLossPercent(core), 25u);
	EXPECT_EQ(MultiSocketRUDPCoreTestAccess::GetSimulatedPacketLossSeed(core), 12345);
	EXPECT_EQ(MultiSocketRUDPCoreTestAccess::GetCoreServerIp(core), "127.0.0.1");
	EXPECT_EQ(MultiSocketRUDPCoreTestAccess::GetSessionBrokerPort(core), 12011);
}

TEST_F(CoreOptionParserTest, MissingOptionalRtoBoundsUseInitialRtoAndLossOptionsDefaultToZero)
{
	MultiSocketRUDPCore core{ L"", L"" };

	ASSERT_TRUE(Parse(
		core,
		MakeCoreOptions(std::nullopt, std::nullopt, false),
		MakeBrokerOptions()));

	EXPECT_EQ(core.GetInitialRetransmissionMs(), 30u);
	EXPECT_EQ(core.GetMinRetransmissionMs(), 30u);
	EXPECT_EQ(core.GetMaxRetransmissionMs(), 30u);
	EXPECT_EQ(MultiSocketRUDPCoreTestAccess::GetSimulatedPacketLossPercent(core), 0u);
	EXPECT_EQ(MultiSocketRUDPCoreTestAccess::GetSimulatedPacketLossSeed(core), 0);
}

TEST_F(CoreOptionParserTest, OnlyOneOptionalRtoBoundIsRejected)
{
	MultiSocketRUDPCore missingMaximum{ L"", L"" };
	MultiSocketRUDPCore missingMinimum{ L"", L"" };

	EXPECT_FALSE(Parse(
		missingMaximum,
		MakeCoreOptions(16, std::nullopt),
		MakeBrokerOptions()));
	EXPECT_FALSE(Parse(
		missingMinimum,
		MakeCoreOptions(std::nullopt, 100),
		MakeBrokerOptions()));
}

TEST_F(CoreOptionParserTest, InvalidRtoOrderingIsRejected)
{
	const std::array invalidBounds{
		std::tuple{ 0u, 100u },
		std::tuple{ 31u, 100u },
		std::tuple{ 16u, 29u }
	};

	for (const auto& [minimum, maximum] : invalidBounds)
	{
		MultiSocketRUDPCore core{ L"", L"" };
		EXPECT_FALSE(Parse(core, MakeCoreOptions(minimum, maximum), MakeBrokerOptions()))
			<< "minimum=" << minimum << ", maximum=" << maximum;
	}
}

TEST_F(CoreOptionParserTest, EqualRtoBoundsAtInitialValueAreAccepted)
{
	MultiSocketRUDPCore core{ L"", L"" };

	ASSERT_TRUE(Parse(core, MakeCoreOptions(30, 30), MakeBrokerOptions()));
	EXPECT_EQ(core.GetInitialRetransmissionMs(), 30u);
	EXPECT_EQ(core.GetMinRetransmissionMs(), 30u);
	EXPECT_EQ(core.GetMaxRetransmissionMs(), 30u);
}

TEST_F(CoreOptionParserTest, MissingRequiredCoreAndBrokerOptionsAreRejected)
{
	const std::array coreKeys{
		L"THREAD_COUNT",
		L"NUM_OF_SOCKET",
		L"MAX_PACKET_RETRANSMISSION_COUNT",
		L"WORKER_THREAD_ONE_FRAME_MS",
		L"RETRANSMISSION_MS",
		L"HEARTBEAT_THREAD_SLEEP_MS",
		L"TIMER_TICK_MS",
		L"MAX_HOLDING_PACKET_QUEUE_SIZE",
		L"PACKET_CODE",
		L"PACKET_KEY"
	};
	for (const auto* key : coreKeys)
	{
		std::wstring coreOptions = MakeCoreOptions();
		RemoveLineContaining(coreOptions, key);
		MultiSocketRUDPCore core{ L"", L"" };
		EXPECT_FALSE(Parse(core, coreOptions, MakeBrokerOptions()));
	}

	const std::array brokerKeys{ L"CORE_IP", L"SESSION_BROKER_PORT" };
	for (const auto* key : brokerKeys)
	{
		std::wstring brokerOptions = MakeBrokerOptions();
		RemoveLineContaining(brokerOptions, key);
		MultiSocketRUDPCore core{ L"", L"" };
		EXPECT_FALSE(Parse(core, MakeCoreOptions(), brokerOptions));
	}
}

TEST_F(CoreOptionParserTest, EmptyAndOverlongServerIpAreRejected)
{
	MultiSocketRUDPCore emptyIpCore{ L"", L"" };
	MultiSocketRUDPCore overlongIpCore{ L"", L"" };

	EXPECT_FALSE(Parse(emptyIpCore, MakeCoreOptions(), MakeBrokerOptions(L"")));
	EXPECT_FALSE(Parse(overlongIpCore, MakeCoreOptions(), MakeBrokerOptions(L"1234567890123456")));
}
