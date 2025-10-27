#include "PreCompile.h"
#include "TLSHelper.h"
#include <vector>

#define SECURITY_WIN32

namespace TLSHelper
{
    constexpr size_t HANDSHAKE_BUFFER_SIZE = 4096;

    TLSHelperBase::TLSHelperBase()
    {
        ZeroMemory(&credHandle, sizeof(credHandle));
        ZeroMemory(&ctxtHandle, sizeof(ctxtHandle));
    }

    TLSHelperBase::~TLSHelperBase()
    {
        if (handshakeCompleted)
        {
            DeleteSecurityContext(&ctxtHandle);
        }
        if (credHandle.dwLower || credHandle.dwUpper)
        {
            FreeCredentialsHandle(&credHandle);
        }
    }

    bool TLSHelperBase::EncryptData(const char* plainData, size_t plainSize, char* encryptedBuffer, size_t& encryptedSize)
    {
        if (not handshakeCompleted)
        {
            return false;
        }

        SecBufferDesc bufferDesc;
        SecBuffer buffers[4];

        buffers[0].BufferType = SECBUFFER_DATA;
        buffers[0].pvBuffer = (void*)plainData;
        buffers[0].cbBuffer = static_cast<unsigned long>(plainSize);

        buffers[1].BufferType = SECBUFFER_STREAM_HEADER;
        buffers[1].pvBuffer = encryptedBuffer;
        buffers[1].cbBuffer = streamSizes.cbHeader;

        buffers[2].BufferType = SECBUFFER_STREAM_TRAILER;
        buffers[2].pvBuffer = encryptedBuffer + streamSizes.cbHeader + plainSize;
        buffers[2].cbBuffer = streamSizes.cbTrailer;

        buffers[3].BufferType = SECBUFFER_EMPTY;
        buffers[3].pvBuffer = nullptr;
        buffers[3].cbBuffer = 0;

        bufferDesc.cBuffers = 4;
        bufferDesc.pBuffers = buffers;
        bufferDesc.ulVersion = SECBUFFER_VERSION;

        SECURITY_STATUS status = EncryptMessage(&ctxtHandle, 0, &bufferDesc, 0);
        if (status != SEC_E_OK)
        {
            return false;
        }

        encryptedSize = buffers[1].cbBuffer + buffers[0].cbBuffer + buffers[2].cbBuffer;
        memcpy(encryptedBuffer + buffers[1].cbBuffer, plainData, plainSize);

        return true;
    }

    bool TLSHelperBase::DecryptData(const char* encryptedData, size_t encryptedSize, char* plainBuffer, size_t& plainSize)
    {
        if (not handshakeCompleted)
        {
            return false;
        }

        SecBufferDesc bufferDesc;
        SecBuffer buffers[2];

        buffers[0].BufferType = SECBUFFER_DATA;
        buffers[0].pvBuffer = (void*)encryptedData;
        buffers[0].cbBuffer = static_cast<unsigned long>(encryptedSize);

        buffers[1].BufferType = SECBUFFER_EMPTY;
        buffers[1].pvBuffer = nullptr;
        buffers[1].cbBuffer = 0;

        bufferDesc.cBuffers = 2;
        bufferDesc.pBuffers = buffers;
        bufferDesc.ulVersion = SECBUFFER_VERSION;

        SECURITY_STATUS status = DecryptMessage(&ctxtHandle, &bufferDesc, 0, nullptr);
        if (status != SEC_E_OK && status != SEC_I_RENEGOTIATE)
        {
            return false;
        }

        plainSize = buffers[0].cbBuffer;
        memcpy(plainBuffer, buffers[0].pvBuffer, plainSize);

        return true;
    }

    bool TLSHelperClient::Initialize()
    {
        SCHANNEL_CRED cred = {};
        cred.dwVersion = SCHANNEL_CRED_VERSION;
        cred.grbitEnabledProtocols = SP_PROT_TLS1_2_CLIENT;
        cred.dwFlags = SCH_CRED_MANUAL_CRED_VALIDATION;

        SECURITY_STATUS status = AcquireCredentialsHandle(
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

        return status == SEC_E_OK;
    }

    bool TLSHelperClient::Handshake(SOCKET socket)
    {
        handshakeCompleted = false;

        SecBufferDesc outBufferDesc;
        SecBuffer outBuffers[1];
        outBuffers[0].pvBuffer = malloc(HANDSHAKE_BUFFER_SIZE);
        outBuffers[0].cbBuffer = HANDSHAKE_BUFFER_SIZE;
        outBuffers[0].BufferType = SECBUFFER_TOKEN;
        outBufferDesc.cBuffers = 1;
        outBufferDesc.pBuffers = outBuffers;
        outBufferDesc.ulVersion = SECBUFFER_VERSION;

        std::vector<char> recvBuffer(HANDSHAKE_BUFFER_SIZE);
        SecBufferDesc inBufferDesc{};
        SecBuffer inBuffers[1];

        CtxtHandle* pContext = nullptr;
        DWORD contextAttr;
        SECURITY_STATUS status;

        do
        {
            status = InitializeSecurityContext(
                &credHandle,
                pContext,
                nullptr,
                ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT | ISC_REQ_CONFIDENTIALITY | ISC_REQ_ALLOCATE_MEMORY,
                0,
                SECURITY_NATIVE_DREP,
                pContext ? &inBufferDesc : nullptr,
                0,
                &ctxtHandle,
                &outBufferDesc,
                &contextAttr,
                nullptr
            );

            if (status != SEC_I_CONTINUE_NEEDED && status != SEC_E_OK)
            {
                free(outBuffers[0].pvBuffer);
                return false;
            }

            send(socket, (const char*)outBuffers[0].pvBuffer, outBuffers[0].cbBuffer, 0);
            if (status == SEC_E_OK)
            {
                break;
            }

            int received = 0;
            int totalReceived = 0;
            do
            {
                received = recv(socket, recvBuffer.data() + totalReceived, (int)recvBuffer.size() - totalReceived, 0);
                if (received <= 0)
                {
                    free(outBuffers[0].pvBuffer);
                    return false;
                }
                totalReceived += received;
            } while (totalReceived < 1);

            inBuffers[0].pvBuffer = recvBuffer.data();
            inBuffers[0].cbBuffer = totalReceived;
            inBuffers[0].BufferType = SECBUFFER_TOKEN;
            inBufferDesc.cBuffers = 1;
            inBufferDesc.pBuffers = inBuffers;
            inBufferDesc.ulVersion = SECBUFFER_VERSION;

            pContext = &ctxtHandle;

        } while (status == SEC_I_CONTINUE_NEEDED);

        handshakeCompleted = true;
        QueryContextAttributes(&ctxtHandle, SECPKG_ATTR_STREAM_SIZES, &streamSizes);

        free(outBuffers[0].pvBuffer);
        return true;
    }

    TLSHelperServer::TLSHelperServer(const std::wstring& inStoreName, const std::wstring& inCertSubjectName)
		: storeName(inStoreName)
		, certSubjectName(inCertSubjectName)
    {
    }

    bool TLSHelperServer::Initialize()
    {
        HCERTSTORE hStore = CertOpenStore(
            CERT_STORE_PROV_SYSTEM,
            0,
            0,
            CERT_SYSTEM_STORE_LOCAL_MACHINE,
			storeName.c_str()
        );
        if (!hStore)
            return false;

        PCCERT_CONTEXT pCertContext = CertFindCertificateInStore(
            hStore,
            X509_ASN_ENCODING,
            0,
            CERT_FIND_SUBJECT_STR,
			certSubjectName.c_str(),
            nullptr
        );

        if (not pCertContext)
        {
            CertCloseStore(hStore, 0);
            return false;
        }

        SCHANNEL_CRED cred = {};
        cred.dwVersion = SCHANNEL_CRED_VERSION;
        cred.grbitEnabledProtocols = SP_PROT_TLS1_2_SERVER;
        cred.cCreds = 1;
        cred.paCred = &pCertContext;
        cred.dwFlags = SCH_CRED_NO_DEFAULT_CREDS;

        SECURITY_STATUS status = AcquireCredentialsHandle(
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

        CertFreeCertificateContext(pCertContext);
        CertCloseStore(hStore, 0);

        return status == SEC_E_OK;
    }

    bool TLSHelperServer::Handshake(SOCKET socket)
    {
        handshakeCompleted = false;

        SecBufferDesc inBufferDesc, outBufferDesc;
        SecBuffer inBuffers[1], outBuffers[1];
        DWORD contextAttr;
        SECURITY_STATUS status;

        outBuffers[0].pvBuffer = malloc(HANDSHAKE_BUFFER_SIZE);
        outBuffers[0].cbBuffer = HANDSHAKE_BUFFER_SIZE;
        outBuffers[0].BufferType = SECBUFFER_TOKEN;

        outBufferDesc.cBuffers = 1;
        outBufferDesc.pBuffers = outBuffers;
        outBufferDesc.ulVersion = SECBUFFER_VERSION;

        CtxtHandle* pContext = nullptr;
        char recvBuffer[HANDSHAKE_BUFFER_SIZE];

        do
        {
            int received = 0;
            int totalReceived = 0;
            do
            {
                received = recv(socket, recvBuffer, sizeof(recvBuffer) - totalReceived, 0);
                if (received <= 0)
                {
                    free(outBuffers[0].pvBuffer);
                    return false;
                }
                totalReceived += received;
            } while (totalReceived < 1);

            inBuffers[0].pvBuffer = recvBuffer;
            inBuffers[0].cbBuffer = totalReceived;
            inBuffers[0].BufferType = SECBUFFER_TOKEN;
            inBufferDesc.cBuffers = 1;
            inBufferDesc.pBuffers = inBuffers;
            inBufferDesc.ulVersion = SECBUFFER_VERSION;

            status = AcceptSecurityContext(
                &credHandle,
                pContext,
                &inBufferDesc,
                ASC_REQ_SEQUENCE_DETECT | ASC_REQ_REPLAY_DETECT | ASC_REQ_CONFIDENTIALITY | ASC_REQ_ALLOCATE_MEMORY,
                SECURITY_NATIVE_DREP,
                &ctxtHandle,
                &outBufferDesc,
                &contextAttr,
                nullptr
            );

            if (status != SEC_I_CONTINUE_NEEDED && status != SEC_E_OK)
            {
                free(outBuffers[0].pvBuffer);
                return false;
            }

            int totalSent = 0;
            int sendSize = outBuffers[0].cbBuffer;
            while (totalSent < sendSize)
            {
                int sent = send(socket, (const char*)outBuffers[0].pvBuffer + totalSent, sendSize - totalSent, 0);
                if (sent == SOCKET_ERROR)
                {
                    free(outBuffers[0].pvBuffer);
                    return false;
                }
                totalSent += sent;
            }

            pContext = &ctxtHandle;

        } while (status == SEC_I_CONTINUE_NEEDED);

        handshakeCompleted = true;
        QueryContextAttributes(&ctxtHandle, SECPKG_ATTR_STREAM_SIZES, &streamSizes);

        free(outBuffers[0].pvBuffer);
        return true;
    }
}
