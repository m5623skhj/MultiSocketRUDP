#include "PreCompile.h"
#include "TLSHelper.h"
#include <fstream>
#include <array>
#include <utility>
#include <vector>
#include <chrono>

namespace TLSHelper
{
    namespace
    {
        [[nodiscard]]
        SECURITY_STATUS AcquireServerCredentials(PCCERT_CONTEXT certContext, OUT CredHandle& credHandle)
        {
            SCHANNEL_CRED cred = {};
            cred.dwVersion = SCHANNEL_CRED_VERSION;
            cred.grbitEnabledProtocols = SP_PROT_TLS1_2_SERVER;
            cred.cCreds = 1;
            cred.paCred = &certContext;
            cred.dwFlags = SCH_CRED_NO_DEFAULT_CREDS;

            return AcquireCredentialsHandle(
                nullptr,
                const_cast<LPWSTR>(UNISP_NAME),
                SECPKG_CRED_INBOUND,
                nullptr,
                &cred,
                nullptr,
                nullptr,
                &credHandle,
                nullptr
            );
        }
    }

    TLSHelperServer::TLSHelperServer(ServerCertificateConfig inCertificateConfig)
        : certificateConfig(std::move(inCertificateConfig))
    {
    }

    bool TLSHelperServer::Initialize()
    {
        switch (certificateConfig.source)
        {
        case ServerCertificateSource::Store:
            return InitializeFromStore();
        case ServerCertificateSource::PfxFile:
            return InitializeFromPfxFile();
        default:
            return false;
        }
    }

    bool TLSHelperServer::InitializeFromStore()
    {
        const HCERTSTORE hStore = CertOpenStore(
            CERT_STORE_PROV_SYSTEM,
            0,
            0,
            CERT_SYSTEM_STORE_CURRENT_USER,
            certificateConfig.storeName.c_str()
        );

        if (not hStore)
        {
            return false;
        }

        // A profile pin avoids subject-order selection when legacy certificates share a name.
        std::array<BYTE, 20> thumbprintBytes{};
        CRYPT_HASH_BLOB thumbprint{};
        DWORD findType = CERT_FIND_SUBJECT_STR;
        const void* findValue = certificateConfig.certSubjectName.c_str();
        if (not certificateConfig.certThumbprint.empty())
        {
            const auto& value = certificateConfig.certThumbprint;
            const auto digit = [](wchar_t inCharacter)
            {
                if (inCharacter >= L'0' && inCharacter <= L'9')
                {
                    return inCharacter - L'0';
                }

                if (inCharacter >= L'a' && inCharacter <= L'f')
                {
                    return inCharacter - L'a' + 10;
                }

                if (inCharacter >= L'A' && inCharacter <= L'F')
                {
                    return inCharacter - L'A' + 10;
                }

                return -1;
            };

            if (value.size() != thumbprintBytes.size() * 2)
            {
                lastStatus = SEC_E_INVALID_TOKEN;
                CertCloseStore(hStore, 0);
                return false;
            }

            for (size_t index = 0; index < thumbprintBytes.size(); ++index)
            {
                const int high = digit(value[index * 2]), low = digit(value[index * 2 + 1]);
                if (high < 0 || low < 0)
                {
                    lastStatus = SEC_E_INVALID_TOKEN;
                    CertCloseStore(hStore, 0);
                    return false;
                }

                thumbprintBytes[index] = static_cast<BYTE>((high << 4) | low);
            }

            thumbprint.cbData = static_cast<DWORD>(thumbprintBytes.size());
            thumbprint.pbData = thumbprintBytes.data();
            findType = CERT_FIND_HASH;
            findValue = &thumbprint;
        }

        PCCERT_CONTEXT pCertContext = CertFindCertificateInStore(
            hStore,
            X509_ASN_ENCODING,
            0,
            findType,
            findValue,
            nullptr
        );

        if (nullptr == pCertContext)
        {
            lastStatus = SEC_E_CERT_UNKNOWN;
            CertCloseStore(hStore, 0);
            return false;
        }

        lastStatus = AcquireServerCredentials(pCertContext, credHandle);

        CertFreeCertificateContext(pCertContext);
        CertCloseStore(hStore, 0);

        return lastStatus == SEC_E_OK;
    }

    bool TLSHelperServer::InitializeFromPfxFile()
    {
        std::ifstream pfxStream(certificateConfig.pfxFilePath, std::ios::binary | std::ios::ate);
        if (not pfxStream.is_open())
        {
            return false;
        }

        const std::streamsize pfxSize = pfxStream.tellg();
        if (pfxSize <= 0)
        {
            return false;
        }

        std::vector<char> pfxBuffer(static_cast<size_t>(pfxSize));
        pfxStream.seekg(0, std::ios::beg);
        if (not pfxStream.read(pfxBuffer.data(), pfxSize))
        {
            return false;
        }

        CRYPT_DATA_BLOB pfxBlob{};
        pfxBlob.cbData = static_cast<DWORD>(pfxBuffer.size());
        pfxBlob.pbData = reinterpret_cast<BYTE*>(pfxBuffer.data());

        const HCERTSTORE hStore = PFXImportCertStore(
            &pfxBlob,
            certificateConfig.pfxPassword.c_str(),
            CRYPT_EXPORTABLE
        );
        if (hStore == nullptr)
        {
            return false;
        }

        PCCERT_CONTEXT pCertContext = CertFindCertificateInStore(
            hStore,
            X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
            0,
            CERT_FIND_HAS_PRIVATE_KEY,
            nullptr,
            nullptr
        );
        if (pCertContext == nullptr)
        {
            CertCloseStore(hStore, 0);
            return false;
        }

        lastStatus = AcquireServerCredentials(pCertContext, credHandle);

        CertFreeCertificateContext(pCertContext);
        CertCloseStore(hStore, 0);

        return lastStatus == SEC_E_OK;
    }

    bool TLSHelperServer::Handshake(const SOCKET socket)
    {
        return Handshake(socket, std::stop_token{});
    }

    bool TLSHelperServer::Handshake(const SOCKET socket, const std::stop_token& stopToken)
    {
        if (ctxtHandle.dwLower || ctxtHandle.dwUpper)
        {
            DeleteSecurityContext(&ctxtHandle);
            ZeroMemory(&ctxtHandle, sizeof(ctxtHandle));
        }

        handshakeCompleted = false;
        pendingHandshakeData.clear();
        handshakeResult = {};
        std::vector<char> recvBuffer;
        const auto started = std::chrono::steady_clock::now();
        const auto deadline = connectionDeadline == (std::chrono::steady_clock::time_point::max)()
            ? started + std::chrono::seconds(12) : connectionDeadline;
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
            while (!stopToken.stop_requested() && std::chrono::steady_clock::now() < deadline)
            {
                fd_set readable{};
                FD_SET(socket, &readable);
                timeval timeout{ 0, 100000 };
                const int ready = select(0, &readable, nullptr, nullptr, &timeout);
                if (ready == SOCKET_ERROR)
                {
                    const int socketError = WSAGetLastError();
                    return fail(stopToken.stop_requested() ? "cancelled" : "socket-error",
                        "wait-read", socketError);
                }
                if (ready > 0)
                {
                    int socketError{};
                    if (ReceiveHandshakeData(socket, recvBuffer, socketError)) return true;
                    return fail(stopToken.stop_requested() ? "cancelled"
                        : socketError == WSAETIMEDOUT ? "timeout"
                        : socketError ? "socket-error" : "peer-closed", "receive", socketError);
                }
            }
            return fail(stopToken.stop_requested() ? "cancelled" : "timeout", "wait-read");
        };

        CtxtHandle* context = nullptr;
        bool receiveRequired = true;
        while (true)
        {
            if (stopToken.stop_requested() || std::chrono::steady_clock::now() >= deadline)
                return fail(stopToken.stop_requested() ? "cancelled" : "timeout", "handshake");

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
            inBuffers[0].pvBuffer = recvBuffer.data();
            inBuffers[0].cbBuffer = static_cast<DWORD>(recvBuffer.size());
            inBuffers[0].BufferType = SECBUFFER_TOKEN;
            inBuffers[1].BufferType = SECBUFFER_EMPTY;
            inBufferDesc.cBuffers = 2;
            inBufferDesc.pBuffers = inBuffers;
            inBufferDesc.ulVersion = SECBUFFER_VERSION;

            DWORD contextAttributes{};
            ++handshakeResult.securityCalls;
            lastStatus = AcceptSecurityContext(
                &credHandle, context, &inBufferDesc,
                ASC_REQ_SEQUENCE_DETECT | ASC_REQ_REPLAY_DETECT | ASC_REQ_CONFIDENTIALITY | ASC_REQ_STREAM | ASC_REQ_ALLOCATE_MEMORY,
                SECURITY_NATIVE_DREP, &ctxtHandle, &outBufferDesc, &contextAttributes, nullptr);

            int socketError{};
            if (!SendHandshakeToken(socket, outBuffers[0], socketError))
                return fail(stopToken.stop_requested() ? "cancelled"
                    : socketError == WSAETIMEDOUT ? "timeout"
                    : socketError ? "socket-error" : "send-no-progress", "send-token", socketError);

            if (lastStatus == SEC_E_OK)
            {
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
}
