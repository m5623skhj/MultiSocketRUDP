#pragma once
#define SECURITY_WIN32
#include <winsock2.h>
#include <windows.h>
#include <security.h>
#include <schannel.h>
#include <vector>
#include <string>
#include <optional>

#pragma comment(lib, "Secur32.lib")
#pragma comment(lib, "Crypt32.lib")

namespace TLSHelper
{
	namespace StoreNames
	{
		constexpr const wchar_t* MY = L"MY";
		constexpr const wchar_t* ROOT = L"ROOT";
		constexpr const wchar_t* CA = L"CA";
		constexpr const wchar_t* TRUST = L"TRUST";
		constexpr const wchar_t* AuthRoot = L"AuthRoot";
	};

	class TLSHelperBase
	{
	public:
		TLSHelperBase();
		virtual ~TLSHelperBase();

	public:
		virtual bool Initialize() = 0;
		virtual bool Handshake(SOCKET socket) = 0;

		[[nodiscard]]
		bool EncryptData(const char* plainData, size_t plainSize, char* encryptedBuffer, size_t& encryptedSize);
		[[nodiscard]]
		bool DecryptData(const char* encryptedData, size_t encryptedSize, char* plainBuffer, size_t& plainSize);
		[[nodiscard]]
		bool DecryptDataStream(std::vector<char>& encryptedStream, char* plainBuffer, size_t& plainSize);

	protected:
		CredHandle credHandle;
		CtxtHandle ctxtHandle;

		bool handshakeCompleted = false;
		SecPkgContext_StreamSizes streamSizes{};
	};

	class TLSHelperClient : public TLSHelperBase
	{
	public:
		~TLSHelperClient() override = default;

	public:
		[[nodiscard]]
		bool Initialize() override;
		[[nodiscard]]
		bool Handshake(SOCKET socket) override;
	};

	class TLSHelperServer : public TLSHelperBase
	{
	public:
		explicit TLSHelperServer(const std::wstring& inStoreName, const std::wstring& inCertSubjectName);
		~TLSHelperServer() override = default;

	public:
		[[nodiscard]]
		bool Initialize() override;
		[[nodiscard]]
		bool Handshake(SOCKET socket) override;

	private:
		std::wstring storeName{};
		std::wstring certSubjectName{};
	};
}