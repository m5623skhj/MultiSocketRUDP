using MultiSocketRUDPBotTester.Buffer;
using MultiSocketRUDPBotTester.ClientCore;
using System.Security.Cryptography;
using System.Text.Json;

var vectorPath = Path.Combine(AppContext.BaseDirectory, "ProtocolInteropVector.json");
var testVectors = JsonSerializer.Deserialize<ProtocolInteropVector[]>(
    File.ReadAllText(vectorPath),
    new JsonSerializerOptions { PropertyNameCaseInsensitive = true })
    ?? throw new InvalidOperationException("Protocol interop vectors are empty.");
if (testVectors.Length == 0)
{
    throw new InvalidOperationException("Protocol interop vectors are empty.");
}

var mismatches = new List<string>();
foreach (var testVector in testVectors)
{
    if (testVector.IsCorePacket != (testVector.PacketId is null))
    {
        throw new InvalidOperationException(
            $"{testVector.Name}: core packets must omit packetId and full packets must include it.");
    }

    var key = Convert.FromHexString(testVector.KeyHex);
    var salt = Convert.FromHexString(testVector.SaltHex);
    var plaintext = Convert.FromHexString(testVector.PlaintextHex);

    NetBuffer.HeaderCode = testVector.HeaderCode;
    var packet = new NetBuffer(128);
    packet.ReserveHeader();
    packet.WriteBytes(plaintext);
    packet.InsertPacketType((PacketType)testVector.PacketType);
    packet.InsertPacketSequence(testVector.Sequence);
    if (!testVector.IsCorePacket)
    {
        packet.InsertPacketId((PacketId)(testVector.PacketId
            ?? throw new InvalidOperationException($"{testVector.Name}: full packet requires packetId.")));
    }

    using var aes = new AesGcm(key, CryptoHelper.AuthTagSize);
    NetBuffer.EncodePacket(
        aes,
        packet,
        testVector.Sequence,
        (PacketDirection)testVector.Direction,
        salt,
        testVector.IsCorePacket);
    var expectedPacketHex = testVector.EncodedPacketHex.ToUpperInvariant();
    var actualPacketHex = Convert.ToHexString(packet.GetPacketBuffer());
    if (packet.GetPacketBuffer()[0] != testVector.HeaderCode)
    {
        mismatches.Add($"{testVector.Name}: encoded header code does not match the vector.");
    }
    if (actualPacketHex != expectedPacketHex)
    {
        mismatches.Add(
            $"{testVector.Name}: protocol vector mismatch. Expected={expectedPacketHex}, Actual={actualPacketHex}");
    }

    if (!NetBuffer.DecodePacket(
        aes,
        packet,
        testVector.IsCorePacket,
        salt,
        (PacketDirection)testVector.Direction,
        out var failure))
    {
        throw new InvalidOperationException($"{testVector.Name}: golden vector decode failed: {failure}");
    }

    packet.SkipBytes(5);
    var decodedPacketType = packet.ReadByte();
    var decodedSequence = packet.ReadULong();
    uint? decodedPacketId = testVector.IsCorePacket ? null : packet.ReadUInt();
    var decodedPlaintext = packet.ReadBytes(plaintext.Length);
    if (decodedPacketType != testVector.PacketType
        || decodedSequence != testVector.Sequence
        || decodedPacketId != testVector.PacketId
        || !decodedPlaintext.SequenceEqual(plaintext))
    {
        mismatches.Add($"{testVector.Name}: decoded packet fields do not match the vector.");
    }
}

if (mismatches.Count > 0)
{
    throw new InvalidOperationException(string.Join(Environment.NewLine, mismatches));
}

Console.WriteLine($"C# protocol interop vectors passed ({testVectors.Length}).");

internal sealed record ProtocolInteropVector(
    string Name,
    ulong Sequence,
    string KeyHex,
    string SaltHex,
    byte HeaderCode,
    byte Direction,
    bool IsCorePacket,
    byte PacketType,
    uint? PacketId,
    string PlaintextHex,
    string EncodedPacketHex);
