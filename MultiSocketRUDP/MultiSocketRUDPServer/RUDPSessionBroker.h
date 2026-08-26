#pragma once

#include <thread>
#include <mutex>
#include <queue>

#include "../Common/etc/CoreType.h"
#include "../Common/TLS/TLSHelper.h"
#include "NetServerSerializeBuffer.h"

class RUDPSession;
class MultiSocketRUDPCore;
class ISessionDelegate;

// ----------------------------------------
// @brief TLS로 보호된 TCP 연결을 통해 클라이언트에 예약 RUDP 세션 정보를 발급합니다.
// accept 전용 스레드가 연결을 큐에 넣고 고정 크기 워커 풀이 TLS handshake와 세션 예약을 처리합니다.
// 응답 전송 실패 시 예약 세션을 중단하여 세션 풀이 누수되지 않도록 합니다.
// ----------------------------------------
class RUDPSessionBroker
{
public:
	explicit RUDPSessionBroker(
		MultiSocketRUDPCore& inCore,
		ISessionDelegate& inSessionDelegate,
		TLSHelper::ServerCertificateConfig inServerCertificateConfig);
    ~RUDPSessionBroker();

	RUDPSessionBroker(const RUDPSessionBroker&) = delete;
	RUDPSessionBroker& operator=(const RUDPSessionBroker&) = delete;
	RUDPSessionBroker(RUDPSessionBroker&&) = delete;
	RUDPSessionBroker& operator=(RUDPSessionBroker&&) = delete;

public:
    [[nodiscard]]
	// ----------------------------------------
	// @brief 브로커 listen 소켓과 워커 풀을 시작합니다.
	// @param listenPort 브로커가 TCP 연결을 수신할 포트입니다.
	// @param rudpSessionIP 클라이언트 응답에 기록할 RUDP 서버 주소입니다.
	// @return 이미 실행 중이거나 소켓 초기화에 실패하면 false를 반환합니다.
	// ----------------------------------------
	bool Start(PortType listenPort, const std::string& rudpSessionIP);
	// ----------------------------------------
	// @brief listen 소켓을 닫아 accept를 해제하고 모든 브로커 스레드를 join합니다.
	// ----------------------------------------
	void Stop();

private:
	// ----------------------------------------
	// accept 스레드와 TLS 처리 워커가 공유하는 clientQueue는 clientQueueLock으로 보호됩니다.
	// ----------------------------------------
	void RunSessionBrokerThread(const std::stop_token& stopToken, const std::string& rudpSessionIP);
	void RunBrokerWorkerThread(const std::stop_token& stopToken);
	void HandleClientConnection(SOCKET clientSocket, const std::string& rudpSessionIP);

	[[nodiscard]]
	bool OpenSessionBrokerSocket(PortType listenPort);
	void CloseListenSocket();

    [[nodiscard]]
	// ----------------------------------------
	// @brief 풀에서 세션을 예약하고 키·솔트를 생성하여 브로커 응답 버퍼를 구성합니다.
	// @return 풀 고갈 시 nullptr입니다. 초기화 실패 시 반환된 포인터는 이미 세션 정리 경로에 진입했을 수 있습니다.
	// ----------------------------------------
    RUDPSession* ReserveSession(OUT NetBuffer& sendBuffer, const std::string& rudpServerIP) const;
	[[nodiscard]]
	static CONNECT_RESULT_CODE InitReserveSession(OUT RUDPSession& session);

private:
	[[nodiscard]]
	// ----------------------------------------
	// 세션별 암호 키·솔트와 BCrypt 키 핸들 초기화
	// ----------------------------------------
	bool InitSessionCrypto(OUT RUDPSession& session) const;
	[[nodiscard]]
	bool GenerateSessionKey(OUT RUDPSession& session) const;
	[[nodiscard]]
	bool GenerateSaltKey(OUT RUDPSession& session) const;

private:
    void SetSessionInfoToBuffer(const RUDPSession& session, const std::string& rudpServerIP, OUT NetBuffer& buffer) const;
	[[nodiscard]]
	static bool SendSessionInfoToClient(const SOCKET& clientSocket, TLSHelper::TLSHelperServer& localTlsHelper, OUT NetBuffer& sendBuffer);
	[[nodiscard]]
	static bool SendAll(const SOCKET& socket, const char* sendBuffer, const size_t sendSize);

private:
	MultiSocketRUDPCore& core;
	ISessionDelegate& sessionDelegate;

	TLSHelper::ServerCertificateConfig serverCertificateConfig{};

	SOCKET sessionBrokerListenSocket = INVALID_SOCKET;
	std::jthread sessionBrokerThread{};

	static constexpr unsigned int BROKER_THREAD_POOL_SIZE = 4;
	std::vector<std::jthread> threadPool;

	std::queue<std::pair<SOCKET, std::string>> clientQueue;
	std::mutex clientQueueLock;
	std::condition_variable clientQueueCV;

	bool isRunning{};
};
