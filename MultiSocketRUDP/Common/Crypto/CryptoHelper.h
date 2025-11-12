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
		const std::vector<unsigned char>& key,
		const std::vector<unsigned char>& nonce,
		const std::vector<unsigned char>& plaintext,
		std::vector<unsigned char>& ciphertext,
		std::vector<unsigned char>& tag,
		const BCRYPT_KEY_HANDLE keyHandle
	);
	static bool DecryptAESGCM(
		const std::vector<unsigned char>& key,
		const std::vector<unsigned char>& nonce,
		const std::vector<unsigned char>& ciphertext,
		const std::vector<unsigned char>& tag,
		std::vector<unsigned char>& plaintext,
		const BCRYPT_KEY_HANDLE keyHandle
	);
	static bool EncryptAESGCM(
		const unsigned char* key,
		const size_t sessionKeySize,
		const unsigned char* nonce,
		const size_t nonceSize,
		const char* plaintext,
		const size_t plaintextSize,
		char* ciphertext,
		const size_t ciphertextBufferSize,
		const unsigned char* tag,
		const BCRYPT_KEY_HANDLE keyHandle
	);
	static bool DecryptAESGCM(
		const unsigned char* key,
		const size_t sessionKeySize,
		const unsigned char* nonce,
		const size_t nonceSize,
		const char* ciphertext,
		const size_t ciphertextSize,
		const unsigned char* tag,
		char* plaintext,
		const size_t plaintextBufferSize,
		const BCRYPT_KEY_HANDLE keyHandle
	);
	BCRYPT_KEY_HANDLE GetSymmetricKeyHandle(OUT unsigned char* keyObject, unsigned char* key) const;
	static void DestroySymmetricKeyHandle(BCRYPT_KEY_HANDLE keyHandle);

	static std::optional<std::vector<unsigned char>> GenerateSecureRandomBytes(unsigned short length);
	static std::vector<unsigned char> GenerateNonce(const std::vector<unsigned char>& sessionSalt, const PacketSequence packetSequence, const PACKET_DIRECTION direction);
	static std::vector<unsigned char> GenerateNonce(const unsigned char* sessionSalt, const size_t sessionSaltLen, const PacketSequence packetSequence, const PACKET_DIRECTION direction);

private:
	BCRYPT_ALG_HANDLE aesAlg = nullptr;
};
