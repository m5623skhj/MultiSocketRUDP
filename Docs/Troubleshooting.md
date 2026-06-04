# 트러블슈팅 가이드

> 현재 코드 기준으로 자주 헷갈리는 설정과 순서를 빠르게 점검하기 위한 문서다.

---

## 패킷 핸들러가 호출되지 않을 때

### 먼저 볼 체크리스트

- `RegisterPacketHandler(...)`가 세션 생성자에서 호출되는가
- `PacketId`가 서버와 클라이언트에서 일치하는가
- `ContentsPacketRegister::Init()`이 `StartServer()` 이전에 호출됐는가
- `DecodePacket failed` 로그가 없는가

여기서 가장 중요한 정정점은 세 번째다.  
`ContentsPacketRegister::Init()`은 `StartServer()` 이후가 아니라 **이전** 호출이 맞다.

---

## SessionBroker 연결이 안 될 때

- `SessionBrokerOption.txt`의 포트가 열려 있는지 확인
- 서버가 해당 포트에 bind/listen 중인지 확인
- 개발용 인증서를 만들었는지 확인
- 클라이언트 설정 파일 경로가 현재 `ClientOptionFile/SessionGetterOption.txt` 기준과 맞는지 확인

---

## TLS 핸드셰이크 실패

- 인증서 저장소/subject가 실제 설정과 맞는지 확인
- 현재 TLS 문서는 `TLSHelperServer(ServerCertificateConfig)` 기준으로 읽어야 한다
- 개발 환경이면 `CreateDevTLSCert.bat` 실행 여부 확인

---

## CONNECT 이후 연결이 안 붙을 때

- 브로커에서 받은 `sessionId`를 그대로 CONNECT에 넣는지 확인
- CONNECT 시퀀스가 0인지 확인
- 동일 세션으로 중복 CONNECT를 보내는 테스트 코드가 없는지 확인

---

## 복호화 실패

- `sessionKey`, `sessionSalt` 저장이 맞는지 확인
- nonce 생성 규칙이 서버/클라이언트에서 일치하는지 확인
- `isCorePacket` 판정이 맞는지 확인
- `PACKET_CODE`가 동일한지 확인

---

## 세션이 자주 끊길 때

- `RETRANSMISSION_MS`
- `MAX_PACKET_RETRANSMISSION_COUNT`
- worker 병목
- 실제 네트워크 손실

이 순서로 보는 편이 효율적이다.

---

## 메모리 추적 관련

현재 `MemoryTracer` 문서는 아래 시그니처 기준으로 봐야 한다.

```cpp
MemoryTracer::GetObjectHistory(void* ptr);
MemoryTracer::GetThreadStatistics();
```

예전 `out` 파라미터 버전 예제는 현재 헤더와 맞지 않는다.

---

## 관련 문서

- [[GettingStarted]] - 기본 기동 순서
- [[TLSHelper]] - TLS 설정
- [[MemoryTracer]] - 추적 도구
