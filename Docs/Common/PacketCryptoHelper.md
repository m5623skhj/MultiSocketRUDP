# PacketCryptoHelper

> **`NetBuffer` 湲곕컲 ?⑦궥 援ъ“瑜??댄빐?섍퀬 [[CryptoHelper]]瑜??몄텧?섎뒗 ?붾났?명솕 ?섑띁.**  
> ?쒕쾭([[RUDPSession]])? ?대씪?댁뼵??[[RUDPClientCore]]) ?묒そ?먯꽌 怨듯넻?쇰줈 ?ъ슜?섎ŉ,  
> ?⑦궥 踰꾪띁???대뵒遺???대뵒源뚯? ?뷀샇?뷀븷吏, ?ㅻ뜑瑜??대뼸寃?援ъ꽦?섎뒗吏瑜??꾨떞?쒕떎.

---

## 紐⑹감

1. [?ㅺ퀎 ?꾩튂](#1-?ㅺ퀎-?꾩튂)
2. [SetHeader ???ㅻ뜑 ?꾩꽦](#2-setheader--?ㅻ뜑-?꾩꽦)
3. [EncodePacket ???⑦궥 ?뷀샇??(#3-encodepacket--?⑦궥-?뷀샇??
4. [DecodePacket ???⑦궥 蹂듯샇??(#4-decodepacket--?⑦궥-蹂듯샇??
5. [?ㅽ봽???곸닔 ?뺣━](#5-?ㅽ봽???곸닔-?뺣━)
6. [IPacketCryptoHelper / Adapter ?⑦꽩](#6-ipacketcryptohelper--adapter-?⑦꽩)
7. [m_bIsEncoded 以묐났 ?몄퐫??諛⑹?](#7-m_bisencoded-以묐났-?몄퐫??諛⑹?)
8. [肄??ъ씠???꾩껜 紐⑸줉](#8-肄??ъ씠???꾩껜-紐⑸줉)

---

## 1. ?ㅺ퀎 ?꾩튂

```
[RUDPSession::SendPacketImmediate]     [RUDPClientCore::SendPacket]
              ??                                   ??              ?붴???????????????р???????????????????????                             ??                  PacketCryptoHelper::EncodePacket()
                             ??                   CryptoHelper::GetTLSInstance()
                             ??                   CryptoHelper::EncryptAESGCM()


[RUDPPacketProcessor::ProcessByPacketType]  [RUDPClientCore::ProcessRecvPacket]
                    ??                                   ??                    ?붴???????????????р???????????????????????                                   ??                      PacketCryptoHelper::DecodePacket()
                                   ??                         CryptoHelper::DecryptAESGCM()
```

---

## 2. SetHeader ???ㅻ뜑 ?꾩꽦

```cpp
static void PacketCryptoHelper::SetHeader(
    OUT NetBuffer& netBuffer,
    const int extraSize = 0
)
```

**??븷:** `NetBuffer`???볦씤 ?섏씠濡쒕뱶 湲곗??쇰줈 3諛붿씠???ㅻ뜑瑜??꾩꽦?쒕떎.

```cpp
void PacketCryptoHelper::SetHeader(OUT NetBuffer& netBuffer, const int extraSize)
{
    // Byte 0: HeaderCode
    netBuffer.m_pSerializeBuffer[0] = NetBuffer::m_byHeaderCode;

    // Byte 1-2: PayloadLength (?ㅻ뜑 ?댄썑 諛붿씠????
    //   m_iWrite = ?꾩옱源뚯? ?곗씤 諛붿씠??(?ㅻ뜑 ?ы븿)
    //   extraSize = AUTH_TAG_SIZE(16) ???꾩쭅 AuthTag?????쇱?留?湲몄씠???ы븿
    *reinterpret_cast<uint16_t*>(&netBuffer.m_pSerializeBuffer[1])
        = static_cast<uint16_t>(netBuffer.m_iWrite - df_HEADER_SIZE + extraSize);

    // ?쎄린 而ㅼ꽌 珥덇린??    netBuffer.m_iRead = 0;

    // 理쒖쥌 ???꾩튂 (AuthTag ?ы븿)
    netBuffer.m_iWriteLast = netBuffer.m_iWrite + extraSize;
}
```

**`extraSize`媛 ?꾩슂???댁쑀:**

```
EncodePacket ?몄텧 ??踰꾪띁 ?곹깭:
  [Header 3B][Type 1B][Seq 8B][PacketId 4B][Payload N B]
  m_iWrite = 3+1+8+4+N

SetHeader(extraSize=AUTH_TAG_SIZE=16):
  PayloadLen = m_iWrite - 3 + 16 = 1+8+4+N+16  ??AuthTag ?ы븿
  m_iWriteLast = m_iWrite + 16                  ??EncodePacket ??AuthTag瑜??ш린源뚯? 異붽?

EncodePacket ??
  [Header 3B][Type 1B][Seq 8B][PacketId 4B][Ciphertext N B][AuthTag 16 B]
  m_iWrite = m_iWriteLast
```

---

## 3. EncodePacket ???⑦궥 ?뷀샇??
```cpp
static void PacketCryptoHelper::EncodePacket(
    OUT NetBuffer& packet,
    PacketSequence packetSequence,
    CryptoHelper::PACKET_DIRECTION direction,
    const unsigned char* sessionSalt,
    size_t sessionSaltSize,
    const BCRYPT_KEY_HANDLE& keyHandle,
    bool isCorePacket
)
```

**?대? 援ы쁽:**

```cpp
{
    // ??以묐났 ?몄퐫??諛⑹?
    if (packet.m_bIsEncoded) return;

    // ???ㅻ뜑 ?꾩꽦 (PayloadLen??AuthTag ?ш린 誘몃━ ?ы븿)
    SetHeader(packet, AUTH_TAG_SIZE);

    // ??AAD 異붿텧 (?ㅻ뜑 3B + PacketType 1B + Sequence 8B = 12 bytes)
    const unsigned char* aad = reinterpret_cast<const unsigned char*>(
        packet.m_pSerializeBuffer);
    constexpr size_t aadSize = df_HEADER_SIZE + sizeof(PACKET_TYPE) + sizeof(PacketSequence);
    // = 3 + 1 + 8 = 12

    // ??Nonce ?앹꽦
    auto nonce = CryptoHelper::GenerateNonce(
        sessionSalt, sessionSaltSize, packetSequence, direction);

    // ???뷀샇??踰붿쐞 寃곗젙
    size_t bodyOffset;
    if (isCorePacket) {
        bodyOffset = df_HEADER_SIZE + sizeof(PACKET_TYPE) + sizeof(PacketSequence);
        // = 3 + 1 + 8 = 12  (PacketId ?놁쓬)
    } else {
        bodyOffset = df_HEADER_SIZE + sizeof(PACKET_TYPE)
                   + sizeof(PacketSequence) + sizeof(PacketId);
        // = 3 + 1 + 8 + 4 = 16
    }

    char* bodyStart   = packet.m_pSerializeBuffer + bodyOffset;
    size_t bodySize   = packet.m_iWrite - bodyOffset;
    // ?꾩옱 m_iWrite??SetHeader ?댁쟾 媛?(AuthTag ?꾩쭅 ???)

    // ??AES-GCM ?뷀샇??(in-place)
    unsigned char authTag[AUTH_TAG_SIZE];
    bool ok = CryptoHelper::EncryptAESGCM(
        nonce.data(), nonce.size(),
        aad, aadSize,
        bodyStart, bodySize,   // plaintext
        bodyStart, bodySize,   // ciphertext (媛숈? 踰꾪띁 ??in-place)
        authTag,
        keyHandle
    );

    if (!ok) {
        LOG_ERROR("EncodePacket: EncryptAESGCM failed");
        return;
    }

    // ??AuthTag 踰꾪띁 ?앹뿉 異붽?
    memcpy(packet.m_pSerializeBuffer + packet.m_iWrite, authTag, AUTH_TAG_SIZE);
    packet.m_iWrite = packet.m_iWriteLast;  // m_iWrite 媛깆떊

    // ???몄퐫???꾨즺 ?쒖떆
    packet.m_bIsEncoded = true;
}
```

---

## 4. DecodePacket ???⑦궥 蹂듯샇??
```cpp
static bool PacketCryptoHelper::DecodePacket(
    OUT NetBuffer& packet,
    const unsigned char* sessionSalt,
    size_t sessionSaltSize,
    const BCRYPT_KEY_HANDLE& keyHandle,
    bool isCorePacket,
    CryptoHelper::PACKET_DIRECTION direction
)
```

> `PacketType`? `ProcessByPacketType`?먯꽌 ?대? ?쎌? ???몄텧??  
> 利? `m_iRead`??PacketType(1B) ?댄썑???꾩튂???곹깭.

**?대? 援ы쁽:**

```cpp
{
    // ??理쒖냼 ?ш린 寃??    // GetUseSize() = m_iWrite - m_iRead (?꾩옱 ?쎄린 媛?ν븳 諛붿씠??
    const size_t minBodySize = isCorePacket
        ? sizeof(PacketSequence) + AUTH_TAG_SIZE               // 8 + 16 = 24
        : sizeof(PacketSequence) + sizeof(PacketId) + AUTH_TAG_SIZE; // 8+4+16=28

    if (packet.GetUseSize() < minBodySize) {
        LOG_ERROR("DecodePacket: packet too small");
        return false;
    }

    // ??PacketSequence 異붿텧 (?뷀샇??諛? ?ㅻ뜑+Type ??
    //    m_iRead???꾩쭅 PacketType ?댄썑?대?濡?Sequence ?꾩튂 = m_iRead
    PacketSequence packetSequence;
    memcpy(&packetSequence,
           packet.m_pSerializeBuffer + df_HEADER_SIZE + sizeof(PACKET_TYPE),
           sizeof(PacketSequence));
    // 吏곸젒 踰꾪띁?먯꽌 ?쎌쓬 (m_iRead 蹂寃?????

    // ??AuthTag ?꾩튂 怨꾩궛
    const unsigned char* authTag = reinterpret_cast<const unsigned char*>(
        packet.m_pSerializeBuffer + packet.m_iWrite - AUTH_TAG_SIZE);

    // ??body 踰붿쐞 寃곗젙
    size_t bodyOffset;
    if (isCorePacket) {
        bodyOffset = df_HEADER_SIZE + sizeof(PACKET_TYPE) + sizeof(PacketSequence);
        // = 12
    } else {
        bodyOffset = df_HEADER_SIZE + sizeof(PACKET_TYPE)
                   + sizeof(PacketSequence) + sizeof(PacketId);
        // = 16
    }

    char* bodyStart = packet.m_pSerializeBuffer + bodyOffset;
    size_t bodySize = (packet.m_iWrite - AUTH_TAG_SIZE) - bodyOffset;

    // ??AAD (?ㅻ뜑 + Type + Sequence = 12 bytes, ?뷀샇??????
    const unsigned char* aad = reinterpret_cast<const unsigned char*>(
        packet.m_pSerializeBuffer);
    constexpr size_t aadSize = df_HEADER_SIZE + sizeof(PACKET_TYPE) + sizeof(PacketSequence);

    // ??Nonce ?앹꽦
    auto nonce = CryptoHelper::GenerateNonce(
        sessionSalt, sessionSaltSize, packetSequence, direction);

    // ??AES-GCM 蹂듯샇??+ ?몄쬆 ?쒓렇 寃利?    bool ok = CryptoHelper::DecryptAESGCM(
        nonce.data(), nonce.size(),
        aad, aadSize,
        bodyStart, bodySize,    // ciphertext
        authTag,                // 寃利앺븷 ?쒓렇
        bodyStart, bodySize,    // plaintext (in-place)
        keyHandle
    );

    if (!ok) {
        // ?몄쬆 ?ㅽ뙣 ??濡쒓렇留??④린怨?false 諛섑솚 (?몄텧?먭? ?⑦궥 ?먭린)
        LOG_ERROR(std::format(
            "DecodePacket: DecryptAESGCM failed. direction={}", static_cast<int>(direction)));
        return false;
    }

    // ??AuthTag ?쒓굅 (m_iWrite 議곗젙)
    packet.m_iWrite -= AUTH_TAG_SIZE;

    return true;
}
```

---

## 5. ?ㅽ봽???곸닔 ?뺣━

```
PacketType ?댄썑 湲곗? (m_iRead 湲곗?):

isCorePacket=false (?쇰컲 ?⑦궥):
  m_iRead ??[Sequence 8B][PacketId 4B][Payload N B]
  bodyOffsetWithNotHeader = sizeof(PacketType) + sizeof(Sequence) + sizeof(PacketId)
                          = 1 + 8 + 4 = 13

isCorePacket=true (肄붿뼱 ?⑦궥):
  m_iRead ??[Sequence 8B][Payload N B]
  bodyOffsetWithNotHeaderForCorePacket = sizeof(PacketType) + sizeof(Sequence)
                                       = 1 + 8 = 9

?덈? ?ㅽ봽??湲곗? (m_pSerializeBuffer[0]):

isCorePacket=false:
  bodyOffset = df_HEADER_SIZE + PacketType + Sequence + PacketId
             = 3 + 1 + 8 + 4 = 16
  ?? bodyOffsetWithHeader = 16

isCorePacket=true:
  bodyOffset = df_HEADER_SIZE + PacketType + Sequence
             = 3 + 1 + 8 = 12
  ?? bodyOffsetWithHeaderForCorePacket = 12

AAD 踰붿쐞 (??긽 ?숈씪):
  [0 .. 11] = df_HEADER_SIZE(3) + PacketType(1) + Sequence(8)

AuthTag ?꾩튂:
  m_iWrite - AUTH_TAG_SIZE  (蹂듯샇????
  m_iWrite (蹂듯샇???? AUTH_TAG_SIZE留뚰겮 以꾩뼱??
```

---

## 6. IPacketCryptoHelper / Adapter ?⑦꽩

?뚯뒪???⑹씠?깆쓣 ?꾪빐 ?명꽣?섏씠?ㅼ? ?대뙌?곕줈 遺꾨━:

```cpp
class IPacketCryptoHelper {
public:

    virtual bool DecodePacket(
        OUT NetBuffer& packet,
        const unsigned char* sessionSalt,
        size_t sessionSaltSize,
        const BCRYPT_KEY_HANDLE& keyHandle,
        bool isCorePacket,
        CryptoHelper::PACKET_DIRECTION direction
    ) = 0;

    virtual void EncodePacket(
        OUT NetBuffer& packet,
        PacketSequence packetSequence,
        CryptoHelper::PACKET_DIRECTION direction,
        const unsigned char* sessionSalt,
        size_t sessionSaltSize,
        const BCRYPT_KEY_HANDLE& keyHandle,
        bool isCorePacket
    ) = 0;

    virtual void SetHeader(OUT NetBuffer& netBuffer, int extraSize = 0) = 0;
};

class PacketCryptoHelperAdapter final : public IPacketCryptoHelper {
    bool DecodePacket(...) override {
        return PacketCryptoHelper::DecodePacket(...);  // ?뺤쟻 硫붿꽌???꾩엫
    }
    void EncodePacket(...) override {
        PacketCryptoHelper::EncodePacket(...);
    }
    void SetHeader(...) override {
        PacketCryptoHelper::SetHeader(...);
    }
};
```

**`RUDPPacketProcessor`??`IPacketCryptoHelper*`瑜??앹꽦??二쇱엯?쇰줈 諛쏆븘**  
?⑥쐞 ?뚯뒪?몄뿉??`MockPacketCryptoHelper`濡??泥?媛?ν븯??

---

## 7. `m_bIsEncoded` 以묐났 ?몄퐫??諛⑹?

```cpp
// NetBuffer 硫ㅻ쾭
bool m_bIsEncoded = false;

// EncodePacket ?대?
if (packet.m_bIsEncoded) return;  // ?대? ?뷀샇?붾맖 ??利됱떆 諛섑솚
// ...
packet.m_bIsEncoded = true;        // ?꾨즺 ???쒖떆
```

**以묐났 ?몄퐫?⑹씠 諛쒖깮?????덈뒗 ?쒕굹由ъ삤:**

```
?ъ쟾??寃쎈줈:
  理쒖큹 ?꾩넚: EncodePacket() ??m_bIsEncoded=true
  ?ъ쟾??    core.SendPacket(sendPacketInfo)
                ??媛숈? NetBuffer* ?ъ궗??                ??EncodePacket() ?ㅼ떆 ?몄텧?????덉쓬
                ??m_bIsEncoded 泥댄겕濡??ㅽ궢
```

`SendPacketImmediate`?먯꽌 `m_bIsEncoded == false`??寃쎌슦留?`EncodePacket`???몄텧?섎룄濡? 
泥댄겕?섎뒗 寃쎌슦???덉?留? `EncodePacket` ?먯껜???대??먯꽌 以묐났 諛⑹??쒕떎.

---

## 8. 肄??ъ씠???꾩껜 紐⑸줉

### `EncodePacket` ?몄텧泥?
```cpp
// ?쒕쾭
RUDPSession::SendPacketImmediate()
  ??if (!buffer.m_bIsEncoded)
       PacketCryptoHelper::EncodePacket(buffer, sequence, direction,
           session.GetSessionSalt(), SESSION_SALT_SIZE,
           session.GetSessionKeyHandle(), isCorePacket)

// ?대씪?댁뼵??(C++)
RUDPClientCore::SendPacket(buffer, sequence, isCorePacket)
  ??PacketCryptoHelper::EncodePacket(...)
```

### `DecodePacket` ?몄텧泥?
```cpp
// ?쒕쾭 (DECODE_PACKET 留ㅽ겕濡쒕줈 媛먯떥???몄텧)
RUDPPacketProcessor::ProcessByPacketType()
  ??switch PacketType:
      CONNECT_TYPE:
        DecodePacket(buffer, salt, saltSize, keyHandle,
                     isCorePacket=true, CLIENT_TO_SERVER)
      SEND_TYPE:
        DecodePacket(buffer, ..., isCorePacket=false, CLIENT_TO_SERVER)
      SEND_REPLY_TYPE / HEARTBEAT_REPLY_TYPE:
        DecodePacket(buffer, ..., isCorePacket=true, CLIENT_TO_SERVER_REPLY)
      DISCONNECT_TYPE:
        DecodePacket(buffer, ..., isCorePacket=true, CLIENT_TO_SERVER)

// ?대씪?댁뼵??(C++)
RUDPClientCore::ProcessRecvPacket()
  ??switch PacketType:
      SEND_TYPE:
        DecodePacket(buffer, ..., isCorePacket=false, SERVER_TO_CLIENT)
      HEARTBEAT_TYPE:
        DecodePacket(buffer, ..., isCorePacket=true, SERVER_TO_CLIENT)
      SEND_REPLY_TYPE:
        DecodePacket(buffer, ..., isCorePacket=true, SERVER_TO_CLIENT_REPLY)
```

### `SetHeader` ?몄텧泥?
```cpp
// 吏곸젒 ?몄텧 (EncodePacket ?대?媛 ?꾨땶 寃쎌슦)
RUDPSessionBroker::SendSessionInfoToClient()
  ??SetHeader(sendBuffer)  ??extraSize=0 (?뷀샇???놁씠 TLS濡??꾩넚)
```

---

## 愿??臾몄꽌
- [[CryptoHelper]] ??BCrypt ??섏? ?곗궛
- [[CryptoSystem]] ??Nonce 援ъ“, ?뷀샇??踰붿쐞 ?ㅺ퀎
- [[PacketFormat]] ???⑦궥 ?덉씠?꾩썐 諛??ㅽ봽???곸닔
- [[PacketProcessing]] ??DECODE_PACKET 留ㅽ겕濡??ъ슜泥?- [[RUDPSession]] ??EncodePacket ?몄텧 (SendPacketImmediate)
---

## ?꾩옱 肄붾뱶 湲곗? ?⑥닔 ?ㅻ챸

### 怨듦컻 ?⑥닔

#### `static void EncodePacket(NetBuffer& packet, PacketSequence packetSequence, PACKET_DIRECTION direction, const std::vector<unsigned char>& sessionSalt, const BCRYPT_KEY_HANDLE& sessionKeyHandle, bool isCorePacket)`
- 踰≫꽣 湲곕컲 ?몄뀡 ?뷀듃瑜?諛쏅뒗 ?ㅻ쾭濡쒕뱶??

#### `static void EncodePacket(NetBuffer& packet, PacketSequence packetSequence, PACKET_DIRECTION direction, const unsigned char* sessionSalt, size_t sessionSaltSize, const BCRYPT_KEY_HANDLE& sessionKeyHandle, bool isCorePacket)`
- ?ㅻ뜑 ?묒꽦, nonce ?앹꽦, AAD 怨꾩궛, AES-GCM ?뷀샇?? AuthTag 遺李⑹쓣 ?섑뻾?쒕떎.

#### `static bool DecodePacket(NetBuffer& packet, const std::vector<unsigned char>& sessionSalt, const BCRYPT_KEY_HANDLE& sessionKeyHandle, bool isCorePacket, const PACKET_DIRECTION direction)`
- 踰≫꽣 湲곕컲 ?몄뀡 ?뷀듃瑜?諛쏅뒗 蹂듯샇???ㅻ쾭濡쒕뱶??

#### `static bool DecodePacket(NetBuffer& packet, const unsigned char* sessionSalt, size_t sessionSaltSize, const BCRYPT_KEY_HANDLE& sessionKeyHandle, bool isCorePacket, const PACKET_DIRECTION direction)`
- 理쒖냼 ?ш린 寃?? ?쒗??異붿텧, nonce/AAD 怨꾩궛, AES-GCM 蹂듯샇?붾? ?섑뻾?쒕떎.

#### `static void SetHeader(NetBuffer& netBuffer, const int extraSize = 0)`
- ?ㅻ뜑 肄붾뱶? payload 湲몄씠瑜?湲곕줉?섍퀬 `m_iRead`, `m_iWriteLast`瑜??뺣━?쒕떎.

### ?대? ?⑥닔

#### `static PACKET_DIRECTION DetermineDirection(uint8_t packetType)`
- ?⑦궥 ???諛붿씠?몃? 諛⑺뼢 enum?쇰줈 留ㅽ븨?쒕떎.
