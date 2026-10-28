# NetBuffer 컨테이너 직렬화

> `external/CommonCode/Common/NetBufferContainerTraits.h`, `NetBufferContainerOperators.inl`, `NetServerSerializeBuffer.*`의 현재 직렬화 계약을 정리한다.

## 목차

1. [지원 타입과 전송 형식](#지원-타입과-전송-형식)
2. [순서와 비교 정책](#순서와-비교-정책)
3. [길이·용량 검사와 실패 처리](#길이용량-검사와-실패-처리)
4. [동시성과 검증](#동시성과-검증)

## 지원 타입과 전송 형식

`NetBuffer`는 `vector`, `set`, `map`, `unordered_set`, `unordered_map`, `list`의 `<<` / `>>`를 지원한다. 송신은 변경 가능한 컨테이너와 `const` 컨테이너 모두 가능하며, 수신 대상은 변경 가능해야 한다.

| 컨테이너 | 전송 형식 |
|---|---|
| `vector` | `uint32` 개수 + 순서대로 원소 |
| `set` | 정렬 방향 1바이트 + `uint32` 개수 + 원소 |
| `map` | 정렬 방향 1바이트 + `uint32` 개수 + 키/값 쌍 |
| `unordered_set` | `uint32` 개수 + 원소 |
| `unordered_map` | `uint32` 개수 + 키/값 쌍 |
| `list` | `size_t` 개수 + 순서대로 원소 |

원소와 map의 값에는 산술 타입, `std::string`, `std::wstring`, 컨테이너, `NetBufferCodec<T>`가 정의된 생성 구조체를 사용할 수 있다. 생성기는 map 키와 set 원소를 기본 타입·문자열로 제한한다. `vector<bool>`의 각 원소는 0 또는 1인 1바이트다. 등록되지 않은 구조체·포인터·지원하지 않는 비교자는 컴파일 오류가 발생하며 객체 메모리를 그대로 복사하는 경로로 대체하지 않는다.

숫자는 기존 바이트 순서와 타입 폭을 유지한다. 문자열은 16비트 바이트 길이, 넓은 문자열은 Windows의 UTF-16LE 형식을 사용한다. `list`의 개수는 `size_t`이므로 C# 상호운용은 Windows x64의 8바이트 형식을 기준으로 한다.

모든 `list` 연산자는 문자열·넓은 문자열 목록을 포함해 `NetBufferContainerOperators.inl`에 있다. 변경 가능한 기본 타입·문자열 `list`의 기존 연산자는 수신 대상에 원소를 추가하는 동작을 유지한다. 생성 구조체·중첩 컨테이너의 목록, `WriteValue`/`ReadValue`를 통한 목록은 검사된 재귀 직렬화와 성공 후 대상 교체를 사용한다. 생성 패킷의 필드도 이 재귀 경로를 사용한다.

## 순서와 비교 정책

정렬 방향은 `std::less<Key>`가 0, `std::greater<Key>`가 1이다. 두 비교자만 지원하며 빈 컨테이너라도 송수신 타입의 방향이 같아야 한다. 다른 방향값이나 불일치는 실패하고, 수신 대상의 비교자를 변경하지 않는다.

비정렬 컨테이너는 값만 보존한다. 삽입 순서·순회 순서·버킷 배치는 보존하지 않으므로 같은 논리적 값도 프로세스나 실행마다 다른 바이트 순서로 직렬화될 수 있다. 위치 기반 필드 대응, 결정적 해시, 재현 가능한 출력에 순회 순서를 사용하면 안 된다. 송수신 측의 해시·동등성 정책도 같은 키를 동등하게 취급해야 한다.

## 길이·용량 검사와 실패 처리

컨테이너 송신은 버퍼 한도를 검사하고 필요하면 실제 할당 용량을 확장한다. `BUFFFER_MAX`는 16,384바이트의 저장 용량·개수 검사 기준이며, C++ RUDP의 최종 프레임 한도인 `MAX_WIRE_PACKET_SIZE` 1,024바이트와 다르다. 콘텐츠 본문은 헤더·타입·시퀀스·ID·태그를 제외한 최대 990바이트다.

수신은 과도한 개수, 잘린 입력, 0/1 이외의 불리언, 홀수 바이트 길이의 넓은 문자열, 중복 set 원소·map 키를 거부한다. 신뢰하지 않는 개수를 그대로 메모리 예약에 사용하지 않으며 남은 바이트와 원소의 최소 인코딩 크기를 대조한다. 빈 생성 구조체는 0바이트이므로 개수 상한만 적용한다.

재귀 경로의 검증 실패는 `GetBufferError()`를 읽기 오류 1 또는 쓰기 오류 2로 설정하고 `std::runtime_error`를 던진다. 하위 버퍼 연산의 기존 예외 타입은 유지한다. 수신은 대상의 정책·할당자를 사용하는 임시 컨테이너에 읽은 뒤 성공 시 교체한다. 실패하면 대상 값은 유지하지만 읽기 커서는 복구하지 않는다. 쓰기 실패도 부분 출력을 남길 수 있으므로 실패한 버퍼는 폐기한다.

## 동시성과 검증

직렬화 중 원본 컨테이너 변경, 같은 버퍼 또는 수신 대상의 동시 접근은 호출자가 방지해야 한다. 직렬화 연산 자체는 내부 잠금을 제공하지 않는다.

`MultiSocketRUDP/CoreTest/NetBufferContainerTest.cpp`는 전송 형식과 실패 처리를 검증한다. `NetBufferContainerCompileFail.cpp`는 의도적으로 프로젝트에서 제외된 컴파일 실패 검증 자료다. Visual Studio x64 개발 셸에서 저장소 루트 기준으로 실행하며, 아래 명령은 각각 실패해야 한다.

```powershell
cl /Zs /std:c++20 /EHsc /DUNICODE /D_UNICODE /Iexternal\CommonCode\Common /DNETBUFFER_REJECT_COMPARATOR MultiSocketRUDP\CoreTest\NetBufferContainerCompileFail.cpp
cl /Zs /std:c++20 /EHsc /DUNICODE /D_UNICODE /Iexternal\CommonCode\Common /DNETBUFFER_REJECT_STRUCT MultiSocketRUDP\CoreTest\NetBufferContainerCompileFail.cpp
cl /Zs /std:c++20 /EHsc /DUNICODE /D_UNICODE /Iexternal\CommonCode\Common /DNETBUFFER_REJECT_CONST_READ MultiSocketRUDP\CoreTest\NetBufferContainerCompileFail.cpp
```

## 관련 문서

- [[PacketGenerator]] — YAML 구조체와 컨테이너 정의
- [[Server/PacketFormat]] — 프레임 크기와 전송 레이아웃
- [[Testing]] — 생성 코드·직렬화 검증
