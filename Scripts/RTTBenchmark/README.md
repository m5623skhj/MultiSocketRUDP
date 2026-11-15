# RTT Benchmark 자동화

## 비신뢰성·혼합 채널 측정

기존 PR 갱신·main push·수동 실행 RTT workflow에서 다음 시나리오도 순차 실행합니다.

| 시나리오 | 요청 방식 |
| --- | --- |
| `unreliable-only` | 응답 또는 요청별 타임아웃 후 즉시 다음 1건 |
| `reliable-baseline` | 응답 후 즉시 다음 1건, 타임아웃은 측정 실패 |
| `mixed` | 비신뢰성 10개 + 신뢰성 1개 전송 후 10ms 대기 |

단독 측정은 양쪽 모두 워밍업 100건·본 측정 1,000건을 각 3회 실행합니다. 요청별 제한 시간은 송신 API 호출 직전부터 1초이며, 응답 수신 시 고정 대기 없이 다음 요청을 보냅니다. 비신뢰 타임아웃은 `timedOut`으로 기록하고 다음 요청으로 진행합니다. 늦은 응답은 RTT 표본이나 다음 요청의 완료로 사용하지 않습니다. 신뢰 타임아웃은 기존 Ping/Pong처럼 측정 실패입니다. 취소와 run 전체 제한 시간(기본 60초)은 그대로 적용됩니다.

혼합 측정은 기존과 같이 비신뢰성 1,000건과 신뢰성 100건이며, 각 phase 후 1초간 응답을 기다립니다. 단독 측정에는 이 마지막 대기가 없습니다. Run별 실제 경과 시간도 JSON에 기록합니다.

단독 측정은 schemaVersion 3이며, 혼합 측정은 버전 1을 유지합니다. `channel-comparison.svg`는 같은 커밋·조건의 버전 3 단독 결과를 짝지어 동일한 µs 축에서 신뢰·비신뢰 P95/P99와 응답률을 비교합니다. 실제 시도 수가 양쪽 모두 SampleCount와 일치해야 표시합니다. 새 버전의 결과가 없으면 대기 안내를 표시하며, 버전 1·2와 기존 Ping/Pong 결과를 새 추세선에 섞지 않습니다. 기존 이력은 최근 150개 결과 보존 정책 내에서 유지합니다.

전용 콘텐츠 패킷 7/8의 본문은 `uint64 requestId`와 `byte unreliable`입니다. 서버가 ID와 선택값을 그대로 돌려주며, 비신뢰성 요청은 응답도 비신뢰성으로 전송합니다. 워밍업과 본 측정의 ID를 재사용하지 않습니다. RTT 시작점은 송신 API 호출 직전, 끝점은 인증·순서 검사 후 수신 처리 시점이며 송신 큐 대기를 포함합니다.

결과는 채널별 요청 수·수용 수·수신 수·늦은 응답 수·타임아웃 수·응답률·초당 응답 수·p50/p95/p99입니다. 응답률의 분모는 시도한 전체 요청이며, 미수신에는 네트워크 유실·오래된 번호 제거·큐 초과 제거가 모두 포함됩니다. 미수신 요청은 RTT 표본에 넣지 않고 표본이 없으면 RTT를 `null`로 기록합니다. 초당 응답 수는 타임아웃 대기를 포함한 전체 측정 시간을 분모로 사용합니다. 혼합 측정에는 마지막 1초 대기도 포함됩니다.

요약 표의 `Timeouts`는 버전 3 단독 측정의 run별 `timedOut` 중앙값이며, 기존 버전과 혼합 측정에는 `N/A`를 표시합니다. 타임아웃 후 늦게 도착한 응답은 `late`에도 기록될 수 있으므로 두 값을 더해 유실 수로 해석하지 않습니다. 늦은 응답은 해당 phase가 관측한 범위에서만 기록됩니다. 비신뢰 타임아웃은 다음 요청으로 진행하지만 run 전체 제한 시간을 초과하거나 사용한 채널의 응답이 전무하면 측정은 실패합니다.

양쪽은 동일한 순차 요청 방식을 사용하지만 실제 요청률을 고정하지는 않습니다. 응답과 타임아웃이 다음 송신 시점을 결정하므로, 최대 처리량이나 같은 초당 부하에서의 성능 우위는 별도 측정이 필요합니다.

`channel-*.json` 원본과 `channel-summary.md`를 artifact로 올리고 PR 코멘트·Job Summary에 RTT와 응답률을 함께 표시합니다. 동일 버전·조건의 이전 공식 커밋과 p95 및 응답률 변화량을 비교합니다. 버전 3 단독 측정과 버전 1 혼합 측정은 부하 방식이 달라 직접 증감률을 계산하지 않습니다. main 결과는 `benchmark-data`의 `channel-history.json`에 최근 150개 시나리오 결과를 저장합니다. 최초 측정이나 조건이 다르면 비교값은 N/A입니다.

RTT 증가나 일부 비신뢰성 유실만으로 CI를 실패시키지 않습니다. 연결 실패·제한 시간 초과·연결 종료·송신 수용 실패·사용한 채널의 응답 전무는 실패입니다. 장시간 부하나 인위적 손실 테스트는 이 짧은 커밋 측정에 포함하지 않습니다.

로컬에서 기존 Ping/Pong 시나리오를 건너뛰려면 `Invoke-RttBenchmark.ps1`에 `-ChannelsOnly`를 전달합니다. `-ChannelSamples`와 `-ChannelRuns`로 검증 규모를 줄일 수 있습니다. 결과는 실행 시 지정한 OutputDirectory에 저장합니다.

RTT Benchmark는 `ContentsServer`와 BotTester를 서로 다른 프로세스로 실행하고 루프백 Ping/Pong RTT를 측정합니다.

## 고정 측정 조건

| 항목 | 설정 |
| --- | --- |
| 서버 | MSVC x64 Release, `/O2` |
| IO worker | `/DRUDP_RTT_BENCHMARK_BUILD`로 `NO_USE_IO_WORKER_THREAD_SLEEP_FOR_FRAME` 강제 |
| 서버 thread | 측정 중에만 `THREAD_COUNT=1`로 변경하고 종료 시 원본 설정 복원 |
| BotTester | .NET 9 Release, `Optimize=true` |
| 유실률 0% | 워밍업 500회, 1,000회 × 5 runs |
| 유실률 10% | 워밍업 100회, 1,000회 × 5 runs |
| 유실 모델 | BotTester 송신과 수신에 각각 독립 적용 |
| 대표값 | 각 run 통계의 중앙값 |
| Ping/Pong 실행 제한 | warmup과 각 run은 각각 최대 300초 |
| 채널 실행 제한 | 각 run의 연결·워밍업·본 측정을 합쳐 60초, 요청별 timeout 1초 |
| workflow 실행 제한 | 최대 45분 |

두 유실 시나리오는 같은 러너에서 순차 실행합니다. 병렬 실행은 CPU 경합으로 RTT를 왜곡하고 singleton BotTester 상태를 공유할 수 있으므로 사용하지 않습니다.
Ping/Pong은 각 warmup/run의 시작과 완료, P95/P99, 경과 시간, 초당 처리량을 콘솔에 출력합니다. 채널 측정의 run별 경과 시간과 결과는 `channel-*.json`에서 확인합니다.

## GitHub Actions 동작

- PR에서는 측정 결과와 직전 공식 측정 대비 P95/P99 변화율을 PR 코멘트와 Job Summary에 출력합니다.
- `main` push에서는 측정을 다시 수행한 뒤 `benchmark-data` 브랜치의 `rtt-history.json`을 자동 갱신합니다.
- 측정 대상 commit의 제목을 자동 기록하고, 그래프 하단과 `benchmark-data` README에 최근 10회의 날짜·commit·P95/P99 RTT(ms)를 표시합니다.
- 같은 commit의 Ping/Pong 결과는 교체합니다. 채널 결과는 commit·시나리오·측정 버전이 모두 같은 항목만 교체합니다.
- Ping/Pong JSON은 전체 이력을, 채널 JSON은 최근 150개 시나리오 결과를 보존합니다. 그래프에는 최근 10회 또는 조건이 맞는 최근 10쌍을 표시합니다.
- 수동 `workflow_dispatch` 실행은 진단용이며 공식 이력을 변경하지 않습니다.

워크플로가 `GITHUB_TOKEN`으로 만든 `benchmark-data` push는 새로운 `main` push 실행을 만들지 않습니다. 이력 갱신 job은 `contents: write`만 사용하고, PR 측정 job은 저장소 읽기 권한으로 분리되어 있습니다.

## 결과 파일

채널 SVG는 PR 측정 artifact에도 생성하며, `main` 공식 이력 갱신 시 공개 그래프에 반영합니다. `channel-comparison.svg`는 버전 3의 신뢰·비신뢰 단독 측정을 같은 커밋과 조건으로 짝짓고, 최신 쌍과 조건이 같은 최근 10쌍을 표시합니다. 하단 표는 양쪽 P95/P99와 응답률입니다. `channel-unreliable-only.svg`와 `channel-mixed.svg`는 각 시나리오의 최신 결과와 조건이 같은 최근 10회를 선택하며, 하단에 직전 호환 측정 대비 P95/P99 변화율도 표시합니다.

채널 그래프의 RTT 축·툴팁·하단 표와 `channel-summary.md`의 p50/p95/p99는 µs 단위(소수점 한 자리)로 표시합니다. 원본 JSON의 `p50Ms`·`p95Ms`·`p99Ms`는 ms를 유지하고 표시할 때만 1,000배 변환합니다. 응답률·변화율은 기존 % 단위를 유지하며, 표시 단위 변경으로 측정 버전을 올리거나 이력을 초기화하지 않습니다. RTT와 응답률 모두 run별 값의 중앙값입니다. 응답이 없는 RTT는 `N/A`로 남기고 그래프 선을 연결하지 않습니다. 조건 비교에는 schema·시나리오·샘플/워밍업 수·timeout·서버 스레드 수·OS·프로세서 수·run 수를 사용하며 실제 CPU 모델 차이까지 보장하지는 않습니다.

`channel_history.py --output-dir <directory>`로 SVG를 생성합니다. 채널 JSON은 최근 150개 시나리오 결과를 보존합니다.

| 파일 | 내용 |
| --- | --- |
| `rtt-loss-0.json` | 유실률 0% 개별 run과 집계값 |
| `rtt-loss-10.json` | TX/RX 유실률 각각 10% 개별 run과 집계값 |
| `rtt-history.json` | commit 제목을 포함한 commit별 전체 공식 이력 |
| `rtt-loss-0.svg` | 최근 10회 P95/P99 추세와 측정 상세 표 |
| `rtt-loss-10.svg` | 최근 10회 P95/P99 추세와 측정 상세 표 |
| `channel-comparison.svg` | 동일한 순차 방식의 신뢰·비신뢰 단독 RTT·응답률 비교 |
| `channel-unreliable-only.svg` | 비신뢰성 단독 전송의 RTT·응답률 추이와 변화율 표 |
| `channel-mixed.svg` | 혼합 전송 중 비신뢰성 채널의 RTT·응답률 추이와 변화율 표 |

양수 변화율은 RTT 증가를 의미합니다. `Max`는 OS 스케줄링 노이즈에 민감하므로 그래프와 초기 실패 판정에는 사용하지 않고 결과 표의 참고값으로만 보존합니다.
