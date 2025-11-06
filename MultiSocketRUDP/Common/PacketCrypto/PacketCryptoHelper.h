#pragma once
#include "NetServerSerializeBuffer.h"
#include "../Crypto/CryptoHelper.h"

class PacketCryptoHelper
{
public:
	static void EncodePacket(OUT NetBuffer& packet, const PacketSequence packetSequence, const PACKET_DIRECTION direction, const std::vector<unsigned char>& sessionKey, const std::vector<unsigned char>& sessionSalt, const BCRYPT_KEY_HANDLE& sessionKeyHandle)
	{
		constexpr unsigned int bodyOffset = df_HEADER_SIZE + sizeof(PACKET_TYPE) + sizeof(PacketSequence) + sizeof(PacketId);
		constexpr unsigned int bodyOffsetWithNotHeader = sizeof(PACKET_TYPE) + sizeof(PacketSequence) + sizeof(PacketId);
		if (packet.m_bIsEncoded == true)
		{
			return;
		}

		std::vector<unsigned char> nonce = CryptoHelper::GenerateNonce(sessionSalt, packetSequence, direction);
		std::vector<unsigned char> authTag;

		CryptoHelper::EncryptAESGCM(
			sessionKey,
			nonce,
			&packet.m_pSerializeBuffer[bodyOffset],
			packet.GetUseSize() - bodyOffsetWithNotHeader,
			&packet.m_pSerializeBuffer[bodyOffset],
			packet.GetAllUseSize(),
			authTag,
			sessionKeyHandle
		);
		packet.WriteBuffer(reinterpret_cast<char*>(authTag.data()), static_cast<int>(authTag.size()));

		packet.m_bIsEncoded = true;
	}
};
