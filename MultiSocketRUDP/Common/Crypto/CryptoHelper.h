#pragma once
#include <vector>
#include <cstdint>
#include <optional>

class CryptoHelper
{
private:
	CryptoHelper() = default;
	~CryptoHelper() = default;
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
	std::optional<std::vector<char>> HmacSha256WithCng(const char* key, size_t keyLength, const char* data, size_t dataLength);
	std::optional<std::vector<char>> HmacSha256WithCng(const std::vector<char>& key, const std::vector<char>& data);

private:
	BCRYPT_ALG_HANDLE algHandle = nullptr;
	DWORD hashObjectLength = 0;
};