#pragma once
#include "NetServerSerializeBuffer.h"
#include "../Crypto/CryptoHelper.h"

class PacketCryptoHelper
{
public:
	static void EncodePacket(OUT NetBuffer& packet, const PacketSequence packetSequence, const PACKET_DIRECTION direction, const std::vector<unsigned char>& sessionSalt, const BCRYPT_KEY_HANDLE& sessionKeyHandle, const bool isCorePacket)
	{
		EncodePacket(
			packet,
			packetSequence,
			direction,
			sessionSalt.data(),
			sessionSalt.size(),
			sessionKeyHandle,
			isCorePacket
		);
	}

	static void EncodePacket(OUT NetBuffer& packet, const PacketSequence packetSequence, const PACKET_DIRECTION direction, const unsigned char* sessionSalt, const size_t sessionSaltSize, const BCRYPT_KEY_HANDLE& sessionKeyHandle, const bool isCorePacket)
	{
		if (packet.m_bIsEncoded == true)
		{
			return;
		}

		std::vector<unsigned char> nonce = CryptoHelper::GenerateNonce(sessionSalt, sessionSaltSize, packetSequence, direction);
		unsigned char authTag[AUTH_TAG_SIZE];
		const int bodyOffset = isCorePacket ? bodyOffsetWithNotHeaderForCorePacket : bodyOffsetWithNotHeader;
		const int bodySize = packet.GetUseSize() - bodyOffset;

		CryptoHelper::EncryptAESGCM(
			nonce.data(),
			nonce.size(),
			&packet.m_pSerializeBuffer[bodyOffset],
			bodySize,
			&packet.m_pSerializeBuffer[bodyOffset],
			bodySize,
			authTag,
			sessionKeyHandle
		);

		packet.WriteBuffer(reinterpret_cast<char*>(authTag), AUTH_TAG_SIZE);
		SetHeader(packet);
		packet.m_bIsEncoded = true;
	}

	static bool DecodePacket(OUT NetBuffer& packet, const std::vector<unsigned char>& sessionSalt, const BCRYPT_KEY_HANDLE& sessionKeyHandle, const bool isCorePacket)
	{
		return DecodePacket(
			packet,
			sessionSalt.data(),
			sessionSalt.size(),
			sessionKeyHandle,
			isCorePacket
		);
	}

	static bool DecodePacket(OUT NetBuffer& packet, const unsigned char* sessionSalt, const size_t sessionSaltSize, const BCRYPT_KEY_HANDLE& sessionKeyHandle, const bool isCorePacket)
	{
        // Since the packet type has already been extracted earlier, it is not extracted here
		constexpr int minimumPacketSize = sizeof(PacketSequence) + sizeof(PacketId) + AUTH_TAG_SIZE;
		constexpr int minimumCorePacketSize = sizeof(PacketSequence) + AUTH_TAG_SIZE;
		constexpr int packetSequenceOffset = df_HEADER_SIZE + sizeof(PACKET_TYPE);
		constexpr int sizeOfHeaderWithPacketType = df_HEADER_SIZE + sizeof(PACKET_TYPE);

		const int packetUseSize = packet.GetUseSize();
		const int minimumRecvPacketSize = isCorePacket ? minimumCorePacketSize : minimumPacketSize;
		if (packetUseSize < minimumRecvPacketSize)
		{
			return false;
		}

		uint8_t packetDirection = packet.m_pSerializeBuffer[packetUseSize - AUTH_TAG_SIZE];

		uint64_t packetSequence = 0;
		memcpy(&packetSequence, &packet.m_pSerializeBuffer[packetSequenceOffset], sizeof(packetSequence));

		size_t authTagOffset = packet.m_iWrite - AUTH_TAG_SIZE;
		size_t bodySize = packetUseSize + sizeOfHeaderWithPacketType - AUTH_TAG_SIZE - bodyOffset;

		const unsigned char* authTag = reinterpret_cast<const unsigned char*>(&packet.m_pSerializeBuffer[authTagOffset]);

		PACKET_DIRECTION direction = DetermineDirection(packetDirection);

		std::vector<unsigned char> nonce = CryptoHelper::GenerateNonce(sessionSalt, sessionSaltSize, packetSequence, direction);
		if (nonce.empty())
		{
			return false;
		}

		return CryptoHelper::DecryptAESGCM(
			nonce.data(),
			nonce.size(),
			&packet.m_pSerializeBuffer[bodyOffset],
			bodySize,
			authTag,
			&packet.m_pSerializeBuffer[bodyOffset],
			bodySize,
			sessionKeyHandle
		);
	}

	static void SetHeader(OUT NetBuffer& netBuffer)
	{
		netBuffer.m_pSerializeBuffer[0] = NetBuffer::m_byHeaderCode;
		*(reinterpret_cast<short*>(&netBuffer.m_pSerializeBuffer[1])) = static_cast<short>(netBuffer.m_iWrite - df_HEADER_SIZE);

		netBuffer.m_iRead = 0;
		netBuffer.m_iWriteLast = netBuffer.m_iWrite;
	}

private:
	static PACKET_DIRECTION DetermineDirection(uint8_t packetType)
	{
		switch (packetType)
		{
		case 0x00: return PACKET_DIRECTION::CLIENT_TO_SERVER;
		case 0x01: return PACKET_DIRECTION::CLIENT_TO_SERVER_REPLY;
		case 0x02: return PACKET_DIRECTION::SERVER_TO_CLIENT;
		case 0x03: return PACKET_DIRECTION::SERVER_TO_CLIENT_REPLY;
		default:   return PACKET_DIRECTION::INVALID;
		}
	}

private:
	static const unsigned int bodyOffset = df_HEADER_SIZE + sizeof(PACKET_TYPE) + sizeof(PacketSequence) + sizeof(PacketId);
	static const unsigned int bodyOffsetWithNotHeader = sizeof(PACKET_TYPE) + sizeof(PacketSequence) + sizeof(PacketId);
	static const unsigned int bodyOffsetWithNotHeaderForCorePacket = sizeof(PACKET_TYPE) + sizeof(PacketSequence);
};
