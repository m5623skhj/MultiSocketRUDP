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
	enum class TlsDecryptResult : uint8_t
	{
		None = 0,
		PlainData,
		CloseNotify,
		Error
	};

	namespace StoreNames
	{
		constexpr const wchar_t* MY = L"MY";
		constexpr const wchar_t* ROOT = L"ROOT";
		constexpr const wchar_t* CA = L"CA";
		constexpr const wchar_t* TRUST = L"TRUST";
		constexpr const wchar_t* AuthRoot = L"AuthRoot";
	};

	enum class ServerCertificateSource : unsigned char
	{
		Store,
		PfxFile
	};

	struct ServerCertificateConfig
	{
		ServerCertificateSource source{ ServerCertificateSource::Store };
		std::wstring storeName{};
		std::wstring certSubjectName{};
		std::wstring pfxFilePath{};
		std::wstring pfxPassword{};

		[[nodiscard]]
		static ServerCertificateConfig FromStore(const std::wstring& inStoreName, const std::wstring& inCertSubjectName)
		{
			ServerCertificateConfig config;
			config.source = ServerCertificateSource::Store;
			config.storeName = inStoreName;
			config.certSubjectName = inCertSubjectName;
			return config;
		}

		[[nodiscard]]
		static ServerCertificateConfig FromPfxFile(const std::wstring& inPfxFilePath, const std::wstring& inPfxPassword)
		{
			ServerCertificateConfig config;
			config.source = ServerCertificateSource::PfxFile;
			config.pfxFilePath = inPfxFilePath;
			config.pfxPassword = inPfxPassword;
			return config;
		}
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
		SECURITY_STATUS GetLastStatus() const
		{
			return lastStatus;
		}

		[[nodiscard]]
		bool EncryptData(const char* plainData, size_t plainSize, char* encryptedBuffer, size_t& encryptedSize);
		[[nodiscard]]
		bool DecryptData(const char* encryptedData, size_t encryptedSize, char* plainBuffer, size_t& plainSize);
		[[nodiscard]]
		TlsDecryptResult DecryptDataStream(std::vector<char>& encryptedStream, char* plainBuffer, size_t& plainSize);
		[[nodiscard]]
		bool EncryptCloseNotify(char* buffer, const size_t bufferSize, size_t& encryptedSize);

	protected:
		CredHandle credHandle;
		CtxtHandle ctxtHandle;
		SECURITY_STATUS lastStatus = SEC_E_OK;

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

	private:
		std::wstring serverName{};
	};

	class TLSHelperServer : public TLSHelperBase
	{
	public:
		explicit TLSHelperServer(ServerCertificateConfig inCertificateConfig);
		~TLSHelperServer() override = default;

	public:
		[[nodiscard]]
		bool Initialize() override;
		[[nodiscard]]
		bool Handshake(SOCKET socket) override;

	private:
		[[nodiscard]]
		bool InitializeFromStore();
		[[nodiscard]]
		bool InitializeFromPfxFile();

	private:
		ServerCertificateConfig certificateConfig{};
	};
}
