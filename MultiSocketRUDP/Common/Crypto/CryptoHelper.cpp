#include "PreCompile.h"
#include "CryptoHelper.h"
#include <stdexcept>

#pragma comment(lib, "Bcrypt.lib")

constexpr size_t NONCE_SIZE = 12;

CryptoHelper::CryptoHelper()
{
	NTSTATUS status = BCryptOpenAlgorithmProvider(&aesAlg, BCRYPT_AES_ALGORITHM, nullptr, 0);
	if (not BCRYPT_SUCCESS(status))
	{
		throw std::runtime_error("BCryptOpenAlgorithmProvider(AES) failed");
	}

	status = BCryptSetProperty(
		aesAlg,
		BCRYPT_CHAINING_MODE,
		(PUCHAR)BCRYPT_CHAIN_MODE_GCM,
		sizeof(BCRYPT_CHAIN_MODE_GCM),
		0
	);

	if (not BCRYPT_SUCCESS(status))
	{
		throw std::runtime_error("BCryptSetProperty(GCM) failed");
	}

	ULONG cbResult = 0;
	status = BCryptGetProperty(
		aesAlg,
		BCRYPT_OBJECT_LENGTH,
		reinterpret_cast<PUCHAR>(&keyObjectSize),
		sizeof(ULONG),
		&cbResult,
		0
	);

	if (not BCRYPT_SUCCESS(status))
	{
		throw std::runtime_error("BCryptGetProperty(OBJECT_LENGTH) failed");
	}
}

CryptoHelper::~CryptoHelper()
{
	if (aesAlg != nullptr)
	{
		std::ignore = BCryptCloseAlgorithmProvider(aesAlg, 0);
		aesAlg = nullptr;
	}
}

bool CryptoHelper::EncryptAESGCM(
	const unsigned char* nonce,
	const size_t nonceSize,
	const unsigned char* aad,
	size_t aadSize,
	const char* plaintext,
	const size_t plaintextSize,
	char* ciphertext,
	const size_t ciphertextBufferSize,
	unsigned char* tag,
	const BCRYPT_KEY_HANDLE keyHandle
)
{
	if (keyHandle == nullptr)
	{
		return false;
	}

	if (nonceSize != NONCE_SIZE)
	{
		return false;
	}

	if (plaintext == nullptr || ciphertext == nullptr)
	{
		return false;
	}

	if (ciphertextBufferSize < plaintextSize)
	{
		return false;
	}

	BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO authInfo;
	BCRYPT_INIT_AUTH_MODE_INFO(authInfo)
	authInfo.pbNonce = const_cast<unsigned char*>(nonce);
	authInfo.cbNonce = static_cast<ULONG>(nonceSize);
	authInfo.pbAuthData = const_cast<unsigned char*>(aad);
	authInfo.cbAuthData = static_cast<ULONG>(aadSize);
	authInfo.pbTag = tag;
	authInfo.cbTag = static_cast<ULONG>(AUTH_TAG_SIZE);

	ULONG bytesDone = 0;
	const auto status = BCryptEncrypt(
		keyHandle,
		reinterpret_cast<PUCHAR>(const_cast<char*>(plaintext)),
		static_cast<ULONG>(plaintextSize),
		&authInfo,
		nullptr,
		0,
		reinterpret_cast<PUCHAR>(ciphertext),
		static_cast<ULONG>(ciphertextBufferSize),
		&bytesDone,
		0
	);

	return BCRYPT_SUCCESS(status);
}

bool CryptoHelper::DecryptAESGCM(
	const unsigned char* nonce,
	const size_t nonceSize,
	const unsigned char* aad,
	size_t aadSize,
	const char* ciphertext,
	const size_t ciphertextSize,
	const unsigned char* tag,
	char* plaintext,
	const size_t plaintextBufferSize,
	const BCRYPT_KEY_HANDLE keyHandle
)
{
	if (keyHandle == nullptr)
	{
		return false;
	}

	if (nonceSize != NONCE_SIZE)
	{
		return false;
	}

	if (ciphertext == nullptr || plaintext == nullptr)
	{
		return false;
	}

	if (plaintextBufferSize < ciphertextSize)
	{
		return false;
	}

	BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO authInfo;
	BCRYPT_INIT_AUTH_MODE_INFO(authInfo)
	authInfo.pbNonce = const_cast<unsigned char*>(nonce);
	authInfo.cbNonce = static_cast<ULONG>(nonceSize);
	authInfo.pbAuthData = const_cast<unsigned char*>(aad);
	authInfo.cbAuthData = static_cast<ULONG>(aadSize);
	authInfo.pbTag = const_cast<unsigned char*>(tag);
	authInfo.cbTag = AUTH_TAG_SIZE;

	ULONG bytesDone = 0;
	const auto status = BCryptDecrypt(
		keyHandle,
		reinterpret_cast<PUCHAR>(const_cast<char*>(ciphertext)),
		static_cast<ULONG>(ciphertextSize),
		&authInfo,
		nullptr,
		0,
		reinterpret_cast<PUCHAR>(plaintext),
		static_cast<ULONG>(plaintextBufferSize),
		&bytesDone,
		0
	);

	return BCRYPT_SUCCESS(status);
}

BCRYPT_KEY_HANDLE CryptoHelper::GetSymmetricKeyHandle(OUT unsigned char* keyObject, unsigned char* sessionKey) const
{
	if (keyObjectSize == 0)
	{
		return nullptr;
	}

	BCRYPT_KEY_HANDLE keyHandle = nullptr;
	const NTSTATUS status = BCryptGenerateSymmetricKey(
		aesAlg,
		&keyHandle,
		keyObject,
		keyObjectSize,
		sessionKey,
		SESSION_KEY_SIZE,
		0
	);

	return BCRYPT_SUCCESS(status) ? keyHandle : nullptr;
}

void CryptoHelper::DestroySymmetricKeyHandle(const BCRYPT_KEY_HANDLE keyHandle)
{
	if (keyHandle == nullptr)
	{
		return;
	}

	std::ignore = BCryptDestroyKey(keyHandle);
}

std::optional<std::vector<unsigned char>> CryptoHelper::GenerateSecureRandomBytes(const unsigned short length)
{
	std::vector<unsigned char> bytes;
	bytes.resize(length);

	const auto status = BCryptGenRandom(
		nullptr,
		bytes.data(),
		length,
		BCRYPT_USE_SYSTEM_PREFERRED_RNG
	);

	if (not BCRYPT_SUCCESS(status))
	{
		return std::nullopt;
	}

	return bytes;
}

std::vector<unsigned char> CryptoHelper::GenerateNonce(const unsigned char* sessionSalt, const size_t sessionSaltSize, const PacketSequence packetSequence, const PACKET_DIRECTION direction)
{
	if (sessionSalt == nullptr || sessionSaltSize != SESSION_SALT_SIZE)
	{
		return {};
	}

	std::vector<unsigned char> nonce;
	nonce.resize(NONCE_SIZE);

	const unsigned char directionBits = static_cast<unsigned char>(direction) << 6;
	nonce[0] = directionBits | (sessionSalt[0] & 0x3F);
	nonce[1] = sessionSalt[1];
	nonce[2] = sessionSalt[2];
	nonce[3] = sessionSalt[3];

	nonce[4] = (packetSequence >> 56) & 0xFF;
	nonce[5] = (packetSequence >> 48) & 0xFF;
	nonce[6] = (packetSequence >> 40) & 0xFF;
	nonce[7] = (packetSequence >> 32) & 0xFF;
	nonce[8] = (packetSequence >> 24) & 0xFF;
	nonce[9] = (packetSequence >> 16) & 0xFF;
	nonce[10] = (packetSequence >> 8) & 0xFF;
	nonce[11] = packetSequence & 0xFF;

	return nonce;
}