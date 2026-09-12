#include "PreCompile.h"
#include "TLSHelper.h"
#include <utility>
#include <vector>
#include <chrono>

namespace TLSHelper
{
	bool TLSHelperBase::SendProtocolVersion(const SOCKET socket, const uint32_t version)
	{
		const char plain[4] = { static_cast<char>(version >> 24), static_cast<char>(version >> 16),
			static_cast<char>(version >> 8), static_cast<char>(version) };
		std::vector<char> encrypted(streamSizes.cbHeader + sizeof(plain) + streamSizes.cbTrailer);
		size_t size{};
		if (not EncryptData(plain, sizeof(plain), encrypted.data(), size)) return false;
		for (size_t sent = 0; sent < size;)
		{
			const int result = send(socket, encrypted.data() + sent, static_cast<int>(size - sent), 0);
			if (result <= 0) return false;
			sent += result;
		}
		return true;
	}

	bool TLSHelperBase::ReceiveProtocolVersion(const SOCKET socket, const uint32_t expectedVersion, const std::stop_token& stopToken)
	{
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		std::vector<char> encrypted;
		std::vector<char> plain;
		constexpr size_t MAX_VERSION_STREAM = 65536;
		std::vector<char> decoded(MAX_VERSION_STREAM);
		size_t totalEncrypted{};
		while (not stopToken.stop_requested() && std::chrono::steady_clock::now() < deadline)
		{
			fd_set readable{};
			FD_SET(socket, &readable);
			timeval timeout{ 0, 100000 };
			const int ready = select(0, &readable, nullptr, nullptr, &timeout);
			if (ready == SOCKET_ERROR) return false;
			if (ready == 0) continue;
			char bytes[4096];
			const int received = recv(socket, bytes, sizeof(bytes), 0);
			if (received <= 0) return false;
			totalEncrypted += received;
			if (totalEncrypted > MAX_VERSION_STREAM) return false;
			encrypted.insert(encrypted.end(), bytes, bytes + received);
			size_t size{};
			const auto result = DecryptDataStream(encrypted, decoded.data(), size);
			if (result == TlsDecryptResult::Error || result == TlsDecryptResult::CloseNotify) return false;
			if (plain.size() + size > 4) return false;
			plain.insert(plain.end(), decoded.begin(), decoded.begin() + size);
			if (plain.size() == 4)
			{
				uint32_t version{};
				for (const unsigned char value : plain) version = (version << 8) | value;
				return version == expectedVersion && encrypted.empty();
			}
		}
		return false;
	}

    namespace
    {
        constexpr size_t HANDSHAKE_BUFFER_SIZE = 4096;

        class ContextBufferGuard
        {
        public:
            explicit ContextBufferGuard(SecBuffer& inBuffer)
                : buffer(inBuffer)
            {
            }

            ~ContextBufferGuard()
            {
                if (buffer.pvBuffer)
                {
                    FreeContextBuffer(buffer.pvBuffer);
                }
            }

            ContextBufferGuard(const ContextBufferGuard&) = delete;
            ContextBufferGuard& operator=(const ContextBufferGuard&) = delete;

        private:
            SecBuffer& buffer;
        };
    }

    TLSHelperBase::TLSHelperBase()
    {
        ZeroMemory(&credHandle, sizeof(credHandle));
        ZeroMemory(&ctxtHandle, sizeof(ctxtHandle));
    }

    TLSHelperBase::~TLSHelperBase()
    {
        // A timed-out or cancelled handshake may already own a partial context.
        if (ctxtHandle.dwLower || ctxtHandle.dwUpper)
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

    TlsDecryptResult TLSHelperBase::DecryptDataStream(
        std::vector<char>& encryptedStream,
        char* plainBuffer,
        size_t& plainSize)
    {
        plainSize = 0;
        if (not handshakeCompleted)
        {
            return TlsDecryptResult::Error;
        }

        while (not encryptedStream.empty())
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
                return TlsDecryptResult::None;
            }

            if (status == SEC_I_CONTEXT_EXPIRED)
            {
                encryptedStream.clear();
                return TlsDecryptResult::CloseNotify;
            }

            if (status != SEC_E_OK)
            {
                return TlsDecryptResult::Error;
            }

            const SecBuffer* dataBuf = nullptr;
            const SecBuffer* extraBuf = nullptr;

            for (const auto& buffer : buffers)
            {
                if (buffer.BufferType == SECBUFFER_DATA && buffer.cbBuffer > 0)
                {
                    dataBuf = &buffer;
                }
                else if (buffer.BufferType == SECBUFFER_EXTRA)
                {
                    extraBuf = &buffer;
                }
            }

            if (dataBuf)
            {
                memcpy(plainBuffer + plainSize, dataBuf->pvBuffer, dataBuf->cbBuffer);
                plainSize += dataBuf->cbBuffer;
            }

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

        return plainSize > 0 ? TlsDecryptResult::PlainData : TlsDecryptResult::None;
    }

    bool TLSHelperBase::EncryptCloseNotify(char* buffer, const size_t bufferSize, size_t& encryptedSize)
    {
        encryptedSize = 0;

        DWORD shutdownToken = SCHANNEL_SHUTDOWN;
        SecBuffer controlBuffer;
        controlBuffer.BufferType = SECBUFFER_TOKEN;
        controlBuffer.cbBuffer = sizeof(shutdownToken);
        controlBuffer.pvBuffer = &shutdownToken;

        SecBufferDesc controlDesc;
        controlDesc.ulVersion = SECBUFFER_VERSION;
        controlDesc.cBuffers = 1;
        controlDesc.pBuffers = &controlBuffer;

        if (ApplyControlToken(&ctxtHandle, &controlDesc) != SEC_E_OK)
        {
            return false;
        }

        SecBuffer buffers[1];
        buffers[0].BufferType = SECBUFFER_TOKEN;
        buffers[0].cbBuffer = static_cast<ULONG>(bufferSize);
        buffers[0].pvBuffer = buffer;

        SecBufferDesc desc;
        desc.ulVersion = SECBUFFER_VERSION;
        desc.cBuffers = 1;
        desc.pBuffers = buffers;

        if (EncryptMessage(&ctxtHandle, 0, &desc, 0) != SEC_E_OK)
        {
            return false;
        }

        encryptedSize = buffers[0].cbBuffer;
        return true;
    }

    bool TLSHelperBase::SendHandshakeToken(const SOCKET socket, SecBuffer& tokenBuffer)
    {
        ContextBufferGuard bufferGuard(tokenBuffer);
        if (tokenBuffer.cbBuffer == 0 || tokenBuffer.pvBuffer == nullptr)
        {
            return true;
        }

        int totalSent = 0;
        const int sendSize = static_cast<int>(tokenBuffer.cbBuffer);
        while (totalSent < sendSize)
        {
            const int sent = send(
                socket,
                static_cast<const char*>(tokenBuffer.pvBuffer) + totalSent,
                sendSize - totalSent,
                0);
            if (sent <= 0)
            {
                return false;
            }

            totalSent += sent;
        }

        return true;
    }

    bool TLSHelperBase::ReceiveHandshakeData(const SOCKET socket, std::vector<char>& recvBuffer)
    {
        char tempBuffer[HANDSHAKE_BUFFER_SIZE];
        const int received = recv(socket, tempBuffer, sizeof(tempBuffer), 0);
        if (received <= 0)
        {
            return false;
        }

        recvBuffer.insert(recvBuffer.end(), tempBuffer, tempBuffer + received);
        return true;
    }

    void TLSHelperBase::PreserveExtraHandshakeData(std::vector<char>& recvBuffer, const SecBuffer& extraBuffer)
    {
        if (extraBuffer.BufferType != SECBUFFER_EXTRA || extraBuffer.cbBuffer == 0)
        {
            recvBuffer.clear();
            return;
        }

        const auto* extraDataBegin = static_cast<const char*>(extraBuffer.pvBuffer);
        std::vector<char> extraData(extraDataBegin, extraDataBegin + extraBuffer.cbBuffer);
        recvBuffer = std::move(extraData);
    }

    bool TLSHelperBase::FinalizeHandshake()
    {
        lastStatus = QueryContextAttributes(&ctxtHandle, SECPKG_ATTR_STREAM_SIZES, &streamSizes);
        if (lastStatus != SEC_E_OK)
        {
            DeleteSecurityContext(&ctxtHandle);
            ZeroMemory(&ctxtHandle, sizeof(ctxtHandle));
            return false;
        }

        handshakeCompleted = true;
        return true;
    }
}
