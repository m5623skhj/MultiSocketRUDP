using System.Net.Security;
using System.Net.Sockets;
using System.Security.Authentication;

namespace MultiSocketRUDPBotTester.ClientCore
{
    public class SessionGetter : IDisposable
    {
        private TcpClient? tcpClient;
        private SslStream? sslStream;

        public async Task ConnectAsync(string host, int port, string? certFingerprint = null)
        {
            tcpClient = new TcpClient();
            await tcpClient.ConnectAsync(host, port);

            sslStream = new SslStream(tcpClient.GetStream()
                , false
                , (_, certificate, chain, SslPolicyErrors) =>
                {
                    if (certFingerprint == null)
                    {
                        return true;
                    }

                    if (certificate == null)
                    {
                        return false;
                    }

                    var serverFp = certificate.GetCertHashString();
                    return string.Equals(serverFp, certFingerprint, StringComparison.OrdinalIgnoreCase);
                });

            await sslStream.AuthenticateAsClientAsync(
                targetHost: host,
                clientCertificates: null,
                enabledSslProtocols: SslProtocols.Tls12,
                checkCertificateRevocation: true);
        }

        public async Task<int> ReceiveAsync(byte[] buffer, int offset)
        {
            if (sslStream == null)
            {
                throw new InvalidOperationException("Not connected. Call ConnectAsync first.");
            }

            return await sslStream.ReadAsync(buffer, offset, buffer.Length - offset)!;
        }

        public void Close()
        {
            sslStream?.Dispose();
            tcpClient?.Dispose();

            sslStream = null;
            tcpClient = null;
        }

        public void Dispose()
        {
            Close();
            GC.SuppressFinalize(this);
        }
    }
}
