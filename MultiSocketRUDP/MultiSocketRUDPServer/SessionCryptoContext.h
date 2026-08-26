#pragma once
#include <bcrypt.h>
#include "../Common/etc/CoreType.h"

class SessionCryptoContext
{
public:
	SessionCryptoContext() = default;
	~SessionCryptoContext();

	SessionCryptoContext(const SessionCryptoContext&) = delete;
	SessionCryptoContext(SessionCryptoContext&&) = delete;
	SessionCryptoContext& operator=(const SessionCryptoContext&) = delete;
	SessionCryptoContext& operator=(SessionCryptoContext&&) = delete;

public:
	void Initialize();

	[[nodiscard]]
	// ----------------------------------------
	// @brief 세션의 암호화 키를 반환합니다.
	// @return 세션 키 버퍼에 대한 포인터
	// ----------------------------------------
	const unsigned char* GetSessionKey() const;
	void SetSessionKey(const unsigned char* inSessionKey);

	[[nodiscard]]
	// ----------------------------------------
	// @brief 세션의 암호화 솔트를 반환합니다.
	// @return 세션 솔트 버퍼에 대한 포인터
	// ----------------------------------------
	const unsigned char* GetSessionSalt() const;
	void SetSessionSalt(const unsigned char* inSessionSalt);

	[[nodiscard]]
	// ----------------------------------------
	// @brief 세션의 암호화 키 핸들을 반환합니다.
	// @return 세션 키 핸들
	// ----------------------------------------
	const BCRYPT_KEY_HANDLE& GetSessionKeyHandle() const;
	void SetSessionKeyHandle(const BCRYPT_KEY_HANDLE& inKeyHandle);

	[[nodiscard]]
	// ----------------------------------------
	// @brief 키 오브젝트 버퍼를 반환합니다.
	// @return 키 오브젝트 버퍼에 대한 포인터
	// ----------------------------------------
	unsigned char* GetKeyObjectBuffer() const;
	void SetKeyObjectBuffer(unsigned char* inKeyObjectBuffer);

	// ----------------------------------------
	// @brief 세션의 암호화 컨텍스트를 해제합니다.
	// ----------------------------------------
	void Release();

private:
	// TLS 세션 브로커에서 생성한 대칭키와 nonce 솔트를 세션 수명 동안 보관합니다.
	// keyObjectBuffer는 BCrypt 키 핸들의 backing storage이며 Release가 핸들과 함께 해제합니다.
	unsigned char sessionKey[SESSION_KEY_SIZE]{};
	unsigned char sessionSalt[SESSION_SALT_SIZE]{};
	unsigned char* keyObjectBuffer{};
	BCRYPT_KEY_HANDLE sessionKeyHandle{};
};
