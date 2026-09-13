# AddressSanitizer 검사

`AddressSanitizer` workflow는 매일 한국 시간 04:30에 기본 브랜치에서 실행하며,
Actions의 `Run workflow`로도 실행할 수 있다. 예약 실행은 지연될 수 있다.
초기에는 PR 필수 검사와 분리해 운영한다.

## 로컬 실행

Visual Studio C++ 도구와 MSVC AddressSanitizer 구성 요소, 저장소의 재귀 submodule이 필요하다.
프로젝트의 현재 `v145` 도구 집합을 설치한 환경에서, 저장소 루트의 PowerShell 7로 실행한다.

```powershell
./Scripts/ASan/Invoke-ASan.ps1
```

스크립트는 Visual Studio 개발 환경을 초기화하고 매번 `tmp/asan/<실행 ID>/`에 새로 빌드한다.
일반 Debug/Release 산출물과 GTest 캐시를 재사용하거나 덮어쓰지 않는다.
제품 코드와 일반 프로젝트 빌드 설정을 수정하지 않으며, ASan 전용 targets를 빌드 인자로 주입한다.

## 검사 범위

- Debug x64 CoreTest 및 solution 의존 대상인 서버·클라이언트·Logger 라이브러리를 계측한다.
  실제 검사 범위는 CoreTest 실행 시 링크되고 실행되는 코드다.
- GTest도 동일한 MSVC compiler, `/MTd`, `/fsanitize=address`로 별도 컴파일한다.
- `/RTC` 검사와 증분 링크를 해제하고 `/Zi` 디버그 정보를 사용한다.
- 링크 시 전용 출력 디렉터리에서 프로젝트 라이브러리를 찾도록 제한한다.
- 의도적인 heap-buffer-overflow를 발생시키는 `DetectionProbe`가 비정상 종료하고
  정확한 ASan 진단을 출력해야 검출 확인에 성공한다. 이 오류는 검증용으로 예상된 오류다.
- 이어서 CoreTest를 실행한다. ASan 진단, 비정상 종료, timeout, XML 누락·손상,
  테스트 0건, skip 또는 assertion 실패는 workflow 실패로 처리한다. 재시도하지 않는다.

검출 확인은 최대 30초, CoreTest는 종료까지 최대 15분, 전체 CI job은 최대 45분이다.
ASan 계측으로 실행 시간과 메모리 사용량이 늘어나므로 RTT 벤치마크와 결합하지 않는다.

## 결과와 한계

실행 디렉터리의 `build.log`, `probe.*.log`, `CoreTest.*.log`, `CoreTest.xml`,
`environment.txt`, `summary.md`를 확인한다. CI에서는 해당 진단 파일을 14일간 artifact로 보존한다.
CoreTest가 중간에 중단되면 XML이 생성되지 않을 수 있으므로 stderr의 ASan 스택을 먼저 확인한다.
job 자체가 강제 취소되면 로그나 artifact 저장이 완료되지 않을 수 있다.

ASan은 데이터 경쟁이나 모든 메모리 누수를 검사하는 도구가 아니다.
커스텀 메모리 풀 내부의 객체 반환은 실제 heap 해제와 다르므로 풀 내부의 반환 후 접근이나
하위 할당 경계 침범을 놓칠 수 있다. 이번 도입은 풀을 우회하거나 별도 poison/unpoison 계측을
추가하지 않는다. 기존 멀티스레드 반복 검사는 별도로 유지한다.

참고: [MSVC ASan 제약](https://learn.microsoft.com/en-us/cpp/sanitizers/asan-known-issues),
[커스텀 할당기와 ASan](https://learn.microsoft.com/en-us/cpp/sanitizers/asan-runtime).
