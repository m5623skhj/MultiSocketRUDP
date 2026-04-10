# CryptoHelper

> **Windows BCrypt API를 래핑한 AES-GCM 저수준 암호화 헬퍼.**  
> 스레드마다 독립 인스턴스(`thread_local`)를 유지해 락 없이 병렬 암복호화를 수행한다.

---

## 목차

1. [인스턴스 획득 (thread_local)](#1-인스턴스-획득-thread_local)
2. [생성자 — BCrypt 초기화](#2-생성자-bcrypt-초기화)
3. [키 핸들 생성/파괴](#3-키-핸들-생성파괴)
4. [난수 생성 — `GenerateSecureRandomBytes`](#4-난수-생성-generatesecurerandombytes)
5. [Nonce 생성 — `GenerateNonce`](#5-nonce-생성-generatenonce)
6. [암호화 — `EncryptAESGCM`](#6-암호화-encryptaesgcm)
7. [복호화 — `Decrypt`](#7-복호화-decrypt)
8. [BCrypt AUTHENTICATED_CIPHER_MODE_INFO 구조](#8-bcrypt-authenticated_cipher_mode_info-구조)
9. [에러 코드 대응표](#9-에러-코드-대응표)
10. [전체 사용 예시](#10-전체-사용-예시)

---

## 1. 인스턴스 획득 (thread_local)

```cpp
static CryptoHelper& CryptoHelper::GetTLSInstance()
{
    thread_local CryptoHelper instance;
    // ↑ 스레드 최초 호출 시 생성자 실행 → BCrypt 핸들 초기화
    // 이후 같은 스레드에서는 동일 인스턴스 반환
    return instance;
}
```

**thread_local을 사용하는 이유:**

BCrypt는 `BCRYPT_ALG_HANDLE`과 `BCRYPT_KEY_HANDLE`이 스레드 안전하지 않다.  
스레드마다 독립 인스턴스를 보유하면:
- `BCryptEncrypt` / `BCryptDecrypt` 호출 시 락 불필요
- IO Worker N개, RecvLogic N개가 각자 핸들을 보유 → 완전 병렬 처리

**서버에서의 스레드별 호출 경로:**

```
IO Worker Thread id=0
  → RecvIOCompleted: (없음, 복사만)

RecvLogic Worker Thread id=0
  → PacketCryptoHelper::DecodePacket()
      → CryptoHelper::GetTLSInstance()  ← 이 스레드 전용 인스턴스

Retransmission Thread id=0
  → core.SendPacket()
      → (이미 인코딩됨, DecodePacket 없음)

SessionBroker Worker Thread (4개)
  → PacketCryptoHelper::EncodePacket()  ← 세션 키 생성 등
      → CryptoHelper::GetTLSInstance()
```

---

## 2. 생성자 — BCrypt 초기화

```cpp
CryptoHelper::CryptoHelper()
{
    // ① AES 알고리즘 공급자 열기
    NTSTATUS status = BCryptOpenAlgorithmProvider(
        &aesAlg,                // OUT: 알고리즘 핸들
        BCRYPT_AES_ALGORITHM,   // L"AES"
        nullptr,                // 기본 구현 공급자 (Microsoft Primitive Provider)
        0
    );
    if (!BCRYPT_SUCCESS(status)) {
        LOG_ERROR("BCryptOpenAlgorithmProvider failed");
        return;
    }

    // ② GCM 체인 모드 설정
    status = BCryptSetProperty(
        aesAlg,
        BCRYPT_CHAINING_MODE,
        reinterpret_cast<PUCHAR>(BCRYPT_CHAIN_MODE_GCM),  // L"ChainingModeGCM"
        sizeof(BCRYPT_CHAIN_MODE_GCM),
        0
    );
    if (!BCRYPT_SUCCESS(status)) {
        LOG_ERROR("BCryptSetProperty CHAINING_MODE failed");
        return;
    }

    // ③ 키 오브젝트 크기 조회 (GetSymmetricKeyHandle에서 사용)
    ULONG resultSize = 0;
    status = BCryptGetProperty(
        aesAlg,
        BCRYPT_OBJECT_LENGTH,       // 키 핸들 내부 오브젝트 필요 크기
        reinterpret_cast<PUCHAR>(&keyObjectSize),
        sizeof(keyObjectSize),
        &resultSize,
        0
    );
    if (!BCRYPT_SUCCESS(status)) {
        throw std::runtime_error("BCryptGetProperty(OBJECT_LENGTH) failed");
    }
    // 초기화 성공 시 keyObjectSize > 0 보장
}

CryptoHelper::~CryptoHelper()
{
    if (aesAlg) {
        std::ignore = BCryptCloseAlgorithmProvider(aesAlg, 0);
        aesAlg = nullptr;
    }
}
```

**`keyObjectSize`의 의미:**  
`BCryptGenerateSymmetricKey`는 키 상태를 내부 구조체에 저장해야 한다.  
이 구조체의 크기는 알고리즘마다 다르며, `BCRYPT_OBJECT_LENGTH`로 런타임에 조회해야 한다.  
`GetSymmetricKeyHandle` 호출자가 이 크기의 버퍼를 미리 할당해 전달한다.

---

## 3. 키 핸들 생성/파괴

### `GetSymmetricKeyHandle`

```cpp
BCRYPT_KEY_HANDLE CryptoHelper::GetSymmetricKeyHandle(
    OUT unsigned char* keyObject,   // 호출자가 keyObjectSize 크기로 미리 할당
    unsigned char* sessionKey       // 16 bytes AES-128 키 (SESSION_KEY_SIZE)
) const
```

```cpp
{
    if (keyObjectSize == 0) return nullptr;  // 생성자에서 초기화 실패 시

    BCRYPT_KEY_HANDLE keyHandle = nullptr;
    NTSTATUS status = BCryptGenerateSymmetricKey(
        aesAlg,                  // 알고리즘 핸들 (GCM 모드로 설정됨)
        &keyHandle,              // OUT: 키 핸들
        keyObject,               // 키 오브젝트 버퍼 (외부 제공)
        keyObjectSize,           // 버퍼 크기
        sessionKey,              // 키 데이터 (16 bytes)
        SESSION_KEY_SIZE,        // 키 길이
        0
    );

    if (!BCRYPT_SUCCESS(status)) {
        LOG_ERROR(std::format("BCryptGenerateSymmetricKey failed: {:X}", status));
        return nullptr;
    }
    return keyHandle;
}
```

> **`keyObject` 버퍼의 소유권**: 호출자(`SessionCryptoContext`)가 소유하며,  
> `keyHandle`이 살아있는 동안 버퍼도 유효해야 한다.  
> `BCryptDestroyKey` 이후에 `delete[]` 해야 한다.

### `DestroySymmetricKeyHandle`

```cpp
static void CryptoHelper::DestroySymmetricKeyHandle(BCRYPT_KEY_HANDLE keyHandle)
{
    if (keyHandle) BCryptDestroyKey(keyHandle);
}
```

**세션 종료 시 정리 순서:**

```cpp
// SessionCryptoContext::Release()
CryptoHelper::DestroySymmetricKeyHandle(sessionKeyHandle);  // ① 핸들 먼저
sessionKeyHandle = nullptr;
delete[] keyObjectBuffer;                                    // ② 버퍼 나중에
keyObjectBuffer = nullptr;
```

---

## 4. 난수 생성 — `GenerateSecureRandomBytes`

```cpp
static std::optional<std::vector<unsigned char>>
CryptoHelper::GenerateSecureRandomBytes(unsigned short length)
{
    std::vector<unsigned char> buffer(length);
    NTSTATUS status = BCryptGenRandom(
        nullptr,                             // 알고리즘 공급자 (nullptr = OS 기본 RNG)
        buffer.data(),
        static_cast<ULONG>(length),
        BCRYPT_USE_SYSTEM_PREFERRED_RNG      // OS가 선택한 CSPRNG 사용
    );

    if (!BCRYPT_SUCCESS(status)) {
        LOG_ERROR(std::format("BCryptGenRandom failed: {:X}", status));
        return std::nullopt;
    }
    return buffer;
}
```

**`BCRYPT_USE_SYSTEM_PREFERRED_RNG`:**  
Windows Vista 이상에서 `BCryptGenRandom`은 OS가 선택한 가장 안전한 CSPRNG을 사용한다.  
Intel CPU에서는 RDRAND 명령어를 통한 하드웨어 엔트로피를 포함한다.

**`std::optional` 반환 이유:**  
예외 대신 optional로 실패를 명시. 호출자가 `if (!result) return false;`로 처리.

---

## 5. Nonce 생성 — `GenerateNonce`

```cpp
static std::vector<unsigned char> CryptoHelper::GenerateNonce(
    const unsigned char* sessionSalt,
    size_t sessionSaltSize,          // SESSION_SALT_SIZE = 16
    PacketSequence packetSequence,
    PACKET_DIRECTION direction
)
```

```cpp
{
    constexpr size_t NONCE_SIZE = 12;
    std::vector<unsigned char> nonce(NONCE_SIZE, 0);

    // Byte 0: direction 상위 2비트 + sessionSalt[0] 하위 6비트
    nonce[0] = (static_cast<unsigned char>(direction) << 6)
             | (sessionSalt[0] & 0x3F);

    // Byte 1-3: sessionSalt[1..3] 복사
    if (sessionSaltSize >= 4) {
        nonce[1] = sessionSalt[1];
        nonce[2] = sessionSalt[2];
        nonce[3] = sessionSalt[3];
    }

    // Byte 4-11: packetSequence big-endian (네트워크 바이트 순서)
    PacketSequence seq = packetSequence;
    for (int i = 11; i >= 4; --i) {
        nonce[i] = static_cast<unsigned char>(seq & 0xFF);
        seq >>= 8;
    }

    return nonce;
}
```

> **big-endian 선택 이유:** Nonce의 의미 있는 부분이 뒷부분에 있으면  
> 시퀀스가 증가할 때 마지막 바이트부터 변경되어 Nonce 유일성이 더 명확히 보인다.  
> 또한 네트워크 표준(big-endian)을 따라 디버깅이 쉽다.

---

## 6. 암호화 — `EncryptAESGCM`

```cpp
static bool CryptoHelper::EncryptAESGCM(
    const unsigned char* nonce,   size_t nonceSize,       // 12 bytes
    const unsigned char* aad,     size_t aadSize,         // 12 bytes
    const char* plaintext,        size_t plaintextSize,
    char* ciphertext,             size_t ciphertextBufferSize,
    unsigned char* tag,                                   // 출력: 16 bytes
    BCRYPT_KEY_HANDLE keyHandle
)
```

```cpp
{
    // BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO 구조체 초기화
    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO authInfo;
    BCRYPT_INIT_AUTH_MODE_INFO(authInfo);  // 버전 + 크기 설정 매크로

    authInfo.pbNonce    = const_cast<PUCHAR>(nonce);
    authInfo.cbNonce    = static_cast<ULONG>(nonceSize);
    authInfo.pbTag      = tag;
    authInfo.cbTag      = AUTH_TAG_SIZE;  // 16
    authInfo.pbAuthData = const_cast<PUCHAR>(aad);
    authInfo.cbAuthData = static_cast<ULONG>(aadSize);

    ULONG ciphertextLength = 0;
    NTSTATUS status = BCryptEncrypt(
        keyHandle,
        reinterpret_cast<PUCHAR>(const_cast<char*>(plaintext)),
        static_cast<ULONG>(plaintextSize),
        &authInfo,           // pPaddingInfo → GCM에서는 authInfo 전달
        nullptr,             // pbIV (GCM에서는 Nonce를 authInfo에서 사용)
        0,                   // cbIV
        reinterpret_cast<PUCHAR>(ciphertext),
        static_cast<ULONG>(ciphertextBufferSize),
        &ciphertextLength,
        0                    // dwFlags (패딩 없음)
    );

    return BCRYPT_SUCCESS(status);
}
```

**in-place 암호화:**  
`plaintext`와 `ciphertext`가 같은 포인터를 가리켜도 BCrypt GCM은 올바르게 동작한다.  
`PacketCryptoHelper::EncodePacket`에서 버퍼를 복사하지 않고 직접 제자리 암호화한다.

---

## 7. 복호화 — `Decrypt`

```csharp
public static bool Decrypt(
    AesGcm aesGcm,
    byte[] nonce,
    Span<byte> cipherText,
    ReadOnlySpan<byte> aad,
    ReadOnlySpan<byte> authTag)
```

`AesGcm`을 사용하여 암호문을 복호화하고 인증 태그를 검증한다.

| 파라미터 | 타입 | 설명 |
|----------|------|------|
| `aesGcm` | `AesGcm` | 복호화에 사용할 `AesGcm` 인스턴스 |
| `nonce` | `byte[]` | 암호화 시 사용된 nonce |
| `cipherText` | `Span<byte>` | 복호화할 암호문 (복호화 후 평문으로 덮어씀) |
| `aad` | `ReadOnlySpan<byte>` | 인증 추가 데이터 (AAD) |
| `authTag` | `ReadOnlySpan<byte>` | 검증할 인증 태그 (AuthTag) |

**반환값**

| 반환값 | 조건 |
|--------|------|
| `true` | 복호화 성공 및 인증 태그 검증 완료 |
| `false` | 복호화 실패 또는 인증 태그 불일치 (변조 또는 키 불일치) |

> **주의:** 복호화 과정에서 `CryptographicException`이 발생하면 `false`를 반환한다.

**인증 태그 불일치 발생 원인:**

| 원인 | 설명 |
|------|------|
| Nonce 불일치 | 암호화/복호화 시 사용된 nonce가 다름 |
| AAD 변조 | 헤더/타입/시퀀스 필드가 전송 중 변조됨 |
| 페이로드 변조 | 암호문이 변조됨 (중간자 공격) |
| AuthTag 변조 | 태그가 변조됨 |
| 세션 키 불일치 | 서버/클라이언트가 다른 키를 사용 |
## 8. BCrypt AUTHENTICATED_CIPHER_MODE_INFO 구조

```cpp
typedef struct _BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO {
    ULONG   cbSize;          // sizeof(BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO)
    ULONG   dwInfoVersion;   // BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO_VERSION (1)

    PUCHAR  pbNonce;         // Nonce 버퍼 포인터
    ULONG   cbNonce;         // Nonce 크기 (12 bytes for GCM)

    PUCHAR  pbAuthData;      // AAD 버퍼 포인터
    ULONG   cbAuthData;      // AAD 크기

    PUCHAR  pbTag;           // 암호화: 출력 버퍼 / 복호화: 입력 버퍼
    ULONG   cbTag;           // 태그 크기 (16 bytes)

    PUCHAR  pbMacContext;    // 연속 암호화 시 MAC 컨텍스트 (단일 호출 시 nullptr)
    ULONG   cbMacContext;

    ULONG   cbAAD;           // 연속 AAD 크기
    ULONGLONG cbData;        // 연속 데이터 크기
    ULONG   dwFlags;         // BCRYPT_AUTH_MODE_CHAIN_CALLS_FLAG (연속 시)
} BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO;

// 초기화 매크로
#define BCRYPT_INIT_AUTH_MODE_INFO(_AUTH_INFO_STRUCT)  \
    RtlZeroMemory(&(_AUTH_INFO_STRUCT), sizeof(BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO)); \
    (_AUTH_INFO_STRUCT).cbSize = sizeof(BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO); \
    (_AUTH_INFO_STRUCT).dwInfoVersion = BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO_VERSION;
```

---

## 9. 에러 코드 대응표

| NTSTATUS | 십육진수 | 의미 |
|----------|---------|------|
| `STATUS_SUCCESS` | `0x00000000` | 성공 |
| `STATUS_AUTH_TAG_MISMATCH` | `0xC000A002` | 인증 태그 불일치 (복호화 실패) |
| `STATUS_INVALID_PARAMETER` | `0xC000000D` | 잘못된 파라미터 (크기, nullptr 등) |
| `STATUS_NOT_SUPPORTED` | `0xC00000BB` | GCM 미지원 OS |
| `STATUS_NO_MEMORY` | `0xC0000017` | 메모리 부족 |
| `STATUS_BUFFER_TOO_SMALL` | `0xC0000023` | 출력 버퍼 크기 부족 |

**`BCRYPT_SUCCESS(status)` 매크로:**
```cpp
#define BCRYPT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
// NTSTATUS 음수(0x8xxx, 0xCxxx)이면 실패
```

---

## 10. 전체 사용 예시

```cpp
// 1회성 암복호화 예시 (콘텐츠 서버에서 직접 사용할 일은 거의 없음)
// 패킷 암복호화는 PacketCryptoHelper가 처리

auto& crypto = CryptoHelper::GetTLSInstance();

// 키 오브젝트 버퍼 준비
auto keyObjBuf = std::make_unique<unsigned char[]>(crypto.GetKeyObjectSize());

// 세션 키로 핸들 생성
auto keyBytes = CryptoHelper::GenerateSecureRandomBytes(SESSION_KEY_SIZE);
BCRYPT_KEY_HANDLE handle = crypto.GetSymmetricKeyHandle(
    keyObjBuf.get(), keyBytes->data());

if (!handle) { /* 오류 처리 */ }

// Nonce 생성
auto saltBytes = CryptoHelper::GenerateSecureRandomBytes(SESSION_SALT_SIZE);
auto nonce = CryptoHelper::GenerateNonce(
    saltBytes->data(), SESSION_SALT_SIZE,
    1,  // sequence
    PACKET_DIRECTION::SERVER_TO_CLIENT
);

// 암호화
const char plaintext[] = "Hello, RUDP!";
char ciphertext[sizeof(plaintext)];
unsigned char tag[AUTH_TAG_SIZE];
unsigned char aad[12] = {};  // 헤더 + type + sequence

bool ok = CryptoHelper::EncryptAESGCM(
    nonce.data(), nonce.size(),
    aad, sizeof(aad),
    plaintext, sizeof(plaintext),
    ciphertext, sizeof(ciphertext),
    tag,
    handle
);

// 복호화
char decrypted[sizeof(plaintext)];
bool verified = CryptoHelper::DecryptAESGCM(
    nonce.data(), nonce.size(),
    aad, sizeof(aad),
    ciphertext, sizeof(ciphertext),
    tag,
    decrypted, sizeof(decrypted),
    handle
);

// 정리
CryptoHelper::DestroySymmetricKeyHandle(handle);
// keyObjBuf는 unique_ptr이 자동 해제
```

---

## 관련 문서
- [[CryptoSystem]] — 전체 암호화 구조 및 Nonce 설계
- [[PacketCryptoHelper]] — 패킷 버퍼 수준 래퍼
- [[RUDPSessionBroker]] — GenerateSecureRandomBytes로 키/솔트 생성
- [[SessionComponents]] — SessionCryptoContext에서 키 핸들 관리
