# RTT Benchmark 자동화

## 비신뢰성·혼합 채널 측정

기존 PR 갱신·main push·수동 실행 RTT workflow에서 다음 시나리오도 순차 실행합니다.

| 시나리오 | 10ms 대기 간격마다 전송하는 요청 |
| --- | --- |
| `unreliable-only` | 비신뢰성 10개 |
| `reliable-baseline` | 신뢰성 1개 |
| `mixed` | 비신뢰성 10개 + 신뢰성 1개 |

기본 측정은 100개 단위 워밍업과 1,000개 단위 본 측정을 각 3회 실행합니다. 신뢰성 요청은 10개 단위당 1개이므로 본 측정에서 100개입니다. OS 타이머와 송신 작업 시간에 따라 실제 송신 빈도는 달라지며 고정 초당 부하를 보장하지 않습니다. 각 phase 후 1초간 응답을 기다리고 각 요청의 RTT가 1초를 넘으면 늦은 응답으로 분류합니다. Run별 실제 경과 시간도 JSON에 기록합니다.

전용 콘텐츠 패킷 7/8의 본문은 `uint64 requestId`와 `byte unreliable`입니다. 서버가 ID와 선택값을 그대로 돌려주며, 비신뢰성 요청은 응답도 비신뢰성으로 전송합니다. 워밍업과 본 측정의 ID를 재사용하지 않습니다. RTT 시작점은 송신 API 호출 직전, 끝점은 인증·순서 검사 후 수신 처리 시점이며 송신 큐 대기를 포함합니다.

결과는 채널별 요청 수·수용 수·수신 수·늦은 응답 수·응답률·초당 응답 수·p50/p95/p99입니다. 응답률의 분모는 시도한 전체 요청이며, 미수신에는 네트워크 유실·오래된 번호 제거·큐 초과 제거가 모두 포함됩니다. 미수신 요청은 RTT 표본에 넣지 않고 표본이 없으면 RTT를 `null`로 기록합니다. 초당 응답 수는 마지막 1초 대기까지 포함한 전체 측정 시간을 분모로 사용합니다.

`channel-*.json` 원본과 `channel-summary.md`를 artifact로 올리고 PR 코멘트·Job Summary에 RTT와 응답률을 함께 표시합니다. 동일 조건의 이전 공식 커밋과 p95 및 응답률 변화량을 비교하며, 혼합 전송의 신뢰성 p95를 신뢰성 단독 측정과도 비교합니다. main 결과는 `benchmark-data`의 `channel-history.json`에 최근 150개 시나리오 결과를 저장합니다. 최초 측정이나 조건이 다르면 비교값은 N/A입니다.

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
| 실행 제한 | warmup과 각 run은 최대 300초, workflow는 최대 45분 |

두 유실 시나리오는 같은 러너에서 순차 실행합니다. 병렬 실행은 CPU 경합으로 RTT를 왜곡하고 singleton BotTester 상태를 공유할 수 있으므로 사용하지 않습니다.
각 warmup/run의 시작과 완료, P95/P99, 경과 시간, 초당 처리량은 콘솔에 출력되어 CI에서 진행 상태를 확인할 수 있습니다.

## GitHub Actions 동작

- PR에서는 측정 결과와 직전 공식 측정 대비 P95/P99 변화율을 PR 코멘트와 Job Summary에 출력합니다.
- `main` push에서는 측정을 다시 수행한 뒤 `benchmark-data` 브랜치의 `rtt-history.json`을 자동 갱신합니다.
- 측정 대상 commit의 제목을 자동 기록하고, 그래프 하단과 `benchmark-data` README에 최근 10회의 날짜·commit·P95/P99 RTT(ms)를 표시합니다.
- 같은 commit을 다시 측정하면 중복 행을 추가하지 않고 해당 commit 결과를 교체합니다.
- 전체 JSON 이력은 보존하고 SVG에는 최근 10회만 표시합니다.
- 수동 `workflow_dispatch` 실행은 진단용이며 공식 이력을 변경하지 않습니다.

워크플로가 `GITHUB_TOKEN`으로 만든 `benchmark-data` push는 새로운 `main` push 실행을 만들지 않습니다. 이력 갱신 job은 `contents: write`만 사용하고, PR 측정 job은 저장소 읽기 권한으로 분리되어 있습니다.

## 결과 파일

| 파일 | 내용 |
| --- | --- |
| `rtt-loss-0.json` | 유실률 0% 개별 run과 집계값 |
| `rtt-loss-10.json` | TX/RX 유실률 각각 10% 개별 run과 집계값 |
| `rtt-history.json` | commit 제목을 포함한 commit별 전체 공식 이력 |
| `rtt-loss-0.svg` | 최근 10회 P95/P99 추세와 측정 상세 표 |
| `rtt-loss-10.svg` | 최근 10회 P95/P99 추세와 측정 상세 표 |

양수 변화율은 RTT 증가를 의미합니다. `Max`는 OS 스케줄링 노이즈에 민감하므로 그래프와 초기 실패 판정에는 사용하지 않고 결과 표의 참고값으로만 보존합니다.
