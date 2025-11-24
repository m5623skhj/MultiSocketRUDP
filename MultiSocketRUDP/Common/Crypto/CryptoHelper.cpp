#include "PreCompile.h"
#include "CryptoHelper.h"
#include <stdexcept>

#pragma comment(lib, "Bcrypt.lib")

constexpr size_t AES_KEY_SIZE_128 = 16;
constexpr size_t AES_KEY_SIZE_192 = 24;
constexpr size_t AES_KEY_SIZE_256 = 32;
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
	const std::vector<unsigned char>& key,
	const std::vector<unsigned char>& nonce,
	const std::vector<unsigned char>& plaintext,
	std::vector<unsigned char>& ciphertext,
	std::vector<unsigned char>& tag,
	const BCRYPT_KEY_HANDLE keyHandle
)
{
	if (keyHandle == nullptr)
	{
		return false;
	}

	if (key.size() != AES_KEY_SIZE_128 &&
		key.size() != AES_KEY_SIZE_192 &&
		key.size() != AES_KEY_SIZE_256)
	{
		return false;
	}

	if (nonce.size() != NONCE_SIZE)
	{
		return false;
	}

	BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO authInfo;
	BCRYPT_INIT_AUTH_MODE_INFO(authInfo)
	authInfo.pbNonce = const_cast<unsigned char*>(nonce.data());
	authInfo.cbNonce = static_cast<ULONG>(nonce.size());

	tag.resize(AUTH_TAG_SIZE);
	authInfo.pbTag = tag.data();
	authInfo.cbTag = static_cast<ULONG>(tag.size());

	ciphertext.resize(plaintext.size());
	ULONG bytesDone = 0;

	const auto status = BCryptEncrypt(
		keyHandle,
		const_cast<unsigned char*>(plaintext.data()),
		static_cast<ULONG>(plaintext.size()),
		&authInfo,
		nullptr,
		0,
		ciphertext.data(),
		static_cast<ULONG>(ciphertext.size()),
		&bytesDone,
		0
	);

	return BCRYPT_SUCCESS(status);
}

bool CryptoHelper::DecryptAESGCM(
	const std::vector<unsigned char>& key,
	const std::vector<unsigned char>& nonce,
	const std::vector<unsigned char>& ciphertext,
	std::vector<unsigned char>& tag,
	std::vector<unsigned char>& plaintext,
	const BCRYPT_KEY_HANDLE keyHandle
)
{
	if (keyHandle == nullptr)
	{
		return false;
	}

	if (key.size() != AES_KEY_SIZE_128 &&
		key.size() != AES_KEY_SIZE_192 &&
		key.size() != AES_KEY_SIZE_256)
	{
		return false;
	}

	if (nonce.size() != NONCE_SIZE)
	{
		return false;
	}

	BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO authInfo;
	BCRYPT_INIT_AUTH_MODE_INFO(authInfo);
	authInfo.pbNonce = const_cast<unsigned char*>(nonce.data());
	authInfo.cbNonce = static_cast<ULONG>(nonce.size());
	authInfo.pbTag = tag.data();
	authInfo.cbTag = static_cast<ULONG>(tag.size());

	plaintext.resize(ciphertext.size());
	ULONG bytesDone = 0;

	const auto status = BCryptDecrypt(
		keyHandle,
		const_cast<unsigned char*>(ciphertext.data()),
		static_cast<ULONG>(ciphertext.size()),
		&authInfo,
		nullptr,
		0,
		plaintext.data(),
		static_cast<ULONG>(plaintext.size()),
		&bytesDone,
		0
	);

	return BCRYPT_SUCCESS(status);
}

bool CryptoHelper::EncryptAESGCM(
	const unsigned char* nonce,
	const size_t nonceSize,
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
	BCRYPT_INIT_AUTH_MODE_INFO(authInfo);
	authInfo.pbNonce = const_cast<unsigned char*>(nonce);
	authInfo.cbNonce = static_cast<ULONG>(nonceSize);

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
	BCRYPT_INIT_AUTH_MODE_INFO(authInfo);
	authInfo.pbNonce = const_cast<unsigned char*>(nonce);
	authInfo.cbNonce = static_cast<ULONG>(nonceSize);
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
	BCRYPT_KEY_HANDLE keyHandle = nullptr;
	const NTSTATUS status = BCryptGenerateSymmetricKey(
		aesAlg,
		&keyHandle,
		keyObject,
		KEY_OBJECT_BUFFER_SIZE,
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

std::vector<unsigned char> CryptoHelper::GenerateNonce(const std::vector<unsigned char>& sessionSalt, const PacketSequence packetSequence, const PACKET_DIRECTION direction)
{
	return GenerateNonce(sessionSalt.data(), sessionSalt.size(), packetSequence, direction);
}

std::vector<unsigned char> CryptoHelper::GenerateNonce(const unsigned char* sessionSalt, const size_t sessionSaltSize, const PacketSequence packetSequence, const PACKET_DIRECTION direction)
{
	if (sessionSalt == nullptr || sessionSaltSize != SESSION_SALT_SIZE)
	{
		return {};
	}

	std::vector<unsigned char> nonce;
	nonce.resize(NONCE_SIZE);

	memcpy(nonce.data(), sessionSalt, 8);

	nonce[8] = (packetSequence >> 24) & 0x3F;
	nonce[9] = (packetSequence >> 16) & 0xFF;
	nonce[10] = (packetSequence >> 8) & 0xFF;
	nonce[11] = packetSequence & 0xFF;

	const uint8_t directionBits = static_cast<uint8_t>(direction) << 6;
	nonce[8] |= directionBits;

	return nonce;
}