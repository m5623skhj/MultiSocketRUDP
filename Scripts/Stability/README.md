# 멀티스레드 안정성 반복 검사

`Native Stability`는 매일 한국 시간 03:30에 기본 브랜치에서 실행하며, Actions의
`Run workflow`에서도 실행할 수 있다. 예약 시각은 실행 보장 시각이 아니다.
PR 필수 검사에 반복 실행을 추가하지 않는다. 새 통합 테스트 자체는 기존 Native GTest에서도 실행된다.

## 검증 범위

- 기존 통합 테스트 설정의 서버 스레드 2개, 소켓 8개를 사용한다.
- `ConcurrentTrafficDisconnectAndSessionReuseAcrossWaves`: 클라이언트 4개가 각자 별도
  worker에서 8개의 echo 응답을 확인하고 연결을 해제한다. 같은 서버에서 풀 크기를
  초과하는 접속 수가 될 때까지 반복한다. 매 wave마다 접속·해제·요청 수, 연결 수 0,
  세션 풀 회수를 검증하고 실제 세션 ID 재사용도 확인한다.
- 세션 풀 순환, 연결 해제, 클라이언트 정상 종료, 유휴 TLS 연결을 가진 서버 종료,
  reliable/unreliable 채널 송수신의 기존 시나리오도 반복한다.

동시성은 각 테스트 내부에서 발생한다. 테스트 케이스끼리는 순차 실행하고, 매번
새 프로세스를 사용해 서버/RIO 및 임시 설정 상태가 다음 케이스에 섞이지 않도록 한다.
같은 작업 폴더에서 다른 IntegrationTest 실행과 동시에 실행하지 않는다.
스레드 스케줄링은 비결정적이므로 같은 명령이 항상 같은 경합을 재현하지는 않는다.
이 검사는 데이터 경쟁이 없음을 증명하거나 장시간 메모리 누수를 측정하는 검사는 아니다.

## 로컬 실행

기존 개발 환경에서 Debug x64 `IntegrationTest` 대상과 개발용 TLS 인증서를 준비한 뒤,
저장소 루트의 PowerShell 7에서 실행한다.

```powershell
./Scripts/Stability/Invoke-StabilityTests.ps1 -Iterations 2
```

기본값은 6개 케이스 × 5회다. 케이스당 프로세스 종료까지 180초, 전체 테스트 실행은
40분으로 제한한다. 전체 예산 안에 모든 케이스를 완료하지 못해도 실패다.
CI job은 빌드·artifact 업로드를 포함해 최대 60분이다.

## 실패 조사

최초 실패에서 중단하며 성공할 때까지 재시도하지 않는다. 종료 코드가 0이어도
XML이 없거나, 다른 케이스가 실행되었거나, 테스트가 skip되었으면 실패한다.
timeout에는 프로세스 트리를 종료한다.

`tmp/stability-results/<실행 ID>/`에 다음을 보존한다. CI에서는 `stability-results/`를
14일 동안 artifact로 보존하며 빌드 로그·테스트 설정·바이너리 해시·submodule revision도 포함한다.

- `results.json`: 완료한 케이스, 최초 실패, 반복 번호, 소요 시간, 종료 코드와 재현 명령
- `environment.json`: 실행 환경, 바이너리 해시, CI commit과 제한 시간
- 케이스별 GTest XML 및 stdout/stderr 로그
- `summary.md`: 수행 수, 실패 이유, 단일 케이스 재실행 명령

실패한 반복의 로그에서 assertion 실패인지, 프로세스 crash인지, 종료 timeout인지 먼저
구분한다. 단일 케이스로 조사한 뒤 반복 검사를 다시 실행한다. 재실행 성공으로 최초 실패를
무시하지 않는다. job 자체가 강제 취소되면 artifact 업로드가 완료되지 않을 수 있다.

실행기의 성공·실패·skip·XML 누락/손상·케이스 불일치·중복·timeout 판정은
`./Scripts/Stability/Test-StabilityRunner.ps1`로 검증한다. 이 검사는 가짜 실행 파일을
컴파일하므로 Windows의 .NET Framework C# compiler가 필요하며, CI에서도 먼저 실행한다.
