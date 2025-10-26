#pragma once
#define SECURITY_WIN32
#include <winsock2.h>
#include <windows.h>
#include <security.h>
#include <schannel.h>

#pragma comment(lib, "Secur32.lib")
#pragma comment(lib, "Crypt32.lib")

namespace TLSHelper
{
	class TLSHelperBase
	{
	public:
		TLSHelperBase();
		virtual ~TLSHelperBase();

	public:
		virtual bool Initialize() = 0;
		virtual bool Handshake(SOCKET socket) = 0;

		bool EncryptData(const char* plainData, size_t plainSize, char* encryptedBuffer, size_t& encryptedSize);
		bool DecryptData(const char* encryptedData, size_t encryptedSize, char* plainBuffer, size_t& plainSize);

	protected:
		CredHandle credHandle;
		CtxtHandle ctxtHandle;

		bool handshakeCompleted = false;
		SecPkgContext_StreamSizes streamSizes{};
	};

	class TLSHelperClient : public TLSHelperBase
	{
	public:
		bool Initialize() override;
		bool Handshake(SOCKET socket) override;
	};

	class TLSHelperServer : public TLSHelperBase
	{
	public:
		bool Initialize() override;
		bool Handshake(SOCKET socket) override;
	};
}