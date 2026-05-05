# RUDPSession

> **?대씪?댁뼵???곌껐???⑥쐞 媛앹껜?댁옄 肄섑뀗痢??쒕쾭???듭떖 ?뺤옣 吏??**  
> ?곹깭癒몄떊, ?뷀샇??而⑦뀓?ㅽ듃, RIO I/O, ?먮쫫 ?쒖뼱, ?⑦궥 ?쒖꽌 蹂댁옣???대??먯꽌 泥섎━?섎ŉ,  
> 肄섑뀗痢?媛쒕컻?먮뒗 ???대옒?ㅻ? **?곸냽**???⑦궥 ?몃뱾?ъ? ?대깽???낅쭔 援ы쁽?섎㈃ ?쒕떎.

---

## 紐⑹감

1. [肄섑뀗痢?媛쒕컻??鍮좊Ⅸ ?쒖옉](#1-肄섑뀗痢?媛쒕컻??鍮좊Ⅸ-?쒖옉)
2. [?곹깭 ?꾩씠 ?붿빟](#2-?곹깭-?꾩씠-?붿빟)
3. [?⑦궥 ?몃뱾???깅줉](#3-?⑦궥-?몃뱾???깅줉)
4. [?대깽????(#4-?대깽????
5. [?⑥닔 ?ㅻ챸](#5-?⑥닔-?ㅻ챸)
6. [?⑦궥 ?≪떊 API](#6-?⑦궥-?≪떊-api)
7. [?섎룞 ?곌껐 ?댁젣](#7-?섎룞-?곌껐-?댁젣)
8. [?대? ?섏떊 ?뚯씠?꾨씪??(#8-?대?-?섏떊-?뚯씠?꾨씪??
9. [?대? ?≪떊 ?뚯씠?꾨씪??(#9-?대?-?≪떊-?뚯씠?꾨씪??
10. [?먮쫫 ?쒖뼱? 蹂대쪟 ??(#10-?먮쫫-?쒖뼱?-蹂대쪟-??
11. [?섑듃鍮꾪듃 硫붿빱?덉쬁](#11-?섑듃鍮꾪듃-硫붿빱?덉쬁)
12. [?숈떆??蹂댄샇 ?꾨왂](#12-?숈떆??蹂댄샇-?꾨왂)
13. [二쇱쓽?ы빆 諛??뷀븳 ?ㅼ닔](#13-二쇱쓽?ы빆-諛??뷀븳-?ㅼ닔)

---

## 1. 肄섑뀗痢?媛쒕컻??鍮좊Ⅸ ?쒖옉

### 理쒖냼 援ы쁽 ?덉떆

```cpp
// Player.h
#pragma once
#include "RUDPSession.h"
#include "Protocol.h"  // PacketGenerator媛 ?앹꽦???⑦궥 ?대옒??
class Player final : public RUDPSession {
public:
    explicit Player(MultiSocketRUDPCore& core);

protected:
    void OnConnected()    override;
    void OnDisconnected() override;
    void OnReleased()     override;

private:
    // ?⑦궥 ?몃뱾??    void OnPing(const Ping& packet);
    void OnMove(const MoveReq& packet);
    void OnChat(const ChatReq& packet);

    // 寃뚯엫 ?곗씠??    std::string playerName;
    int roomId = -1;
};
```

```cpp
// Player.cpp
#include "Player.h"
#include "PacketIdType.h"

Player::Player(MultiSocketRUDPCore& core)
    : RUDPSession(core)
{
    // ??紐⑤뱺 ?⑦궥 ?몃뱾?щ? ?앹꽦?먯뿉???깅줉
    RegisterPacketHandler<Player, Ping>(
        static_cast<PacketId>(PACKET_ID::PING), &Player::OnPing);
    RegisterPacketHandler<Player, MoveReq>(
        static_cast<PacketId>(PACKET_ID::MOVE_REQ), &Player::OnMove);
    RegisterPacketHandler<Player, ChatReq>(
        static_cast<PacketId>(PACKET_ID::CHAT_REQ), &Player::OnChat);
}

void Player::OnConnected() {
    // ?쒕쾭? UDP ?곌껐???꾩쟾???섎┰??吏곹썑 ?몄텧
    // DB 議고쉶, 罹먮┃???곗씠??濡쒕뱶 ??    playerName = "Guest_" + std::to_string(GetSessionId());
    LOG_DEBUG(std::format("Player {} connected", playerName));
}

void Player::OnDisconnected() {
    // RELEASING ?꾩씠 吏곹썑 ?몄텧. ?뚯폆? ?꾩쭅 ?대젮 ?덉쓬
    // 寃뚯엫 猷몄뿉???댁옣 泥섎━, ?곗씠???????    if (roomId >= 0) {
        RoomManager::Leave(roomId, GetSessionId());
    }
}

void Player::OnReleased() {
    // ? 諛섑솚 吏곸쟾. ?뚯폆? ?대? ?ロ? ?덉쓬
    // 硫ㅻ쾭 蹂??珥덇린??    playerName.clear();
    roomId = -1;
}

void Player::OnPing(const Ping& /*packet*/) {
    Pong pong;
    SendPacket(pong);  // ???묐떟 ?꾩넚
}

void Player::OnMove(const MoveReq& packet) {
    // packet.x, packet.y ????쭅?ы솕???곗씠???ъ슜
    MoveRes res;
    res.x = packet.x;
    res.y = packet.y;
    SendPacket(res);
}

void Player::OnChat(const ChatReq& packet) {
    // 梨꾪똿 釉뚮줈?쒖틦?ㅽ듃 ??}
```

```cpp
// ?쒕쾭 吏꾩엯??MultiSocketRUDPCore core(L"MY", L"DevServerCert");
ContentsPacketRegister::Init();  // PacketGenerator ?먮룞 ?앹꽦 ?깅줉 肄붾뱶 (StartServer ?댁쟾)
core.StartServer(
    L"ServerOptionFile/CoreOption.txt",
    L"ServerOptionFile/SessionBrokerOption.txt",
    [](MultiSocketRUDPCore& c) -> RUDPSession* {
        return new Player(c);   // ???⑺넗由? Player ?몄뒪?댁뒪 諛섑솚
    },
    true
);
```

---

## 2. ?곹깭 ?꾩씠 ?붿빟

> ?꾩껜 ?ㅼ씠?닿렇?④낵 媛??꾩씠 肄붾뱶: [[SessionLifecycle]]

```
DISCONNECTED ??[AcquireSession]????RESERVED ??[CONNECT ?섏떊]????CONNECTED
                                        ??                            ??                                 [30珥???꾩븘??               [DoDisconnect]
                                        ??                            ??                                        ?붴?????????????р??????????????????                                                     ??                                                 RELEASING
                                                     ??                                          [IO ?꾨즺 ?湲???
                                                     ??                                              DISCONNECTED (? 諛섑솚)
```

---

## 3. ?⑦궥 ?몃뱾???깅줉

### `RegisterPacketHandler`

```cpp
template <typename DerivedType, typename PacketType>
void RegisterPacketHandler(
    const PacketId packetId,
    void (DerivedType::* func)(const PacketType&)
);
```

| ?뚮씪誘명꽣 | ???| ?ㅻ챸 |
|----------|------|------|
| `packetId` | `PacketId` | ?쇱슦???? 蹂댄넻 `static_cast<PacketId>(PACKET_ID::XXX)` |
| `func` | 硫ㅻ쾭 ?⑥닔 ?ъ씤??| `const PacketType&` ?섎굹瑜?諛쏅뒗 硫ㅻ쾭 ?⑥닔 |

**?대? ?숈옉:**

```
?뚮떎 ??? packetFactoryMap[packetId] = 
    [func, packetId](RUDPSession* session, NetBuffer* buffer) ??std::function<bool()>
    {
        DerivedType* derived = static_cast<DerivedType*>(session);
        shared_ptr<IPacket> packet = BufferToPacket(*buffer, packetId);
        if (packet == nullptr) return []() { return false; };
        return [derived, func, packet]() {
            (derived->*func)(static_cast<PacketType&>(*packet));
            return true;
        };
    };
```

**??쭅?ы솕 ?먮쫫:**

```
NetBuffer ??PacketManager::MakePacket(packetId)
              ??shared_ptr<PacketType> ?앹꽦
              ??packet->BufferToPacket(buffer)
                   ??SetBufferToParameters(buffer, field1, field2, ...)
              ??func(static_cast<PacketType&>(*packet)) ?몄텧
```

### ?깅줉?섏? ?딆? PacketId ?섏떊 ??
```cpp
bool RUDPSession::ProcessPacket(NetBuffer& recvPacket, PacketSequence recvPacketSequence)
{
    PacketId packetId;
    recvPacket >> packetId;

    auto itor = packetFactoryMap.find(packetId);
    if (itor == packetFactoryMap.end()) {
        LOG_ERROR(std::format("Received unknown packet. packetId: {}", packetId));
        return false;  // ??false 諛섑솚 ??session->DoDisconnect() ?몄텧??    }
    // ...
}
```

> **二쇱쓽:** ?깅줉?섏? ?딆? `PacketId`媛 ?섏떊?섎㈃ ?몄뀡??媛뺤젣 醫낅즺?쒕떎.  
> ?대씪?댁뼵?몄? ?쒕쾭??`PacketId` ?뺤쓽??諛섎뱶???숆린?붾릺?댁빞 ?쒕떎.  
> [[PacketGenerator]]瑜??ъ슜?섎㈃ ?묒륫???숈떆???낅뜲?댄듃?????덈떎.

### PacketType ?묒꽦 ?붽뎄?ы빆

`RegisterPacketHandler`???ъ슜?섎뒗 ?⑦궥 ??낆? 諛섎뱶??`IPacket`???곸냽?섍퀬  
`BufferToPacket(NetBuffer&)` + `PacketToBuffer(NetBuffer&)`瑜?援ы쁽?댁빞 ?쒕떎.

```cpp
// Protocol.h (PacketGenerator ?먮룞 ?앹꽦)
class MoveReq final : public IPacket {
public:
    [[nodiscard]] PacketId GetPacketId() const override;
    void BufferToPacket(NetBuffer& buffer) override;  // ??쭅?ы솕
    void PacketToBuffer(NetBuffer& buffer) override;  // 吏곷젹??public:
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};
```

---

## 4. ?대깽????
| ??| ?몄텧 ?ㅻ젅??| ?몄텧 ?쒖젏 | ?뚯폆 ?곹깭 | 沅뚯옣 ?묒뾽 |
|----|------------|-----------|-----------|-----------|
| `OnConnected()` | RecvLogic Worker | `TryConnect()` ?깃났, ACK ?꾩넚 吏곸쟾 | ?대┝ | DB 議고쉶, ?몄뀡 珥덇린??|
| `OnDisconnected()` | `DoDisconnect()` ?몄텧 ?ㅻ젅??| RELEASING ?꾩씠 吏곹썑 | ?대┝ | 猷??댁옣, ?곗씠?????|
| `OnReleased()` | Session Release Thread | ? 諛섑솚 吏곸쟾 | ?ロ옒 | 硫ㅻ쾭 蹂??珥덇린??|

### `OnConnected` 二쇱쓽?ы빆

```cpp
void Player::OnConnected() {
    // ???덉쟾: SendPacket ?몄텧 媛??(?뚯폆 ?대┝)
    WelcomePacket welcome;
    welcome.serverId = 1;
    SendPacket(welcome);

    // ???덉쟾: GetSessionId() ?ъ슜 媛??    LOG_DEBUG(std::format("Session {} connected", GetSessionId()));

    // ?좑툘 二쇱쓽: ?ㅻ옒 嫄몃━???숆린 I/O??湲덈Ъ (RecvLogic Worker瑜?釉붾씫??
    // DB 議고쉶媛 ?꾩슂?섎㈃ 鍮꾨룞湲곕줈 泥섎━?섍굅??蹂꾨룄 ?ㅻ젅?쒖뿉???섑뻾
}
```

### `OnDisconnected` 二쇱쓽?ы빆

```cpp
void Player::OnDisconnected() {
    // ???덉쟾: 硫ㅻ쾭 蹂???쎄린
    LOG_DEBUG(std::format("Player {} disconnected. Room: {}", playerName, roomId));

    // ???덉쟾: ?ㅻⅨ ?몄뀡???뚮┝ (SendPacket ?꾨떂. ?몄뀡 ?ъ씤?곕? ?듯븳 吏곸젒 ?꾩넚? ?꾪뿕)
    // RoomManager::Broadcast(roomId, LeavePacket{GetSessionId()});

    // ???꾪뿕: SendPacket ?몄텧
    // ??DoDisconnect()??RELEASING ?꾩씠 ???몄텧??    // ???대? nowInReleaseThread = true?대?濡?MultiSocketRUDPCore::SendPacket??false 諛섑솚
    // ???ㅼ젣 ?꾩넚?????섍퀬 由ъ냼?ㅻ쭔 ??퉬??}
```

### `OnReleased` 二쇱쓽?ы빆

```cpp
void Player::OnReleased() {
    // ???덉쟾: 硫ㅻ쾭 蹂??珥덇린??    playerName.clear();
    roomId = -1;

    // ???덈? 湲덉?: 硫ㅻ쾭 蹂?????대뼡 李몄“???ъ슜?섏? 留?寃?    // ?????쒖젏???ㅻⅨ ?ㅻ젅?쒓? ???몄뀡??AcquireSession()?쇰줈 媛?멸컝 ???덉쓬
    // ??InitializeSession() ?댄썑???몄텧?섎?濡?sessionId, clientAddr ?깆씠 珥덇린?붾맖
}
```

---

## 5. ?⑥닔 ?ㅻ챸

?앹꽦???뚮㈇?먮뒗 ?뱀닔??寃쎌슦媛 ?꾨땲硫?臾몄꽌?뷀븯吏 ?딅뒗??  
`RUDPSession`? `protected` ?앹꽦?먮? 媛吏??뺤옣 湲곕컲 ?대옒?ㅼ씠誘濡? ?ш린?쒕뒗 肄섑뀗痢?媛쒕컻?먭? 吏곸젒 ?ъ슜?섎뒗 硫붿꽌?쒖? ?뺤옣 ?ъ씤???꾩＜濡??뺣━?쒕떎.

### 肄섑뀗痢좎뿉??吏곸젒 ?ъ슜?섎뒗 怨듦컻 ?⑥닔

#### `void DoDisconnect(const DISCONNECT_REASON disconnectSession)`
- ?몄뀡??RELEASING ?곹깭濡??꾩씠?쒗궎怨??곌껐 ?댁젣 ?덉감瑜??쒖옉?쒕떎.
- ?꾩옱 肄붾뱶?먯꽌??諛섎뱶??`DISCONNECT_REASON`???몄옄濡??섍꺼???쒕떎.
- 以묐났 ?몄텧? ?곹깭 癒몄떊??CAS濡?諛⑹??쒕떎.

#### `bool SendPacket(IPacket& packet)`
- ?쇰컲 肄섑뀗痢??⑦궥 ?≪떊 吏꾩엯?먯씠??
- CONNECTED ?곹깭? ?먮쫫 ?쒖뼱 ?곹깭瑜??뺤씤??利됱떆 ?꾩넚?섍굅??PendingQueue??蹂대쪟?쒕떎.

#### `ThreadIdType GetThreadId() const`
- ?몄뀡??諛곗젙???뚯빱 ?ㅻ젅??ID瑜?諛섑솚?쒕떎.

#### `SessionIdType GetSessionId() const`
- ?몄뀡 怨좎쑀 ID瑜?諛섑솚?쒕떎.

#### `SOCKET GetSocket() const`
- ?몄뀡??UDP ?뚯폆 ?몃뱾??諛섑솚?쒕떎.
- ?대? ?곹깭? 寃쎌웳?????덉쑝誘濡??ㅻ옒 蹂닿??섏? 留먭퀬 利됱떆 ?ъ슜?댁빞 ?쒕떎.

#### `sockaddr_in GetSocketAddress() const`
- ?몄뀡 ?뚯폆 二쇱냼瑜?`sockaddr_in`?쇰줈 諛섑솚?쒕떎.

#### `SOCKADDR_INET GetSocketAddressInet() const`
- ?몄뀡 ?뚯폆 二쇱냼瑜?`SOCKADDR_INET`?쇰줈 諛섑솚?쒕떎.

#### `SOCKADDR_INET& GetSocketAddressInetRef()`
- ?몄뀡 ?뚯폆 二쇱냼??李몄“瑜?諛섑솚?쒕떎.
- ?대????깃꺽??媛뺥븯誘濡??섎챸怨??숈떆?깆뿉 二쇱쓽?댁빞 ?쒕떎.

#### `bool IsConnected() const`
- ?꾩옱 CONNECTED ?곹깭?몄? 諛섑솚?쒕떎.

#### `bool IsReserved() const`
- ?꾩옱 RESERVED ?곹깭?몄? 諛섑솚?쒕떎.

#### `bool IsUsingSession() const`
- RESERVED ?먮뒗 CONNECTED泥섎읆 ?꾩옱 ?ъ슜 以묒씤 ?몄뀡?몄? 諛섑솚?쒕떎.

#### `SESSION_STATE GetSessionState() const`
- ?곹깭 癒몄떊???꾩옱 媛믪쓣 洹몃?濡?諛섑솚?쒕떎.

#### `bool IsReleasing() const`
- ?꾩옱 RELEASING ?곹깭?몄? 諛섑솚?쒕떎.

#### `uint32_t GetSessionGeneration() const`
- ?몄뀡 ?ъ궗???몃?瑜??섑??대뒗 generation 媛믪쓣 諛섑솚?쒕떎.

#### `DISCONNECT_REASON GetDisconnectedReason() const`
- 留덉?留??곌껐 ?댁젣 ?ъ쑀瑜?諛섑솚?쒕떎.

### 肄섑뀗痢??뺤옣 ?ъ씤??
#### `virtual void OnConnected()`
- CONNECT ?깃났 吏곹썑 ?몄텧?섎뒗 ?ъ슜???뺤옣 ?ъ씤?몃떎.
- RecvLogic Worker 臾몃㎘?먯꽌 ?ㅽ뻾?섎?濡??μ떆媛?釉붾줈???묒뾽? ?쇳빐???쒕떎.

#### `virtual void OnDisconnected()`
- `DoDisconnect()`濡?RELEASING???ㅼ뼱媛?吏곹썑 ?몄텧?쒕떎.
- ?쇰컲 ?≪떊 吏?먯씠 ?꾨땲???뺣━ 肄쒕갚?쇰줈 痍④툒?댁빞 ?쒕떎.

#### `virtual void OnReleased()`
- Session Release Thread?먯꽌 ?ㅼ젣 ?먯썝 諛섑솚 吏곸쟾???몄텧?쒕떎.
- 硫ㅻ쾭 蹂??珥덇린?붿? ?ъ궗??以鍮??묒뾽???곹빀?섎떎.

### ?대? ?듭떖 ?⑥닔

#### `bool SendPacket(NetBuffer& buffer, PacketSequence inSendPacketSequence, bool isReplyType, bool isCorePacket)`
- reply/core ?щ????곕씪 異붿쟻/蹂대쪟 ?뺤콉???섎닠 ?곸슜?섎뒗 ?대? ?≪떊 ?⑥닔??

#### `bool SendPacketImmediate(NetBuffer& buffer, PacketSequence inSendPacketSequence, bool isReplyType, bool isCorePacket)`
- PendingQueue瑜??고쉶?섍퀬 利됱떆 RIO ?≪떊???덉빟?쒕떎.

#### `void TryFlushPendingQueue()`
- ACK ?섏떊 ??蹂대쪟 以묒씤 ?⑦궥???ㅼ떆 ?섎젮蹂대궦??

#### `void SendHeartbeatPacket()`
- heartbeat ?⑦궥???앹꽦?섍퀬 ?≪떊?쒕떎.

#### `bool CheckReservedSessionTimeout(unsigned long long now) const`
- RESERVED ?곹깭 ?몄뀡????꾩븘?껊릺?덈뒗吏 寃?ы븳??

#### `void AbortReservedSession()`
- ?덉빟 ?곹깭 ?몄뀡??媛뺤젣濡?RELEASING?쇰줈 ?꾩씠?쒗궓??

#### `bool OnRecvPacket(NetBuffer& recvPacket)`
- SEND_TYPE ?섏떊 ???몄뀡 ?⑥쐞 ?섏떊 泥섎━ 吏꾩엯?먯씠??

#### `bool ProcessPacket(NetBuffer& recvPacket, PacketSequence recvPacketSequence)`
- ?⑦궥 ??쭅?ы솕? ?깅줉???몃뱾???몄텧???대떦?쒕떎.

#### `void SendReplyToClient(PacketSequence recvPacketSequence)`
- ACK ?⑦궥???대씪?댁뼵?몃줈 ?꾩넚?쒕떎.

#### `void OnSendReply(NetBuffer& recvPacket)`
- ?대씪?댁뼵??ACK ?먮뒗 HEARTBEAT_REPLY瑜?泥섎━?섍퀬 ?먮쫫 ?쒖뼱 ?곹깭瑜?媛깆떊?쒕떎.

---

## 6. ?⑦궥 ?≪떊 API

### 6-1. `SendPacket(IPacket&)` ???곗씠???⑦궥 ?꾩넚 (二??ъ슜 API)

```cpp
bool SendPacket(IPacket& packet);
```

**?꾩젣 議곌굔:**
- `IsConnected() == true` (CONNECTED ?곹깭)
- ?먮쫫 ?쒖뼱媛 ?덉슜?섎뒗 寃쎌슦 利됱떆 ?꾩넚, ?꾨땲硫?PendingQueue???湲?
**諛섑솚媛?**

| 諛섑솚媛?| ?섎? | 肄섑뀗痢????|
|--------|------|-------------|
| `true` | RIO Send ?덉빟 ?먮뒗 PendingQueue 蹂닿? ?깃났 | ?뺤긽 |
| `false` | ?몄뀡??CONNECTED ?곹깭 ?꾨떂, ?먮뒗 PendingQueue 媛??李?| ?대? `DoDisconnect()` ?몄텧??|

**?대? ?ㅽ뻾 ?쒖꽌:**

```
1. IsConnected() ?뺤씤 ??false?대㈃ 利됱떆 return false

2. ++lastSendPacketSequence (atomic)

3. NetBuffer 吏곷젹??
   buffer << SEND_TYPE << packetSequence << packet.GetPacketId()
   packet.PacketToBuffer(buffer)

4. SendPacket(buffer, sequence, isReplyType=false, isCorePacket=false)
   ?쒋? [?먮쫫 ?쒖뼱 ?뺤씤]
   ??  scoped_lock(pendingQueueLock)
   ??  if !pendingQueueEmpty || !flowManager.CanSend(sequence):
   ??      pendingPacketQueue.push({sequence, &buffer})
   ??      return true (?湲?
   ??   ?붴? SendPacketImmediate(buffer, sequence, false, false)
       ?쒋? sendPacketInfoPool->Alloc()
       ?쒋? sendPacketInfo.Initialize(this, &buffer, sequence, false)
       ?쒋? InsertSendPacketInfo(sequence, sendPacketInfo)  ???ъ쟾??留?       ?쒋? EncodePacket(AES-GCM, SERVER_TO_CLIENT, ...)
       ?붴? core.SendPacket(sendPacketInfo)
           ?붴? RUDPIOHandler::DoSend()
```

**?ъ슜 ?덉떆:**

```cpp
void Player::OnPing(const Ping& packet) {
    Pong pong;
    pong.timestamp = packet.timestamp;  // ?먯퐫

    bool ok = SendPacket(pong);
    if (!ok) {
        // false???몄뀡???대? ?댁젣 以묒엫???섎?
        // DoDisconnect()媛 ?대? ?몄텧?먯쑝誘濡?異붽? 泥섎━ 遺덊븘??        return;
    }
}
```

**釉뚮줈?쒖틦?ㅽ듃 ?⑦꽩:**

```cpp
// 媛숈? 諛⑹쓽 紐⑤뱺 ?뚮젅?댁뼱?먭쾶 ?꾩넚
void Room::Broadcast(IPacket& packet, SessionIdType excludeId) {
    for (auto sessionId : memberIds) {
        if (sessionId == excludeId) continue;
        auto* player = static_cast<Player*>(
            core.GetUsingSession(sessionId));
        if (player && player->IsConnected()) {
            player->SendPacket(packet);
        }
    }
}
```

---

### 6-2. ?대? ?꾩슜 ?≪떊 硫붿꽌?쒕뱾

肄섑뀗痢??덉씠?댁뿉??吏곸젒 ?몄텧?섎뒗 寃쎌슦??嫄곗쓽 ?놁?留? ?숈옉 ?댄빐瑜??꾪빐 ?ㅻ챸?쒕떎.

#### `SendPacket(NetBuffer&, PacketSequence, bool isReplyType, bool isCorePacket)` ??protected

```cpp
bool SendPacket(NetBuffer& buffer, PacketSequence inSendPacketSequence,
                bool isReplyType, bool isCorePacket);
```

| `isReplyType` | `isCorePacket` | ?ъ슜 ??| ?ъ쟾??異붿쟻 |
|---------------|----------------|---------|------------|
| `false` | `false` | ?쇰컲 ?곗씠???⑦궥 | ??|
| `true` | `true` | ACK, Heartbeat Reply | ??|
| `false` | `true` | Heartbeat | ??|

`isReplyType = true`?대㈃ PendingQueue 寃?щ? 嫄대꼫?곌퀬 利됱떆 `SendPacketImmediate`濡??대룞.  
`isReplyType = false`?대㈃ ?먮쫫 ?쒖뼱 ?뺤씤 ??PendingQueue??蹂닿??섍굅??利됱떆 ?꾩넚.

#### `SendPacketImmediate` ??利됱떆 ?꾩넚 (PendingQueue ?고쉶)

```cpp
bool SendPacketImmediate(NetBuffer& buffer, PacketSequence sequence,
                         bool isReplyType, bool isCorePacket);
```

1. `sendPacketInfoPool->Alloc()` ??TLS 硫붾え由???먯꽌 ?좊떦 (lock-free)
2. `sendPacketInfo.Initialize(...)` ???뚯쑀?? 踰꾪띁, ?쒗?? ?ъ쟾???щ? ?ㅼ젙
3. `isReplyType=false`?대㈃ ??`InsertSendPacketInfo(sequence, info)` (?ъ쟾??留??깅줉)
4. `buffer.m_bIsEncoded == false`?대㈃ ??`PacketCryptoHelper::EncodePacket(...)` (AES-GCM)
5. `core.SendPacket(sendPacketInfo)` ??RIO Send ???쎌엯

**?ㅽ뙣 泥섎━:**

```cpp
if (!core.SendPacket(sendPacketInfo)) {
    if (!isReplyType) {
        // ?ъ쟾??留듭뿉???쒓굅 + EraseSendPacketInfo
        core.EraseSendPacketInfo(sendPacketInfo, threadId);
        rioContext.GetSendContext().EraseSendPacketInfo(inSendPacketSequence);
    } else {
        // Reply ?⑦궥? ?ъ쟾??留듭뿉 ?놁쑝誘濡?洹몃깷 Free
        sendPacketInfo->isErasedPacketInfo.store(true);
        SendPacketInfo::Free(sendPacketInfo);
    }
    return false;
}
```

---

## 7. ?섎룞 ?곌껐 ?댁젣

### `DoDisconnect(const DISCONNECT_REASON disconnectSession)` ???곌껐 ?댁젣 ?쒖옉

```cpp
void DoDisconnect(const DISCONNECT_REASON disconnectSession);
```

**肄섑뀗痢??쒕쾭?먯꽌 吏곸젒 ?몄텧 媛??** ?? ??媛먯?, 猷??꾨컲, ?몄쬆 ?ㅽ뙣 ??

```cpp
void Player::OnMove(const MoveReq& packet) {
    // ?⑦궥 寃利?    if (!IsValidPosition(packet.x, packet.y)) {
        LOG_ERROR(std::format("Player {} sent invalid position: ({}, {})",
            GetSessionId(), packet.x, packet.y));
        DoDisconnect(DISCONNECT_REASON::BY_ERROR);   // ?덉떆
        return;
    }
    // ?뺤긽 泥섎━...
}
```

**以묐났 ?몄텧 ?덉쟾:** `TryTransitionToReleasing()`??CAS?대?濡??щ윭 ?ㅻ젅?쒖뿉?? 
?숈떆???몄텧?대룄 ???섎굹留??깃났?섍퀬 ?섎㉧吏??no-op.

```cpp
void RUDPSession::DoDisconnect(const DISCONNECT_REASON disconnectSession)
{
    // ?대? RELEASING/DISCONNECTED ?곹깭?대㈃ 利됱떆 return
    if (!stateMachine.TryTransitionToReleasing()) return;

    disconnectedReason = disconnectSession;
    nowInReleaseThread.store(true, std::memory_order_seq_cst);
    OnDisconnected();   // ??肄섑뀗痢???
    MultiSocketRUDPCoreFunctionDelegate::PushToDisconnectTargetSession(*this);
}
```

**Disconnect vs DoDisconnect:**

| | `DoDisconnect()` | `Disconnect()` |
|--|------------------|----------------|
| ?몄텧 二쇱껜 | 肄섑뀗痢??쒕쾭, ?⑦궥 ?ㅻ쪟, ?ъ쟾??珥덇낵 | Session Release Thread ?꾩슜 |
| ??븷 | RELEASING ?꾩씠 ?붿껌 | ?ㅼ젣 ?뚯폆 ?リ린 + ? 諛섑솚 |
| ?뚯폆 ?곹깭 | ?꾩쭅 ?대젮 ?덉쓬 | ???⑥닔 ?댁뿉???レ쓬 |
| 吏곸젒 ?몄텧 媛?? | ??| ??(?대? ?꾩슜) |

---

## 8. ?대? ?섏떊 ?뚯씠?꾨씪??
> ?섏떊 ?꾩껜 ?먮쫫: [[PacketProcessing]]

### `OnRecvPacket` (RecvLogic Worker Thread?먯꽌 ?몄텧)

```cpp
bool RUDPSession::OnRecvPacket(NetBuffer& recvPacket);
```

```
1. packetSequence 異붿텧 (buffer >> packetSequence)

2. flowManager.CanAccept(sequence)
   ??RUDPReceiveWindow::CanReceive()
   ??diff = sequence - windowStart
   ??0 <= diff < windowSize?대㈃ true
   ??false?대㈃ ?⑦궥 ?먭린 (濡쒓렇 ?놁쓬, ?뺤긽?곸씤 以묐났/踰붿쐞 ???⑦궥)

3. SessionPacketOrderer::OnReceive(sequence, buffer, callback)

   寃곌낵: PROCESSED        ???뺤긽 泥섎━
   寃곌낵: DUPLICATED_RECV  ???쳾CK ?꾩넚 (SendReplyToClient)
   寃곌낵: PACKET_HELD      ??HoldingQueue??蹂닿?, ACK ?놁쓬
   寃곌낵: ERROR_OCCURED    ??false 諛섑솚 ???몄텧?먭? DoDisconnect() ?몄텧
```

### `ProcessPacket` ???ㅼ젣 ??쭅?ы솕 諛??몃뱾???몄텧

```cpp
bool RUDPSession::ProcessPacket(NetBuffer& recvPacket, PacketSequence recvPacketSequence)
{
    PacketId packetId;
    recvPacket >> packetId;  // 4諛붿씠???쎄린

    auto itor = packetFactoryMap.find(packetId);
    if (itor == packetFactoryMap.end()) {
        LOG_ERROR(std::format("Unknown packet. packetId: {}", packetId));
        return false;  // ??DoDisconnect()
    }

    // ?뚮떎: ??쭅?ы솕 + ?몃뱾???몄텧
    if (!itor->second(this, &recvPacket)()) {
        LOG_ERROR(std::format("Failed to process packet. packetId: {}", packetId));
        return false;  // ??DoDisconnect()
    }

    flowManager.MarkReceived(recvPacketSequence);  // ?섏떊 ?덈룄???낅뜲?댄듃
    SendReplyToClient(recvPacketSequence);          // ACK ?꾩넚
    return true;
}
```

### `SendReplyToClient` ??ACK ?꾩넚

```cpp
void RUDPSession::SendReplyToClient(PacketSequence recvPacketSequence)
{
    NetBuffer* buffer = NetBuffer::Alloc();

    auto packetType = PACKET_TYPE::SEND_REPLY_TYPE;
    BYTE advertiseWindow = flowManager.GetAdvertisableWindow();
    *buffer << packetType << recvPacketSequence << advertiseWindow;

    // isReplyType=true ??PendingQueue ?고쉶, ?ъ쟾??異붿쟻 ?놁쓬
    if (!SendPacket(*buffer, recvPacketSequence, /*isReplyType*/true, /*isCorePacket*/true)) {
        DoDisconnect();
    }
}
```

**advertiseWindow:** `windowSize - usedCount` = ?꾩옱 鍮꾩뼱 ?덈뒗 ?섏떊 ?덈룄??移???  
?대씪?댁뼵?몃뒗 ??媛믪쓣 蹂닿퀬 ?꾩넚 ?띾룄瑜?議곗젅?쒕떎. ??[[FlowController]] 李몄“

---

## 9. ?대? ?≪떊 ?뚯씠?꾨씪??
### ACK ?섏떊 泥섎━ ??`OnSendReply`

```cpp
void RUDPSession::OnSendReply(NetBuffer& recvPacket)
```

?대씪?댁뼵?몃줈遺??`SEND_REPLY_TYPE` ?⑦궥 ?섏떊 ???몄텧?쒕떎.

```
1. packetSequence 異붿텧

2. lastSendPacketSequence < sequence ???좏슚?섏? ?딆? 誘몃옒 ACK, 臾댁떆

3. FindAndEraseSendPacketInfo(sequence)
   ??sendPacketInfoMap?먯꽌 ?쒓굅 (?쎄린 ??unique_lock?쇰줈 ?낃렇?덉씠??
   ??nullptr?대㈃ ?대? ?쒓굅??(以묐났 ACK), return

4. flowManager.OnAckReceived(sequence)
   ??RUDPFlowController::OnReplyReceived()
   ??gap = sequence - lastReplySequence - 1
   ??gap >= 5?대㈃ OnCongestionEvent() (CWND /= 2)
   ???뺤긽?대㈃ cwnd = min(cwnd + 1, MAX_CWND)

5. core.EraseSendPacketInfo(sendPacketInfo, threadId)
   ??sendPacketInfoList[threadId]?먯꽌 ?댄꽣?덉씠?곕줈 O(1) ?쒓굅
   ??SendPacketInfo::Free()

6. TryFlushPendingQueue()
   ???먮쫫 ?쒖뼱媛 ?덉슜?섎㈃ PendingQueue?먯꽌 爰쇰궡 ?꾩넚
```

### `TryFlushPendingQueue` ??蹂대쪟 ??泥섎━

```cpp
void RUDPSession::TryFlushPendingQueue()
```

```cpp
{
    scoped_lock lock(rioContext.GetSendContext().GetPendingQueueLock());
    while (!pendingQueueEmpty) {
        auto& [sequence, _] = pendingQueueFront();
        if (!flowManager.CanSend(sequence)) break;  // ?덈룄??珥덇낵

        pair<PacketSequence, NetBuffer*> item;
        pendingQueue.pop(item);
        sendBuffers.push_back(item);  // Lock 諛뽰뿉???꾩넚
    }
}

// Lock ?댁젣 ???꾩넚 (Lock 蹂댁쑀 ?쒓컙 理쒖냼??
for (auto& [seq, buf] : sendBuffers) {
    if (!SendPacketImmediate(*buf, seq, false, false)) {
        DoDisconnect();
        // ?섎㉧吏 踰꾪띁 Free
        break;
    }
}
```

---

## 10. ?먮쫫 ?쒖뼱? 蹂대쪟 ??
> ?곸꽭 ?숈옉: [[FlowController]]

### ?꾩넚 痢?(Server ??Client)

**CWND(?쇱옟 ?덈룄??:** ??踰덉뿉 ACK ?놁씠 ?꾩넚 媛?ν븳 ?⑦궥 ??

```
珥덇린媛? INITIAL_CWND
ACK ?섏떊留덈떎: cwnd = min(cwnd + 1, MAX_CWND)   // AIMD 利앷?
?⑦궥 ?좎떎 媛먯?(gap >= 5): cwnd = max(cwnd / 2, 1)  // AIMD 媛먯냼
?ъ쟾????꾩븘?? cwnd = 1
```

**PendingQueue 議곌굔:**

```cpp
// SendPacket ?대?
scoped_lock lock(pendingQueueLock);
bool shouldPend = !pendingQueueEmpty               // ?욎뿉 ?대? ?湲?以묒씤 寃껋씠 ?덇굅??               || !flowManager.CanSend(sequence);  // CWND 珥덇낵

if (shouldPend) {
    pendingPacketQueue.push({sequence, &buffer});
    return true;
}
```

> **??PendingQueue媛 苑?李⑤㈃ false瑜?諛섑솚?섎뒗媛?**  
> `RingBuffer<pair<...>>` 怨좎젙 ?ш린 ?먯씠誘濡?`Push`媛 ?ㅽ뙣?섎㈃  
> ?대씪?댁뼵?멸? ?덈Т ?먮젮???쒕쾭媛 蹂대궡???띾룄瑜??곕씪媛吏 紐삵븳?ㅻ뒗 ?좏샇.  
> ??寃쎌슦 ???댁긽 ?⑦궥??蹂대궪 ???놁쑝誘濡?`DoDisconnect()` ?몄텧???곸젅?섎떎.

### ?섏떊 痢?(Client ??Server)

**RecvWindow(?щ씪?대뵫 ?섏떊 ?덈룄??:**

```
windowStart ~ windowStart+windowSize-1 踰붿쐞???쒗?ㅻ쭔 ?섏떊 ?덉슜
MarkReceived()濡??섏떊 留덊궧 + ?욌?遺??щ씪?대뵫
GetAdvertisableWindow() = windowSize - usedCount ??ACK???ы븿
```

---

## 11. ?섑듃鍮꾪듃 硫붿빱?덉쬁

### ?쒕쾭 ???대씪?댁뼵??(HeartbeatThread)

```cpp
void RUDPSession::SendHeartbeatPacket()
{
    if (nowInReleaseThread.load(acquire) || !IsConnected()) return;

    // CWND ?뺤씤 (?곗씠???⑦궥怨??숈씪???먮쫫 ?쒖뼱 ?곸슜)
    PacketSequence nextSeq = lastSendPacketSequence.load() + 1;
    if (!flowManager.CanSend(nextSeq)) return;  // ?덈룄??媛??李⑤㈃ ?앸왂

    NetBuffer* buffer = NetBuffer::Alloc();
    auto type = PACKET_TYPE::HEARTBEAT_TYPE;
    PacketSequence seq = IncrementLastSendPacketSequence();
    *buffer << type << seq;

    // isCorePacket=true, isReplyType=false ???ъ쟾??異붿쟻??    if (!SendPacket(*buffer, seq, false, true)) {
        DoDisconnect();
    }
}
```

### ?대씪?댁뼵?????쒕쾭 (HEARTBEAT_REPLY)

?대씪?댁뼵?몃뒗 `HEARTBEAT_TYPE` ?섏떊 ??利됱떆 `HEARTBEAT_REPLY_TYPE`?쇰줈 ?묐떟?쒕떎.  
?쒕쾭???대? `SEND_REPLY_TYPE`怨??숈씪?섍쾶 泥섎━ ??`OnSendReply(sequence)` ?몄텧.

**?섑듃鍮꾪듃媛 ?ъ쟾??異붿쟻?섎뒗 ?댁쑀:**  
?섑듃鍮꾪듃 ?묐떟(Reply)?????CWND媛 ?좎??쒕떎.  
?묐떟 ?놁씠 ?ъ쟾???쒓퀎 珥덇낵 ???몄뀡 媛뺤젣 醫낅즺 ???대씪?댁뼵???곌껐 ?곹깭 媛먯?.

**?듭뀡 ?뚯씪 愿???ㅼ젙:**

| ?ㅼ젙 | ?뱀뀡 | 沅뚯옣媛?| ?ㅻ챸 |
|------|------|--------|------|
| `HEARTBEAT_THREAD_SLEEP_MS` | `[CORE]` | 3000 | ?섑듃鍮꾪듃 ?꾩넚 二쇨린 (ms) |
| `MAX_PACKET_RETRANSMISSION_COUNT` | `[CORE]` | 10~20 | ?ъ쟾???쒓퀎 (?섑듃鍮꾪듃 ?ы븿) |
| `RETRANSMISSION_MS` | `[CORE]` | 200 | ?ъ쟾???몃━嫄?媛꾧꺽 (ms) |

---

## 12. ?숈떆??蹂댄샇 ?꾨왂

| ?곗씠???먯썝 | 蹂댄샇 ?섎떒 | ?묎렐 ?ㅻ젅??| ?⑦꽩 |
|------------|-----------|-------------|------|
| ?몄뀡 ?곹깭 | `atomic<SESSION_STATE>` + CAS | IO/Logic/Heartbeat 紐⑤몢 | lock-free |
| ?뚯폆 | `shared_mutex socketLock` | DoRecv/DoSend ??shared; CloseSocket ??unique | ?쎄린 怨듭쑀, ?リ린 ?낆젏 |
| ?댁젣 吏꾪뻾 ?щ? | `atomic_bool nowInReleaseThread` | 紐⑤뱺 ?ㅻ젅??| seq_cst 硫붾え由??쒖꽌 |
| ?⑦궥 泥섎━ ?щ? | `atomic_bool nowInProcessingRecvPacket` | Logic Worker ??Release Thread | seq_cst |
| ?ъ쟾??留?| `shared_mutex sendPacketInfoMapLock` | Logic(write) ??Retransmission(read) | ?쎄린 怨듭쑀, 媛깆떊 ?낆젏 |
| ?꾩넚 ??| `mutex sendPacketInfoQueueLock` | Logic Worker ?⑤룆 | ?⑥닚 裕ㅽ뀓??|
| 蹂대쪟 ??| `mutex pendingPacketQueueLock` | Logic Worker ?⑤룆 | ?⑥닚 裕ㅽ뀓??|
| IO 紐⑤뱶 | `atomic<IO_MODE>` + InterlockedCAS | IO Worker ??Logic Worker | lock-free SpinLock |

### IO_SENDING ?뚮옒洹??ㅺ퀎 ?섎룄

```
// DoSend??SpinLock ?⑦꽩
while (true) {
    // CAS: IO_NONE_SENDING(0) ??IO_SENDING(1)
    if (InterlockedCompareExchange(&ioMode, IO_SENDING, IO_NONE_SENDING) ?ㅽ뙣)
        // ?ㅻⅨ ?ㅻ젅?쒓? Send 以????ъ떆??or 醫낅즺
        continue / break;

    // ???쒖젏?먯꽌留?RIOSendEx ?몄텧
    TryRIOSend(session, context);
    // IO_SENDING ??SendIOCompleted?먯꽌 IO_NONE_SENDING?쇰줈 蹂듭썝
    break;
}
```

**???몄뀡???숈떆???섎굹??RIO Send留??덉슜?섎뒗 ?댁쑀:**  
RIO Send ?꾨즺 ?먮뒗 ?쒖꽌瑜?蹂댁옣?섏? ?딆쑝硫? 媛숈? ?몄뀡??蹂듭닔??Send媛 ?숈떆?? 
吏꾪뻾?섎㈃ ?곗씠???쒖꽌媛 ?ㅻ컮?????덈떎. SpinLock?쇰줈 吏곷젹?뷀븳??

---

## 13. 二쇱쓽?ы빆 諛??뷀븳 ?ㅼ닔

### ???섎せ???⑦꽩??
**1. OnDisconnected?먯꽌 SendPacket ?몄텧**
```cpp
void Player::OnDisconnected() {
    GoodbyePacket bye;
    SendPacket(bye);  // ?????쒖젏??nowInReleaseThread=true
                      //    core.SendPacket()??false 諛섑솚?섎ŉ ?꾩넚 ????}
```

**2. OnReleased ?댄썑 ?몄뀡 ?ъ씤??蹂닿?**
```cpp
// ?대뵖媛??Player* ?????OnReleased ?댄썑 ?ъ슜
void GameRoom::AddPlayer(Player* player) {
    members.push_back(player);  // ??? 諛섑솚 ?????ъ씤?곕뒗 ?ъ궗?⑸맖
}
// ???SessionIdType????ν븯怨?GetUsingSession()?쇰줈 ?묎렐??寃?```

**3. ?몃뱾?????μ떆媛??숆린 釉붾씫**
```cpp
void Player::OnLogin(const LoginReq& packet) {
    auto result = db.SyncQuery(...);  // ??RecvLogic Worker瑜??μ떆媛?釉붾씫
    // ??媛숈? ?ㅻ젅?쒖뿉 諛곗젙???ㅻⅨ ?몄뀡?ㅼ쓽 ?⑦궥 泥섎━??吏?곕맖
}
// ?닿껐: 鍮꾨룞湲?DB ?먮뒗 蹂꾨룄 ?ㅻ젅?쒗??먯꽌 泥섎━
```

**4. ?앹꽦?먯뿉??virtual ?⑥닔 ?몄텧**
```cpp
Player::Player(MultiSocketRUDPCore& core) : RUDPSession(core) {
    OnConnected();  // ???몄뀡???꾩쭅 DISCONNECTED ?곹깭
                    //    Player??OnConnected媛 ?몄텧?섏? ?딄퀬 RUDPSession??寃껋씠 ?몄텧??}
```

**5. PacketId 遺덉씪移?*
```cpp
// ?쒕쾭?먯꽌 ?깅줉
RegisterPacketHandler<Player, MoveReq>(
    static_cast<PacketId>(PACKET_ID::MOVE_REQ),  // ?쒕쾭: MOVE_REQ = 5
    &Player::OnMove);

// ?대씪?댁뼵?몄뿉???꾩넚 (C#)
sendBuffer << (uint)PacketId.MoveReq;  // ?대씪?댁뼵?? MoveReq = 6  ??遺덉씪移?
// ???쒕쾭媛 ?????녿뒗 PacketId ?섏떊 ??DoDisconnect()
```

> **?닿껐**: [[PacketGenerator]]瑜??ъ슜???쒕쾭/?대씪?댁뼵?몄쓽 `PacketId` ?뺤쓽瑜??⑥씪 YAML?먯꽌 ?숆린??

---

## 愿??臾몄꽌
- [[SessionLifecycle]] ???곹깭 ?꾩씠 ?ㅼ씠?닿렇?④낵 ?꾩씠 肄붾뱶 ?곸꽭
- [[SessionComponents]] ???쒕툕 而댄룷?뚰듃(StateMachine, CryptoContext ?? ?곸꽭
- [[PacketProcessing]] ???섏떊 ?뚯씠?꾨씪???꾩껜 ?먮쫫
- [[FlowController]] ??CWND? ?섏떊 ?덈룄???숈옉 ?먮━
- [[CryptoSystem]] ??AES-GCM ?뷀샇???곸슜 諛⑹떇
- [[MultiSocketRUDPCore]] ???몄뀡 ? 愿由ъ? ?쒕쾭 ?앸챸二쇨린
- [[PacketGenerator]] ???⑦궥 ?대옒???먮룞 ?앹꽦
- [[GettingStarted]] ??肄섑뀗痢??쒕쾭 泥섏쓬遺??援ы쁽?섍린
