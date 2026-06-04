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

즉 `PacketProcessorAsync()`는 현재 시작 흐름에 포함된다.

---

## 세션 브로커 응답

현재 C#도 아래 순서로 읽는다.

```text
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

---

## 재전송 관련 현재 상수

현재 구현 기준 값은 아래다.

- `RetransmissionWakeUpMs = 16`
- `BufferStore.RetransmissionTimeoutMs = 50`
- `BufferStore.RetransmissionMaxCount = 16`
- 서버 생존 확인 주기 `15초`

예전 문서의 `30ms`, `32ms` 설명은 현재 코드와 맞지 않는다.

---

## 연결 완료와 종료

ACK sequence 0을 받으면:

1. `isConnected = true`
2. `SessionState = Connected`
3. `StartServerAliveCheck()` 시작
4. `OnConnected()` 콜백 큐잉

종료 시에는 `DisconnectAsync()`와 `Cleanup()`이 소켓, 토큰, 보류 저장소, 재전송 저장소를 정리한다.

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
