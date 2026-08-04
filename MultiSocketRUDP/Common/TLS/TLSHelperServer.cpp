#include "PreCompile.h"
#include "TLSHelper.h"
#include <fstream>
#include <utility>
#include <vector>

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

        PCCERT_CONTEXT pCertContext = CertFindCertificateInStore(
            hStore,
            X509_ASN_ENCODING,
            0,
            CERT_FIND_SUBJECT_STR,
            certificateConfig.certSubjectName.c_str(),
            nullptr
        );

        if (nullptr == pCertContext)
        {
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
        if (handshakeCompleted)
        {
            DeleteSecurityContext(&ctxtHandle);
            ZeroMemory(&ctxtHandle, sizeof(ctxtHandle));
        }

        handshakeCompleted = false;
        CtxtHandle* context = nullptr;
        std::vector<char> recvBuffer;

        while (true)
        {
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
                return FinalizeHandshake();
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
