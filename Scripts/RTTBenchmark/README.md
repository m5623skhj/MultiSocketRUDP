# RTT Benchmark 자동화

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
- 같은 commit을 다시 측정하면 중복 행을 추가하지 않고 해당 commit 결과를 교체합니다.
- 전체 JSON 이력은 보존하고 SVG에는 최근 10회만 표시합니다.
- 수동 `workflow_dispatch` 실행은 진단용이며 공식 이력을 변경하지 않습니다.

워크플로가 `GITHUB_TOKEN`으로 만든 `benchmark-data` push는 새로운 `main` push 실행을 만들지 않습니다. 이력 갱신 job은 `contents: write`만 사용하고, PR 측정 job은 저장소 읽기 권한으로 분리되어 있습니다.

## 결과 파일

| 파일 | 내용 |
| --- | --- |
| `rtt-loss-0.json` | 유실률 0% 개별 run과 집계값 |
| `rtt-loss-10.json` | TX/RX 유실률 각각 10% 개별 run과 집계값 |
| `rtt-history.json` | commit별 전체 공식 이력 |
| `rtt-loss-0.svg` | 최근 10회 P95/P99 추세 |
| `rtt-loss-10.svg` | 최근 10회 P95/P99 추세 |

양수 변화율은 RTT 증가를 의미합니다. `Max`는 OS 스케줄링 노이즈에 민감하므로 그래프와 초기 실패 판정에는 사용하지 않고 결과 표의 참고값으로만 보존합니다.
