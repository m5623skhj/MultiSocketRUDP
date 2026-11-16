#include "PreCompile.h"
#include "TLSHelper.h"
#include <vector>
#include <chrono>
#include <ws2tcpip.h>
#include <wincrypt.h>
#include <memory>

namespace TLSHelper
{
    bool TLSHelperClient::Initialize()
    {
        // Reusing a client for a new session must release its previous TLS state.
        if (ctxtHandle.dwLower || ctxtHandle.dwUpper) DeleteSecurityContext(&ctxtHandle);
        if (credHandle.dwLower || credHandle.dwUpper) FreeCredentialsHandle(&credHandle);
        ZeroMemory(&ctxtHandle, sizeof(ctxtHandle));
        ZeroMemory(&credHandle, sizeof(credHandle));
        handshakeCompleted = false;
        SCHANNEL_CRED cred = {};
        cred.dwVersion = SCHANNEL_CRED_VERSION;
        cred.grbitEnabledProtocols = SP_PROT_TLS1_2_CLIENT;
        cred.dwFlags = SCH_CRED_MANUAL_CRED_VALIDATION | SCH_CRED_NO_DEFAULT_CREDS;

        lastStatus = AcquireCredentialsHandle(
            nullptr,
            const_cast<LPWSTR>(UNISP_NAME),
            SECPKG_CRED_OUTBOUND,
            nullptr,
            &cred,
            nullptr,
            nullptr,
            &credHandle,
            nullptr
        );

        return lastStatus == SEC_E_OK;
    }

    bool TLSHelperClient::Handshake(const SOCKET socket)
    {
        return Handshake(socket, std::stop_token{});
    }

    bool TLSHelperClient::Handshake(const SOCKET socket, const std::stop_token& inStopToken)
    {
        handshakeCompleted = false;
        pendingHandshakeData.clear();
        handshakeResult = {};
        std::vector<char> recvBuffer;
        const auto started = std::chrono::steady_clock::now();
        const auto deadline = connectionDeadline == (std::chrono::steady_clock::time_point::max)()
            ? started + std::chrono::seconds(15) : connectionDeadline;
        const auto fail = [&](const char* inReason, const char* inStage, int inSocketError = 0)
        {
            // Preserve cumulative I/O even when a later send/receive fails.
            handshakeResult.reason = inReason;
            handshakeResult.stage = inStage;
            handshakeResult.socketError = inSocketError;
            handshakeResult.elapsedMs = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - started).count());
            handshakeResult.bufferedBytes = recvBuffer.size();
            return false;
        };

        // Only the connection worker reads this socket; keep receive cancellation bounded.
        const auto receiveData = [&]()
        {
            while (!inStopToken.stop_requested() && std::chrono::steady_clock::now() < deadline)
            {
                fd_set readable{};
                FD_SET(socket, &readable);
                timeval timeout{ 0, 100000 };
                const int ready = select(0, &readable, nullptr, nullptr, &timeout);
                if (ready == SOCKET_ERROR)
                {
                    const int socketError = WSAGetLastError();
                    return fail(inStopToken.stop_requested() ? "cancelled" : "socket-error",
                        "wait-read", socketError);
                }
                if (ready > 0)
                {
                    int socketError{};
                    if (ReceiveHandshakeData(socket, recvBuffer, socketError)) return true;
                    return fail(inStopToken.stop_requested() ? "cancelled"
                        : socketError == WSAETIMEDOUT ? "timeout"
                        : socketError ? "socket-error" : "peer-closed", "receive", socketError);
                }
            }
            return fail(inStopToken.stop_requested() ? "cancelled" : "timeout", "wait-read");
        };

        // Legacy callers still verify the connected IPv4 peer when no name was supplied.
        if (serverName.empty())
        {
            sockaddr_in peer{};
            int size = sizeof(peer);
            wchar_t address[INET_ADDRSTRLEN]{};
            if (getpeername(socket, reinterpret_cast<sockaddr*>(&peer), &size) != 0)
            {
                const int socketError = WSAGetLastError();
                lastStatus = SEC_E_WRONG_PRINCIPAL;
                return fail("socket-error", "peer-name", socketError);
            }
            if (peer.sin_family != AF_INET || !InetNtopW(AF_INET, &peer.sin_addr, address, INET_ADDRSTRLEN))
            {
                lastStatus = SEC_E_WRONG_PRINCIPAL;
                return fail("server-name-unavailable", "peer-name");
            }
            serverName = address;
        }

        CtxtHandle* context = nullptr;
        bool receiveRequired = false;
        while (true)
        {
            if (inStopToken.stop_requested() || std::chrono::steady_clock::now() >= deadline)
                return fail(inStopToken.stop_requested() ? "cancelled" : "timeout", "handshake");

            // EXTRA is already received input. Read again only when it is absent or incomplete.
            if (receiveRequired && !receiveData()) return false;

            SecBuffer outBuffers[1]{};
            outBuffers[0].BufferType = SECBUFFER_TOKEN;
            SecBufferDesc outBufferDesc{};
            outBufferDesc.cBuffers = 1;
            outBufferDesc.pBuffers = outBuffers;
            outBufferDesc.ulVersion = SECBUFFER_VERSION;

            SecBuffer inBuffers[2]{};
            SecBufferDesc inBufferDesc{};
            if (context)
            {
                inBuffers[0].pvBuffer = recvBuffer.data();
                inBuffers[0].cbBuffer = static_cast<DWORD>(recvBuffer.size());
                inBuffers[0].BufferType = SECBUFFER_TOKEN;
                inBuffers[1].BufferType = SECBUFFER_EMPTY;
                inBufferDesc.cBuffers = 2;
                inBufferDesc.pBuffers = inBuffers;
                inBufferDesc.ulVersion = SECBUFFER_VERSION;
            }

            DWORD contextAttributes{};
            ++handshakeResult.securityCalls;
            lastStatus = InitializeSecurityContext(
                &credHandle, context, serverName.data(),
                ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT | ISC_REQ_CONFIDENTIALITY | ISC_REQ_STREAM | ISC_REQ_ALLOCATE_MEMORY,
                0, SECURITY_NATIVE_DREP, context ? &inBufferDesc : nullptr, 0,
                &ctxtHandle, &outBufferDesc, &contextAttributes, nullptr);

            int socketError{};
            if (!SendHandshakeToken(socket, outBuffers[0], socketError))
                return fail(inStopToken.stop_requested() ? "cancelled"
                    : socketError == WSAETIMEDOUT ? "timeout"
                    : socketError ? "socket-error" : "send-no-progress", "send-token", socketError);

            if (lastStatus == SEC_E_OK)
            {
                if (!VerifyServerCertificate()) return fail("certificate-verification-failed", "verify-certificate");
                if (!FinalizeHandshake()) return fail("stream-sizes-failed", "finalize");
                PreserveExtraHandshakeData(recvBuffer, inBuffers[1]);
                pendingHandshakeData = std::move(recvBuffer);
                handshakeResult.reason = "succeeded";
                handshakeResult.stage = "complete";
                handshakeResult.elapsedMs = static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - started).count());
                handshakeResult.bufferedBytes = pendingHandshakeData.size();
                return true;
            }
            if (lastStatus == SEC_E_INCOMPLETE_MESSAGE)
            {
                // Preserve this partial token and append bytes before trying Schannel again.
                receiveRequired = true;
                continue;
            }
            if (lastStatus != SEC_I_CONTINUE_NEEDED)
                return fail("security-context-failed", "security-context");

            PreserveExtraHandshakeData(recvBuffer, inBuffers[1]);
            context = &ctxtHandle;
            receiveRequired = recvBuffer.empty();
        }
    }
    /** Verify Windows trust, validity, server EKU and the requested DNS/IP name.
     * No unknown-CA, name-mismatch or expiry exceptions are permitted.
     * Revocation retrieval is not requested (private test CAs have no CRL).
     */
    bool TLSHelperClient::VerifyServerCertificate()
    {
        PCCERT_CONTEXT rawCertificate{};
        lastStatus = QueryContextAttributes(&ctxtHandle, SECPKG_ATTR_REMOTE_CERT_CONTEXT, &rawCertificate);
        if (lastStatus != SEC_E_OK) return false;
        const auto freeCertificate = [](PCCERT_CONTEXT value) { CertFreeCertificateContext(value); };
        std::unique_ptr<const CERT_CONTEXT, decltype(freeCertificate)> certificate(rawCertificate, freeCertificate);
        CERT_CHAIN_PARA chainParameters{};
        chainParameters.cbSize = sizeof(chainParameters);
        LPSTR usage = const_cast<LPSTR>(szOID_PKIX_KP_SERVER_AUTH);
        chainParameters.RequestedUsage.dwType = USAGE_MATCH_TYPE_AND;
        chainParameters.RequestedUsage.Usage.cUsageIdentifier = 1;
        chainParameters.RequestedUsage.Usage.rgpszUsageIdentifier = &usage;
        PCCERT_CHAIN_CONTEXT rawChain{};
        if (!rawCertificate || !CertGetCertificateChain(nullptr, rawCertificate, nullptr,
            rawCertificate->hCertStore, &chainParameters, CERT_CHAIN_CACHE_ONLY_URL_RETRIEVAL, nullptr, &rawChain))
        { lastStatus = SEC_E_UNTRUSTED_ROOT; return false; }
        const auto freeChain = [](PCCERT_CHAIN_CONTEXT value) { CertFreeCertificateChain(value); };
        std::unique_ptr<const CERT_CHAIN_CONTEXT, decltype(freeChain)> chain(rawChain, freeChain);
        SSL_EXTRA_CERT_CHAIN_POLICY_PARA ssl{};
        ssl.cbSize = sizeof(ssl);
        ssl.dwAuthType = AUTHTYPE_SERVER;
        ssl.pwszServerName = serverName.data();
        CERT_CHAIN_POLICY_PARA policy{};
        policy.cbSize = sizeof(policy);
        policy.pvExtraPolicyPara = &ssl;
        CERT_CHAIN_POLICY_STATUS status{};
        status.cbSize = sizeof(status);
        if (!CertVerifyCertificateChainPolicy(CERT_CHAIN_POLICY_SSL, rawChain, &policy, &status) || status.dwError)
        {
            lastStatus = status.dwError ? static_cast<SECURITY_STATUS>(status.dwError) : SEC_E_CERT_UNKNOWN;
            return false;
        }
        return true;
    }

}
