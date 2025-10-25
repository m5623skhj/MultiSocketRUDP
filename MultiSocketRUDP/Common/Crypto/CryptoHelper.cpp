#include "PreCompile.h"
#include "CryptoHelper.h"
#include <stdexcept>

#pragma comment(lib, "Bcrypt.lib")

constexpr size_t AES_KEY_SIZE_128 = 16;
constexpr size_t AES_KEY_SIZE_192 = 24;
constexpr size_t AES_KEY_SIZE_256 = 32;
constexpr size_t NONCE_SIZE = 12;
constexpr size_t TAG_SIZE = 16;

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
		BCryptCloseAlgorithmProvider(aesAlg, 0);
		aesAlg = nullptr;
	}
}

bool CryptoHelper::EncryptAESGCM(
	const std::vector<char>& key,
	const std::vector<char>& nonce,
	const std::vector<char>& plaintext,
	std::vector<char>& ciphertext,
	std::vector<char>& tag,
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
	authInfo.pbNonce = reinterpret_cast<PUCHAR>(const_cast<char*>(nonce.data()));
	authInfo.cbNonce = static_cast<ULONG>(nonce.size());

	tag.resize(TAG_SIZE);
	authInfo.pbTag = reinterpret_cast<PUCHAR>(tag.data());
	authInfo.cbTag = static_cast<ULONG>(tag.size());

	ciphertext.resize(plaintext.size());
	ULONG bytesDone = 0;

	auto status = BCryptEncrypt(
		keyHandle,
		reinterpret_cast<PUCHAR>(const_cast<char*>(plaintext.data())),
		static_cast<ULONG>(plaintext.size()),
		&authInfo,
		nullptr,
		0,
		reinterpret_cast<PUCHAR>(ciphertext.data()),
		static_cast<ULONG>(ciphertext.size()),
		&bytesDone,
		0
	);

	return BCRYPT_SUCCESS(status);
}

bool CryptoHelper::DecryptAESGCM(
	const std::vector<char>& key,
	const std::vector<char>& nonce,
	const std::vector<char>& ciphertext,
	const std::vector<char>& tag,
	std::vector<char>& plaintext,
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
	authInfo.pbNonce = reinterpret_cast<PUCHAR>(const_cast<char*>(nonce.data()));
	authInfo.cbNonce = static_cast<ULONG>(nonce.size());
	authInfo.pbTag = reinterpret_cast<PUCHAR>(const_cast<char*>(tag.data()));
	authInfo.cbTag = static_cast<ULONG>(tag.size());

	plaintext.resize(ciphertext.size());
	ULONG bytesDone = 0;

	auto status = BCryptDecrypt(
		keyHandle,
		reinterpret_cast<PUCHAR>(const_cast<char*>(ciphertext.data())),
		static_cast<ULONG>(ciphertext.size()),
		&authInfo,
		nullptr,
		0,
		reinterpret_cast<PUCHAR>(plaintext.data()),
		static_cast<ULONG>(plaintext.size()),
		&bytesDone,
		0
	);

	return BCRYPT_SUCCESS(status);
}

BCRYPT_KEY_HANDLE CryptoHelper::GetSymmetricKeyHandle(const std::vector<char>& key)
{
	BCRYPT_KEY_HANDLE keyHandle = nullptr;
	NTSTATUS status = BCryptGenerateSymmetricKey(
		aesAlg,
		&keyHandle,
		nullptr,
		0,
		reinterpret_cast<PUCHAR>(const_cast<char*>(key.data())),
		static_cast<ULONG>(key.size()),
		0
	);

	return BCRYPT_SUCCESS(status) ? keyHandle : nullptr;
}

void CryptoHelper::DestroySymmetricKeyHandle(BCRYPT_KEY_HANDLE keyHandle)
{
	if (keyHandle == nullptr)
	{
		return;
	}

	BCryptDestroyKey(keyHandle);
}

std::vector<char> CryptoHelper::GenerateNonce(const std::vector<char>& sessionSalt, PacketSequence packetSequence)
{
	if (sessionSalt.size() != 8)
	{
		return {};
	}

	std::vector<char> nonce;
	nonce.resize(NONCE_SIZE);
	memcpy(nonce.data(), sessionSalt.data(), 8);
	nonce[8] = (packetSequence >> 24) & 0xFF;
	nonce[9] = (packetSequence >> 16) & 0xFF;
	nonce[10] = (packetSequence >> 8) & 0xFF;
	nonce[11] = packetSequence & 0xFF;

	return nonce;
}