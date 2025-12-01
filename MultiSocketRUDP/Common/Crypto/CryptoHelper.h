#pragma once
#include <vector>
#include <cstdint>
#include <optional>
#include <bcrypt.h>
#include <Windows.h>
#include <string>
#include "../MultiSocketRUDPServer/CoreType.h"

constexpr size_t AUTH_TAG_SIZE = 16;

class CryptoHelper
{
private:
	CryptoHelper();
	~CryptoHelper();
	CryptoHelper(const CryptoHelper&) = delete;
	CryptoHelper& operator=(const CryptoHelper&) = delete;
	CryptoHelper(CryptoHelper&&) = delete;
	CryptoHelper& operator=(CryptoHelper&&) = delete;

public:
	static CryptoHelper& GetTLSInstance()
	{
		thread_local CryptoHelper instance;
		return instance;
	}

public:
	static bool EncryptAESGCM(
		const unsigned char* nonce,
		const size_t nonceSize,
		const char* plaintext,
		const size_t plaintextSize,
		char* ciphertext,
		const size_t ciphertextBufferSize,
		unsigned char* tag,
		const BCRYPT_KEY_HANDLE keyHandle
	);
	static bool DecryptAESGCM(
		const unsigned char* nonce,
		const size_t nonceSize,
		const char* ciphertext,
		const size_t ciphertextSize,
		const unsigned char* tag,
		char* plaintext,
		const size_t plaintextBufferSize,
		const BCRYPT_KEY_HANDLE keyHandle
	);
	[[nodiscard]]
	BCRYPT_KEY_HANDLE GetSymmetricKeyHandle(OUT unsigned char* keyObject, unsigned char* sessionKey) const;
	[[nodiscard]]
	ULONG GetKeyOjbectSize() const { return keyObjectSize; }
	static void DestroySymmetricKeyHandle(BCRYPT_KEY_HANDLE keyHandle);

	static std::optional<std::vector<unsigned char>> GenerateSecureRandomBytes(unsigned short length);
	static std::vector<unsigned char> GenerateNonce(const unsigned char* sessionSalt, const size_t sessionSaltLen, const PacketSequence packetSequence, const PACKET_DIRECTION direction);

private:
	BCRYPT_ALG_HANDLE aesAlg = nullptr;
	ULONG keyObjectSize{};
};
