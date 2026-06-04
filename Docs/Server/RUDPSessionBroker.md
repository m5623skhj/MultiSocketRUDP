# RUDPSessionBroker

> TCP + TLS로 세션 정보를 발급하고, 이후 UDP 세션 접속을 위한 정보를 넘기는 서버 측 브로커다.

---

## 현재 생성자

```cpp
RUDPSessionBroker(
    MultiSocketRUDPCore& inCore,
    ISessionDelegate& inSessionDelegate,
    TLSHelper::ServerCertificateConfig inServerCertificateConfig);
```

즉 브로커는 현재 `storeName`, `subjectName` 두 문자열을 직접 받지 않는다.  
TLS 설정은 `ServerCertificateConfig`로 전달된다.

---

## 시작 흐름

`Start(listenPort, rudpSessionIP)`는 아래를 수행한다.

1. `OpenSessionBrokerSocket(listenPort)`
2. 워커 thread pool 4개 시작
3. `sessionBrokerThread` 시작
4. `accept()`로 들어온 클라이언트를 큐에 적재
5. 워커가 `HandleClientConnection(...)` 처리

---

## 클라이언트 처리

각 연결은 worker thread에서 아래 흐름으로 처리한다.

1. `TLSHelperServer localTlsHelper(serverCertificateConfig)`
2. `localTlsHelper.Initialize()`
3. `localTlsHelper.Handshake(clientSocket)`
4. `ReserveSession(sendBuffer, rudpSessionIP)`
5. `SendSessionInfoToClient(clientSocket, localTlsHelper, sendBuffer)`
6. 실패 시 `sessionDelegate.AbortReservedSession(*session)`

핵심은 TLS helper가 "브로커 전역 1개"가 아니라 "연결당 1개"라는 점이다.

---

## 세션 예약

`ReserveSession(...)`는 아래를 수행한다.

1. `AcquireSession()`
2. `InitReserveSession(*session)`
3. `InitSessionCrypto(*session)`
4. `sendBuffer << connectResultCode`
5. 성공 시 `serverIp`, `port`, `sessionId`, `sessionKey`, `sessionSalt` 기록
6. 실패 시 `session->DoDisconnect(DISCONNECT_REASON::BY_ERROR)`

---

## 세션 정보 페이로드

현재 브로커 응답의 결과 코드는 `1B`다.

```text
[HeaderCode 1B]
[PayloadLen 2B]
[CONNECT_RESULT_CODE 1B]
[serverIp string]
[serverPort 2B]
[sessionId 2B]
[sessionKey 16B]
[sessionSalt 16B]
```

예전 문서의 `CONNECT_RESULT_CODE 4B` 설명은 현재 코드와 맞지 않는다.

---

## TLS 종료

`SendSessionInfoToClient(...)`는 아래 순서로 마무리한다.

1. `PacketCryptoHelper::SetHeader(sendBuffer)`
2. `localTlsHelper.EncryptData(...)`
3. `SendAll(...)`
4. `localTlsHelper.EncryptCloseNotify(...)`
5. `SendAll(...)`
6. `shutdown(clientSocket, SD_SEND)`
7. 짧은 `recv(...)` 루프로 FIN 정리

---

## 문서상 주의점

- 현재 브로커 생성자는 `ServerCertificateConfig` 기반이다.
- 현재 TLS helper는 per-connection 인스턴스로 생성된다.
- `ReserveSession()` 실패 시 세션 정리 흐름까지 포함한다.
- 세션 정보 응답 포맷의 결과 코드는 1바이트다.

---

## 관련 문서

- [[TLSHelper]] - TLS 래퍼
- [[RUDPClientCore]] - 클라이언트 측 응답 파싱
- [[MultiSocketRUDPCore]] - 세션 예약 내부 경로
