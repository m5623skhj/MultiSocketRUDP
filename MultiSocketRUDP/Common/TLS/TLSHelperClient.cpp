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
        // Legacy callers did not provide a name. Verify their connected IP instead
        // of silently retaining the former no-validation behavior.
        if (serverName.empty())
        {
            sockaddr_in peer{};
            int size = sizeof(peer);
            wchar_t address[INET_ADDRSTRLEN]{};
            if (getpeername(socket, reinterpret_cast<sockaddr*>(&peer), &size) != 0 ||
                peer.sin_family != AF_INET || !InetNtopW(AF_INET, &peer.sin_addr, address, INET_ADDRSTRLEN))
            { lastStatus = SEC_E_WRONG_PRINCIPAL; return false; }
            serverName = address;
        }
        CtxtHandle* context = nullptr;
        std::vector<char> recvBuffer;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(6);
        // Only this thread reads the socket; readiness polling makes receive cancellable.
        const auto receiveData = [&]()
        {
            while (!inStopToken.stop_requested() && std::chrono::steady_clock::now() < deadline)
            {
                fd_set readable{};
                FD_SET(socket, &readable);
                timeval timeout{ 0, 100000 };
                const int ready = select(0, &readable, nullptr, nullptr, &timeout);
                if (ready == SOCKET_ERROR) return false;
                if (ready > 0) return ReceiveHandshakeData(socket, recvBuffer);
            }
            return false;
        };

        while (true)
        {
            if (inStopToken.stop_requested() || std::chrono::steady_clock::now() >= deadline)
            {
                return false;
            }
            SecBuffer outBuffers[1] = {};
            outBuffers[0].BufferType = SECBUFFER_TOKEN;

            SecBufferDesc outBufferDesc = {};
            outBufferDesc.cBuffers = 1;
            outBufferDesc.pBuffers = outBuffers;
            outBufferDesc.ulVersion = SECBUFFER_VERSION;

            SecBuffer inBuffers[2] = {};
            SecBufferDesc inBufferDesc = {};
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

            DWORD contextAttributes = 0;
            lastStatus = InitializeSecurityContext(
                &credHandle,
                context,
                serverName.data(),
                ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT | ISC_REQ_CONFIDENTIALITY | ISC_REQ_STREAM | ISC_REQ_ALLOCATE_MEMORY,
                0,
                SECURITY_NATIVE_DREP,
                context ? &inBufferDesc : nullptr,
                0,
                &ctxtHandle,
                &outBufferDesc,
                &contextAttributes,
                nullptr
            );

            if (not SendHandshakeToken(socket, outBuffers[0]))
            {
                return false;
            }

            if (lastStatus == SEC_E_OK)
            {
                if (not VerifyServerCertificate() || not FinalizeHandshake()) return false;
                PreserveExtraHandshakeData(recvBuffer, inBuffers[1]);
                pendingHandshakeData = std::move(recvBuffer);
                return true;
            }

            if (lastStatus == SEC_E_INCOMPLETE_MESSAGE)
            {
                // Schannel needs the existing partial token plus additional socket data.
                if (not receiveData())
                {
                    return false;
                }
                continue;
            }

            if (lastStatus != SEC_I_CONTINUE_NEEDED)
            {
                return false;
            }

            if (context)
            {
                PreserveExtraHandshakeData(recvBuffer, inBuffers[1]);
            }
            else
            {
                recvBuffer.clear();
            }

            if (not receiveData())
            {
                return false;
            }

            context = &ctxtHandle;
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
