# SessionGetter (C# TLS 세션 수신)

> TLS 1.2 (`SslStream`)을 사용해 [[RUDPSessionBroker|세션 브로커]]로부터 세션 정보를 수신하는 클라이언트.

---

## 인증서 검증 전략

```csharp
new SslStream(tcpClient.GetStream(), false,
    (_, certificate, _, _) => {
        if (certFingerprint == null) return true;  // 핀 없으면 모든 인증서 허용 (개발 환경)
        return certificate?.GetCertHashString() == certFingerprint;
    }
);
```

> 개발 환경에서는 `certFingerprint = null`로 호출해 자체 서명 인증서를 허용.  
> 운영 환경에서는 인증서 지문(SHA-1)을 전달해 핀닝 적용 권장.

---

## 연결 흐름

```
ConnectAsync(host, port, certFingerprint?)
  1. TcpClient.ConnectAsync(host, port)
  2. SslStream 생성 (인증서 검증 콜백 포함)
  3. SslStream.AuthenticateAsClientAsync(
       targetHost: host,
       enabledSslProtocols: TLS 1.2,
       checkCertificateRevocation: true
     )

ReceiveAsync(buffer, offset)
  → sslStream.ReadAsync(buffer, offset, buffer.Length - offset)

Close()
  → sslStream.Close() + tcpClient.Close()
```

---

## BotTesterCore에서의 사용

```
SessionGetter.ConnectAsync(hostIp, hostPort)
while true:
  receivedBytes = await SessionGetter.ReceiveAsync(buffer, totalBytes)
  if receivedBytes == 0 → break
  totalBytes += receivedBytes
  if totalBytes < PacketHeaderSize → continue
  payloadLength = BitConverter.ToUInt16(buffer, PayloadPosition)  // offset 2
  if totalBytes >= payloadLength + PacketHeaderSize → break

sessionGetter.Close()
new Client(buffer[PacketHeaderSize..totalBytes])
```

### `ConnectAsync`

```csharp
public async Task ConnectAsync(string host, int port, string? certFingerprint = null)
```

지정된 호스트와 포트로 TCP 연결을 맺고 TLS 인증을 수행한다.

| 파라미터 | 타입 | 설명 |
|----------|------|------|
| `host` | `string` | 접속할 호스트 주소 |
| `port` | `int` | 접속할 포트 번호 |
| `certFingerprint` | `string?` | 인증서 검증을 위한 지문. `null`이면 인증서를 검증하지 않음 |

### `ReceiveAsync`

```csharp
public async Task<int> ReceiveAsync(byte[] buffer, int offset)
```

SSL 스트림으로부터 데이터를 수신한다.

| 파라미터 | 타입 | 설명 |
|----------|------|------|
| `buffer` | `byte[]` | 데이터를 저장할 버퍼 |
| `offset` | `int` | 버퍼 내 데이터 저장 시작 위치 |

**반환값**: 수신된 바이트 수.
## 관련 문서
- [[BotTesterCore]] — SessionGetter 호출 흐름
- [[RUDPSessionBroker]] — 서버 측 세션 발급
- [[TLSHelper]] — 서버 측 TLS 구현
