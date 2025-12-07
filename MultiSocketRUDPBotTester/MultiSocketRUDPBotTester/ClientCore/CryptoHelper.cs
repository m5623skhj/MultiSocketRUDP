using System.Security.Cryptography;

namespace MultiSocketRUDPBotTester.ClientCore
{
    public static class CryptoHelper
    {
        public static void Encrypt(byte[] key
            , byte[] nonce
            , byte[] plaintext
            , byte[] add
            , out byte[] ciphertext
            , out byte[] tag)
        {
            ciphertext = new byte[plaintext.Length];
            tag = new byte[16];

            using var aesGcm = new AesGcm(key, tag.Length);
            aesGcm.Encrypt(nonce, plaintext, ciphertext, tag, add);
        }

        public static byte[]? Decrypt(byte[] key
            , byte[] nonce
            , byte[] ciphertext
            , byte[] add
            , byte[] tag)
        {
            var plaintext = new byte[ciphertext.Length];

            try
            {
                using var aesGcm = new AesGcm(key, tag.Length);
                aesGcm.Decrypt(nonce, ciphertext, tag, plaintext, add);
                return plaintext;
            }
            catch (CryptographicException)
            {
                return null;
            }
        }
    }
}
