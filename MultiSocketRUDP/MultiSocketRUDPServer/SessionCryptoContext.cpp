#include "PreCompile.h"
#include "SessionCryptoContext.h"

SessionCryptoContext::~SessionCryptoContext()
{
	Release();
}

void SessionCryptoContext::Initialize()
{
	ZeroMemory(sessionKey, SESSION_KEY_SIZE);
	ZeroMemory(sessionSalt, SESSION_SALT_SIZE);

	if (keyObjectBuffer != nullptr)
	{
		delete[] keyObjectBuffer;
		keyObjectBuffer = nullptr;
	}

	if (sessionKeyHandle != nullptr)
	{
		std::ignore = BCryptDestroyKey(sessionKeyHandle);
		sessionKeyHandle = nullptr;
	}
}

const unsigned char* SessionCryptoContext::GetSessionKey() const
{
	return sessionKey;
}

void SessionCryptoContext::SetSessionKey(const unsigned char* inSessionKey)
{
	std::copy_n(inSessionKey, SESSION_KEY_SIZE, sessionKey);
}

const unsigned char* SessionCryptoContext::GetSessionSalt() const
{
	return sessionSalt;
}

void SessionCryptoContext::SetSessionSalt(const unsigned char* inSessionSalt)
{
	std::copy_n(inSessionSalt, SESSION_SALT_SIZE, sessionSalt);
}

const BCRYPT_KEY_HANDLE& SessionCryptoContext::GetSessionKeyHandle() const
{
	return sessionKeyHandle;
}

void SessionCryptoContext::SetSessionKeyHandle(const BCRYPT_KEY_HANDLE& inKeyHandle)
{
	sessionKeyHandle = inKeyHandle;
}

unsigned char* SessionCryptoContext::GetKeyObjectBuffer() const
{
	return keyObjectBuffer;
}

void SessionCryptoContext::SetKeyObjectBuffer(unsigned char* inKeyObjectBuffer)
{
	keyObjectBuffer = inKeyObjectBuffer;
}

void SessionCryptoContext::Release()
{
	if (sessionKeyHandle != nullptr)
	{
		std::ignore = BCryptDestroyKey(sessionKeyHandle);
		sessionKeyHandle = nullptr;
	}

	if (keyObjectBuffer != nullptr)
	{
		delete[] keyObjectBuffer;
		keyObjectBuffer = nullptr;
	}
}
