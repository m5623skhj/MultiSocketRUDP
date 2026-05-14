# RTT Test Comparison Checklist

## 목적

이 문서는 `ENet`, `SLikeNet`, `GameNetworkingSockets`, `MultiSocketRUDP`의 현재 RTT/TPS 테스트가 얼마나 같은 조건인지 비교하고, `MultiSocketRUDP`를 비교군과 공정하게 맞추기 위해 무엇을 제거하거나 유지해야 하는지 정리한다.

현재 `MultiSocketRUDP` 수치는 `raw transport RTT`라기보다 `bot/action graph + async wait + RUDP control path + server worker handoff`까지 포함한 `closed-loop RTT`로 해석해야 한다.

또한 현재와 비교군의 기본 루프는 모두 `stop-and-wait` 계열이므로, 이 결과만으로 `최대 처리량`을 직접 결론내리면 안 된다.

## 비교 표

| 항목 | ENet | SLikeNet | GameNetworkingSockets | MultiSocketRUDP |
|---|---|---|---|---|
| 기준 파일 | [`enet_rtt.cpp`](C:/Users/KimHyeongJin/Desktop/비교군/enet-1.3.18/examples/enet_rtt.cpp) | [`Ping.cpp`](C:/Users/KimHyeongJin/Desktop/비교군/SLikeNet-master/SLikeNet-master/Samples/Ping/Ping.cpp) | [`example_rtt.cpp`](C:/Users/KimHyeongJin/Desktop/비교군/GameNetworkingSockets-master/GameNetworkingSockets-master/examples/example_rtt.cpp) | [`BotActionGraph.botgraph.json`](C:/Users/KimHyeongJin/source/repos/MultiSocketRUDP/MultiSocketRUDPBotTester/MultiSocketRUDPBotTester/bin/BotActionGraph.botgraph.json), [`Client.cs`](C:/Users/KimHyeongJin/source/repos/MultiSocketRUDP/MultiSocketRUDPBotTester/MultiSocketRUDPBotTester/Contents/Client/Client.cs), [`RUDPSession.cpp`](C:/Users/KimHyeongJin/source/repos/MultiSocketRUDP/MultiSocketRUDP/MultiSocketRUDPServer/RUDPSession.cpp) |
| 측정 목표 | 라이브러리 raw RTT에 가깝게 단순화 | 라이브러리 raw RTT에 가깝게 단순화 | 라이브러리 RTT 비교, 단 내부 처리 포함 | bot + 앱 프로토콜 + 서버 구조 포함 closed-loop |
| 기본 루프 구조 | `Ping -> Pong -> next Ping` | `Ping -> Pong -> next Ping` | `Ping -> Pong -> next Ping` | `SendPacketNode(Ping) -> WaitForPacketNode(Pong)` |
| 클라이언트 루프 방식 | 코드에서 직접 다음 ping 전송 | 코드에서 직접 다음 ping 전송 | 코드에서 직접 다음 ping 전송 | action graph가 노드 체인 실행 |
| 서버 응답 방식 | 수신 즉시 동일 payload echo | 수신 즉시 pong 전송 | 수신 즉시 pong 전송 후 flush | 수신 처리 후 `Pong` 전송 |
| 클라이언트 대기 방식 | 라이브러리 event loop | `Receive()` polling loop | `ReceiveMessagesOnConnection()` | `TaskCompletionSource` 기반 `WaitForNextPacketAsync` |
| 클라이언트 수신 후처리 | 거의 없음 | 거의 없음 | 거의 없음 | 패킷 핸들러 실행 + waiter 완료 + trigger dispatch |
| bot / graph 런타임 | 없음 | 없음 | 없음 | 있음 |
| 클라이언트 내부 큐/채널 | 거의 없음 | 거의 없음 | 거의 없음 | `ReceiveAsync -> Channel<Action> -> PacketProcessorAsync` |
| 서버 스레드 handoff | 비교적 단순 | 비교적 단순 | 비교적 단순 | `IO worker -> recv logic worker` 분리 |
| 앱 레벨 별도 ACK | 없음 | 없음 | 없음 | 있음, `SEND_REPLY_TYPE` |
| reliable 처리 | ENet reliable packet | `IMMEDIATE_PRIORITY + RELIABLE` | `ReliableNoNagle` | 자체 RUDP reliable/ordering/flow control |
| flush 제어 | `enet_host_flush()` | 라이브러리 send path | `FlushMessagesOnConnection()` 명시 | 내부 send queue / pending queue / send completion |
| Nagle/지연 전송 영향 | 작음 | priority 설정 영향 큼 | 기본값 영향 큼, 튜닝 필요 | 내부 큐/ACK/worker hop 영향 큼 |
| 암복호화 | 기본 없음 | 기본 없음 | 기본 포함 | 현재 포함 |
| 패킷 1회 왕복의 실제 두께 | 얇음 | 얇음 | 중간 | 두꺼움 |
| 측정값 해석 | raw RTT에 가까움 | raw RTT에 가까움 | 암복호화 포함 RTT | 애플리케이션 포함 end-to-end RTT |
| 현재 수치와 직접 비교 공정성 | 높음 | 높음 | 부분적으로 높음 | 낮음, 조건이 다름 |

## 공정 비교 체크리스트

아래 체크리스트는 `MultiSocketRUDP`를 `ENet / SLikeNet / GameNetworkingSockets` RTT 예제와 최대한 같은 수준으로 맞추기 위한 기준이다.

### 1. RTT 마이크로벤치로 맞출 때

- [ ] bot action graph를 우회하고, 전용 테스트 루프로 `Send Ping -> Wait Pong -> repeat`만 수행한다.
- [ ] `TaskCompletionSource`, trigger dispatch, node execution 비용을 측정 경로에서 제거한다.
- [ ] 클라이언트 수신 후처리를 최소화하고, `pong` 수신 직후 바로 다음 `ping`을 보낸다.
- [ ] 서버도 contents 레이어 외 추가 후처리가 없는지 확인한다.
- [ ] 서버 응답 직후 send path가 즉시 flush되는지 확인한다.
- [ ] 측정값은 `elapsed / sample_count` 기준 평균 RTT로 통일한다.
- [ ] 평균 RTT에서 `TPS = 1000 / RTT(ms)`를 계산하되, 이것이 `single outstanding request` 기준임을 명시한다.

### 2. 비교군과 다른 요소를 인정하고 유지할 때

- [ ] `bot/action graph` 비용이 포함된 테스트인지 명시한다.
- [ ] 클라이언트 `Channel<Action>` 경유 비용이 포함된 테스트인지 명시한다.
- [ ] 서버 `IO worker -> recv logic worker` handoff 비용이 포함된 테스트인지 명시한다.
- [ ] `SEND_REPLY_TYPE` ACK 패킷 비용이 포함된 테스트인지 명시한다.
- [ ] 암복호화 포함 여부를 라이브러리별로 분리 기재한다.
- [ ] 이 경우 결과 이름을 `raw RTT`가 아니라 `closed-loop RTT` 또는 `end-to-end RTT`로 표기한다.

### 3. 최대 처리량을 보려면 별도 테스트로 분리할 때

- [ ] `stop-and-wait` 테스트와 `windowed in-flight` 테스트를 분리한다.
- [ ] `in-flight N개 유지` 모델을 사용한다.
- [ ] `서버 수신 TPS`, `클라이언트 완료 TPS`, `평균 RTT`, `queue depth`를 분리 측정한다.
- [ ] ACK 지연, pending queue, flow control, retransmission 영향이 지표에 어떻게 반영되는지 분리해서 본다.
- [ ] 이 결과를 `최대 처리량` 해석에 사용하고, `stop-and-wait` 결과와 혼용하지 않는다.

## MultiSocketRUDP 현재 테스트 해석 가이드

현재 `MultiSocketRUDP`의 `5500~7000 TPS`는 비교군보다 낮게 나와도 이상하지 않다. 그 이유는 다음 요소가 동시에 포함되기 때문이다.

1. bot graph 실행 비용
2. 클라이언트 비동기 대기 비용
3. 클라이언트 수신 큐 비용
4. 서버 스레드 handoff 비용
5. 앱/프로토콜 ACK 비용
6. 암복호화 비용

즉, 현재 수치는 `서버 라이브러리 코어만의 한계`라기보다 `현재 테스트 경로 전체의 closed-loop 비용`으로 해석하는 것이 맞다.

## 권장 분리 기준

실무적으로는 아래 세 가지를 분리해서 관리하는 것이 가장 깔끔하다.

1. `RTT 마이크로벤치`
   - `ENet / SLikeNet / GameNetworkingSockets` 예제와 같은 수준으로 단순화한 테스트
2. `실사용 closed-loop`
   - 현재처럼 `BotActionGraph`를 포함한 통합 테스트
3. `최대 처리량`
   - `in-flight N개 유지` 기반 테스트

이 세 결과를 한 표에 넣더라도 해석 열을 따로 두는 것이 좋다. 특히 멀티스레드 환경에서는 worker handoff, queue depth, wakeup latency가 RTT에 직접 섞일 수 있으므로, `raw RTT`와 `integration RTT`를 같은 의미로 다루면 해석 오류가 생긴다.

---

## 최신 RTT 기준선

최신 `MultiSocketRUDP` 전용 RTT 테스트는 `bot 1`, `localhost`, `Release / O2`, `100,000 samples` 기준으로 반복 측정했다.

### 측정 결과

| 회차 | Avg RTT | Min RTT | Max RTT | Elapsed |
|---|---:|---:|---:|---:|
| 1 | 0.164 ms | 0.028 ms | 104.142 ms | 16.453 s |
| 2 | 0.101 ms | 0.029 ms | 88.137 ms | 10.167 s |
| 3 | 0.105 ms | 0.030 ms | 117.623 ms | 10.633 s |
| 4 | 0.128 ms | 0.029 ms | 101.216 ms | 12.849 s |

### 해석 기준

- 1회차는 cold start / JIT / thread warm-up 영향이 섞인 outlier로 본다.
- 2~4회차 평균을 실제 기준선으로 보는 편이 더 적절하다.

### 현재 추천 기준선

- warm runs 기준 평균 RTT: 약 `0.111 ms`
- warm runs 기준 elapsed 평균: 약 `11.216 s`
- 환산 TPS: 약 `8,916`

### 비교군과의 최신 비교

| 라이브러리 | Avg RTT | 비고 |
|---|---:|---|
| ENet | 0.040 ms | raw RTT에 가장 가까운 단순 예제 |
| SLikeNet | 0.053 ms | `IMMEDIATE_PRIORITY + RELIABLE` 기준 |
| GameNetworkingSockets | 0.616 ms | crypto와 send tuning 포함 |
| MultiSocketRUDP | 0.111 ms | bot graph 제거 RTT, client fast path 일부 반영 |

따라서 현재 `MultiSocketRUDP`는 최신 RTT 전용 경로 기준으로 `ENet / SLikeNet`보다는 느리지만, `GameNetworkingSockets`보다는 빠른 위치에 있다.
