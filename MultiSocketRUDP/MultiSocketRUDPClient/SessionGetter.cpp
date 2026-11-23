#include "PreCompile.h"
#include "RUDPClientCore.h"
#include "Logger.h"
#include "LogExtension.h"
#include "../Common/Crypto/CryptoHelper.h"
#include "ClientOptionFile.h"
#include <chrono>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <string_view>
#include <mstcpip.h>

#if USE_IOCP_SESSION_GETTER
bool RUDPClientCore::SessionGetter::Start(const std::wstring& optionFilePath)
{
	if (CNetClient::Start(optionFilePath.c_str()) == false)
	{
		auto log = Logger::MakeLogObject<ClientLog>();
		log->logString = "SessionGetter::Start failed";
		Logger::GetInstance().WriteLog(log);
		return false;
	}

	return true;
}

void RUDPClientCore::SessionGetter::OnConnectionComplete()
{
}

void RUDPClientCore::SessionGetter::OnRecv(CNetServerSerializationBuf* recvBuffer)
{

}

void RUDPClientCore::SessionGetter::OnSend(int sendSize)
{
}

void RUDPClientCore::SessionGetter::OnWorkerThreadBegin()
{
}

void RUDPClientCore::SessionGetter::OnWorkerThreadEnd()
{
}

void RUDPClientCore::SessionGetter::OnError(st_Error* error)
{
	std::cout << "sessionGetter::OnError : ServerErr " << error->ServerErr << " GetLastError " << error->GetLastErr << std::endl;
}

#else

namespace
{
	std::uint64_t ElapsedMilliseconds(const std::chrono::steady_clock::time_point inStarted)
	{
		return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now() - inStarted).count());
	}

	// Connection-worker only. Capture API errors before calling this helper or cleaning up resources.
	// No endpoint credentials, TLS tokens, keys or session payloads are recorded.
	void LogBrokerStage(const std::uint64_t inConnectionId, const char* inStage, const char* inReason,
		const std::chrono::steady_clock::time_point inStarted,
		const char* inErrorDomain = "none", const std::int64_t inErrorCode = 0,
		const std::string_view inDetails = {}) noexcept
	{
		try
		{
			const auto log = Logger::MakeLogObject<ClientLog>();
			log->logString = std::format(
				"Client broker connection; process={}; connection={}; stage={}; reason={}; elapsed_ms={}; error_domain={}; error_code={}; error_hex={:#010x}{}",
				GetCurrentProcessId(), inConnectionId, inStage, inReason, ElapsedMilliseconds(inStarted),
				inErrorDomain, inErrorCode, static_cast<std::uint32_t>(inErrorCode), inDetails);
			if (std::string_view(inReason) == "started")
				(void)Logger::GetInstance().TryWriteLog(log);
			else
				Logger::GetInstance().WriteLog(log); // Retain terminal outcomes in the ordinary bounded queue.
		}
		catch (...)
		{
			Logger::GetInstance().RecordDroppedDiagnostic();
		}
	}

	void LogClientProgress(const std::uint64_t inConnectionId, const char* inEvent,
		const std::uint64_t inElapsedMs) noexcept
	{
		try
		{
			const auto log = Logger::MakeLogObject<ClientLog>();
			log->logString = std::format(
				"Client broker TLS progress; process={}; connection={}; event={}; elapsed_ms={}",
				GetCurrentProcessId(), inConnectionId, inEvent, inElapsedMs);
			(void)Logger::GetInstance().TryWriteLog(log);
		}
		catch (...)
		{
			Logger::GetInstance().RecordDroppedDiagnostic();
		}
	}

	bool WanBrokerTraceEnabled() noexcept
	{
		wchar_t enabled[2]{};
		return GetEnvironmentVariableW(L"ACTIONRPG_BOT_WAN_TCP_TRACE", enabled, 2) == 1 && enabled[0] == L'1';
	}

	// Query the same socket before it closes. TCP information is diagnostic only.
	void LogClientSocketProbe(SOCKET inSocket, std::uint64_t inConnectionId, const char* inStage) noexcept
	{
		const auto started = std::chrono::steady_clock::now();
		try
		{
			sockaddr_in local{}, peer{};
			int localSize = sizeof(local), peerSize = sizeof(peer);
			char localText[INET_ADDRSTRLEN]{}, peerText[INET_ADDRSTRLEN]{};
			const bool haveLocal = getsockname(inSocket, reinterpret_cast<sockaddr*>(&local), &localSize) == 0
				&& InetNtopA(AF_INET, &local.sin_addr, localText, sizeof(localText));
			const int localError = haveLocal ? 0 : WSAGetLastError();
			const bool havePeer = getpeername(inSocket, reinterpret_cast<sockaddr*>(&peer), &peerSize) == 0
				&& InetNtopA(AF_INET, &peer.sin_addr, peerText, sizeof(peerText));
			const int peerError = havePeer ? 0 : WSAGetLastError();
			auto details = haveLocal ? std::format("; local={}:{}", localText, ntohs(local.sin_port))
				: std::format("; local_error={}", localError);
			details += havePeer ? std::format("; peer={}:{}", peerText, ntohs(peer.sin_port))
				: std::format("; peer_error={}", peerError);

			ULONG version = 0;
			DWORD returned{};
			TCP_INFO_v0 info{};
			const int result = WSAIoctl(inSocket, SIO_TCP_INFO, &version, sizeof(version),
				&info, sizeof(info), &returned, nullptr, nullptr);
			const int infoError = result == SOCKET_ERROR ? WSAGetLastError() : 0;
			if (result == 0 && returned >= sizeof(info))
				details += std::format("; tcp_state={}; rtt_us={}; tcp_tx={}; tcp_rx={}; tcp_in_flight={}; tcp_retrans_bytes={}; tcp_syn_retrans={}; tcp_timeouts={}",
					static_cast<int>(info.State), info.RttUs, info.BytesOut, info.BytesIn, info.BytesInFlight,
					info.BytesRetrans, static_cast<unsigned>(info.SynRetrans), info.TimeoutEpisodes);
			else
				details += std::format("; tcp_info_error={}; tcp_info_bytes={}", infoError, returned);
			LogBrokerStage(inConnectionId, inStage, "observed", started, "none", 0, details);
		}
		catch (...) { Logger::GetInstance().RecordDroppedDiagnostic(); }
	}

	// Counters contain sizes only; neither TLS tokens nor certificate/key data are logged.
	void LogClientHandshake(const TLSHelper::TLSHelperClient& inTlsHelper, const bool inSucceeded,
		const std::uint64_t inConnectionId, const std::uint64_t inCredentialMs,
		const std::uint64_t inConnectMs) noexcept
	{
		try
		{
			const auto& result = inTlsHelper.GetHandshakeResult();
			auto message = std::format(
				"TLS Handshake {} in GetSessionFromServer(); process={}; connection={}; credential_ms={}; connect_ms={}; reason={}; stage={}; socket_error={}; elapsed_ms={}; buffered_bytes={}; security_status={:#010x}; tls_sent_bytes={}; tls_received_bytes={}; send_calls={}; recv_calls={}; security_calls={}",
				inSucceeded ? "succeeded" : "failed", GetCurrentProcessId(), inConnectionId, inCredentialMs, inConnectMs,
				result.reason, result.stage, result.socketError, result.elapsedMs, result.bufferedBytes,
				static_cast<unsigned long>(inTlsHelper.GetLastStatus()), result.sentBytes, result.receivedBytes,
				result.sendCalls, result.receiveCalls, result.securityCalls);
			if (inTlsHelper.HasHandshakeTrace())
				message += std::format("; hello_tag={}; first_send_utc_ms={}; first_recv_utc_ms={}",
					result.clientHelloTag ? std::format("{:016x}", *result.clientHelloTag) : "unavailable",
					result.firstSendUtcMs, result.firstReceiveUtcMs);
			if (inSucceeded)
			{
				const auto log = Logger::MakeLogObject<ClientLog>();
				log->logString = message;
				Logger::GetInstance().WriteLog(log);
			}
			else
			{
				LOG_ERROR(message);
			}
		}
		catch (...)
		{
			Logger::GetInstance().RecordDroppedDiagnostic();
		}
	}
}

bool RUDPClientCore::RunGetSessionFromServer(const std::wstring& optionFilePath, const std::stop_token& inStopToken)
{
	// Process-local sequence also separates simultaneous clients and reconnect attempts.
	static std::atomic_uint64_t nextConnectionId{};
	const auto connectionId = nextConnectionId.fetch_add(1, std::memory_order_relaxed) + 1;
	wanBrokerConnectionId = WanBrokerTraceEnabled() ? connectionId : 0;
	const auto startupStarted = std::chrono::steady_clock::now();
	try
	{
		tlsHelper.SetServerName(sessionBrokerIP);
		LogClientProgress(connectionId, "credential_initialize_started", 0);
		const auto credentialStarted = std::chrono::steady_clock::now();
		const bool initialized = tlsHelper.Initialize();
		const auto credentialMs = ElapsedMilliseconds(credentialStarted);
		if (not initialized)
		{
			LOG_ERROR(std::format(
				"RUDPClientCore::tlsHelper.Initialize() failed with security status {:#010x}; process={}; connection={}; credential_ms={}",
				static_cast<unsigned long>(tlsHelper.GetLastStatus()), GetCurrentProcessId(), connectionId, credentialMs));
			return false;
		}
		LogClientProgress(connectionId, "credential_ready", credentialMs);

		if (not GetSessionFromServer(inStopToken, connectionId, credentialMs))
		{
			LOG_ERROR(std::format("RUDPClientCore::GetSessionFromServer() failed; process={}; connection={}", GetCurrentProcessId(), connectionId));
			return false;
		}

		return true;
	}
	catch (...)
	{
		LogBrokerStage(connectionId, "broker-startup", "exception", startupStarted);
		throw; // Preserve the caller's existing startup/cleanup policy.
	}
}

bool RUDPClientCore::ReadSessionGetterOptionFile(const std::wstring& optionFilePath)
{
	const auto started = std::chrono::steady_clock::now();
	const auto fail = [started](const char* inReason)
	{
		// Configuration is checked before a connection ID is allocated; zero denotes pre-connection work.
		LogBrokerStage(0, "configuration", inReason, started);
		return false;
	};
	hostLoopback = false;
	ClientOptionFile options;
	if (not options.Load(optionFilePath))
	{
		return fail("option-load-failed");
	}

	const auto ip = options.GetValue(L"SESSION_BROKER", L"IP");
	if (not ip || ip->size() < 3 || ip->front() != L'"' || ip->back() != L'"')
	{
		return fail("invalid-address-option");
	}

	const auto address = ip->substr(1, ip->size() - 2);
	if (address.empty() || address.size() >= std::size(sessionBrokerIP) ||
		!std::all_of(address.begin(), address.end(), [](wchar_t c)
		{
			return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') ||
				(c >= L'0' && c <= L'9') || c == L'.' || c == L'-';
		}))
	{
		return fail("invalid-address");
	}

	unsigned int brokerPort{}, packetCode{}, packetKey{};
	if (not options.GetNumber(L"SESSION_BROKER", L"PORT", 1, 65535, brokerPort) ||
		not options.GetNumber(L"SERIALIZEBUF", L"PACKET_CODE", 0, 255, packetCode) ||
		not options.GetNumber(L"SERIALIZEBUF", L"PACKET_KEY", 0, 255, packetKey))
	{
		return fail("invalid-port-or-packet-options");
	}

	unsigned int loopback{};
	if (options.GetValue(L"SESSION_BROKER", L"HOST_LOOPBACK") &&
		not options.GetNumber(L"SESSION_BROKER", L"HOST_LOOPBACK", 0, 1, loopback))
	{
		return fail("invalid-loopback-option");
	}

	hostLoopback = loopback != 0;
	wcscpy_s(sessionBrokerIP, address.c_str());
	sessionBrokerPort = static_cast<PortType>(brokerPort);
	NetBuffer::m_byHeaderCode = static_cast<BYTE>(packetCode);
	NetBuffer::m_byXORCode = static_cast<BYTE>(packetKey);

	return true;
}

bool RUDPClientCore::GetSessionFromServer(const std::stop_token& inStopToken,
	const std::uint64_t inConnectionId, const std::uint64_t inCredentialMs)
{
	const auto socketStarted = std::chrono::steady_clock::now();
	if (inStopToken.stop_requested())
	{
		LogBrokerStage(inConnectionId, "socket-create", "cancelled", socketStarted);
		return false;
	}

	sessionBrokerSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sessionBrokerSocket == INVALID_SOCKET)
	{
		const int socketError = WSAGetLastError();
		LogBrokerStage(inConnectionId, "socket-create", "socket-failed", socketStarted, "winsock", socketError);
		return false;
	}

	const auto connectStarted = std::chrono::steady_clock::now();
	const bool connected = TryConnectToSessionBroker(inStopToken, inConnectionId);
	const auto connectMs = ElapsedMilliseconds(connectStarted);
	if (not connected)
	{
		closesocket(sessionBrokerSocket);
		sessionBrokerSocket = INVALID_SOCKET;
		return false;
	}

	tlsHelper.SetConnectionDeadline(std::chrono::steady_clock::now() + std::chrono::seconds(15));
	LogClientProgress(inConnectionId, "tcp_connected", connectMs);
	if (WanBrokerTraceEnabled())
	{
		tlsHelper.EnableHandshakeTrace();
		LogClientSocketProbe(sessionBrokerSocket, inConnectionId, "tcp-before-tls");
	}

	// A host-only override must never redirect a remotely reached broker's UDP endpoint.
	// Check the connected peer before closing this socket; TLS name/chain checks still follow.
	if (hostLoopback)
	{
		const auto peerStarted = std::chrono::steady_clock::now();
		sockaddr_in brokerPeer{};
		int peerSize = sizeof(brokerPeer);
		const int peerResult = getpeername(sessionBrokerSocket, reinterpret_cast<sockaddr*>(&brokerPeer), &peerSize);
		const int peerError = peerResult == SOCKET_ERROR ? WSAGetLastError() : 0;
		if (peerResult == SOCKET_ERROR ||
			brokerPeer.sin_family != AF_INET || (ntohl(brokerPeer.sin_addr.s_addr) >> 24) != 127)
		{
			LogBrokerStage(inConnectionId, "loopback-check", peerResult == SOCKET_ERROR
				? "peer-name-failed" : "non-loopback-peer", peerStarted, peerError ? "winsock" : "none", peerError);
			closesocket(sessionBrokerSocket);
			sessionBrokerSocket = INVALID_SOCKET;
			return false;
		}
	}

	// Bound sends as well as reads before the TLS handshake starts.
	constexpr DWORD BROKER_IO_TIMEOUT_MS = 15000;
	const auto timeoutStarted = std::chrono::steady_clock::now();
	const int receiveSetup = setsockopt(sessionBrokerSocket, SOL_SOCKET, SO_RCVTIMEO,
		reinterpret_cast<const char*>(&BROKER_IO_TIMEOUT_MS), sizeof(BROKER_IO_TIMEOUT_MS));
	const int receiveError = receiveSetup == SOCKET_ERROR ? WSAGetLastError() : 0;
	const int sendSetup = receiveSetup == SOCKET_ERROR ? 0 : setsockopt(sessionBrokerSocket, SOL_SOCKET, SO_SNDTIMEO,
		reinterpret_cast<const char*>(&BROKER_IO_TIMEOUT_MS), sizeof(BROKER_IO_TIMEOUT_MS));
	const int sendError = sendSetup == SOCKET_ERROR ? WSAGetLastError() : 0;
	if (receiveSetup == SOCKET_ERROR || sendSetup == SOCKET_ERROR)
	{
		LogBrokerStage(inConnectionId, "socket-timeout-setup", receiveSetup == SOCKET_ERROR
			? "receive-timeout-setup-failed" : "send-timeout-setup-failed", timeoutStarted,
			"winsock", receiveSetup == SOCKET_ERROR ? receiveError : sendError);
		closesocket(sessionBrokerSocket);
		sessionBrokerSocket = INVALID_SOCKET;
		return false;
	}

	const bool handshakeSucceeded = tlsHelper.Handshake(sessionBrokerSocket, inStopToken);
	LogClientHandshake(tlsHelper, handshakeSucceeded, inConnectionId, inCredentialMs, connectMs);
	if (tlsHelper.HasHandshakeTrace()) LogClientSocketProbe(sessionBrokerSocket, inConnectionId, "tcp-after-tls");
	if (not handshakeSucceeded)
	{
		closesocket(sessionBrokerSocket);
		sessionBrokerSocket = INVALID_SOCKET;
		return false;
	}

	const auto protocolStarted = std::chrono::steady_clock::now();
	const bool protocolCancelled = inStopToken.stop_requested();
	const bool protocolSent = !protocolCancelled && tlsHelper.SendProtocolVersion(sessionBrokerSocket, RUDP_PROTOCOL_VERSION);
	if (!protocolSent)
	{
		const auto& result = tlsHelper.GetProtocolVersionSendResult();
		const bool encryptFailed = !protocolCancelled && std::string_view(result.reason) == "encrypt-failed";
		LogBrokerStage(inConnectionId, "protocol-send", protocolCancelled ? "cancelled" : result.reason,
			protocolStarted, protocolCancelled ? "none" : encryptFailed ? "sspi" : result.socketError ? "winsock" : "none",
			protocolCancelled ? 0 : encryptFailed ? static_cast<std::int64_t>(tlsHelper.GetLastStatus()) : result.socketError);
		closesocket(sessionBrokerSocket);
		sessionBrokerSocket = INVALID_SOCKET;
		return false;
	}

	LogBrokerStage(inConnectionId, "protocol-send", "succeeded", protocolStarted);
	return TrySetTargetSessionInfo(inStopToken, inConnectionId);
}

// Poll a nonblocking connect so startup cancellation never waits for the OS TCP timeout.
bool RUDPClientCore::TryConnectToSessionBroker(const std::stop_token& inStopToken,
	const std::uint64_t inConnectionId) const
{
	sockaddr_in sessionGetterAddr{};
	sessionGetterAddr.sin_family = AF_INET;
	sessionGetterAddr.sin_port = htons(sessionBrokerPort);
	const auto dnsStarted = std::chrono::steady_clock::now();
	if (InetPtonW(AF_INET, sessionBrokerIP, &sessionGetterAddr.sin_addr) != 1)
	{
		LogBrokerStage(inConnectionId, "dns", "started", dnsStarted);
		// Async DNS shares startup cancellation; its result storage remains alive
		// until cancellation has completed. The wire's game endpoint stays IPv4.
		ADDRINFOEXW hints{};
		hints.ai_family = AF_INET;
		hints.ai_socktype = SOCK_STREAM;
		PADDRINFOEXW result{};

		OVERLAPPED pending{};
		pending.hEvent = WSACreateEvent();
		if (pending.hEvent == WSA_INVALID_EVENT)
		{
			const int eventError = WSAGetLastError();
			LogBrokerStage(inConnectionId, "dns", "event-create-failed", dnsStarted, "winsock", eventError);
			return false;
		}

		HANDLE lookup{};
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(6);

		int status = GetAddrInfoExW(sessionBrokerIP, nullptr, NS_DNS, nullptr, &hints,
			&result, nullptr, &pending, nullptr, &lookup);
		DWORD waitError{};
		bool waitFailed{}, timedOut{}, cancelled{};
		if (status == WSA_IO_PENDING)
		{
			DWORD waited = WAIT_TIMEOUT;
			while (!inStopToken.stop_requested() && std::chrono::steady_clock::now() < deadline)
			{
				waited = WaitForSingleObject(pending.hEvent, 100);
				if (waited == WAIT_FAILED) waitError = GetLastError();
				if (waited != WAIT_TIMEOUT)
				{
					break;
				}
			}

			if (waited != WAIT_OBJECT_0)
			{
				waitFailed = waited != WAIT_TIMEOUT;
				cancelled = inStopToken.stop_requested();
				timedOut = !waitFailed && !cancelled;
				// The primary cause is logged before cancellation/join can replace its error state.
				LogBrokerStage(inConnectionId, "dns-wait", waitFailed ? "wait-failed"
					: cancelled ? "cancelled" : "timeout", dnsStarted,
					waitFailed ? "win32" : "winsock", waitFailed ? waitError
					: cancelled ? WSA_OPERATION_ABORTED : WSAETIMEDOUT);
				const auto cancelStarted = std::chrono::steady_clock::now();
				const int cancelStatus = GetAddrInfoExCancel(&lookup);
				if (cancelStatus != 0)
					LogBrokerStage(inConnectionId, "dns-cancel", "cancel-request-failed", cancelStarted, "resolver", cancelStatus);
				// The OS must stop referencing pending/result before these locals die.
				const DWORD joined = WaitForSingleObject(pending.hEvent, INFINITE);
				const DWORD joinError = joined == WAIT_FAILED ? GetLastError() : 0;
				LogBrokerStage(inConnectionId, "dns-cancel", joined == WAIT_OBJECT_0 ? "completed" : "wait-failed",
					cancelStarted, joined == WAIT_OBJECT_0 ? "none" : "win32", joinError);
			}

			status = GetAddrInfoExOverlappedResult(&pending);
		}

		const bool resolved = status == 0 && result && result->ai_addr &&
			result->ai_addrlen >= sizeof(sockaddr_in) && !inStopToken.stop_requested() &&
			std::chrono::steady_clock::now() < deadline;
		if (resolved)
		{
			sessionGetterAddr.sin_addr = reinterpret_cast<sockaddr_in*>(result->ai_addr)->sin_addr;
		}
		cancelled = cancelled || inStopToken.stop_requested();
		timedOut = timedOut || std::chrono::steady_clock::now() >= deadline;
		LogBrokerStage(inConnectionId, "dns", resolved ? "succeeded" : waitFailed ? "wait-failed"
			: cancelled ? "cancelled" : timedOut ? "timeout" : status != 0 ? "resolve-failed" : "invalid-address-result",
			dnsStarted, status != 0 ? "resolver" : waitFailed ? "win32" : "none", status != 0 ? status : waitError);

		if (result)
		{
			FreeAddrInfoExW(result);
		}

		if (!WSACloseEvent(pending.hEvent))
		{
			const int closeError = WSAGetLastError();
			LogBrokerStage(inConnectionId, "dns-event-close", "event-close-failed", dnsStarted, "winsock", closeError);
		}
		if (!resolved)
		{
			return false;
		}
	}
	else LogBrokerStage(inConnectionId, "dns", "skipped-literal-ip", dnsStarted);

	const auto tcpStarted = std::chrono::steady_clock::now();
	if (WanBrokerTraceEnabled())
	{
		char address[INET_ADDRSTRLEN]{};
		if (InetNtopA(AF_INET, &sessionGetterAddr.sin_addr, address, sizeof(address)))
		{
			try
			{
				const auto details = std::format("; peer={}:{}", address, sessionBrokerPort);
				LogBrokerStage(inConnectionId, "tcp-target", "selected", tcpStarted, "none", 0, details);
			}
			catch (...) { Logger::GetInstance().RecordDroppedDiagnostic(); }
		}
	}
	LogBrokerStage(inConnectionId, "tcp", "started", tcpStarted);
	u_long nonblocking = 1;
	if (ioctlsocket(sessionBrokerSocket, FIONBIO, &nonblocking) == SOCKET_ERROR)
	{
		const int setupError = WSAGetLastError();
		LogBrokerStage(inConnectionId, "tcp", "nonblocking-setup-failed", tcpStarted, "winsock", setupError);
		return false;
	}

	bool connected = false;
	const char* failureReason = "cancelled";
	const char* errorDomain = "none";
	int failureError{};
	if (!inStopToken.stop_requested())
	{
		const int result = connect(sessionBrokerSocket, reinterpret_cast<sockaddr*>(&sessionGetterAddr), sizeof(sessionGetterAddr));
		connected = result == 0;
		const int connectError = connected ? 0 : WSAGetLastError();
		failureReason = "connect-failed";
		errorDomain = connected ? "none" : "winsock";
		failureError = connectError;
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(6);
		bool waitFailed{};
		while (!connected && (connectError == WSAEWOULDBLOCK || connectError == WSAEINPROGRESS)
			&& !inStopToken.stop_requested() && std::chrono::steady_clock::now() < deadline)
		{
			fd_set writable{}, errors{};
			FD_SET(sessionBrokerSocket, &writable);
			FD_SET(sessionBrokerSocket, &errors);
			timeval timeout{ 0, 100000 };
			const int ready = select(0, nullptr, &writable, &errors, &timeout);
			if (ready == SOCKET_ERROR)
			{
				failureError = WSAGetLastError();
				failureReason = "select-failed";
				waitFailed = true;
				break;
			}

			if (ready == 0)
			{
				continue;
			}

			int socketError{};
			int errorSize = sizeof(socketError);
			const int queryResult = getsockopt(sessionBrokerSocket, SOL_SOCKET, SO_ERROR,
				reinterpret_cast<char*>(&socketError), &errorSize);
			const int queryError = queryResult == SOCKET_ERROR ? WSAGetLastError() : 0;
			connected = queryResult == 0 && socketError == 0 && FD_ISSET(sessionBrokerSocket, &writable);
			failureError = queryResult == SOCKET_ERROR ? queryError : socketError;
			failureReason = queryResult == SOCKET_ERROR ? "socket-error-query-failed"
				: socketError != 0 ? "connect-failed" : "not-writable";
			waitFailed = true;
			break;
		}
		if (!connected && !waitFailed && (connectError == WSAEWOULDBLOCK || connectError == WSAEINPROGRESS))
		{
			failureReason = inStopToken.stop_requested() ? "cancelled" : "timeout";
			failureError = inStopToken.stop_requested() ? WSA_OPERATION_ABORTED : WSAETIMEDOUT;
		}
	}

	// Restoring socket mode may reset WSAGetLastError. Keep the connect/select/SO_ERROR result above.
	nonblocking = 0;
	const int restoreResult = ioctlsocket(sessionBrokerSocket, FIONBIO, &nonblocking);
	const int restoreError = restoreResult == SOCKET_ERROR ? WSAGetLastError() : 0;
	if (restoreResult == SOCKET_ERROR)
		LogBrokerStage(inConnectionId, "tcp-restore", "blocking-restore-failed", tcpStarted, "winsock", restoreError);
	const bool cancelled = inStopToken.stop_requested();
	const bool succeeded = restoreResult == 0 && connected && !cancelled;
	LogBrokerStage(inConnectionId, "tcp", succeeded ? "succeeded" : !connected ? failureReason
		: restoreResult == SOCKET_ERROR ? "blocking-restore-failed" : "cancelled", tcpStarted,
		!connected ? errorDomain : restoreResult == SOCKET_ERROR ? "winsock" : "none",
		!connected ? failureError : restoreError);
	if (!succeeded && WanBrokerTraceEnabled())
		LogClientSocketProbe(sessionBrokerSocket, inConnectionId, "tcp-connect-failed");
	return succeeded;
}

bool RUDPClientCore::TrySetTargetSessionInfo(const std::stop_token& inStopToken,
	const std::uint64_t inConnectionId)
{
	const auto receiveStarted = std::chrono::steady_clock::now();
	LogBrokerStage(inConnectionId, "session-receive", "started", receiveStarted);
	auto& recvBuffer = *NetBuffer::Alloc();
	recvBuffer.m_iRead = 0;
	// Append plaintext across TLS records, including a split session header.
	recvBuffer.m_iWrite = 0;

	constexpr size_t maxTlsPacketSize = 16 * 1024 + 512;
	auto encryptedStream = tlsHelper.TakePendingHandshakeData();
	bool processBufferedData = not encryptedStream.empty();

	int totalPlainReceived{};
	BYTE code{};
	WORD payloadLength{};

	bool payloadComplete{ false };
	const char* receiveReason = "timeout";
	int receiveError = WSAETIMEDOUT;
	const auto deadline = tlsHelper.GetConnectionDeadline();

	while (!inStopToken.stop_requested() && std::chrono::steady_clock::now() < deadline)
	{
		if (not processBufferedData)
		{
			fd_set readable{};
			FD_SET(sessionBrokerSocket, &readable);
			timeval timeout{ 0, 100000 };
			const int ready = select(0, &readable, nullptr, nullptr, &timeout);
			if (ready == SOCKET_ERROR)
			{
				receiveError = WSAGetLastError();
				receiveReason = "select-failed";
				break;
			}

			if (ready == 0)
			{
				continue;
			}

			char tlsRecvBuffer[maxTlsPacketSize];
			if (!tlsHelper.ApplyRemainingTimeout(sessionBrokerSocket))
			{
				receiveError = WSAGetLastError();
				receiveReason = receiveError == WSAETIMEDOUT ? "timeout" : "socket-timeout-setup-failed";
				break;
			}
			const int bytesReceived = recv(sessionBrokerSocket, tlsRecvBuffer, sizeof(tlsRecvBuffer), 0);
			if (bytesReceived <= 0)
			{
				const int error = bytesReceived == SOCKET_ERROR ? WSAGetLastError() : 0;
				receiveError = error;
				receiveReason = bytesReceived == 0 ? "peer-closed"
					: error == WSAETIMEDOUT ? "timeout" : "socket-receive-failed";
				if (bytesReceived == 0 || error == WSAETIMEDOUT)
				{
					break;
				}

				LogBrokerStage(inConnectionId, "session-receive", receiveReason, receiveStarted, "winsock", receiveError);
				closesocket(sessionBrokerSocket);
				sessionBrokerSocket = INVALID_SOCKET;
				NetBuffer::Free(&recvBuffer);

				return false;
			}

			encryptedStream.insert(encryptedStream.end(), tlsRecvBuffer, tlsRecvBuffer + bytesReceived);
		}

		processBufferedData = false;

		char plainBuffer[maxTlsPacketSize];
		size_t plainSize = 0;

		const auto decryptResult = tlsHelper.DecryptDataStream(encryptedStream, plainBuffer, plainSize);
		if (decryptResult == TLSHelper::TlsDecryptResult::Error)
		{
			const auto decryptStatus = tlsHelper.GetLastStatus();
			LogBrokerStage(inConnectionId, "session-receive", "decrypt-failed", receiveStarted, "sspi", decryptStatus);
			closesocket(sessionBrokerSocket);
			sessionBrokerSocket = INVALID_SOCKET;
			NetBuffer::Free(&recvBuffer);

			return false;
		}

		if (plainSize > 0)
		{
			recvBuffer.WriteBuffer(plainBuffer, static_cast<int>(plainSize));
			totalPlainReceived += static_cast<int>(plainSize);
		}

		if (totalPlainReceived < df_HEADER_SIZE)
		{
			continue;
		}

		if (payloadLength == 0)
		{
			recvBuffer >> code >> payloadLength;
		}

		if (totalPlainReceived >= payloadLength + df_HEADER_SIZE)
		{
			payloadComplete = true;
		}

		if (payloadComplete && decryptResult == TLSHelper::TlsDecryptResult::CloseNotify)
		{
			break;
		}
		if (decryptResult == TLSHelper::TlsDecryptResult::CloseNotify)
		{
			// Keep existing receive/accept behavior; retain why an incomplete payload cannot finish.
			receiveReason = "tls-close-notify-before-payload";
			receiveError = 0;
		}
	}

	shutdown(sessionBrokerSocket, SD_BOTH);
	closesocket(sessionBrokerSocket);
	sessionBrokerSocket = INVALID_SOCKET;

	const bool receiveCancelled = inStopToken.stop_requested();
	LogBrokerStage(inConnectionId, "session-receive", receiveCancelled ? "cancelled"
		: payloadComplete ? "succeeded" : receiveReason, receiveStarted,
		receiveCancelled || payloadComplete || receiveError == 0 ? "none" : "winsock",
		receiveCancelled || payloadComplete ? 0 : receiveError);
	if (receiveCancelled || not payloadComplete)
	{
		NetBuffer::Free(&recvBuffer);
		return false;
	}

	recvBuffer.m_iRead = df_HEADER_SIZE;
	const bool retval = SetTargetSessionInfo(recvBuffer, inConnectionId);
	NetBuffer::Free(&recvBuffer);

	return retval;
}

bool RUDPClientCore::SetTargetSessionInfo(OUT NetBuffer& receivedBuffer, const std::uint64_t inConnectionId)
{
	const auto decodeStarted = std::chrono::steady_clock::now();
	LogBrokerStage(inConnectionId, "session-decode", "started", decodeStarted);
	if (receivedBuffer.GetUseSize() < sizeof(uint32_t) + sizeof(char))
	{
		LogBrokerStage(inConnectionId, "session-decode", "header-too-short", decodeStarted);
		return false;
	}

	uint32_t version{};
	receivedBuffer >> version;
	if (version != RUDP_PROTOCOL_VERSION)
	{
		LogBrokerStage(inConnectionId, "session-decode", "unsupported-protocol-version", decodeStarted, "protocol", version);
		return false;
	}

	char connectResultCode;
	receivedBuffer >> connectResultCode;
	if (connectResultCode != 0)
	{
		LogBrokerStage(inConnectionId, "session-decode", "broker-rejected", decodeStarted,
			"broker-result", static_cast<unsigned char>(connectResultCode));
		return false;
	}

	receivedBuffer >> serverIp >> port >> sessionId >> sessionKey >> sessionSalt;
	// Port, session credentials and the wire layout remain the broker's authenticated values.
	if (hostLoopback)
	{
		serverIp = "127.0.0.1";
	}

	if (keyObjectBuffer == nullptr)
	{
		keyObjectBuffer = new unsigned char[CryptoHelper::GetTLSInstance().GetKeyObjectSize()];
	}

	if (sessionKeyHandle != nullptr)
	{
		CryptoHelper::DestroySymmetricKeyHandle(sessionKeyHandle);
		sessionKeyHandle = nullptr;
	}

	NTSTATUS keyStatus{};
	const bool keySizeAvailable = CryptoHelper::GetTLSInstance().GetKeyObjectSize() != 0;
	sessionKeyHandle = CryptoHelper::GetTLSInstance().GetSymmetricKeyHandle(keyObjectBuffer, sessionKey, &keyStatus);

	LogBrokerStage(inConnectionId, "session-decode", sessionKeyHandle ? "succeeded"
		: keySizeAvailable ? "session-key-initialize-failed" : "key-object-size-unavailable",
		decodeStarted, !sessionKeyHandle && keySizeAvailable ? "ntstatus" : "none", keyStatus);
	if (sessionKeyHandle && tlsHelper.HasHandshakeTrace())
	{
		try
		{
			const auto details = std::format("; udp_port={}; rudp_session={}", port, sessionId);
			LogBrokerStage(inConnectionId, "rudp-reservation", "received", decodeStarted, "none", 0, details);
		}
		catch (...) { Logger::GetInstance().RecordDroppedDiagnostic(); }
	}
	return sessionKeyHandle != nullptr;
}

#endif
