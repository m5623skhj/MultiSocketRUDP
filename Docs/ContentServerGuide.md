# 콘텐츠 서버 개발 가이드

> 현재 저장소 구조와 실제 엔트리포인트 기준으로 콘텐츠 서버를 붙이는 절차를 정리한다.

---

## 실제 시작점

현재 샘플 서버의 시작점은 `MultiSocketRUDP/ContentsServer/main.cpp`다.

핵심 순서는 아래와 같다.

1. `ContentsPacketRegister::Init()`
2. `TestServer::GetInst().Start(L"ServerOptionFile/CoreOption.txt", L"ServerOptionFile/SessionBrokerOption.txt")`
3. 종료 시 `Stop()`

즉 예전 문서에 있던 `ServerOption/`, `PreCompile.*`, `WithGeminiClient/`를 필수 전제로 둘 필요는 없다.

---

## 권장 파일 구성

콘텐츠 서버 구현에 필요한 최소 파일은 아래 정도면 충분하다.

```text
ContentsServer/
  main.cpp
  Player.h
  Player.cpp
  Protocol.h / Protocol.cpp
  PacketIdType.h
  ContentsPacketRegister.h / .cpp
  ServerOptionFile/
    CoreOption.txt
    SessionBrokerOption.txt
```

`Protocol.*`, `PacketIdType.h`, `ContentsPacketRegister.*`, 일부 `Player` 스켈레톤은 PacketGenerator 결과물 기준으로 맞춰 쓰는 편이 안전하다.

---

## 패킷 정의와 생성

1. `Tool/PacketDefine.yml` 수정
2. `Tool/PacketGenerate.bat` 실행
3. 생성된 `PACKET_ID`, 패킷 클래스, 등록 코드 확인

핸들러는 보통 세션 생성자에서 등록한다.

```cpp
Player::Player(MultiSocketRUDPCore& inCore)
    : RUDPSession(inCore)
{
    RegisterPacketHandler<Player, Ping>(
        static_cast<PacketId>(PACKET_ID::PING),
        &Player::OnPing);
}
```

---

## 세션 구현

`RUDPSession`을 상속해서 연결 훅과 패킷 핸들러를 구현한다.

```cpp
class Player final : public RUDPSession
{
public:
    explicit Player(MultiSocketRUDPCore& inCore);

private:
    void OnConnected() override;
    void OnDisconnected() override;
    void OnReleased() override;

    void OnPing(const Ping& packet);
};
```

```cpp
void Player::OnConnected()
{
    LOG_DEBUG(std::format("Session {} connected", GetSessionId()));
}

void Player::OnDisconnected()
{
    LOG_DEBUG(std::format("Session {} disconnect requested", GetSessionId()));
}

void Player::OnReleased()
{
    // 다음 재사용 전 상태 초기화
}
```

---

## 서버 시작 예시

```cpp
int main()
{
    MultiSocketRUDPCore core(L"MY", L"DevServerCert");

    ContentsPacketRegister::Init();

    if (!core.StartServer(
            L"ServerOptionFile/CoreOption.txt",
            L"ServerOptionFile/SessionBrokerOption.txt",
            [](MultiSocketRUDPCore& inCore) -> RUDPSession*
            {
                return new Player(inCore);
            },
            true))
    {
        core.StopServer();
        return -1;
    }

    std::cin.get();
    core.StopServer();
    return 0;
}
```

---

## 연결 종료

현재 `RUDPSession::DoDisconnect()`는 인자를 받는다.

```cpp
DoDisconnect(DISCONNECT_REASON::BY_ERROR);
```

예전 문서처럼 `DoDisconnect()` 무인자 호출 예제를 남겨 두면 컴파일되지 않는다.

---

## 다른 세션 참조에 대한 주의

현재 `MultiSocketRUDPCore::GetUsingSession()`과 `GetReleasingSession()`은 `private`이다.  
콘텐츠 코드에서 직접 호출하는 예제는 현재 API 표면과 맞지 않는다.

권장 패턴은 아래 둘 중 하나다.

1. `SessionIdType`만 별도 매니저에 저장하고, 브로드캐스트는 세션 내부 훅이나 별도 서비스 계층에서 조합한다.
2. 코어 내부 동작 설명이 필요하면 문서에서 "내부 구현 설명"으로만 다루고, 외부 사용 예제로 쓰지 않는다.

즉 아래 같은 예제는 현재 공개 API 기준으로 제거해야 한다.

```cpp
auto* session = core.GetUsingSession(sessionId); // 현재 콘텐츠 코드에서는 불가
```

---

## 운영 체크리스트

- `ContentsPacketRegister::Init()`는 `StartServer()` 이전에 호출한다.
- `CoreOption.txt`와 `SessionBrokerOption.txt` 경로는 `ServerOptionFile/` 기준으로 맞춘다.
- TLS 인증서 기본 예시는 `MY` / `DevServerCert`다.
- `OnDisconnected()`에서는 정리 위주로 처리하고, 무거운 블로킹 작업은 피한다.
- `OnReleased()`에서는 재사용 전 멤버 상태 초기화만 수행한다.

---

## 관련 문서

- [[GettingStarted]] - 최소 기동 절차
- [[RUDPSession]] - 세션 확장 포인트
- [[MultiSocketRUDPCore]] - 서버 코어 공개 API
- [[Server/RUDPSessionBroker]] - 세션 발급 경로
