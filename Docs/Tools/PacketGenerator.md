# 패킷 코드 자동 생성기

`MultiSocketRUDP/Tool/PacketDefine.yml`에서 C++ 서버·클라이언트와 BotTester 코드를 함께 생성한다.
ID는 YAML 목록 순서로 자동 부여하며 사용자가 지정하지 않는다. 기존 ID 1~8은 유지했다.
목록 끝에 추가하면 기존 ID가 유지된다. 중간 삽입·삭제·정렬 시 이후 ID가 바뀌므로 양쪽을 함께 생성·배포해야 한다.
서로 다른 버전의 연결 차단 기능은 이번 작업에 포함하지 않는다.

## 실행

Python 3.10 이상에서 저장소 루트 기준으로 실행한다.

```powershell
python -m pip install -r MultiSocketRUDP/Tool/PacketGenerator/requirements.txt
./MultiSocketRUDP/Tool/PacketGenerate.bat
./MultiSocketRUDP/Tool/PacketGenerate.bat --check
python -m unittest discover -s MultiSocketRUDP/Tool/PacketGenerator -p 'test_*.py'
python -m unittest discover -s MultiSocketRUDP/Tool/PacketGenerator/tests -p 'test_*.py'
```

생성기는 자기 위치에서 저장소 경로를 찾는다. `--check`는 파일을 수정하지 않고 생성 결과가 최신인지 확인한다.
배치 파일은 인자와 종료 코드를 전달하며 인자가 없을 때만 대기한다. 기존 `nopause` 호출도 지원한다.
`PacketGenerateAndUploader.bat`는 생성 실패 시 업로드를 진행하지 않는다.

## 정의

```yaml
Packet:
  - Type: RequestPacket
    PacketName: InventoryReq
    Items:
      - Type: uint32_t
        Name: count
  - Type: ReplyPacket
    PacketName: InventoryRes
    Items:
      - Type: std::string
        Name: message
```

`RequestPacket`은 서버 수신, `ReplyPacket`은 BotTester 수신 방향이다. 필드가 없으면 `Items`를 생략한다.

| C++ 타입 | BotTester FieldType | 전송 형식 |
|---|---|---|
| `BYTE`, `uint8_t` | Byte | 1바이트 unsigned |
| `unsigned short`, `uint16_t` | Ushort | 2바이트 little-endian |
| `int`, `int32_t` | Int | 4바이트 signed little-endian |
| `unsigned int`, `uint32_t` | Uint | 4바이트 unsigned little-endian |
| `uint64_t`, `unsigned long long` | Ulong | 8바이트 unsigned little-endian |
| `std::string` | String | 2바이트 바이트 수 + UTF-8 데이터 |

추가로 부호 있는 8/16/64비트 정수, bool, float, double, std::wstring과
PacketSchema.py에 정의된 Windows 정수 별칭을 지원한다. C++ std::string에는 UTF-8 바이트를 넣어야 한다.
Windows/MSVC 형식을 기준으로 long은 32비트, wchar_t는 16비트, long double은 64비트다.
이름은 양쪽 언어에서 유효한 식별자로 작성한다.

## 구조체·컨테이너

main의 전체 스키마 검증, 구조체 의존성 정렬, NetBufferCodec 생성과 안전한 역직렬화를 유지한다.
전방 참조는 허용하며 순환 참조, 미정의 타입, 중복·예약 이름은 출력 변경 전에 거부한다.
빈 Structs/Packet 목록도 스키마상 유효하지만, 삭제된 요청의 수동 핸들러는 먼저 정리해야 한다.
Unique는 기존 C++ 팩토리 등록 의미를 유지하며 자동 응답 핸들러를 만들지 않는다.

```yaml
Structs:
  - Name: Position
    Items:
      - Type: float
        Name: x
      - Type: float
        Name: y
Packet:
  - Type: RequestPacket
    PacketName: PositionsReq
    Items:
      - Type: std::vector<Position>
        Name: positions
```

BotTester의 Send Packet 필드에는 구조체를 JSON 객체, 컨테이너를 JSON 배열로 입력한다.
위 positions의 입력 예시는 `[{"x":1.25,"y":-2.5}]`다.
구조체의 생략한 필드는 기본값을 사용하며, 알 수 없는 멤버와 중복 멤버는 거부한다.
map은 `[[key,value], ...]` 형식이며 값에 구조체나 컨테이너를 중첩할 수 있다.

| 타입 | 전송 형식 |
|---|---|
| 구조체 | YAML 필드 순서대로 재귀 직렬화, 패딩 없음 |
| vector | uint32 개수 + 원소 |
| list | Windows x64 size_t(uint64) 개수 + 원소 |
| set / map | 정렬 방향 1바이트(less=0, greater=1) + uint32 개수 + 원소 또는 key/value |
| unordered_set / unordered_map | uint32 개수 + 원소 또는 key/value, 순서 계약 없음 |
| std::wstring | uint16 바이트 길이 + UTF-16LE |

BotTester의 list 상호운용은 x64 서버를 대상으로 한다. 정렬 컨테이너는 less/greater에 맞게 정렬하며,
set의 중복 원소와 map의 중복 키는 C++ 수신 규칙에 따라 거부한다. 컨테이너 개수는 16,384개 이하로 제한한다.
복합 패킷은 16 KiB 버퍼 한도 안에서 생성하며 실제 전송 시 헤더·인증 태그 공간도 필요하다.
기존 기본형 패킷의 256바이트 초기 버퍼는 유지한다.
디스크립터는 초기화 후 읽기 전용으로 사용하며 직렬화 상태·JSON 값은 호출별로 처리한다.

C++ 생성기는 ReadValue/WriteValue를 사용하고 모든 필드를 임시 객체에 읽은 뒤 대상에 반영한다.
읽기 실패 시 부분적으로 갱신된 객체를 노출하지 않는다. 읽기 커서는 복구하지 않으므로 실패 패킷은 폐기한다.
BotTester의 자동 지원은 스키마 기반 송신 직렬화이며, 응답 처리는 아래 수동 partial 핸들러에서 구현한다.

## 생성 결과와 수동 코드

| 대상 | 갱신 방식 |
|---|---|
| C++ 양쪽 `PacketIdType.h`, `Protocol.h`, `Protocol.cpp` | 전체 재생성 |
| 서버 `PlayerPacketHandlerRegister.cpp` | 요청 팩토리 등록 재생성 |
| 서버 `PacketHandlerRegister.cpp` | 실제 Player 핸들러 연결 재생성 |
| 서버 `Player.h`, `PlayerPacketHandler.cpp` | 없는 선언·구현 골격만 추가 |
| BotTester `Generated/PacketId.g.cs` | 순서 기반 enum |
| BotTester `Generated/PacketSchema.g.cs` | 필드 이름·타입·순서·기본값 |
| BotTester `Generated/PacketRegister.g.cs` | 응답 핸들러 연결 |
| BotTester `Generated/PacketHandlers.g.cs` | 응답별 partial 핸들러 |
| 양쪽 `GeneratedPacketPayloadTest(s)` | ID·전송 바이트 검사 |

서버의 새 요청은 생성된 `On<PacketName>` 본문을 구현한다. 기존 선언의 인자 타입으로 핸들러를 찾아
이름과 본문을 보존한다. `ChannelEchoReq`는 기존 `OnChannelEcho`에 연결된다.
요청 삭제·개명 후 수동 선언이 남으면 생성이 실패한다. 선언과 구현을 직접 정리하거나 이전한 뒤 재생성한다.

BotTester 송신은 기존 스키마 기반 노드가 담당한다. 새 응답 처리는 생성 파일 대신 별도 파일에서 구현한다.

```csharp
using MultiSocketRUDPBotTester.Buffer;
namespace MultiSocketRUDPBotTester.Contents.Client.Action;

public partial class InventoryResHandler
{
    partial void OnPacket(NetBuffer buffer)
    {
        // 후속 대기자·그래프를 위해 버퍼 읽기 위치를 보존한다.
    }
}
```

기본 핸들러는 버퍼를 읽지 않는다. 기존 `PacketRegister.cs`의 수동 override는 자동 등록 후 적용되므로
PongAction 등의 기존 처리를 유지한다. 해당 기존 패킷에서는 수동 override가 우선한다.
사전 초기화는 Client 생성 시에만 수행하며 런타임 교체 기능은 추가하지 않는다.

## CI와 검증

프로토콜 관련 경로 변경 시 생성 결과 검사와 C++·BotTester 검사를 모두 실행한다.
생성 누락·불일치 또는 한쪽 실패도 PR 필수 체크 `build-and-test`를 실패시킨다.
네트워크 구현·콘텐츠 서버는 누락 방지를 위해 보수적으로 포함하고 UI만 변경하면 기존 분류를 유지한다.

모든 출력 준비 후 임시 파일을 교체하며 교체 오류 시 이미 교체한 파일을 복원한다.
강제 종료·전원 장애까지 여러 파일의 원자적 갱신을 보장하지는 않는다. 이 경우 재생성 후 `--check`로 확인한다.
회귀 검증은 추가·변경·삭제·잘못된 정의·수동 코드 보존·재실행 무변경·교체 실패 복원을 확인한다.
main의 구조체 fixture도 별도 회귀 테스트로 유지한다. 동일 fixture를 이용한 C++/C# 바이트 검사는
중첩 구조체, vector/list/set/map 및 unordered 컨테이너를 검증한다.
unordered 컨테이너의 정확한 바이트 비교에는 순서가 결정적인 단일 원소를 사용한다.

C++ 실제 패킷 클래스의 직렬화·역직렬화와 C# 실제 송신 노드의 직렬화를 같은 예상 바이트와 비교한다.
같은 타입의 필드에도 서로 다른 값을 사용하고 UTF-8 문자열을 포함한다. 대표 입력 검사이며 모든 값·길이·
구버전 호환성을 보장하지는 않는다. 암호화·헤더의 기존 interop vector 검사도 유지한다.

관련 문서: [CI 가이드](../Testing/CI.md), [업로더](PacketUploader.md).
