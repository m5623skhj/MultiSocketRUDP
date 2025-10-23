#include "PreCompile.h"
#include "CryptoHelper.h"
#include <stdexcept>
#include <bcrypt.h>

#pragma comment(lib, "Bcrypt.lib")

CryptoHelper::CryptoHelper()
	: algHandle(nullptr)
    , hashObjectLength(0)
{
    NTSTATUS status = BCryptOpenAlgorithmProvider(
        &algHandle,
        BCRYPT_SHA256_ALGORITHM,
        nullptr,
        BCRYPT_ALG_HANDLE_HMAC_FLAG
    );
    
    if (not BCRYPT_SUCCESS(status))
    {
        throw std::runtime_error("BCryptOpenAlgorithmProvider failed");
    }

    DWORD length = 0;
    status = BCryptGetProperty(
        algHandle,
        BCRYPT_OBJECT_LENGTH,
        reinterpret_cast<PUCHAR>(&hashObjectLength),
        sizeof(hashObjectLength),
        &length,
        0
    );
    
    if (not BCRYPT_SUCCESS(status))
    {
        throw std::runtime_error("BCryptGetProperty failed");
    }
}

CryptoHelper::~CryptoHelper()
{
	if (algHandle)
	{
		BCryptCloseAlgorithmProvider(algHandle, 0);

		hashObjectLength = 0;
		algHandle = nullptr;
	}
}

std::optional<std::vector<char>> CryptoHelper::HmacSha256WithCng(const char* key, size_t keyLength, const char* data, size_t dataLength)
{
    constexpr size_t SHA256_HASH_SIZE = 32;

    if (not algHandle)
    {
        return std::nullopt;
    }

    NTSTATUS status;
    std::vector<uint8_t> objectBuffer(hashObjectLength);

    BCRYPT_HASH_HANDLE hHash = nullptr;
    status = BCryptCreateHash(
        algHandle,
        &hHash,
        objectBuffer.data(),
        hashObjectLength,
        reinterpret_cast<PUCHAR>(const_cast<char*>(key)),
        static_cast<ULONG>(keyLength),
        0
    );

    if (not BCRYPT_SUCCESS(status))
    {
        return std::nullopt;
    }

    status = BCryptHashData(
        hHash,
        reinterpret_cast<PUCHAR>(const_cast<char*>(data)),
        static_cast<ULONG>(dataLength),
        0
    );

    if (not BCRYPT_SUCCESS(status))
    {
        BCryptDestroyHash(hHash);
        return std::nullopt;
    }

    std::vector<char> hash(SHA256_HASH_SIZE);
    status = BCryptFinishHash(hHash, reinterpret_cast<PUCHAR>(hash.data()), (ULONG)hash.size(), 0);
    BCryptDestroyHash(hHash);
    if (not BCRYPT_SUCCESS(status))
    {
        return std::nullopt;
    }

    return hash;
}

std::optional<std::vector<char>> CryptoHelper::HmacSha256WithCng(const std::vector<char>& key, const std::vector<char>& data)
{
    return HmacSha256WithCng(key.data(), key.size(), data.data(), data.size());
}