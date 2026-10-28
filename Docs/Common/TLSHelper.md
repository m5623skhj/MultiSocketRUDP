# TLSHelper

> Windows Schannel을 사용해 브로커의 TLS 1.2 핸드셰이크와 스트림 암복호화를 수행한다.
> 구현은 `MultiSocketRUDP/Common/TLS/TLSHelper.h`, `TLSHelper.cpp`, `TLSHelperClient.cpp`, `TLSHelperServer.cpp`에 있다.

## 목차

1. [구성과 소유권](#구성과-소유권)
2. [서버 인증서 설정](#서버-인증서-설정)
3. [클라이언트 인증 정책](#클라이언트-인증-정책)
4. [핸드셰이크와 잔여 데이터](#핸드셰이크와-잔여-데이터)
5. [스트림 암복호화와 종료](#스트림-암복호화와-종료)
6. [프로토콜 버전 확인과 진단](#프로토콜-버전-확인과-진단)
7. [개발·테스트 인증서](#개발테스트-인증서)

## 구성과 소유권

`TLSHelperBase`가 Schannel credential·context·스트림 크기·핸드셰이크 잔여 데이터를 소유하고, `TLSHelperClient`와 `TLSHelperServer`가 역할별 초기화·핸드셰이크를 구현한다. 소멸 시 context와 credential을 해제한다. 서버 브로커는 연결마다 별도 helper를 생성한다.

helper는 내부 잠금으로 병렬 I/O를 직렬화하지 않는다. 연결 담당 스레드에서 설정·핸드셰이크·복호화·잔여 데이터 이동을 수행해야 하며, I/O 도중 `SetServerName()`을 호출하면 안 된다.

## 서버 인증서 설정

```cpp
enum class ServerCertificateSource : unsigned char { Store, PfxFile };

[[nodiscard]]
static ServerCertificateConfig FromStore(
    const std::wstring& inStoreName, const std::wstring& inCertSubjectName);
[[nodiscard]]
static ServerCertificateConfig FromStoreThumbprint(
    const std::wstring& inStoreName, const std::wstring& inThumbprint);
[[nodiscard]]
static ServerCertificateConfig FromPfxFile(
    const std::wstring& inPfxFilePath, const std::wstring& inPfxPassword);
```

위 정적 팩토리는 `TLSHelper::ServerCertificateConfig`의 멤버다. 반환값을 무시하면 컴파일 경고가 발생한다. 호출 측에서 반드시 검사해야 한다.

```cpp
auto config = TLSHelper::ServerCertificateConfig::FromStore(L"MY", L"DevServerCert");
TLSHelper::TLSHelperServer server(std::move(config));
if (!server.Initialize())
{
    // 인증서 선택·키 접근·credential 초기화 실패 처리
}
```

| 설정 | 현재 동작 |
|---|---|
| 저장소·subject | `CurrentUser` 저장소의 `CERT_FIND_SUBJECT_STR` 검색 |
| 저장소·지문 | 40자리 16진 지문을 20바이트로 바꿔 `CERT_FIND_HASH` 검색 |
| PFX | 파일 읽기 → `PFXImportCertStore` → 개인 키가 있는 인증서 선택 |

지문이 잘못됐거나 일치하는 인증서가 없으면 실패하며 subject 검색으로 대체하지 않는다. 같은 subject의 인증서가 여럿이면 subject만으로 원하는 인증서가 선택된다고 보장할 수 없다. PFX 경로·암호·개인 키는 배포 환경에서 관리한다.

`StoreNames`는 `MY`, `ROOT`, `CA`, `TRUST`, `AuthRoot` 문자열 상수를 제공한다. 서버 저장소 조회는 `CurrentUser`이며 `LocalMachine`을 자동으로 함께 조회하지 않는다.

이 저장소 밖의 ActionRPG 실행기에서 수행하는 인증서 사전 점검은 이 라이브러리의 보장 범위에 포함하지 않는다.

## 클라이언트 인증 정책

```cpp
void SetServerName(std::wstring inServerName);
[[nodiscard]]
bool Initialize() override;
[[nodiscard]]
bool Handshake(SOCKET socket) override;
[[nodiscard]]
bool Handshake(SOCKET socket, const std::stop_token& inStopToken);
```

클라이언트 credential은 `SCH_CRED_MANUAL_CRED_VALIDATION | SCH_CRED_NO_DEFAULT_CREDS`를 사용한다. 수동 검증 플래그는 검증 생략을 뜻하지 않는다. 핸드셰이크 후 `VerifyServerCertificate()`가 다음을 검사하며 모두 성공해야 세션 정보를 사용한다.

- Windows 신뢰 체인과 유효 기간
- 서버 인증 EKU
- 요청한 DNS 또는 IP의 SSL 이름 정책

신뢰하지 않는 루트·이름 불일치·만료를 허용하는 예외 플래그는 없다. 이름을 지정하지 않은 직접 호출은 연결된 peer의 IPv4 주소를 검증 대상으로 사용한다. SessionGetter는 옵션에 입력한 브로커 이름을 전달하므로 DNS 해석 결과 IP로 인증서 이름을 바꾸지 않는다.

체인 구성에는 `CERT_CHAIN_CACHE_ONLY_URL_RETRIEVAL`을 사용한다. 중간 인증서는 서버가 제공하거나 이미 캐시에 있어야 하며 AIA·CRL·OCSP 네트워크 조회를 요청하지 않는다. 따라서 온라인 폐기 상태 확인을 보장하는 정책으로 설명하면 안 된다. 라이브러리는 신뢰 저장소를 변경하지 않는다.

> C# BotTester의 `SessionGetter`는 별도 구현이다. 현재 지문이 없으면 인증서를 허용하고, 지문이 있으면 해시만 비교한다. 이 C++ 검증 정책과 동일하지 않다. [[SessionGetter_CS]]를 참고한다.

## 핸드셰이크와 잔여 데이터

서버는 `AcceptSecurityContext`, 클라이언트는 `InitializeSecurityContext` 반복으로 핸드셰이크한다. `SEC_E_INCOMPLETE_MESSAGE`는 기존 토큰을 보존하고 추가 수신하며, `SEC_I_CONTINUE_NEEDED`는 후속 토큰을 처리한다. 성공 후 스트림 크기를 조회한다.

```cpp
[[nodiscard]]
bool TLSHelperServer::Handshake(SOCKET socket, const std::stop_token& stopToken);
[[nodiscard]]
std::vector<char> TakePendingHandshakeData();
```

서버 수신 대기는 하나의 5초 기준 제한, 클라이언트는 6초 기준 제한과 시작 취소를 관찰한다. 준비 상태는 100ms 단위로 확인한다. 이 시간은 각 수신 대기의 기준이며 DNS·TCP·TLS 전체가 하나의 6초 안에 끝난다는 뜻은 아니다. 소켓 송신 제한은 호출자가 설정하며 브로커와 C++ SessionGetter가 각각 적용한다.

성공한 핸드셰이크와 첫 애플리케이션 레코드가 같은 TCP 수신에 들어올 수 있다. 양쪽 helper는 `SECBUFFER_EXTRA`를 `pendingHandshakeData`에 보존한다. 연결 스레드는 `TakePendingHandshakeData()`로 한 번 이동해 버전 또는 세션 정보 수신기에 전달하고, 새 `recv()`보다 먼저 처리한다. 일부 레코드만 남았으면 다음 수신으로 완성한다. 새 핸드셰이크는 이전 잔여 데이터를 비운다.

## 스트림 암복호화와 종료

```cpp
[[nodiscard]]
bool EncryptData(const char* plainData, size_t plainSize,
    char* encryptedBuffer, size_t& encryptedSize);
[[nodiscard]]
bool DecryptData(const char* encryptedData, size_t encryptedSize,
    char* plainBuffer, size_t& plainSize);
[[nodiscard]]
TlsDecryptResult DecryptDataStream(std::vector<char>& encryptedStream,
    char* plainBuffer, size_t& plainSize);
[[nodiscard]]
bool EncryptCloseNotify(char* buffer, const size_t bufferSize, size_t& encryptedSize);
```

반환값을 무시하면 컴파일 경고가 발생한다. 호출 측에서 반드시 검사해야 한다.

`EncryptData()`는 Schannel의 헤더·본문·후미 공간에 TLS 레코드를 작성한다. `DecryptData()`는 단일 호출 결과를 처리하며 TCP 누적 수신에는 `DecryptDataStream()`을 사용한다. 후자는 현재 스트림의 완전한 레코드를 처리하고 미완성 데이터는 남긴다.

| `TlsDecryptResult` | 의미 |
|---|---|
| `None` | 추가 데이터가 필요하거나 평문이 없음 |
| `PlainData` | 평문을 얻음 |
| `CloseNotify` | TLS 종료 알림을 확인함 |
| `Error` | 복호화 오류 |

`CloseNotify`와 평문이 같은 호출에서 나올 수 있으므로 결과 코드뿐 아니라 `plainSize`도 확인한다. 복호화 API에는 출력 용량 파라미터가 없다. 호출자는 한 번에 복호화할 누적 평문 전체를 담을 수 있는 공간을 제공해야 한다. helper의 한도 검사만으로 임의 크기 출력 버퍼가 안전해지지 않는다.

브로커 송신은 헤더 작성 → 세션 정보 암호화·전체 송신 → `EncryptCloseNotify()`·전체 송신 → `shutdown(SD_SEND)` → 제한된 FIN 수신 순서다. `EncryptCloseNotify()`의 출력 버퍼는 호출자 소유다.

## 프로토콜 버전 확인과 진단

```cpp
bool SendProtocolVersion(SOCKET socket, uint32_t version);
bool ReceiveProtocolVersion(SOCKET socket, uint32_t expectedVersion,
    const std::stop_token& stopToken);
[[nodiscard]]
const ProtocolVersionReceiveResult& GetProtocolVersionResult() const;
[[nodiscard]]
SECURITY_STATUS GetLastStatus() const;
```

버전 요청은 TLS로 보호한 정확히 4바이트 big-endian 값이다. 현재 버전은 `2`다. 서버는 5초 기준 제한과 중지 요청을 확인하며 추가 평문·불완전 버전·값 불일치를 실패 처리한다. 세션 예약은 검증 성공 후에 수행한다. 응답 안의 4바이트 버전은 `NetBuffer`의 little-endian 형식이다.

`ProtocolVersionReceiveResult`는 `reason`, 선택적 `receivedVersion`, `plainBytes`, `socketError`를 보관한다. 브로커 거절 로그에는 `expected_version`, `received_version` 또는 `unavailable`, `plain_bytes`, `socket_error`도 포함된다.

| 진단 | 해석 |
|---|---|
| `version-mismatch` | 실제로 읽은 버전이 기대값과 다름 |
| `version-not-received`, `version-incomplete` | 버전 미수신 또는 일부만 수신 |
| `peer-closed`·TLS/소켓 오류 | 버전을 얻기 전 연결·복호화·송수신 실패 |

`GetLastStatus()`는 credential·핸드셰이크·인증서 검증에서 저장한 마지막 상태다. 모든 소켓·파일·저장소 실패가 이 값을 새로 설정하는 것은 아니므로, 모든 `false` 원인을 대표하는 값으로 단정하지 않는다. 반환값을 무시하면 컴파일 경고가 발생한다. 호출 측에서 반드시 검사해야 한다.

진단 필드와 잔여 데이터는 연결 스레드 소유다. 키·솔트·수신 평문을 진단에 추가하지 않는다.

## 개발·테스트 인증서

`CreateDevTLSCert.bat`는 `CurrentUser\My`에 DNS 이름 `DevServerCert`인 자체 서명 인증서를 만든다. `CreateDevTLSPfx.bat`도 같은 이름의 인증서를 PFX로 내보낸 뒤 저장소 원본을 지운다. 두 스크립트 모두 클라이언트의 신뢰 루트나 `localhost`·`127.0.0.1` SAN을 자동으로 준비하지 않는다.

> 현재 C++ 클라이언트로 루프백 접속하려면 Windows에서 신뢰되고 접속 DNS/IP와 SAN이 일치하는 인증서가 필요하다. 기존 스크립트 실행만으로 C++ 샘플·통합 테스트 연결 성공을 보장할 수 없다. 해당 테스트 환경의 신뢰·이름 구성을 별도로 준비해야 한다.

인증서 삭제·신뢰 저장소 변경·운영 PFX 교체는 문서 점검 과정에서 실행하지 않는다. 실제 인증서 준비 절차는 배포 환경의 신뢰 정책에 맞춘다.

## 관련 문서

- [[Server/RUDPSessionBroker]] — 연결별 helper와 버전 검증
- [[Client/RUDPSessionBroker]] — 응답 파싱
- [[RUDPClientCore]] — DNS·시작 취소·세션 수신
- [[SessionGetter_CS]] — C# 인증 정책
- [[CryptoSystem]] — UDP 패킷 암호화
- [[DevelopmentScripts]] — 인증서 배치 파일
- [[Troubleshooting]] — TLS 실패 진단
