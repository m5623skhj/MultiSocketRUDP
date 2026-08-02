# 유닛 테스트

> C++ 서버 코어와 C# BotTester의 빠르고 격리된 검증 경로를 정리한다.

---

## C++ CoreTest

프로젝트는 `MultiSocketRUDP/CoreTest/CoreTest.vcxproj`이며 GoogleTest를 사용한다. 패킷 처리, 암호화, 흐름 제어, RIO handler, 세션 상태·수명, 재전송 scheduler, timer 같은 서버 코어 계약을 검증한다.

```powershell
msbuild .\MultiSocketRUDP\MultiSocketRUDP.sln /t:CoreTest /p:Configuration=Debug /p:Platform=x64
.\MultiSocketRUDP\x64\Debug\CoreTest.exe
```

단일 suite나 test만 확인할 때는 GoogleTest filter를 사용한다.

```powershell
.\MultiSocketRUDP\x64\Debug\CoreTest.exe --gtest_filter=RUDPSessionManagerTest.*
```

### 테스트 작성 기준

- public 결과뿐 아니라 상태 전이와 실패 후 정리 상태를 검증한다.
- lock-free 또는 ref-count 경로는 중복 해제, stale entry, 잘못된 generation을 포함한다.
- RIO와 socket 의존 코드는 실패 시 context, I/O mode, 등록 버퍼가 원복되는지 확인한다.
- 패킷·암호화 테스트는 실제 helper 경로를 사용하고 임의의 대체 구현으로 계약을 재작성하지 않는다.

---

## C# BotTester 유닛 테스트

프로젝트는 `MultiSocketRUDPBotTester/MultiSocketRUDPBotTester.UnitTests/MultiSocketRUDPBotTester.UnitTests.csproj`이며 xUnit을 사용한다.

```powershell
dotnet test .\MultiSocketRUDPBotTester\MultiSocketRUDPBotTester.UnitTests\MultiSocketRUDPBotTester.UnitTests.csproj --configuration Debug
```

주요 검증 범위는 `BufferStore`, 패킷 직렬화·암호화, graph 검증, runtime 통계, trigger 조건, AI 응답 parsing, packet schema, 손실 시뮬레이터다.

WPF 타입을 참조하므로 target framework와 `UseWPF` 설정을 임의로 제거하지 않는다. static registry나 schema를 변경하는 테스트는 원래 상태를 복원해야 하며, 병렬 실행 설정을 바꿀 때는 공유 상태의 thread-safety를 먼저 검토한다.

---

## 새 테스트 파일 추가 시

저장소의 광범위한 `*.cpp` ignore 규칙 때문에 새 C++ 테스트 파일이 자동 추적되지 않을 수 있다.

```powershell
git status --short --ignored -- <path>
git ls-files -- <path>
```

프로젝트 파일에는 들어 있지만 원격 커밋에 소스가 없으면 빌드에서 `error C1083: Cannot open source file`이 발생한다.

---

## 더 자세히 보기

- [테스트 상세 레퍼런스 — 유닛 테스트](TestingReference.md#유닛-테스트)
- [테스트 상세 레퍼런스 — BotTester 유닛 테스트](TestingReference.md#bottester-유닛-테스트)

