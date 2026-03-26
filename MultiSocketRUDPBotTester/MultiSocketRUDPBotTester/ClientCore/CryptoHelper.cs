using System.Security.Cryptography;

namespace MultiSocketRUDPBotTester.ClientCore
{
    public static class CryptoHelper
    {
        public const int AuthTagSize = 16;
        public const int SessionSaltSize = 16;
        public const int NonceSize = 12;

        public static void Encrypt(
            AesGcm aesGcm,
            byte[] buffer,
            int bodyOffset,
            int bodySize,
            ReadOnlySpan<byte> nonce,
            Span<byte> authTag,
            ReadOnlySpan<byte> aad = default)
        {
            var bodySpan = buffer.AsSpan(bodyOffset, bodySize);

            aesGcm.Encrypt(
                nonce,
                bodySpan,
                bodySpan,
                authTag,
                aad
            );
        }

        public static bool Decrypt(
            AesGcm aesGcm,
            byte[] key,
            byte[] nonce,
            Span<byte> cipherText,
            ReadOnlySpan<byte> aad,
            ReadOnlySpan<byte> authTag)
        {
            try
            {
                aesGcm.Decrypt(
                    nonce,
                    cipherText,
                    authTag,
                    cipherText,
                    aad);
            }
            catch (CryptographicException)
            {
                return false;
            }

            return true;
        }

        public static byte[] GenerateNonce(byte[] sessionSalt, ulong packetSequence, PacketDirection direction)
        {
            var nonce = new byte[NonceSize];

            var directionBits = (byte)((byte)direction << 6);
            nonce[0] = (byte)(directionBits | (sessionSalt[0] & 0x3F));
            nonce[1] = sessionSalt[1];
            nonce[2] = sessionSalt[2];
            nonce[3] = sessionSalt[3];

            nonce[4] = (byte)((packetSequence >> 56) & 0xFF);
            nonce[5] = (byte)((packetSequence >> 48) & 0xFF);
            nonce[6] = (byte)((packetSequence >> 40) & 0xFF);
            nonce[7] = (byte)((packetSequence >> 32) & 0xFF);
            nonce[8] = (byte)((packetSequence >> 24) & 0xFF);
            nonce[9] = (byte)((packetSequence >> 16) & 0xFF);
            nonce[10] = (byte)((packetSequence >> 8) & 0xFF);
            nonce[11] = (byte)(packetSequence & 0xFF);

            return nonce;
        }
    }
}
