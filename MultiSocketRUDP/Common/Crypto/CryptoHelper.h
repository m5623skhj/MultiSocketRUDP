#pragma once
#include <vector>
#include <cstdint>
#include <optional>
#include <bcrypt.h>
#include <Windows.h>
#include <string>
#include "../MultiSocketRUDPServer/CoreType.h"

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
	bool EncryptAESGCM(
		const std::vector<char>& key,
		const std::vector<char>& nonce,
		const std::vector<char>& plaintext,
		std::vector<char>& ciphertext,
		std::vector<char>& tag,
		const BCRYPT_KEY_HANDLE keyHandle
	);
	bool DecryptAESGCM(
		const std::vector<char>& key,
		const std::vector<char>& nonce,
		const std::vector<char>& ciphertext,
		const std::vector<char>& tag,
		std::vector<char>& plaintext,
		const BCRYPT_KEY_HANDLE keyHandle
	);
	BCRYPT_KEY_HANDLE GetSymmetricKeyHandle(const std::vector<char>& key);
	void DestroySymmetricKeyHandle(BCRYPT_KEY_HANDLE keyHandle);

	static std::optional<std::string> GenerateSecureRandomBytes(unsigned short length);
	static std::vector<char> GenerateNonce(const std::vector<char>& sessionSalt, PacketSequence packetSequence);

private:
	BCRYPT_ALG_HANDLE aesAlg = nullptr;
};
