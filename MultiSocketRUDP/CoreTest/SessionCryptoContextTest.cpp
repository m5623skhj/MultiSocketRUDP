#include "PreCompile.h"
#include <gtest/gtest.h>

#include "../MultiSocketRUDPServer/SessionCryptoContext.h"

class SessionCryptoContextTest : public ::testing::Test
{
protected:
	SessionCryptoContext context;
};

TEST_F(SessionCryptoContextTest, Initialize_ZeroesKeyAndSalt)
{
	unsigned char key[SESSION_KEY_SIZE];
	unsigned char salt[SESSION_SALT_SIZE];

	std::fill_n(key, SESSION_KEY_SIZE, 0xAB);
	std::fill_n(salt, SESSION_SALT_SIZE, 0xCD);

	context.SetSessionKey(key);
	context.SetSessionSalt(salt);

	context.Initialize();

	for (int i = 0; i < SESSION_KEY_SIZE; ++i)
	{
		EXPECT_EQ(context.GetSessionKey()[i], 0);
	}

	for (int i = 0; i < SESSION_SALT_SIZE; ++i)
	{
		EXPECT_EQ(context.GetSessionSalt()[i], 0);
	}
}

TEST_F(SessionCryptoContextTest, SetSessionKey_CopiesInputBytes)
{
	unsigned char key[SESSION_KEY_SIZE];
	for (int i = 0; i < SESSION_KEY_SIZE; ++i)
	{
		key[i] = static_cast<unsigned char>(i + 1);
	}

	context.SetSessionKey(key);

	EXPECT_EQ(std::memcmp(context.GetSessionKey(), key, SESSION_KEY_SIZE), 0);
}

TEST_F(SessionCryptoContextTest, SetSessionSalt_CopiesInputBytes)
{
	unsigned char salt[SESSION_SALT_SIZE];
	for (int i = 0; i < SESSION_SALT_SIZE; ++i)
	{
		salt[i] = static_cast<unsigned char>(0xF0 + i);
	}

	context.SetSessionSalt(salt);

	EXPECT_EQ(std::memcmp(context.GetSessionSalt(), salt, SESSION_SALT_SIZE), 0);
}

TEST_F(SessionCryptoContextTest, Setters_UpdatePointersAndHandles)
{
	auto* keyObjectBuffer = new unsigned char[64];
	const auto keyHandle = reinterpret_cast<BCRYPT_KEY_HANDLE>(1);

	context.SetKeyObjectBuffer(keyObjectBuffer);
	context.SetSessionKeyHandle(keyHandle);

	EXPECT_EQ(context.GetKeyObjectBuffer(), keyObjectBuffer);
	EXPECT_EQ(context.GetSessionKeyHandle(), keyHandle);

	context.SetSessionKeyHandle(nullptr);
	context.SetKeyObjectBuffer(nullptr);
	delete[] keyObjectBuffer;
}

TEST_F(SessionCryptoContextTest, Release_ClearsOwnedResources)
{
	auto* keyObjectBuffer = new unsigned char[32];

	context.SetKeyObjectBuffer(keyObjectBuffer);
	context.SetSessionKeyHandle(nullptr);

	context.Release();

	EXPECT_EQ(context.GetKeyObjectBuffer(), nullptr);
	EXPECT_EQ(context.GetSessionKeyHandle(), nullptr);
}

TEST_F(SessionCryptoContextTest, Initialize_AfterRelease_RemainsSafe)
{
	auto* keyObjectBuffer = new unsigned char[16];
	context.SetKeyObjectBuffer(keyObjectBuffer);

	context.Release();

	EXPECT_NO_FATAL_FAILURE(context.Initialize());
	EXPECT_EQ(context.GetKeyObjectBuffer(), nullptr);
	EXPECT_EQ(context.GetSessionKeyHandle(), nullptr);
}
