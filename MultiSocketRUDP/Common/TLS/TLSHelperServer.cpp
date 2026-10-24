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
                if (inCharacter >= L'0' && inCharacter <= L'9') return inCharacter - L'0';
                if (inCharacter >= L'a' && inCharacter <= L'f') return inCharacter - L'a' + 10;
                if (inCharacter >= L'A' && inCharacter <= L'F') return inCharacter - L'A' + 10;
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
        CtxtHandle* context = nullptr;
        std::vector<char> recvBuffer;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);

        while (true)
        {
            // Poll readiness so stopping does not depend on shutdown interrupting recv.
            // A single deadline also bounds peers that trickle incomplete TLS records.
            while (true)
            {
                if (stopToken.stop_requested() || std::chrono::steady_clock::now() >= deadline)
                {
                    return false;
                }
                fd_set readable{};
                FD_SET(socket, &readable);
                timeval timeout{ 0, 100000 };
                const int ready = select(0, &readable, nullptr, nullptr, &timeout);
                if (ready == SOCKET_ERROR)
                {
                    return false;
                }
                if (ready > 0)
                {
                    break;
                }
            }
            if (not ReceiveHandshakeData(socket, recvBuffer))
            {
                return false;
            }

            SecBuffer inBuffers[2] = {};
            inBuffers[0].pvBuffer = recvBuffer.data();
            inBuffers[0].cbBuffer = static_cast<DWORD>(recvBuffer.size());
            inBuffers[0].BufferType = SECBUFFER_TOKEN;
            inBuffers[1].BufferType = SECBUFFER_EMPTY;

            SecBufferDesc inBufferDesc = {};
            inBufferDesc.cBuffers = 2;
            inBufferDesc.pBuffers = inBuffers;
            inBufferDesc.ulVersion = SECBUFFER_VERSION;

            SecBuffer outBuffers[1] = {};
            outBuffers[0].BufferType = SECBUFFER_TOKEN;

            SecBufferDesc outBufferDesc = {};
            outBufferDesc.cBuffers = 1;
            outBufferDesc.pBuffers = outBuffers;
            outBufferDesc.ulVersion = SECBUFFER_VERSION;

            DWORD contextAttributes = 0;
            lastStatus = AcceptSecurityContext(
                &credHandle,
                context,
                &inBufferDesc,
                ASC_REQ_SEQUENCE_DETECT | ASC_REQ_REPLAY_DETECT | ASC_REQ_CONFIDENTIALITY | ASC_REQ_STREAM | ASC_REQ_ALLOCATE_MEMORY,
                SECURITY_NATIVE_DREP,
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
                if (not FinalizeHandshake()) return false;
                PreserveExtraHandshakeData(recvBuffer, inBuffers[1]);
                pendingHandshakeData = std::move(recvBuffer);
                return true;
            }

            if (lastStatus == SEC_E_INCOMPLETE_MESSAGE)
            {
                // Keep the partial token and append more bytes on the next iteration.
                continue;
            }

            if (lastStatus != SEC_I_CONTINUE_NEEDED)
            {
                return false;
            }

            PreserveExtraHandshakeData(recvBuffer, inBuffers[1]);
            context = &ctxtHandle;
        }
    }
}
