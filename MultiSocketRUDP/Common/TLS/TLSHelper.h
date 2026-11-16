#pragma once
#define SECURITY_WIN32
#include <winsock2.h>
#include <windows.h>
#include <security.h>
#include <schannel.h>
#include <cstdint>
#include <vector>
#include <string>
#include <optional>
#include <stop_token>
#include <utility>
#include <chrono>

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
		std::wstring certThumbprint{};
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
		static ServerCertificateConfig FromStoreThumbprint(const std::wstring& inStoreName, const std::wstring& inThumbprint)
		{
			ServerCertificateConfig config;
			config.storeName = inStoreName;
			config.certThumbprint = inThumbprint;
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

	struct HandshakeResult
	{
		const char* reason{ "not-started" };
		const char* stage{ "not-started" };
		int socketError{};
		std::uint64_t elapsedMs{};
		size_t bufferedBytes{};
		// Handshake only: actual successful send/recv bytes, including partial sends.
		// send/recv calls count attempted system calls; securityCalls counts SSPI calls.
		std::uint64_t sentBytes{}, receivedBytes{};
		std::uint64_t sendCalls{}, receiveCalls{}, securityCalls{};
	};

	struct ProtocolVersionReceiveResult
	{
		const char* reason{ "not-started" };
		std::optional<uint32_t> receivedVersion;
		size_t plainBytes{};
		int socketError{};
	};

	class TLSHelperBase
	{
	public:
		TLSHelperBase();
		virtual ~TLSHelperBase();

	public:
        // Owned by the connection worker; the same deadline covers all broker phases.
        void SetConnectionDeadline(std::chrono::steady_clock::time_point value) { connectionDeadline = value; }
        auto GetConnectionDeadline() const { return connectionDeadline; }
        bool ApplyRemainingTimeout(SOCKET socket) const;
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
		bool SendProtocolVersion(SOCKET socket, uint32_t version);
		bool ReceiveProtocolVersion(SOCKET socket, uint32_t expectedVersion, const std::stop_token& stopToken);
		// Inspect on the connection worker after Handshake returns; no token bytes are retained here.
		[[nodiscard]] const HandshakeResult& GetHandshakeResult() const { return handshakeResult; }
		[[nodiscard]] const ProtocolVersionReceiveResult& GetProtocolVersionResult() const { return protocolVersionResult; }
		// Connection-thread only: transfer trailing handshake bytes to the first application receive.
		[[nodiscard]] std::vector<char> TakePendingHandshakeData()
		{
			auto data = std::move(pendingHandshakeData);
			pendingHandshakeData.clear();
			return data;
		}

	protected:
		[[nodiscard]]
		bool SendHandshakeToken(SOCKET socket, SecBuffer& tokenBuffer, int& outSocketError);
		[[nodiscard]]
		bool ReceiveHandshakeData(SOCKET socket, std::vector<char>& recvBuffer, int& outSocketError);
		static void PreserveExtraHandshakeData(std::vector<char>& recvBuffer, const SecBuffer& extraBuffer);
		[[nodiscard]]
		bool FinalizeHandshake();

	protected:
		CredHandle credHandle;
		CtxtHandle ctxtHandle;
		SECURITY_STATUS lastStatus = SEC_E_OK;

        std::chrono::steady_clock::time_point connectionDeadline = (std::chrono::steady_clock::time_point::max)();
		bool handshakeCompleted = false;
		SecPkgContext_StreamSizes streamSizes{};
		std::vector<char> pendingHandshakeData;
		HandshakeResult handshakeResult;
		ProtocolVersionReceiveResult protocolVersionResult;
	};

	class TLSHelperClient : public TLSHelperBase
	{
	public:
		~TLSHelperClient() override = default;

	public:
		[[nodiscard]]
		bool Initialize() override;
		// Set on the connection worker before starting a handshake; never during I/O.
		void SetServerName(std::wstring inServerName) { serverName = std::move(inServerName); }
		[[nodiscard]]
		bool Handshake(SOCKET socket) override;
		[[nodiscard]]
		// Client receive waits share a deadline and observe startup cancellation.
		bool Handshake(SOCKET socket, const std::stop_token& inStopToken);

	private:
		[[nodiscard]] bool VerifyServerCertificate();
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
		[[nodiscard]]
		// Receive waits share the broker deadline and observe cancellation.
		bool Handshake(SOCKET socket, const std::stop_token& stopToken);

	private:
		[[nodiscard]]
		bool InitializeFromStore();
		[[nodiscard]]
		bool InitializeFromPfxFile();

	private:
		ServerCertificateConfig certificateConfig{};
	};
}
