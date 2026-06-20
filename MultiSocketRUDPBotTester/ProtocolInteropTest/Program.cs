using MultiSocketRUDPBotTester.Buffer;
using MultiSocketRUDPBotTester.ClientCore;
using System.Security.Cryptography;

const ulong sequence = 0x0102030405060708UL;
var key = Enumerable.Range(1, 16).Select(value => (byte)value).ToArray();
var salt = Enumerable.Range(0, 16).Select(value => (byte)(0xA0 + value)).ToArray();

NetBuffer.HeaderCode = 0xCC;
var packet = new NetBuffer(128);
packet.ReserveHeader();
packet.WriteBytes([0xDE, 0xAD, 0xBE, 0xEF]);
packet.InsertPacketType(PacketType.SendType);
packet.InsertPacketSequence(sequence);
packet.InsertPacketId((PacketId)0x4D);

using var aes = new AesGcm(key, CryptoHelper.AuthTagSize);
NetBuffer.EncodePacket(aes, packet, sequence, PacketDirection.ServerToClient, salt, false);
const string expectedPacketHex = "CC210000000308070605040302014D0000000F36689548DC6A5962DE6CAA96BDA1A957E7E28B";
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
