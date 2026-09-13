# RudpSession (C# 클라이언트 세션)

> BotTester가 사용하는 C# UDP 세션 구현을 현재 코드 기준으로 정리한다.

---

## 구조

핵심 구성은 아래다.

- `SessionInfo`
- `TargetServerInfo`
- `UdpClient`
- `HoldingPacketStore`
- `BufferStore`
- `Channel<Action>` 기반 수신 후속 처리 큐
- 비신뢰성 송신·수신 대기열 (각 64개, 가득 차면 가장 오래된 대기 패킷 제거)

이 구현은 C++ `RUDPClientCore`와 프로토콜은 같지만 내부 구조는 다르다.

---

## 초기화

세션 브로커 응답을 파싱한 뒤 아래 작업을 수행한다.

1. `SessionInfo` 채움
2. `AesGcm` 생성
3. `UdpClient.Connect(...)`
4. `Task.Run(ReceiveAsync)`
5. `Task.Run(PacketProcessorAsync)`
6. `Task.Run(RetransmissionAsync)`
7. `Task.Run(SendConnectPacketAsync)`
8. `Task.Run(SendUnreliablePacketsAsync)`

즉 `PacketProcessorAsync()`는 현재 시작 흐름에 포함된다.

---

## 세션 브로커 응답

현재 C#도 아래 순서로 읽는다.

```text
protocolVersion 4B (little endian, 값 2)
CONNECT_RESULT_CODE 1B
serverIp string
serverPort 2B
sessionId 2B
sessionKey 16B
sessionSalt 16B
```

응답 결과 코드는 `ConnectResultCode : byte`다.

---

## 송신

`SendPacket(...)`은 아래 순서를 따른다.

1. sequence 증가
2. packet type 삽입
3. sequence 삽입
4. packet id 삽입
5. `NetBuffer.EncodePacket(...)`
6. `BufferStore`에 추적 등록
7. `UdpClient.SendAsync(...)`

`SendUnreliablePacket(NetBuffer, PacketId)`은 별도 64비트 번호(최초 1)와 `ClientToServerUnreliable` 방향으로 암호화하여 비신뢰성 큐에 넣는다. `BufferStore`에 등록하지 않아 ACK·재전송을 사용하지 않는다. 가득 찬 큐의 가장 오래된 미송신 패킷을 버리고 새 패킷을 수용해도 반환값은 `true`이며, 연결 종료 시에는 `false`다. 번호 발급·암호화·삽입은 동일한 잠금으로 보호한다.

---

## 수신

수신 데이터그램은 `ReceiveAsync()`에서 받고, `ProcessReceivedStreamAsync()`가 헤더 기준으로 패킷을 분리한다.

이후 `ProcessReceivedPacketAsync()`가:

- 복호화
- packet type 분기
- ACK 전송
- 순서 보장
- `PacketProcessorAsync()`로 사용자 처리 전달

를 수행한다.

`UnreliableSendType`(7)은 `ServerToClientUnreliable` 방향으로 인증한 뒤 최초 번호를 수용하고, 이후 마지막 수용 번호 이하를 버린다. ACK와 신뢰성 순서 대기를 거치지 않는다. 비신뢰성 콜백 대기열이 가득 차면 가장 오래된 대기 콜백을 제거한다. 두 채널의 콜백은 같은 처리 작업에서 번갈아 실행한다.

서버 생존 검사는 인증에 성공한 수신 횟수를 기준으로 한다. 비신뢰성 패킷만 도착해도 연결을 유지하며 인증 실패 패킷은 반영하지 않는다. 64비트 번호 소진·순환 방어는 이번 범위에 포함하지 않는다.

---

## 재전송 관련 현재 상수

현재 구현 기준 값은 아래다.

- `RetransmissionWakeUpMs = 16`
- `BufferStore.RetransmissionTimeoutMs = 20`
- `BufferStore.RetransmissionMaxCount = 16`
- 서버 생존 확인 주기 `15초`

예전 문서의 `30ms`, `32ms`, `50ms` 설명은 현재 코드와 맞지 않는다.

---

## 연결 완료와 종료

ACK sequence 0을 받으면:

1. `isConnected = true`
2. `SessionState = Connected`
3. `StartServerAliveCheck()` 시작
4. `OnConnected()` 콜백 큐잉

종료 시에는 `Disconnect()`를 호출한다. 해당 메서드는 내부적으로 정리 프로세스(`Cleanup`)를 수행하여 소켓, 토큰, 보류 저장소, 재전송 저장소를 해제한다.

---
## 구현상 주의

- C# 구현은 `remoteAdvertisedWindow` 기반 단순 전송 창 모델을 그대로 쓰지 않는다.
- ACK 이후 정리는 `BufferStore` 중심으로 이뤄진다.
- 수신 후속 처리는 `Channel<Action>` 단일 reader 구조다.

---

## 관련 문서

- [[SessionGetter_CS]] - 브로커 응답 수신
- [[RUDPClientCore]] - C++ 클라이언트 구현
- [[FlowController]] - 공통 개념
- [[BufferStore]] - 미응답 송신 패킷과 재전송 횟수 추적
- [[PacketLossSimulator]] - 송수신 datagram 손실 시뮬레이션
