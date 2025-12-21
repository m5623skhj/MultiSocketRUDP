using System.Security.Cryptography;

namespace MultiSocketRUDPBotTester.ClientCore
{
    public static class CryptoHelper
    {
        public const int AuthTagSize = 16;
        public const int SessionSaltSize = 16;
        public const int NonceSize = 12;

        public static void Encrypt(AesGcm aesGcm
            , byte[] key
            , byte[] nonce
            , byte[] plaintext
            , byte[] add
            , out byte[] ciphertext
            , out byte[] tag)
        {
            ciphertext = new byte[plaintext.Length];
            tag = new byte[16];

            aesGcm.Encrypt(nonce, plaintext, ciphertext, tag, add);
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
            catch (System.Security.Cryptography.CryptographicException)
            {
                return false;
            }

            return true;
        }

        public static byte[] GenerateNonce(byte[] sessionSalt, PacketSequence packetSequence, PacketDirection direction)
        {
            if (sessionSalt.Length != SessionSaltSize)
            {
                return [];
            }

            var nonce = new byte[NonceSize];
            Array.Copy(sessionSalt, 0, nonce, 0, SessionSaltSize);

            nonce[8] = (byte)((packetSequence >> 24) & 0x3F);
            nonce[9] = (byte)((packetSequence >> 16) & 0xFF);
            nonce[10] = (byte)((packetSequence >> 8) & 0xFF);
            nonce[11] = (byte)(packetSequence & 0xFF);

            var directionBits = (byte)((byte)direction << 6);
            nonce[8] |= directionBits;

            return nonce;
        }
    }
}
