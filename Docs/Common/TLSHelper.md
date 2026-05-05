# TLSHelper

> **Windows SChannel(Schannel SSP)을 이용한 TLS 1.2 핸드셰이크 및 스트림 암복호화 모듈.**  
> `RUDPSessionBroker`(서버)와 `RUDPClientCore`(클라이언트)가 세션 정보를 안전하게 교환하는 채널을 제공한다.  
> AES-GCM과 달리 표준 X.509 인증서를 사용하고, 스트림 방식으로 동작한다.

---

## 목차

1. [클래스 계층 구조](#1-클래스-계층-구조)
2. [서버 초기화 — TLSHelperServer::Initialize](#2-서버-초기화--tlshelperserverinitialize)
3. [클라이언트 초기화 — TLSHelperClient::Initialize](#3-클라이언트-초기화--tlshelperclientinitialize)
4. [서버 핸드셰이크 — AcceptSecurityContext](#4-서버-핸드셰이크--acceptsecuritycontext)
5. [클라이언트 핸드셰이크 — InitializeSecurityContext](#5-클라이언트-핸드셰이크--initializesecuritycontext)
6. [데이터 암호화 — EncryptData](#6-데이터-암호화--encryptdata)
7. [데이터 복호화 — DecryptDataStream](#7-데이터-복호화--decryptdatastream)
8. [close_notify 전송 — EncryptCloseNotify](#8-close_notify-전송--encryptclosenotify)
9. [TlsDecryptResult 열거형](#9-tlsdecryptresult-열거형)
10. [인증서 관리](#10-인증서-관리)
11. [개발 환경 자체 서명 인증서 설정](#11-개발-환경-자체-서명-인증서-설정)
12. [SChannel 내부 구조 메모](#12-schannel-내부-구조-메모)

---

## 1. 클래스 계층 구조

```
TLSHelperBase
 ├── credentialsHandle   (CredHandle)
 ├── securityContext     (CtxtHandle)
 ├── streamSizes         (SecPkgContext_StreamSizes)
 │    ├── cbHeader       TLS 레코드 헤더 크기 (보통 5 bytes)
 │    ├── cbTrailer      TLS 레코드 트레일러 크기 (MAC + 패딩)
 │    └── cbMaximumMessage  최대 페이로드 크기
 ├── handshakeCompleted  (bool)
 │
 ├── EncryptData(plainData, size → encryptedBuffer, outSize)
 ├── DecryptData(encryptedData, encryptedSize, outPlain, outPlainSize)
 ├── DecryptDataStream(inStream, inSize, outPlain, outPlainSize) → TlsDecryptResult
 └── EncryptCloseNotify(outBuffer, outSize)

TLSHelperServer : TLSHelperBase
 ├── certStore      (HCERTSTORE)
 ├── certContext    (PCCERT_CONTEXT)
 ├── Initialize(storeName, subjectName) → bool
 └── Handshake(socket) → bool

TLSHelperClient : TLSHelperBase
 ├── Initialize() → bool
 └── Handshake(socket) → bool
```

---

## 2. 서버 초기화 — `TLSHelperServer::Initialize`

```cpp
TLSHelperServer tlsHelperServer(
    storeName,      // 예: L"MY"
    subjectName     // 예: L"DevServerCert"
);

bool initialized = tlsHelperServer.Initialize();
```

```
Step 1. 인증서 저장소 열기
  certStore = CertOpenStore(
      CERT_STORE_PROV_SYSTEM,
      X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
      nullptr,
      CERT_SYSTEM_STORE_LOCAL_MACHINE,  // 로컬 컴퓨터 저장소
      storeName.c_str()
  )
  실패 → LOG_ERROR("CertOpenStore failed") + return false

Step 2. 인증서 검색 (Subject Name 기준)
  certContext = CertFindCertificateInStore(
      certStore,
      X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
      0,
      CERT_FIND_SUBJECT_STR,   // Subject Name 문자열 검색
      subjectName.c_str(),
      nullptr
  )
  실패 → LOG_ERROR("CertFindCertificateInStore failed: " + subjectName) + return false

Step 3. SChannel 자격 증명 설정
  SCHANNEL_CRED cred = {};
  cred.dwVersion = SCHANNEL_CRED_VERSION;
  cred.cCreds = 1;
  cred.paCred = &certContext;           ← 서버 인증서
  cred.grbitEnabledProtocols = SP_PROT_TLS1_2_SERVER;
  cred.dwFlags = SCH_CRED_NO_DEFAULT_CREDS;

  AcquireCredentialsHandle(
      nullptr, UNISP_NAME, SECPKG_CRED_INBOUND,
      nullptr, &cred, nullptr, nullptr,
      &credentialsHandle, nullptr
  )
  실패 → return false
```

### 인증서 저장소 이름 상수

```cpp
namespace TLSHelper::StoreNames {
    constexpr wchar_t* MY       = L"MY";        // 개인 인증서
    constexpr wchar_t* ROOT     = L"ROOT";      // 신뢰할 수 있는 루트 CA
    constexpr wchar_t* CA       = L"CA";
    constexpr wchar_t* AuthRoot = L"AuthRoot";  // 타사 루트 CA
}
```

**`CERT_SYSTEM_STORE_LOCAL_MACHINE` vs `CERT_SYSTEM_STORE_CURRENT_USER`:**

| 저장소 | 기본값 | 용도 |
|--------|--------|------|
| `LOCAL_MACHINE` | 현재 코드 사용 | 서비스, 공유 인증서 |
| `CURRENT_USER` | - | 사용자별 인증서 |

---

## 3. 클라이언트 초기화 — `TLSHelperClient::Initialize`

```cpp
bool TLSHelperClient::Initialize()
```

```
SCHANNEL_CRED cred = {};
cred.dwVersion = SCHANNEL_CRED_VERSION;
cred.grbitEnabledProtocols = SP_PROT_TLS1_2_CLIENT;
cred.dwFlags = SCH_CRED_MANUAL_CRED_VALIDATION;  // ← 핵심

AcquireCredentialsHandle(
    nullptr, UNISP_NAME, SECPKG_CRED_OUTBOUND,
    nullptr, &cred, nullptr, nullptr,
    &credentialsHandle, nullptr
)
```

**`SCH_CRED_MANUAL_CRED_VALIDATION` 의미:**

이 플래그가 있으면 SChannel이 서버 인증서를 자동으로 검증하지 않는다.  
개발 환경의 자체 서명 인증서가 신뢰할 수 없는 CA에서 발급된 경우에도 연결이 성공한다.

> ⚠️ **운영 환경에서는 이 플래그를 제거하고 CA 발급 인증서를 사용해야 한다.**  
> 플래그 없이 자체 서명 인증서를 사용하면 `SEC_E_UNTRUSTED_ROOT` 오류 발생.

---

## 4. 서버 핸드셰이크 — `AcceptSecurityContext` 루프

```cpp
bool TLSHelperServer::Handshake(const SOCKET& clientSocket)
```

```
상태: ACCEPT 루프
  inputBuffer:  클라이언트로부터 수신한 TLS 레코드
  outputBuffer: 서버가 전송할 TLS 레코드

while true:
  InBuf[2] = {
    {SECBUFFER_TOKEN, receivedBytes, receiveBuffer},
    {SECBUFFER_EMPTY, 0, nullptr}
  }
  OutBuf[1] = {
    {SECBUFFER_TOKEN, 0, nullptr}
  }

  status = AcceptSecurityContext(
      &credentialsHandle,
      pContext,      ← 첫 호출은 nullptr
      &InBufDesc,
      ASC_REQ_SEQUENCE_DETECT | ASC_REQ_REPLAY_DETECT |
      ASC_REQ_CONFIDENTIALITY | ASC_REQ_EXTENDED_ERROR |
      ASC_REQ_ALLOCATE_MEMORY | ASC_REQ_STREAM,
      0,
      &securityContext,
      &OutBufDesc,
      &contextAttributes,
      nullptr
  )

  if OutBuf[0].cbBuffer > 0:
    send(clientSocket, OutBuf[0].pvBuffer, OutBuf[0].cbBuffer, 0)
    FreeContextBuffer(OutBuf[0].pvBuffer)

  switch status:
    SEC_E_OK:
      QueryContextAttributes(SECPKG_ATTR_STREAM_SIZES)
      handshakeCompleted = true
      break

    SEC_I_CONTINUE_NEEDED:
      recv(clientSocket, ...) → 더 받기
      continue

    SEC_E_INCOMPLETE_MESSAGE:
      recv(clientSocket, ...) → 불완전한 레코드 → 더 받기
      continue

    else:
      LOG_ERROR("AcceptSecurityContext failed: " + hex(status))
      return false
```

**핸드셰이크 메시지 흐름:**

```
클라이언트              서버
    │── ClientHello ──────►│
    │◄─ ServerHello ───────│
    │◄─ Certificate ───────│
    │◄─ ServerHelloDone ───│
    │── ClientKeyExchange ─►│
    │── ChangeCipherSpec ──►│
    │── Finished ──────────►│
    │◄─ ChangeCipherSpec ───│
    │◄─ Finished ───────────│
    핸드셰이크 완료
```

---

## 5. 클라이언트 핸드셰이크 — `InitializeSecurityContext` 루프

```cpp
bool TLSHelperClient::Handshake(const SOCKET& serverSocket)
```

```
while true:
  status = InitializeSecurityContext(
      &credentialsHandle,
      pContext,
      nullptr,    ← 서버 이름 (nullptr = 검증 안 함)
      ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT |
      ISC_REQ_CONFIDENTIALITY | ISC_REQ_EXTENDED_ERROR |
      ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_STREAM |
      ISC_REQ_MANUAL_CRED_VALIDATION,  ← 인증서 검증 생략
      ...
      &OutBufDesc, &contextAttributes, nullptr
  )

  if OutBuf[0].cbBuffer > 0:
    send(serverSocket, ...) → 클라이언트 레코드 전송

  switch status:
    SEC_E_OK: handshakeCompleted = true; break
    SEC_I_CONTINUE_NEEDED / SEC_E_INCOMPLETE_MESSAGE: recv(...); continue
    else: return false
```

---

## 6. 데이터 암호화 — `EncryptData`

핸드셰이크 완료 후 세션 정보 전송 시 사용.

```cpp
bool TLSHelperBase::EncryptData(
    const char* plainData,
    size_t plainDataSize,
    OUT char* encryptedBuffer,
    OUT size_t& encryptedSize
)
```

```
① 암호화 버퍼 구성
  필요 크기 = cbHeader + plainDataSize + cbTrailer

② 데이터 복사
  memcpy(encryptedBuffer + cbHeader, plainData, plainDataSize)

③ SecBuffer 4개 설정
  buffers[0] = {SECBUFFER_STREAM_HEADER,  cbHeader, encryptedBuffer}
  buffers[1] = {SECBUFFER_DATA,           plainDataSize, encryptedBuffer + cbHeader}
  buffers[2] = {SECBUFFER_STREAM_TRAILER, cbTrailer, encryptedBuffer + cbHeader + plainDataSize}
  buffers[3] = {SECBUFFER_EMPTY, 0, nullptr}

④ EncryptMessage(&securityContext, 0, &bufDesc, 0)
   → SChannel이 제자리(in-place)로 암호화

⑤ encryptedSize = cbHeader + plainDataSize + cbTrailer
```

---

## 7. 데이터 복호화 — `DecryptDataStream`

스트림 방식: TCP는 레코드 경계가 보장되지 않으므로 누적 버퍼로 처리.

```cpp
TlsDecryptResult TLSHelperBase::DecryptDataStream(
    std::vector<unsigned char>& encryptedStream,
    OUT char* plainBuffer,
    OUT size_t& plainSize
)
```

```
① 최소 크기 확인
  if encryptedStream.size() < cbHeader → return NEED_MORE_DATA

② SecBuffer 4개 설정
  buffers[0] = {SECBUFFER_DATA, streamSize, streamData}
  buffers[1..3] = {SECBUFFER_EMPTY, 0, nullptr}

③ DecryptMessage(&securityContext, &bufDesc, 0, nullptr)

  switch 결과:
    SEC_E_OK:
      result buffer[1] = SECBUFFER_DATA → 복호화된 데이터
      result buffer[3] = SECBUFFER_EXTRA → 남은 데이터 (다음 레코드)
      
      memcpy(plainBuffer, buffer[1].pvBuffer, buffer[1].cbBuffer)
      plainSize = buffer[1].cbBuffer
      
      if buffer[3].BufferType == SECBUFFER_EXTRA:
        encryptedStream.erase(처음 부분)  ← 처리된 레코드 제거
      else:
        encryptedStream.clear()
      
      return OK

    SEC_E_INCOMPLETE_MESSAGE:
      return NEED_MORE_DATA

    SEC_I_CONTEXT_EXPIRED (close_notify):
      return CloseNotify

    else:
      return Error
```

**왜 SECBUFFER_EXTRA를 처리하는가:**

```
TCP recv로 받은 데이터:
  [TLS Record 1 완전] [TLS Record 2 일부]
                       ↑ SECBUFFER_EXTRA: 아직 복호화 못한 부분
  
  → 다음 recv 호출 때 이 부분을 앞에 붙여서 계속 처리
```

---

## 8. close_notify 전송 — `EncryptCloseNotify`

TLS 연결을 정상적으로 종료할 때 전송하는 Alert 레코드.

```cpp
bool TLSHelperBase::EncryptCloseNotify(
    OUT char* notifyBuffer,
    OUT size_t& notifySize
)
```

```
ApplyControlToken(&securityContext, SCHANNEL_SHUTDOWN)

SecBuffer shutdownToken = {SECBUFFER_TOKEN, 0, nullptr}
InitializeSecurityContext(
    ...,
    ISC_REQ_ALLOCATE_MEMORY, ...
)

if OutBuf[0].cbBuffer > 0:
  memcpy(notifyBuffer, OutBuf[0].pvBuffer, OutBuf[0].cbBuffer)
  notifySize = OutBuf[0].cbBuffer
  FreeContextBuffer(OutBuf[0].pvBuffer)
```

**SessionBroker 세션 종료 순서:**

```cpp
// RUDPSessionBroker::SendSessionInfoToClient
TlsHelper.EncryptData(sendBuffer → encryptedBuffer)
send(clientSocket, encryptedBuffer, ...)

TlsHelper.EncryptCloseNotify(closeNotify, closeSize)
send(clientSocket, closeNotify, closeSize, 0)

shutdown(clientSocket, SD_SEND)   ← TCP FIN 전송
recv(clientSocket, ...) loop      ← 클라이언트 FIN 대기
```

---

## 9. TlsDecryptResult 열거형

```cpp
enum class TlsDecryptResult {
    OK,              // 복호화 성공, plainBuffer에 데이터 있음
    NEED_MORE_DATA,  // TLS 레코드 불완전, recv 더 필요
    CloseNotify,     // 원격 측이 close_notify 전송 → 정상 종료
    Error            // 오류 (인증 실패, 프로토콜 오류 등)
};
```

**`RUDPClientCore::TrySetTargetSessionInfo`에서의 처리:**

```cpp
while (true) {
    int bytes = recv(socket, buffer, MAX_TLS_PACKET_SIZE, 0);
    if (bytes <= 0) break;
    encryptedStream.insert(encryptedStream.end(), buffer, buffer + bytes);

    size_t plainSize = 0;
    auto result = tlsHelper.DecryptDataStream(encryptedStream, plainBuffer, plainSize);

    if (result == TlsDecryptResult::Error) return false;
    if (result == TlsDecryptResult::NEED_MORE_DATA) continue;
    if (result == TlsDecryptResult::OK) {
        // 복호화된 페이로드 처리 (세션 정보 파싱)
        totalReceived += plainSize;
    }
    if (result == TlsDecryptResult::CloseNotify && payloadComplete) break;
}
```

---

## 10. 인증서 관리

### 운영 환경 인증서 흐름

```
1. CA에서 발급한 인증서를 PFX 파일로 받음

2. Windows 인증서 저장소에 설치
   certmgr.msc → 컴퓨터 계정 → 개인 → 인증서 → 가져오기

3. 서버 코드
   TLSHelperServer::Initialize(L"MY", L"your.domain.com")

4. 클라이언트 코드 (운영)
   SCH_CRED_MANUAL_CRED_VALIDATION 플래그 제거
   → SChannel이 자동으로 신뢰 체인 검증
```

### 인증서 만료 확인

```cpp
// 인증서 만료일 확인 (선택적으로 추가 가능)
FILETIME now;
GetSystemTimeAsFileTime(&now);

if (CompareFileTime(&now, &certContext->pCertInfo->NotAfter) > 0) {
    LOG_ERROR("Server certificate has expired");
    return false;
}
```

---

## 11. 개발 환경 자체 서명 인증서 설정

### `CreateDevTLSCert.bat`

```batch
@echo off
:: PowerShell로 자체 서명 인증서 생성
powershell -Command "& { ^
    $cert = New-SelfSignedCertificate ^
        -Subject 'CN=DevServerCert' ^
        -CertStoreLocation 'Cert:\LocalMachine\MY' ^
        -KeyExportPolicy Exportable ^
        -KeySpec Signature ^
        -KeyLength 2048 ^
        -HashAlgorithm SHA256 ^
        -NotAfter (Get-Date).AddYears(5); ^
    Write-Host ('Created: ' + $cert.Thumbprint) ^
}"
```

**생성 확인:**
```
certmgr.msc → 로컬 컴퓨터 → 개인 → 인증서
→ "DevServerCert" 확인
```

### `RemoveDevTLSCert.bat`

```batch
powershell -Command "& { ^
    $cert = Get-ChildItem -Path 'Cert:\LocalMachine\MY' ^
        | Where-Object { $_.Subject -like '*DevServerCert*' }; ^
    if ($cert) { Remove-Item -Path $cert.PSPath; Write-Host 'Removed' } ^
    else { Write-Host 'Not found' } ^
}"
```

### 관리자 권한 요구사항

`LocalMachine` 저장소에 쓰려면 **관리자 권한**이 필요하다.  
일반 사용자 권한으로는 `CurrentUser` 저장소만 사용 가능하며, 이 경우  
`CERT_SYSTEM_STORE_LOCAL_MACHINE` → `CERT_SYSTEM_STORE_CURRENT_USER`로 변경해야 한다.

---

## 12. SChannel 내부 구조 메모

**cbHeader / cbMaximumMessage / cbTrailer 의미:**

```
QueryContextAttributes(SECPKG_ATTR_STREAM_SIZES)
→ SecPkgContext_StreamSizes {
    cbHeader     : 5   (TLS 레코드 헤더: ContentType 1B + Version 2B + Length 2B)
    cbTrailer    : 32  (TLS 1.2 AES-256-GCM AEAD 태그 + IV 등)
    cbMaximumMessage: 16384  (최대 TLS 레코드 페이로드 크기)
    ...
}
```

**암호화 버퍼 레이아웃:**

```
[cbHeader bytes][plaintext][cbTrailer bytes]
     5B              N B        32B         (TLS 1.2 AES-GCM 예)
```

SChannel이 `EncryptMessage` 호출 시 이 레이아웃을 in-place로 채운다.

---

## 관련 문서
- [[RUDPSessionBroker]] — TLSHelperServer 사용처
- [[RUDPClientCore]] — TLSHelperClient 사용처
- [[CryptoSystem]] — 패킷 암호화 (TLS 이후 UDP 레벨)
- [[Troubleshooting]] — TLS 핸드셰이크 실패 해결
---

## 현재 코드 기준 함수 설명 및 정정

### `TLSHelperBase`

#### `bool EncryptData(const char* plainData, size_t plainSize, char* encryptedBuffer, size_t& encryptedSize)`
- 핸드셰이크 완료 후 평문을 TLS 레코드로 암호화한다.

#### `bool DecryptData(const char* encryptedData, size_t encryptedSize, char* plainBuffer, size_t& plainSize)`
- 단일 TLS 레코드 단위 복호화를 수행한다.

#### `TlsDecryptResult DecryptDataStream(std::vector<char>& encryptedStream, char* plainBuffer, size_t& plainSize)`
- 누적된 TLS 바이트 스트림에서 완성된 레코드만 복호화한다.

#### `bool EncryptCloseNotify(char* buffer, const size_t bufferSize, size_t& encryptedSize)`
- TLS `close_notify` 경고 레코드를 생성한다.

### `TLSHelperClient`

#### `bool Initialize()`
- 클라이언트용 Schannel 자격 증명을 초기화한다.

#### `bool Handshake(SOCKET socket)`
- `InitializeSecurityContext` 기반 TLS 핸드셰이크를 수행한다.

### `TLSHelperServer`

#### `TLSHelperServer(const std::wstring& inStoreName, const std::wstring& inCertSubjectName)`
- 인증서 저장소 이름과 Subject 이름을 받아 서버용 TLS 도우미를 구성한다.

#### `bool Initialize()`
- 생성자에서 받은 인증서 정보를 사용해 서버용 Schannel 자격 증명을 초기화한다.

#### `bool Handshake(SOCKET socket)`
- `AcceptSecurityContext` 기반 서버 TLS 핸드셰이크를 수행한다.

### 정정 메모

- 현재 `TlsDecryptResult` 값은 `None`, `PlainData`, `CloseNotify`, `Error`다.
- 예전 문서에 있던 `Initialize(storeName, subjectName)` 시그니처는 현재 코드와 맞지 않는다.
