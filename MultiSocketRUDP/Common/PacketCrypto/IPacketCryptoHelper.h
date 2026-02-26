#pragma once
#include "NetServerSerializeBuffer.h"
#include "../Common/etc/CoreType.h"
#include "../Common/PacketCrypto/PacketCryptoHelper.h"

class IPacketCryptoHelper
{
public:
	virtual ~IPacketCryptoHelper() = default;

    [[nodiscard]]
    virtual bool DecodePacket(
		OUT NetBuffer& packet,
		const unsigned char* sessionSalt,
		size_t sessionSaltSize,
		const BCRYPT_KEY_HANDLE& sessionKeyHandle,
		bool isCorePacket,
		PACKET_DIRECTION direction) = 0;

	virtual void EncodePacket(
		OUT NetBuffer& packet,
		PacketSequence packetSequence,
		PACKET_DIRECTION direction,
		const unsigned char* sessionSalt,
		size_t sessionSaltSize,
		const BCRYPT_KEY_HANDLE& sessionKeyHandle,
		bool isCorePacket) = 0;

	virtual void SetHeader(OUT NetBuffer& netBuffer) = 0;
};

class PacketCryptoHelperAdapter final : public IPacketCryptoHelper
{
public:
    [[nodiscard]]
    bool DecodePacket(
        OUT NetBuffer& packet,
        const unsigned char* sessionSalt,
        size_t sessionSaltSize,
        const BCRYPT_KEY_HANDLE& sessionKeyHandle,
        bool isCorePacket,
        PACKET_DIRECTION direction) override
    {
        return PacketCryptoHelper::DecodePacket(
            packet,
            sessionSalt,
            sessionSaltSize,
            sessionKeyHandle,
            isCorePacket,
            direction);
    }

    void EncodePacket(
        OUT NetBuffer& packet,
        PacketSequence packetSequence,
        PACKET_DIRECTION direction,
        const unsigned char* sessionSalt,
        size_t sessionSaltSize,
        const BCRYPT_KEY_HANDLE& sessionKeyHandle,
        bool isCorePacket) override
    {
        PacketCryptoHelper::EncodePacket(
            packet,
            packetSequence,
            direction,
            sessionSalt,
            sessionSaltSize,
            sessionKeyHandle,
            isCorePacket);
    }

    void SetHeader(OUT NetBuffer& netBuffer) override
    {
        PacketCryptoHelper::SetHeader(netBuffer);
    }
};