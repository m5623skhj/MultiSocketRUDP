#pragma once
#include "NetServerSerializeBuffer.h"
#include "../Crypto/CryptoHelper.h"
#include "../etc/CoreConstants.h"
#include <atomic>
#include <format>

static_assert(MAX_WIRE_PACKET_SIZE <= dfDEFAULTSIZE, "Receive copies require the initial NetBuffer allocation to fit a frame");

class PacketCryptoHelper
{
public:
    static bool FitsWireLimit(const NetBuffer& packet) noexcept
    {
        return packet.m_iWrite >= df_HEADER_SIZE && packet.m_iWrite <=
            MAX_WIRE_PACKET_SIZE - (packet.m_bIsEncoded ? 0 : static_cast<int>(AUTH_TAG_SIZE));
    }

    // Size headers are unauthenticated here. Rate-limit diagnostics and never log packet bodies.
    static void TraceRejectedReceiveSize(int declaredBytes, int receivedBytes)
    {
        static std::atomic_uint64_t rejectedCount{};
        const auto count = rejectedCount.fetch_add(1, std::memory_order_relaxed) + 1;
        if ((count & (count - 1)) == 0)
            LOG_ERROR(std::format("RUDP receive size rejected: declaredBytes={} receivedBytes={} limit={} count={}",
                declaredBytes, receivedBytes, MAX_WIRE_PACKET_SIZE, count));
    }

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

        if (not FitsWireLimit(packet))
        {
            LOG_ERROR(std::format("RUDP encode size rejected: wireBytes={} limit={}", packet.m_iWrite + AUTH_TAG_SIZE, MAX_WIRE_PACKET_SIZE));
            return;
        }
        // Reserve tag space before obtaining pointers used by AES-GCM.
        if (packet.GetFreeSize() < static_cast<int>(AUTH_TAG_SIZE))
            packet.Resize(packet.m_iWrite + static_cast<int>(AUTH_TAG_SIZE));

		unsigned char nonce[NONCE_SIZE];
		if (not CryptoHelper::FillNonce(sessionSalt, sessionSaltSize, packetSequence, direction, nonce, NONCE_SIZE))
		{
			LOG_ERROR("PacketCryptoHelper::EncodePacket() : FillNonce() failed");
			return;
		}

		unsigned char authTag[AUTH_TAG_SIZE];
		const int bodySize = packet.GetUseSize() - (isCorePacket ? bodyOffsetWithNotHeaderForCorePacket : bodyOffsetWithNotHeader);
		const int bodyOffset = isCorePacket ? bodyOffsetWithHeaderForCorePacket : bodyOffsetWithHeader;

		SetHeader(packet, AUTH_TAG_SIZE);

		constexpr size_t aadSize = df_HEADER_SIZE + sizeof(PACKET_TYPE) + sizeof(PacketSequence);
		const unsigned char* aad = reinterpret_cast<const unsigned char*>(packet.m_pSerializeBuffer);

		if (not CryptoHelper::EncryptAESGCM(
			nonce,
			NONCE_SIZE,
			aad,
			aadSize,
			&packet.m_pSerializeBuffer[bodyOffset],
			bodySize,
			&packet.m_pSerializeBuffer[bodyOffset],
			bodySize,
			authTag,
			sessionKeyHandle
		))
		{
			auto errorCode = GetLastError();
			LOG_ERROR(std::format("PacketCryptoHelper::EncodePacket() : EncryptAESGCM() failed with {} error", errorCode));
			return;
		}

		packet.WriteBuffer(reinterpret_cast<char*>(authTag), AUTH_TAG_SIZE);
		packet.m_bIsEncoded = true;
	}

	static bool DecodePacket(OUT NetBuffer& packet, const std::vector<unsigned char>& sessionSalt, const BCRYPT_KEY_HANDLE& sessionKeyHandle, const bool isCorePacket, const PACKET_DIRECTION direction)
	{
		return DecodePacket(
			packet,
			sessionSalt.data(),
			sessionSalt.size(),
			sessionKeyHandle,
			isCorePacket,
			direction
		);
	}

	static bool DecodePacket(OUT NetBuffer& packet, const unsigned char* sessionSalt, const size_t sessionSaltSize, const BCRYPT_KEY_HANDLE& sessionKeyHandle, const bool isCorePacket, const PACKET_DIRECTION direction)
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

		PacketSequence packetSequence = 0;
		memcpy(&packetSequence, &packet.m_pSerializeBuffer[packetSequenceOffset], sizeof(packetSequence));

		const int bodyOffset = isCorePacket ? (df_HEADER_SIZE + sizeof(PACKET_TYPE) + sizeof(PacketSequence)) : (df_HEADER_SIZE + sizeof(PACKET_TYPE) + sizeof(PacketSequence) + sizeof(PacketId));
		size_t authTagOffset = packet.m_iWrite - AUTH_TAG_SIZE;
		size_t bodySize = packetUseSize + sizeOfHeaderWithPacketType - AUTH_TAG_SIZE - bodyOffset;

		const unsigned char* authTag = reinterpret_cast<const unsigned char*>(&packet.m_pSerializeBuffer[authTagOffset]);

		constexpr size_t aadSize = df_HEADER_SIZE + sizeof(PACKET_TYPE) + sizeof(PacketSequence);
		const unsigned char* aad = reinterpret_cast<const unsigned char*>(packet.m_pSerializeBuffer);

		unsigned char nonce[NONCE_SIZE];
		if (not CryptoHelper::FillNonce(sessionSalt, sessionSaltSize, packetSequence, direction, nonce, NONCE_SIZE))
		{
			return false;
		}

		if (not CryptoHelper::DecryptAESGCM(
			nonce,
			NONCE_SIZE,
			aad,
			aadSize,
			&packet.m_pSerializeBuffer[bodyOffset],
			bodySize,
			authTag,
			&packet.m_pSerializeBuffer[bodyOffset],
			bodySize,
			sessionKeyHandle
		))
		{
			return false;
		}

		// Exclude the authenticated trailer from subsequent payload reads.
		packet.m_iWrite = static_cast<WORD>(authTagOffset);
		packet.m_iWriteLast = packet.m_iWrite;
		return true;
	}

	static void SetHeader(OUT NetBuffer& netBuffer, const int extraSize = 0)
	{
		netBuffer.m_pSerializeBuffer[0] = NetBuffer::m_byHeaderCode;
		*(reinterpret_cast<short*>(&netBuffer.m_pSerializeBuffer[1])) = static_cast<short>(netBuffer.m_iWrite - df_HEADER_SIZE + extraSize);

		netBuffer.m_iRead = 0;
		netBuffer.m_iWriteLast = netBuffer.m_iWrite + extraSize;
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
	static const unsigned int bodyOffsetWithHeader = df_HEADER_SIZE + sizeof(PACKET_TYPE) + sizeof(PacketSequence) + sizeof(PacketId);
	static const unsigned int bodyOffsetWithHeaderForCorePacket = df_HEADER_SIZE + sizeof(PACKET_TYPE) + sizeof(PacketSequence);
	static const unsigned int bodyOffsetWithNotHeader = sizeof(PACKET_TYPE) + sizeof(PacketSequence) + sizeof(PacketId);
	static const unsigned int bodyOffsetWithNotHeaderForCorePacket = sizeof(PACKET_TYPE) + sizeof(PacketSequence);
};
