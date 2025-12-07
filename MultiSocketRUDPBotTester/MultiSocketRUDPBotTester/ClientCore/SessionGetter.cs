using System.Net.Security;
using System.Net.Sockets;
using System.Security.Authentication;

namespace MultiSocketRUDPBotTester.ClientCore
{
    public class SessionGetter
    {
        private TcpClient? tcpClient = null!;
        private SslStream? sslStream = null!;

        public async Task ConnectAsync(string host, int port, string? certFingerprint = null)
        {
            tcpClient = new TcpClient();
            await tcpClient.ConnectAsync(host, port);

            sslStream = new SslStream(tcpClient.GetStream()
                , false
                , (sender, certificate, chain, SslPolicyErrors) =>
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
                enabledSslProtocols: SslProtocols.Tls12 | SslProtocols.Tls13,
                checkCertificateRevocation: true);
        }

        public async Task<int> ReceiveAsync(byte[] buffer)
        {
            return await sslStream?.ReadAsync(buffer, 0, buffer.Length)!;
        }

        public void Close()
        {
            sslStream?.Close();
            tcpClient?.Close();

            sslStream = null;
            tcpClient = null;
        }
    }
}
