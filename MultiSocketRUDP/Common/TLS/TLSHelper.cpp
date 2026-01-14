#include "PreCompile.h"
#include "TLSHelper.h"
#include <vector>
#include <sstream>

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

    bool TLSHelperBase::EncryptData(const char* plainData, const size_t plainSize, char* encryptedBuffer, size_t& encryptedSize)
    {
        if (not handshakeCompleted)
        {
            return false;
        }

        memcpy(encryptedBuffer + streamSizes.cbHeader, plainData, plainSize);

        SecBuffer buffers[4];
        buffers[0].BufferType = SECBUFFER_STREAM_HEADER;
        buffers[0].pvBuffer = encryptedBuffer;
        buffers[0].cbBuffer = streamSizes.cbHeader;

        buffers[1].BufferType = SECBUFFER_DATA;
        buffers[1].pvBuffer = encryptedBuffer + streamSizes.cbHeader;
        buffers[1].cbBuffer = static_cast<unsigned long>(plainSize);

        buffers[2].BufferType = SECBUFFER_STREAM_TRAILER;
        buffers[2].pvBuffer = encryptedBuffer + streamSizes.cbHeader + plainSize;
        buffers[2].cbBuffer = streamSizes.cbTrailer;

        buffers[3].BufferType = SECBUFFER_EMPTY;
        buffers[3].pvBuffer = nullptr;
        buffers[3].cbBuffer = 0;

        SecBufferDesc bufferDesc;
        bufferDesc.cBuffers = 4;
        bufferDesc.pBuffers = buffers;
        bufferDesc.ulVersion = SECBUFFER_VERSION;

        if (const SECURITY_STATUS status = EncryptMessage(&ctxtHandle, 0, &bufferDesc, 0); status != SEC_E_OK)
        {
            return false;
        }

        encryptedSize = buffers[0].cbBuffer + buffers[1].cbBuffer + buffers[2].cbBuffer;
        return true;
    }

    bool TLSHelperBase::DecryptData(const char* encryptedData, const size_t encryptedSize, char* plainBuffer, size_t& plainSize)
    {
        if (not handshakeCompleted)
        {
            return false;
        }

        SecBufferDesc bufferDesc;
        SecBuffer buffers[2];

        buffers[0].BufferType = SECBUFFER_DATA;
        buffers[0].pvBuffer = reinterpret_cast<void*>(const_cast<char*>(encryptedData));
        buffers[0].cbBuffer = static_cast<unsigned long>(encryptedSize);

        buffers[1].BufferType = SECBUFFER_EMPTY;
        buffers[1].pvBuffer = nullptr;
        buffers[1].cbBuffer = 0;

        bufferDesc.cBuffers = 2;
        bufferDesc.pBuffers = buffers;
        bufferDesc.ulVersion = SECBUFFER_VERSION;

        if (const SECURITY_STATUS status = DecryptMessage(&ctxtHandle, &bufferDesc, 0, nullptr); status != SEC_E_OK && status != SEC_I_RENEGOTIATE)
        {
            return false;
        }

        const SecBuffer* dataBuf = nullptr;
        for (const auto& buffer : buffers)
        {
            if (buffer.BufferType == SECBUFFER_DATA)
            {
                dataBuf = &buffer;
                break;
            }
        }

        if (not dataBuf)
        {
            return false;
        }

        plainSize = dataBuf->cbBuffer;
        memcpy(plainBuffer, dataBuf->pvBuffer, plainSize);

        return true;
    }

    bool TLSHelperBase::DecryptDataStream(
        std::vector<char>& encryptedStream,
        char* plainBuffer,
        size_t& plainSize)
    {
        plainSize = 0;
        if (not handshakeCompleted)
        {
            return false;
        }

        while (true)
        {
            SecBuffer buffers[4];
            buffers[0].BufferType = SECBUFFER_DATA;
            buffers[0].pvBuffer = encryptedStream.data();
            buffers[0].cbBuffer = static_cast<unsigned long>(encryptedStream.size());

            buffers[1].BufferType = SECBUFFER_EMPTY;
            buffers[2].BufferType = SECBUFFER_EMPTY;
            buffers[3].BufferType = SECBUFFER_EMPTY;

            SecBufferDesc bufferDesc;
            bufferDesc.cBuffers = 4;
            bufferDesc.pBuffers = buffers;
            bufferDesc.ulVersion = SECBUFFER_VERSION;

            const SECURITY_STATUS status = DecryptMessage(&ctxtHandle, &bufferDesc, 0, nullptr);

            if (status == SEC_E_INCOMPLETE_MESSAGE)
            {
                return true;
            }

            if (status != SEC_E_OK && status != SEC_I_RENEGOTIATE)
            {
                return false;
            }

            const SecBuffer* dataBuf = nullptr;
            const SecBuffer* extraBuf = nullptr;

            for (auto& buffer : buffers)
            {
                if (buffer.BufferType == SECBUFFER_DATA)
                {
                    dataBuf = &buffer;
                }
                else if (buffer.BufferType == SECBUFFER_EXTRA)
                {
                    extraBuf = &buffer;
                }
            }

            if (not dataBuf)
            {
                return false;
            }

            memcpy(plainBuffer + plainSize, dataBuf->pvBuffer, dataBuf->cbBuffer);
            plainSize += dataBuf->cbBuffer;

            if (extraBuf)
            {
                std::vector newStream(
                    static_cast<char*>(extraBuf->pvBuffer),
                    static_cast<char*>(extraBuf->pvBuffer) + extraBuf->cbBuffer);

                encryptedStream.swap(newStream);
            }
            else
            {
                encryptedStream.clear();
                break;
            }
        }

        return true;
    }

    bool TLSHelperClient::Initialize()
    {
        SCHANNEL_CRED cred = {};
        cred.dwVersion = SCHANNEL_CRED_VERSION;
        cred.grbitEnabledProtocols = SP_PROT_TLS1_2_CLIENT;
        cred.dwFlags = SCH_CRED_MANUAL_CRED_VALIDATION;

        return AcquireCredentialsHandle(
            nullptr,
            const_cast<LPWSTR>(UNISP_NAME),
            SECPKG_CRED_OUTBOUND,
            nullptr,
            &cred,
            nullptr,
            nullptr,
            &credHandle,
            nullptr
        ) == SEC_E_OK;
    }

    bool TLSHelperClient::Handshake(const SOCKET socket)
    {
        handshakeCompleted = false;
        CtxtHandle* pContext = nullptr;
        std::vector<char> recvBuffer;
        recvBuffer.reserve(HANDSHAKE_BUFFER_SIZE);

        SECURITY_STATUS status;
        do
        {
            SecBuffer outBuffers[1] = {};
            outBuffers[0].BufferType = SECBUFFER_TOKEN;
            outBuffers[0].pvBuffer = nullptr;
            outBuffers[0].cbBuffer = 0;

            SecBufferDesc outBufferDesc = {};
            outBufferDesc.cBuffers = 1;
            outBufferDesc.pBuffers = outBuffers;
            outBufferDesc.ulVersion = SECBUFFER_VERSION;

            SecBuffer inBuffers[2] = {};
            SecBufferDesc inBufferDesc = {};

            if (pContext)
            {
                inBuffers[0].pvBuffer = recvBuffer.data();
                inBuffers[0].cbBuffer = static_cast<DWORD>(recvBuffer.size());
                inBuffers[0].BufferType = SECBUFFER_TOKEN;
                inBuffers[1].BufferType = SECBUFFER_EMPTY;

                inBufferDesc.cBuffers = 2;
                inBufferDesc.pBuffers = inBuffers;
                inBufferDesc.ulVersion = SECBUFFER_VERSION;
            }

            DWORD contextAttr;
            status = InitializeSecurityContext(
                &credHandle,
                pContext,
                nullptr,
                ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT | ISC_REQ_CONFIDENTIALITY | ISC_REQ_STREAM | ISC_REQ_ALLOCATE_MEMORY,
                0,
                SECURITY_NATIVE_DREP,
                pContext ? &inBufferDesc : nullptr,
                0,
                &ctxtHandle,
                &outBufferDesc,
                &contextAttr,
                nullptr
            );

            if (outBuffers[0].cbBuffer > 0 && outBuffers[0].pvBuffer)
            {
                int totalSent = 0;
                const int sendSize = static_cast<int>(outBuffers[0].cbBuffer);
                while (totalSent < sendSize)
                {
                    const int sent = send(socket
                        , static_cast<const char*>(outBuffers[0].pvBuffer) + totalSent
                        , sendSize - totalSent
                        , 0);

                    if (sent == SOCKET_ERROR)
                    {
                        FreeContextBuffer(outBuffers[0].pvBuffer);
                        return false;
                    }
                    totalSent += sent;
                }
                FreeContextBuffer(outBuffers[0].pvBuffer);
            }

            if (status == SEC_E_OK)
            {
                break;
            }

            if (status != SEC_I_CONTINUE_NEEDED && status != SEC_E_INCOMPLETE_MESSAGE)
            {
                return false;
            }

            if (pContext && (inBuffers[1].BufferType == SECBUFFER_EXTRA && inBuffers[1].cbBuffer > 0))
            {
                std::vector extraData(
                    recvBuffer.end() - inBuffers[1].cbBuffer,
                    recvBuffer.end()
                );
                recvBuffer = std::move(extraData);
            }
            else
            {
                recvBuffer.clear();
            }

            if (status == SEC_I_CONTINUE_NEEDED || status == SEC_E_INCOMPLETE_MESSAGE)
            {
                char tempBuffer[HANDSHAKE_BUFFER_SIZE];
                const int received = recv(socket, tempBuffer, sizeof(tempBuffer), 0);
                if (received <= 0)
                {
                    return false;
                }
                recvBuffer.insert(recvBuffer.end(), tempBuffer, tempBuffer + received);
            }

            pContext = &ctxtHandle;

        } while (status == SEC_I_CONTINUE_NEEDED || status == SEC_E_INCOMPLETE_MESSAGE);

        if (status != SEC_E_OK)
        {
            return false;
        }

        handshakeCompleted = true;
        QueryContextAttributes(&ctxtHandle, SECPKG_ATTR_STREAM_SIZES, &streamSizes);

        return true;
    }

    TLSHelperServer::TLSHelperServer(const std::wstring& inStoreName, const std::wstring& inCertSubjectName)
        : storeName(inStoreName)
        , certSubjectName(inCertSubjectName)
    {
    }

    bool TLSHelperServer::Initialize()
    {
        const HCERTSTORE hStore = CertOpenStore(
            CERT_STORE_PROV_SYSTEM,
            0,
            0,
            CERT_SYSTEM_STORE_CURRENT_USER,
            storeName.c_str()
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
            certSubjectName.c_str(),
            nullptr
        );

        if (nullptr == pCertContext)
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

        const SECURITY_STATUS status = AcquireCredentialsHandle(
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

    bool TLSHelperServer::Handshake(const SOCKET socket)
    {
        handshakeCompleted = false;
        CtxtHandle* pContext = nullptr;
        std::vector<char> recvBuffer;
        recvBuffer.reserve(HANDSHAKE_BUFFER_SIZE);

        SECURITY_STATUS status;
        do
        {
            char tempBuffer[HANDSHAKE_BUFFER_SIZE];
            const int received = recv(socket, tempBuffer, sizeof(tempBuffer), 0);
            if (received <= 0)
            {
                return false;
            }
            recvBuffer.insert(recvBuffer.end(), tempBuffer, tempBuffer + received);

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
            outBuffers[0].pvBuffer = nullptr;
            outBuffers[0].cbBuffer = 0;

            SecBufferDesc outBufferDesc = {};
            outBufferDesc.cBuffers = 1;
            outBufferDesc.pBuffers = outBuffers;
            outBufferDesc.ulVersion = SECBUFFER_VERSION;

            DWORD contextAttr;
            status = AcceptSecurityContext(
                &credHandle,
                pContext,
                &inBufferDesc,
                ASC_REQ_SEQUENCE_DETECT | ASC_REQ_REPLAY_DETECT | ASC_REQ_CONFIDENTIALITY | ASC_REQ_STREAM | ASC_REQ_ALLOCATE_MEMORY,
                SECURITY_NATIVE_DREP,
                &ctxtHandle,
                &outBufferDesc,
                &contextAttr,
                nullptr
            );

            if (outBuffers[0].cbBuffer > 0 && outBuffers[0].pvBuffer)
            {
                int totalSent = 0;
                const int sendSize = static_cast<int>(outBuffers[0].cbBuffer);
                while (totalSent < sendSize)
                {
                    const int sent = send(socket
                        , static_cast<const char*>(outBuffers[0].pvBuffer) + totalSent
                        , sendSize - totalSent
                        , 0
                    );

                    if (sent == SOCKET_ERROR)
                    {
                        FreeContextBuffer(outBuffers[0].pvBuffer);
                        return false;
                    }
                    totalSent += sent;
                }
                FreeContextBuffer(outBuffers[0].pvBuffer);
            }

            if (status == SEC_E_OK || status == SEC_I_CONTINUE_NEEDED)
            {
                if (inBuffers[1].BufferType == SECBUFFER_EXTRA && inBuffers[1].cbBuffer > 0)
                {
                    std::vector extraData(recvBuffer.end() - inBuffers[1].cbBuffer, recvBuffer.end());
                    recvBuffer = std::move(extraData);
                }
                else
                {
                    recvBuffer.clear();
                }
            }
            else if (status == SEC_E_INCOMPLETE_MESSAGE)
            {
                continue;
            }
            else
            {
                return false;
            }

            pContext = &ctxtHandle;

        } while (status == SEC_I_CONTINUE_NEEDED);

        if (status != SEC_E_OK)
        {
            return false;
        }

        handshakeCompleted = true;
        QueryContextAttributes(&ctxtHandle, SECPKG_ATTR_STREAM_SIZES, &streamSizes);

        return true;
    }
}