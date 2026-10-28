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
- 현재 C++ 클라이언트는 Windows 신뢰 체인·만료·서버 인증 용도·요청 DNS/IP 이름을 검사한다. 자체 서명 `DevServerCert` 생성만으로 루프백 이름과 신뢰가 준비되지는 않는다.
- 중간 인증서는 서버 또는 Windows 캐시에 있어야 한다. 라이브러리는 AIA·CRL·OCSP 온라인 조회를 요청하지 않는다.
- C# BotTester는 기본 지문 없는 경로에서 검증을 생략하므로, C# 접속 성공만으로 C++ 인증 설정이 올바르다고 판단하지 않는다.

---

## CONNECT 이후 연결이 안 붙을 때

- 브로커에서 받은 `sessionId`를 그대로 CONNECT에 넣는지 확인
- CONNECT 시퀀스가 0인지 확인
- CONNECT ACK 유실 시 동일 주소·포트의 유효한 중복 CONNECT에는 ACK를 다시 보낸다. 연결 통계·훅은 최초에만 수행하므로 중복 CONNECT 자체를 오류로 단정하지 않는다.
- 외부 접속에서는 브로커 TCP 포트와 실제 세션 UDP 포트를 각각 확인한다. `CORE_IP`는 광고 주소이며 bind 인터페이스가 아니다.
- 고정 UDP 범위를 사용하면 내부·외부 포트 번호를 같게 전달한다. `HOST_LOOPBACK=1`은 실제 브로커 peer가 127/8인 C++ 호스트 로컬 접속에만 사용한다.

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

## 서버 재시작 필요 로그가 발생했을 때

다음 오류는 개별 session 문제가 아니라 공유 worker pipeline을 신뢰할 수 없는 상태다.

- `Fatal RIO completion queue corruption detected`
- `Recv logic wait failed`
- `SetEvent failed in EnqueueContextResult()`

1. `SERVER_FATAL_ERROR_CODE`, `threadId`, `nativeErrorCode`를 확인한다.
2. 상위 레이어가 health를 unhealthy로 전환했는지 확인한다.
3. callback thread에서 `StopServer()`를 직접 호출하지 않았는지 확인한다.
4. 외부 supervisor가 프로세스 교체 요청을 받았는지 확인한다.
5. 재시작 후 CQ 크기, event handle 수명, 종료 순서, 메모리 손상 로그를 조사한다.

정상적인 session disconnect나 해제 중 `WSA_OPERATION_ABORTED`는 이 절의 재시작 대상이 아니다. 오류별 발생 조건과 상위 레이어 callback 예시는 [[Server/FatalErrorHandling|치명 오류 통지와 프로세스 재시작]]을 참고한다.

---

## 관련 문서

- [[GettingStarted]] - 기본 기동 순서
- [[TLSHelper]] - TLS 설정
- [[MemoryTracer]] - 추적 도구
- [[Server/FatalErrorHandling]] - 치명 오류 통지와 프로세스 재시작 절차
