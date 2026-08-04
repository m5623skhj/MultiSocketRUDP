#include "PreCompile.h"
#include "TLSHelper.h"
#include <vector>

namespace TLSHelper
{
    bool TLSHelperClient::Initialize()
    {
        SCHANNEL_CRED cred = {};
        cred.dwVersion = SCHANNEL_CRED_VERSION;
        cred.grbitEnabledProtocols = 0;
        cred.dwFlags = SCH_CRED_MANUAL_CRED_VALIDATION;

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
        handshakeCompleted = false;
        CtxtHandle* context = nullptr;
        std::vector<char> recvBuffer;

        while (true)
        {
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
                nullptr,
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
                return FinalizeHandshake();
            }

            if (lastStatus == SEC_E_INCOMPLETE_MESSAGE)
            {
                // Schannel needs the existing partial token plus additional socket data.
                if (not ReceiveHandshakeData(socket, recvBuffer))
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

            if (not ReceiveHandshakeData(socket, recvBuffer))
            {
                return false;
            }

            context = &ctxtHandle;
        }
    }
}
