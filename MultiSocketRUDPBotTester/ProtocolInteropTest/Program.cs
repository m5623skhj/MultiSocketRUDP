using MultiSocketRUDPBotTester.Buffer;
using MultiSocketRUDPBotTester.ClientCore;
using System.Security.Cryptography;
using System.Text.Json;

var vectorPath = Path.Combine(AppContext.BaseDirectory, "ProtocolInteropVector.json");
var testVector = JsonSerializer.Deserialize<ProtocolInteropVector>(
    File.ReadAllText(vectorPath),
    new JsonSerializerOptions { PropertyNameCaseInsensitive = true })
    ?? throw new InvalidOperationException("Protocol interop vector is empty.");

var key = Convert.FromHexString(testVector.KeyHex);
var salt = Convert.FromHexString(testVector.SaltHex);
var plaintext = Convert.FromHexString(testVector.PlaintextHex);

NetBuffer.HeaderCode = 0xCC;
var packet = new NetBuffer(128);
packet.ReserveHeader();
packet.WriteBytes(plaintext);
packet.InsertPacketType((PacketType)testVector.PacketType);
packet.InsertPacketSequence(testVector.Sequence);
packet.InsertPacketId((PacketId)testVector.PacketId);

using var aes = new AesGcm(key, CryptoHelper.AuthTagSize);
NetBuffer.EncodePacket(aes, packet, testVector.Sequence, PacketDirection.ServerToClient, salt, false);
var expectedPacketHex = testVector.EncodedPacketHex.ToUpperInvariant();
var actualPacketHex = Convert.ToHexString(packet.GetPacketBuffer());
if (actualPacketHex != expectedPacketHex)
{
    throw new InvalidOperationException($"Protocol vector mismatch. Expected={expectedPacketHex}, Actual={actualPacketHex}");
}

if (!NetBuffer.DecodePacket(aes, packet, false, salt, PacketDirection.ServerToClient, out var failure))
{
    throw new InvalidOperationException($"Golden vector decode failed: {failure}");
}

Console.WriteLine("C# protocol interop vector passed.");

internal sealed record ProtocolInteropVector(
    ulong Sequence,
    string KeyHex,
    string SaltHex,
    byte PacketType,
    uint PacketId,
    string PlaintextHex,
    string EncodedPacketHex);
