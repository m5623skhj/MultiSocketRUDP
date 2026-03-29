# 트러블슈팅 가이드

> 서버·클라이언트 개발 중 자주 마주치는 문제와 해결 방법.

---

## 연결 관련

### 증상: 클라이언트가 SessionBroker에 연결되지 않는다

**체크리스트:**
```
□ SessionBrokerOption.txt의 SESSION_BROKER_PORT가 방화벽에서 열려 있는가?
□ 서버 프로세스가 실행 중이고 포트에 바인딩되어 있는가?
  → netstat -ano | findstr :10000
□ TLS 인증서가 올바르게 설치되어 있는가?
  → 개발: Tool\ForTLS\CreateDevTLSCert.bat 실행 여부 확인
  → 운영: certmgr.msc에서 MY 저장소에 인증서 존재 확인
□ 클라이언트의 sessionGetterOptionFilePath에서 IP/Port가 맞는가?
```

---

### 증상: TLS 핸드셰이크 실패 (`TLS Handshake failed`)

```
원인 1: 인증서가 없거나 이름이 다름
  → MultiSocketRUDPCore 생성자의 certSubjectName과 인증서 CN 일치 확인
  → TLSHelperServer::Initialize 내 CertFindCertificateInStore 반환값 확인

원인 2: TLS 1.2 미지원 (구형 OS)
  → Windows 7 이하는 SP_PROT_TLS1_2 지원 안 될 수 있음

원인 3: 클라이언트가 인증서 검증 실패
  → 클라이언트는 SCH_CRED_MANUAL_CRED_VALIDATION 사용
  → 개발 환경에서 자체 서명 인증서는 검증 무시 설정 확인
```

---

### 증상: CONNECT 패킷 이후 `TryConnect()` 실패

```cpp
// RUDPSession::TryConnect의 검증 조건 순서
if (packetSequence != LOGIN_PACKET_SEQUENCE) return false;  // 시퀀스 != 0
if (sessionId != recvSessionId) return false;               // sessionId 불일치
if (!stateMachine.TryTransitionToConnected()) return false; // 이미 CONNECTED
```

**진단:**
```
로그 확인: "TryConnect() failed" or "Invalid session state"

원인 1: 클라이언트가 잘못된 sessionId를 전송
  → SessionBroker에서 수신한 sessionId를 그대로 전송하는지 확인

원인 2: 동일 세션으로 두 번 CONNECT 전송 (재전송으로 인한 중복)
  → 정상: 두 번째는 TryTransitionToConnected() CAS 실패 → false → 무시됨

원인 3: 클라이언트가 CONNECT 패킷 시퀀스를 0이 아닌 다른 값으로 전송
  → LOGIN_PACKET_SEQUENCE = 0 확인
```

---

## 패킷 처리 관련

### 증상: 패킷을 수신했는데 핸들러가 호출되지 않는다

```
체크리스트:
□ RegisterPacketHandler가 생성자에서 올바르게 호출됐는가?
□ PacketId가 정확히 일치하는가?
  → static_cast<PacketId>(PACKET_ID::MY_PACKET) 형변환 확인
□ ContentsPacketRegister::Init()이 StartServer 이후에 호출됐는가?
□ 로그에 "Unknown packet. packetId: N" 메시지가 있는가?
□ DecodePacket이 성공했는가? (로그에 "DecodePacket failed" 없는지 확인)
```

**디버깅 방법:**
```cpp
// ProcessPacket 진입 여부 확인
void Player::OnMove(const MovePacket& packet) {
    LOG_DEBUG("OnMove called");  // 이 로그가 안 뜨면 핸들러 미등록
    // ...
}
```

---

### 증상: AES-GCM 복호화 실패 (`DecodePacket failed`)

```
원인 1: sessionKey 또는 sessionSalt가 서버-클라이언트 불일치
  → SessionBroker가 전달한 키/솔트를 클라이언트가 올바르게 저장했는지 확인

원인 2: Nonce 생성 로직 불일치
  → C++ 서버의 CryptoHelper::GenerateNonce와
     클라이언트(C#) NetBuffer::GenerateNonce의 바이트 배치 비교
  → 특히 direction 비트(상위 2비트)와 시퀀스 번호 엔디안 확인

원인 3: isCorePacket 플래그 불일치
  → 서버가 isCorePacket=true로 암호화했는데 클라이언트가 false로 복호화
  → bodyOffset 계산이 달라져 암호화된 영역이 어긋남

원인 4: PACKET_CODE(헤더 코드)가 다름
  → CoreOption.txt의 PACKET_CODE와 클라이언트 설정값 일치 확인
```

---

### 증상: 패킷이 가끔 순서가 바뀌어 처리된다

이는 정상 동작이다. `SessionPacketOrderer`가 순서를 보장하지만:

```
□ maxHoldingPacketQueueSize가 너무 작은 경우:
  → 홀딩 큐 초과 → ERROR_OCCURED → 세션 종료
  → MAX_HOLDING_PACKET_QUEUE_SIZE 증가 (32 → 64)

□ 순서 역전이 너무 많은 경우:
  → 네트워크 경로 문제 (멀티패스 라우팅)
  → 로그의 "holding queue full" 메시지 확인
```

---

## 재전송 관련

### 증상: 세션이 자꾸 끊긴다 (`Max retransmission count exceeded`)

```
원인 1: 재전송 설정이 너무 타이트함
  → MAX_PACKET_RETRANSMISSION_COUNT 증가 (16 → 32)
  → RETRANSMISSION_MS 증가 (200 → 500)

원인 2: 실제 네트워크 문제
  → 패킷 손실율 측정: ping -n 100 <server_ip>
  → 로컬 테스트(127.0.0.1)에서는 재현되지 않는가?

원인 3: 흐름 제어 미동작 (서버→클라이언트 패킷 누락)
  → CWND가 너무 크게 설정됨 → 버퍼 오버플로우 → 패킷 유실
  → MAX_CWND 조정 검토
```

---

### 증상: 재전송이 전혀 발생하지 않는다

```
□ RETRANSMISSION_THREAD_SLEEP_MS가 너무 큰가?
□ retransmissionThread가 실제로 시작됐는가?
  → 로그에 "Worker thread stopped" 이전 에러 없는지 확인
□ isErasedPacketInfo가 너무 빨리 true가 되는가?
  → EraseSendPacketInfo가 의도치 않은 곳에서 호출되지 않는지 확인
```

---

## 성능 관련

### 증상: CPU 사용률이 예상보다 높다

```
원인 1: IO Worker가 Sleep 없이 폴링 중
  → BuildConfig.h: USE_IO_WORKER_THREAD_SLEEP_FOR_FRAME=1 설정
  → CoreOption.txt: WORKER_THREAD_ONE_FRAME_MS=1 (또는 그 이상)

원인 2: Retransmission Thread 슬립이 너무 짧음
  → RETRANSMISSION_THREAD_SLEEP_MS 증가 (100 → 200)

원인 3: Heartbeat 간격이 너무 짧음
  → HEARTBEAT_THREAD_SLEEP_MS 증가 (1000 → 3000)

진단 방법:
  → Visual Studio Performance Profiler (CPU 샘플링)
  → Process Explorer로 스레드별 CPU 확인
```

---

### 증상: TPS가 예상보다 낮다

```
□ THREAD_COUNT가 물리 코어 수에 비해 적거나 많은가?
  → 과도한 스레드는 컨텍스트 스위치 비용 증가
  → 권장: 물리 코어 수의 50~75%

□ 패킷 핸들러에서 블로킹 작업을 하고 있는가?
  → DB 접근, 파일 IO, Sleep 등은 Logic Worker Thread를 블락함
  → 비동기 처리나 별도 작업 스레드로 분리 권장

□ GetTPS()로 실제 TPS 측정
  → core.GetTPS() 확인 후 core.ResetTPS()
```

---

## 메모리 관련

### 증상: 메모리 사용량이 지속적으로 증가한다

```
원인 1: NetBuffer::Free() 미호출
  → GetReceivedPacket()으로 받은 버퍼를 Free하지 않음
  → MemoryTracer 활성화로 추적:
     MemoryTracer::Enable();
     // 실행 후
     MemoryTracer::GenerateReportToFile("leak.log");

원인 2: SendPacketInfo 누수
  → EraseSendPacketInfo가 호출되지 않는 경로 확인
  → sendPacketInfoMap이 무한 증가하는지 확인

원인 3: PendingQueue 누적
  → remoteAdvertisedWindow가 0으로 고정된 경우
  → 클라이언트 수신 처리 속도 확인
```

---

### 증상: 서버 종료 시 크래시 또는 hang

```
원인 1: StopServer 이전에 StopLoggerThread 호출
  → 반드시 StopServer() 안에서 Logger 종료 처리
  
원인 2: 이미 닫힌 소켓에 Send 시도
  → CloseAllSessions() 이후 스레드가 완전히 종료되기 전에 Send
  → StopAllThreads()가 완료될 때까지 대기 (jthread 소멸자 join 보장)

원인 3: ClearAllSessions()에서 사용 중인 세션 delete
  → 반드시 StopAllThreads() 이후에 ClearAllSessions() 호출
```

---

## 디버그 도구

### MemoryTracer 활성화

```cpp
// Debug 빌드에서
MemoryTracer::Enable();
MemoryTracer::SetOutputFile("memory_trace.log");

// 서버 실행 후
MemoryTracer::GenerateReport();          // 콘솔 출력
MemoryTracer::GenerateReportToFile();    // 파일 출력
MemoryTracer::GetThreadStatistics();     // 스레드별 통계
```

→ [[MemoryTracer]] 참조

### CRT 디버그 (Debug 빌드)

```cpp
// main 시작 시
#if defined(_DEBUG)
EnableCrtDebug();  // DebugHelper.h
#endif
// 프로그램 종료 시 메모리 누수 자동 출력
```

### 로그 레벨 확인

```cpp
// Logger 레벨 설정 (App.cpp 또는 main)
// 서버: LogExtension.h의 LOG_DEBUG 매크로 (_DEBUG 빌드만 활성)
// 릴리즈에서 상세 로그 필요 시 LOG_ERROR를 임시로 사용
```

---

## 관련 문서
- [[MultiSocketRUDPCore]] — 서버 시작/종료
- [[RUDPSession]] — 패킷 핸들러 등록
- [[CryptoSystem]] — 암호화 관련 문제
- [[MemoryTracer]] — 메모리 추적 도구
- [[PerformanceTuning]] — 성능 최적화 가이드
