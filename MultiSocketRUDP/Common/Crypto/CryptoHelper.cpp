#include "PreCompile.h"
#include "CryptoHelper.h"
#include <stdexcept>

#pragma comment(lib, "Bcrypt.lib")

constexpr size_t AES_KEY_SIZE = 32;
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

    status = BCryptOpenAlgorithmProvider(&hmacAlg, BCRYPT_SHA256_ALGORITHM, nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG);
    if (not BCRYPT_SUCCESS(status))
    {
        throw std::runtime_error("BCryptOpenAlgorithmProvider(HMAC) failed");
    }

    DWORD length = 0;
    status = BCryptGetProperty(
        hmacAlg,
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
    if (aesAlg != nullptr)
    {
        BCryptCloseAlgorithmProvider(aesAlg, 0);
        aesAlg = nullptr;
    }

    if (hmacAlg != nullptr)
    {
        BCryptCloseAlgorithmProvider(hmacAlg, 0);
        hmacAlg = nullptr;
    }

    hashObjectLength = 0;
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

bool CryptoHelper::EncryptAESGCM(
    const std::vector<char>& key,
    const std::vector<char>& nonce,
    const std::vector<char>& plaintext,
    std::vector<char>& ciphertext,
    std::vector<char>& tag
)
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

    if (not BCRYPT_SUCCESS(status) || keyHandle == nullptr)
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

    status = BCryptEncrypt(
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

    BCryptDestroyKey(keyHandle);
    return BCRYPT_SUCCESS(status);
}

bool CryptoHelper::DecryptAESGCM(
    const std::vector<char>& key,
    const std::vector<char>& nonce,
    const std::vector<char>& ciphertext,
    const std::vector<char>& tag,
    std::vector<char>& plaintext
)
{
    BCRYPT_KEY_HANDLE hKey = nullptr;
    NTSTATUS status = BCryptGenerateSymmetricKey(
        aesAlg,
        &hKey,
        nullptr,
        0,
        reinterpret_cast<PUCHAR>(const_cast<char*>(key.data())),
        static_cast<ULONG>(key.size()),
        0
    );

    if (not BCRYPT_SUCCESS(status))
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

    status = BCryptDecrypt(
        hKey,
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

    BCryptDestroyKey(hKey);
    return BCRYPT_SUCCESS(status);
}
