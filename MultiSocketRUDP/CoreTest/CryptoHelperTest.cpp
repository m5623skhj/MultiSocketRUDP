#include "PreCompile.h"
#include <gtest/gtest.h>
#include <array>
#include <filesystem>
#include <format>
#include <fstream>
#include "../../external/CommonCode/Common/json-develop/single_include/nlohmann/json.hpp"

#include "PacketManager.h"
#include "../Common/Crypto/CryptoHelper.h"
#ifndef LOG_ERROR
#define LOG_ERROR(...) ((void)0)
#endif
#include "../Common/PacketCrypto/PacketCryptoHelper.h"

namespace
{
	struct ProtocolInteropVector
	{
		std::string name;
		PacketSequence sequence{};
		std::array<unsigned char, SESSION_KEY_SIZE> key{};
		std::array<unsigned char, SESSION_SALT_SIZE> salt{};
		unsigned char headerCode{};
		PACKET_DIRECTION direction{ PACKET_DIRECTION::INVALID };
		bool isCorePacket{};
		PACKET_TYPE packetType{ PACKET_TYPE::INVALID_TYPE };
		std::optional<PacketId> packetId;
		std::vector<unsigned char> plaintext;
		std::vector<unsigned char> encodedPacket;
	};

	std::vector<unsigned char> HexToBytes(const std::string& hex)
	{
		if (hex.size() % 2 != 0)
		{
			throw std::invalid_argument("Hex string must contain complete bytes");
		}

		std::vector<unsigned char> bytes(hex.size() / 2);
		for (size_t i = 0; i < bytes.size(); ++i)
		{
			bytes[i] = static_cast<unsigned char>(std::stoul(hex.substr(i * 2, 2), nullptr, 16));
		}
		return bytes;
	}

	template <size_t Size>
	std::array<unsigned char, Size> HexToArray(const std::string& hex)
	{
		const auto bytes = HexToBytes(hex);
		if (bytes.size() != Size)
		{
			throw std::invalid_argument("Hex string has an unexpected size");
		}

		std::array<unsigned char, Size> result{};
		std::copy(bytes.begin(), bytes.end(), result.begin());
		return result;
	}

	std::vector<ProtocolInteropVector> LoadProtocolInteropVectors()
	{
		std::array<wchar_t, MAX_PATH> executablePath{};
		const DWORD pathLength = GetModuleFileNameW(nullptr, executablePath.data(), static_cast<DWORD>(executablePath.size()));
		if (pathLength == 0 || pathLength == executablePath.size())
		{
			throw std::runtime_error("Failed to locate CoreTest executable");
		}

		const auto vectorPath = std::filesystem::path(executablePath.data()).parent_path() / "ProtocolInteropVector.json";
		std::ifstream vectorFile(vectorPath);
		if (not vectorFile)
		{
			throw std::runtime_error("Failed to open protocol interop vector");
		}

		nlohmann::json json;
		vectorFile >> json;

		std::vector<ProtocolInteropVector> results;
		results.reserve(json.size());
		for (const auto& item : json)
		{
			ProtocolInteropVector result;
			result.name = item.at("name").get<std::string>();
			result.sequence = item.at("sequence").get<PacketSequence>();
			result.key = HexToArray<SESSION_KEY_SIZE>(item.at("keyHex").get<std::string>());
			result.salt = HexToArray<SESSION_SALT_SIZE>(item.at("saltHex").get<std::string>());
			result.headerCode = item.at("headerCode").get<unsigned char>();
			result.direction = static_cast<PACKET_DIRECTION>(item.at("direction").get<uint8_t>());
			result.isCorePacket = item.at("isCorePacket").get<bool>();
			result.packetType = static_cast<PACKET_TYPE>(item.at("packetType").get<uint8_t>());
			if (item.contains("packetId") && not item.at("packetId").is_null())
			{
				result.packetId = item.at("packetId").get<PacketId>();
			}
			result.plaintext = HexToBytes(item.at("plaintextHex").get<std::string>());
			result.encodedPacket = HexToBytes(item.at("encodedPacketHex").get<std::string>());
			results.push_back(std::move(result));
		}
		return results;
	}

	class SymmetricKey
	{
	public:
		SymmetricKey() : SymmetricKey(MakeDefaultKey()) {}

		explicit SymmetricKey(const std::array<unsigned char, SESSION_KEY_SIZE>& inKey)
			: key(inKey)
		{
			keyObject.resize(CryptoHelper::GetTLSInstance().GetKeyObjectSize());
			handle = CryptoHelper::GetTLSInstance().GetSymmetricKeyHandle(keyObject.data(), key.data());
		}

		~SymmetricKey() { CryptoHelper::DestroySymmetricKeyHandle(handle); }
		BCRYPT_KEY_HANDLE Get() const { return handle; }

	private:
		static std::array<unsigned char, SESSION_KEY_SIZE> MakeDefaultKey()
		{
			std::array<unsigned char, SESSION_KEY_SIZE> result{};
			for (size_t i = 0; i < result.size(); ++i)
			{
				result[i] = static_cast<unsigned char>(i + 1);
			}
			return result;
		}

		std::array<unsigned char, SESSION_KEY_SIZE> key{};
		std::vector<unsigned char> keyObject;
		BCRYPT_KEY_HANDLE handle{};
	};

	std::array<unsigned char, SESSION_SALT_SIZE> MakeSalt()
	{
		std::array<unsigned char, SESSION_SALT_SIZE> salt{};
		for (size_t i = 0; i < salt.size(); ++i)
		{
			salt[i] = static_cast<unsigned char>(0xA0 + i);
		}
		return salt;
	}

	void AdvanceToPacketBody(NetBuffer& packet)
	{
		char header[df_HEADER_SIZE]{};
		packet.ReadBuffer(header, sizeof(header));
		PACKET_TYPE packetType{};
		packet >> packetType;
	}

	NetBuffer MakeDataPacket(const PacketSequence sequence, const std::string& payload)
	{
		NetBuffer packet;
		packet.Init();
		PACKET_TYPE type = PACKET_TYPE::SEND_TYPE;
		PacketId packetId = 77;
		packet << type << sequence << packetId << payload;
		return packet;
	}

	NetBuffer MakeCorePacket(const PacketSequence sequence)
	{
		NetBuffer packet;
		packet.Init();
		PACKET_TYPE type = PACKET_TYPE::HEARTBEAT_TYPE;
		packet << type << sequence;
		return packet;
	}
}

class PacketCryptoTest : public ::testing::Test
{
protected:
	SymmetricKey key;
	std::array<unsigned char, SESSION_SALT_SIZE> salt = MakeSalt();
};

TEST_F(PacketCryptoTest, DataPacket_RoundTripPreservesPayload)
{
	constexpr PacketSequence sequence = 42;
	auto packet = MakeDataPacket(sequence, "encrypted-payload");
	PacketCryptoHelper::EncodePacket(packet, sequence, PACKET_DIRECTION::SERVER_TO_CLIENT,
		salt.data(), salt.size(), key.Get(), false);
	AdvanceToPacketBody(packet);
	ASSERT_TRUE(PacketCryptoHelper::DecodePacket(packet, salt.data(), salt.size(), key.Get(), false,
		PACKET_DIRECTION::SERVER_TO_CLIENT));

	PacketSequence decodedSequence{};
	PacketId decodedId{};
	std::string decodedPayload;
	packet >> decodedSequence >> decodedId >> decodedPayload;
	EXPECT_EQ(decodedSequence, sequence);
	EXPECT_EQ(decodedId, 77u);
	EXPECT_EQ(decodedPayload, "encrypted-payload");
}

TEST_F(PacketCryptoTest, CorePacket_EmptyBodyRoundTripSucceedsAtSequenceBoundaries)
{
	constexpr PacketSequence maxSequence = (std::numeric_limits<PacketSequence>::max)();
	for (const PacketSequence sequence : { PacketSequence{ 0 }, maxSequence })
	{
		auto packet = MakeCorePacket(sequence);
		PacketCryptoHelper::EncodePacket(packet, sequence, PACKET_DIRECTION::SERVER_TO_CLIENT,
			salt.data(), salt.size(), key.Get(), true);
		AdvanceToPacketBody(packet);
		EXPECT_TRUE(PacketCryptoHelper::DecodePacket(packet, salt.data(), salt.size(), key.Get(), true,
			PACKET_DIRECTION::SERVER_TO_CLIENT));
	}
}

TEST_F(PacketCryptoTest, TamperedTagIsRejected)
{
	auto packet = MakeDataPacket(7, "payload");
	PacketCryptoHelper::EncodePacket(packet, 7, PACKET_DIRECTION::CLIENT_TO_SERVER,
		salt.data(), salt.size(), key.Get(), false);
	packet.GetReadBufferPtr()[packet.GetUseSize() - 1] ^= 0x01;
	AdvanceToPacketBody(packet);
	EXPECT_FALSE(PacketCryptoHelper::DecodePacket(packet, salt.data(), salt.size(), key.Get(), false,
		PACKET_DIRECTION::CLIENT_TO_SERVER));
}

TEST_F(PacketCryptoTest, WrongDirectionAndSaltAreRejected)
{
	for (const bool useWrongDirection : { false, true })
	{
		auto packet = MakeDataPacket(9, "payload");
		PacketCryptoHelper::EncodePacket(packet, 9, PACKET_DIRECTION::CLIENT_TO_SERVER,
			salt.data(), salt.size(), key.Get(), false);
		AdvanceToPacketBody(packet);

		auto testSalt = salt;
		if (not useWrongDirection)
		{
			testSalt[0] ^= 0x01;
		}
		EXPECT_FALSE(PacketCryptoHelper::DecodePacket(packet, testSalt.data(), testSalt.size(), key.Get(), false,
			useWrongDirection ? PACKET_DIRECTION::SERVER_TO_CLIENT : PACKET_DIRECTION::CLIENT_TO_SERVER));
	}
}

TEST(CryptoHelperTest, FillNonceSeparatesDirectionAndUsesBigEndianSequence)
{
	const auto salt = MakeSalt();
	std::array<unsigned char, NONCE_SIZE> first{};
	std::array<unsigned char, NONCE_SIZE> second{};
	constexpr PacketSequence sequence = 0x0102030405060708ULL;

	ASSERT_TRUE(CryptoHelper::FillNonce(salt.data(), salt.size(), sequence,
		PACKET_DIRECTION::CLIENT_TO_SERVER, first.data(), first.size()));
	ASSERT_TRUE(CryptoHelper::FillNonce(salt.data(), salt.size(), sequence,
		PACKET_DIRECTION::SERVER_TO_CLIENT, second.data(), second.size()));
	EXPECT_NE(first, second);
	EXPECT_EQ((std::array<unsigned char, 8>{ first[4], first[5], first[6], first[7], first[8], first[9], first[10], first[11] }),
		(std::array<unsigned char, 8>{ 1, 2, 3, 4, 5, 6, 7, 8 }));
}

TEST(CryptoHelperTest, FillNonceRejectsInvalidArguments)
{
	const auto salt = MakeSalt();
	std::array<unsigned char, NONCE_SIZE> nonce{};
	EXPECT_FALSE(CryptoHelper::FillNonce(nullptr, salt.size(), 0, PACKET_DIRECTION::CLIENT_TO_SERVER, nonce.data(), nonce.size()));
	EXPECT_FALSE(CryptoHelper::FillNonce(salt.data(), salt.size() - 1, 0, PACKET_DIRECTION::CLIENT_TO_SERVER, nonce.data(), nonce.size()));
	EXPECT_FALSE(CryptoHelper::FillNonce(salt.data(), salt.size(), 0, PACKET_DIRECTION::CLIENT_TO_SERVER, nullptr, nonce.size()));
	EXPECT_FALSE(CryptoHelper::FillNonce(salt.data(), salt.size(), 0, PACKET_DIRECTION::CLIENT_TO_SERVER, nonce.data(), nonce.size() - 1));
}

// ------------------------------------------------------------
// C++과 C#이 공유하는 8개 골든 벡터의 암호화 와이어 형식과 복호화 결과가 일치하는지 확인합니다.
// ------------------------------------------------------------
TEST_F(PacketCryptoTest, AesGcmMatchesCppCSharpGoldenVectors)
{
	const auto testVectors = LoadProtocolInteropVectors();
	ASSERT_EQ(testVectors.size(), 8u);

	for (const auto& testVector : testVectors)
	{
		SCOPED_TRACE(testVector.name);
		ASSERT_EQ(testVector.isCorePacket, not testVector.packetId.has_value());
		ASSERT_FALSE(testVector.encodedPacket.empty());
		ASSERT_EQ(testVector.headerCode, testVector.encodedPacket.front());
		SymmetricKey vectorKey(testVector.key);
		NetBuffer packet;
		packet.Init();
		// NetBuffer reserves five header bytes but does not clear the two legacy
		// random/checksum slots. Initialize the complete AAD explicitly so both
		// implementations encrypt the same deterministic wire representation.
		std::fill_n(packet.GetReadBufferPtr() - df_HEADER_SIZE, df_HEADER_SIZE, 0);
		packet << testVector.packetType << testVector.sequence;
		if (not testVector.isCorePacket)
		{
			ASSERT_TRUE(testVector.packetId.has_value());
			packet << *testVector.packetId;
		}
		if (not testVector.plaintext.empty())
		{
			packet.WriteBuffer(testVector.plaintext.data(), static_cast<int>(testVector.plaintext.size()));
		}

		PacketCryptoHelper::EncodePacket(packet, testVector.sequence, testVector.direction,
			testVector.salt.data(), testVector.salt.size(), vectorKey.Get(), testVector.isCorePacket);

		ASSERT_EQ(packet.GetUseSize(), testVector.encodedPacket.size());
		EXPECT_TRUE(std::equal(testVector.encodedPacket.begin(), testVector.encodedPacket.end(), packet.GetReadBufferPtr(),
			[](const unsigned char expected, const char actual)
			{
				return expected == static_cast<unsigned char>(actual);
			}));

		AdvanceToPacketBody(packet);
		ASSERT_TRUE(PacketCryptoHelper::DecodePacket(packet, testVector.salt.data(), testVector.salt.size(),
			vectorKey.Get(), testVector.isCorePacket, testVector.direction));

		PacketSequence decodedSequence{};
		packet >> decodedSequence;
		EXPECT_EQ(decodedSequence, testVector.sequence);
		if (not testVector.isCorePacket)
		{
			PacketId decodedPacketId{};
			packet >> decodedPacketId;
			EXPECT_EQ(decodedPacketId, *testVector.packetId);
		}
		std::vector<unsigned char> decodedPlaintext(testVector.plaintext.size());
		if (not decodedPlaintext.empty())
		{
			packet.ReadBuffer(reinterpret_cast<char*>(decodedPlaintext.data()), static_cast<int>(decodedPlaintext.size()));
		}
		EXPECT_EQ(decodedPlaintext, testVector.plaintext);
	}
}
