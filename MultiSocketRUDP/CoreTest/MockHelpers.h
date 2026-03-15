#pragma once
#include "../Common/PacketCrypto/IPacketCryptoHelper.h"
#include "../MultiSocketRUDPServer/IMultiSocketRUDPCore.h"
#include <functional>

class MockPacketCryptoHelper final : public IPacketCryptoHelper
{
public:
	[[nodiscard]]
	bool DecodePacket(
		OUT NetBuffer& buffer,
		const unsigned char* _,
		size_t _2,
		const BCRYPT_KEY_HANDLE& _3,
		bool _4,
		PACKET_DIRECTION _5) override
	{
		++decodeCount;
		if (onDecode)
		{
			return onDecode(buffer);
		}

		return decodeReturn;
	}

	int encodeCount = 0;
	void EncodePacket(
		OUT NetBuffer& _,
		PacketSequence _2,
		PACKET_DIRECTION _3,
		const unsigned char* _4,
		size_t _5,
		const BCRYPT_KEY_HANDLE& _6,
		bool _7) override
	{
		++encodeCount;
	}

	void SetHeader(OUT NetBuffer& _) override
	{
		++setHeaderCount;
	}

	void ResetCounts()
	{
		decodeCount = encodeCount = setHeaderCount = 0;
	}

	bool decodeReturn = true;
	int  decodeCount = 0;
	std::function<bool(NetBuffer&)> onDecode;

	int setHeaderCount = 0;
};

class MockCore final : public ICore
{
public:
	[[nodiscard]]
	bool SendPacket(SendPacketInfo* info) override
	{
		++sendPacketCount;
		if (onSendPacket)
		{
			return onSendPacket(info, needAddRefCount);
		}

		return sendPacketReturn;
	}

	void EraseSendPacketInfo(SendPacketInfo* target, const ThreadIdType _) override
	{
		++eraseSendPacketInfoCount;
		lastErasedPacketInfo = target;
	}

	[[nodiscard]]
	RIO_EXTENSION_FUNCTION_TABLE GetRIOFunctionTable() const override
	{
		return dummyTable;
	}

	void DisconnectSession(const SessionIdType id) const override
	{
		++const_cast<MockCore*>(this)->disconnectSessionCount;
		const_cast<MockCore*>(this)->lastDisconnectedSessionId = id;
	}

	void PushToDisconnectTargetSession(RUDPSession& session) override
	{
		++pushToDisconnectCount;
		lastPushedSession = &session;
	}

	void ResetCounts()
	{
		sendPacketCount = eraseSendPacketInfoCount
			= disconnectSessionCount = pushToDisconnectCount = 0;
		lastErasedPacketInfo = nullptr;
		lastDisconnectedSessionId = INVALID_SESSION_ID;
		lastPushedSession = nullptr;
	}

	mutable RIO_EXTENSION_FUNCTION_TABLE dummyTable{};

	int pushToDisconnectCount = 0;
	RUDPSession* lastPushedSession = nullptr;

	int disconnectSessionCount = 0;
	SessionIdType lastDisconnectedSessionId = INVALID_SESSION_ID;

	int eraseSendPacketInfoCount = 0;
	SendPacketInfo* lastErasedPacketInfo = nullptr;

	bool sendPacketReturn = true;
	int  sendPacketCount = 0;
	std::function<bool(SendPacketInfo*, bool)> onSendPacket;
};
