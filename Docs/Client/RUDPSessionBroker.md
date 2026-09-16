# RUDPSessionBroker (Client View)

> 클라이언트가 SessionBroker에서 무엇을 받아 오고, 그 뒤 어떻게 UDP 세션을 여는지 현재 구현 기준으로 정리한다.

---

## 역할

클라이언트는 SessionBroker와 TLS TCP 연결을 맺고 아래 정보를 받는다.

- RUDP 프로토콜 버전 (`2`)
- 연결 결과 코드
- UDP 서버 IP
- UDP 서버 포트
- 세션 ID
- 세션 키 16B
- 세션 솔트 16B

이 정보로 `RUDPClientCore` 또는 C# `RudpSession`이 실제 UDP 연결을 시작한다.

---

## 응답 형식

현재 `CONNECT_RESULT_CODE`는 `unsigned char` 기반 enum이다.

```text
[HeaderCode 1B]
[PayloadLen 2B]
[Reserved 2B]
[RUDP_PROTOCOL_VERSION 4B, little-endian]
[CONNECT_RESULT_CODE 1B]
[serverIp string]
[serverPort 2B]
[sessionId 2B]
[sessionKey 16B]
[sessionSalt 16B]
```

TLS 핸드셰이크 직후 클라이언트는 프로토콜 버전 `2`를 4바이트 big-endian으로 먼저 보낸다. 서버는 값이 없거나 다르면 세션을 예약하지 않고 연결을 닫는다. 응답 안의 버전은 `NetBuffer` 직렬화 규칙에 따른 little-endian이며, 그 뒤 결과 코드를 1바이트로 읽어야 한다.

---

## C++ 클라이언트 흐름

`RUDPClientCore`는 대략 아래 순서로 동작한다.

1. SessionBroker 옵션 로드
2. TCP connect
3. `TLSHelperClient::Initialize()`
4. `TLSHelperClient::Handshake(socket)`
5. `SendProtocolVersion(socket, RUDP_PROTOCOL_VERSION)`
6. `DecryptDataStream(...)`으로 응답 복호화
7. 응답 버전과 결과 코드 검증 후 세션 정보 파싱
8. UDP 소켓 생성
9. CONNECT 패킷 전송

---

## C# BotTester 흐름

`SessionGetter`가 세션 정보를 받아오면 `RudpSession` 생성자가 이를 파싱한다.

그 뒤:

1. `SslStream` 핸드셰이크 후 버전 `2`를 big-endian으로 전송
2. 응답 버전과 결과 코드 검증
3. `AesGcm` 생성
4. `UdpClient.Connect(...)`
5. `ReceiveAsync`
6. `PacketProcessorAsync`
7. `RetransmissionAsync`
8. `SendConnectPacketAsync`

즉 현재 C# 구현도 브로커 응답 직후 바로 UDP 세션 작업을 시작한다.

---

## TLS 관련 주의

- 서버 인증서 로딩 방식은 서버 문서 기준 `ServerCertificateConfig`다.
- 클라이언트 문서는 TLS helper의 옛 생성자 시그니처를 전제로 읽으면 안 된다.
- 개발 환경에서는 `CreateDevTLSCert.bat`로 만든 인증서를 기준으로 맞춘다.

---

## 관련 문서

- [[RUDPClientCore]] - C++ 클라이언트 코어
- [[RudpSession_CS]] - C# 클라이언트 세션
- [[TLSHelper]] - TLS 처리 상세
