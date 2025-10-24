#pragma once
#include <vector>
#include <cstdint>
#include <optional>
#include <bcrypt.h>
#include <Windows.h>
#include "../MultiSocketRUDPServer/CoreType.h"

class CryptoHelper
{
private:
	CryptoHelper();
	~CryptoHelper();
	CryptoHelper(CryptoHelper&) = delete;
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
		std::vector<char>& tag
	);

	bool DecryptAESGCM(
		const std::vector<char>& key,
		const std::vector<char>& nonce,
		const std::vector<char>& ciphertext,
		const std::vector<char>& tag,
		std::vector<char>& plaintext
	);

	static std::vector<char> GenerateNonce(const std::vector<char>& sessionSalt, PacketSequence packetSequence);

private:
	BCRYPT_ALG_HANDLE aesAlg = nullptr;
	BCRYPT_ALG_HANDLE hmacAlg = nullptr;
	DWORD hashObjectLength = 0;
};