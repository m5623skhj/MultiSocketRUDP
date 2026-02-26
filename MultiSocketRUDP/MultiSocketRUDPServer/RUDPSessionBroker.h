#pragma once

#include <thread>

#include "../Common/etc/CoreType.h"
#include "../Common/TLS/TLSHelper.h"
#include "NetServerSerializeBuffer.h"

class RUDPSession;
class MultiSocketRUDPCore;
class ISessionDelegate;

class RUDPSessionBroker
{
public:
	explicit RUDPSessionBroker(
		MultiSocketRUDPCore& inCore,
		ISessionDelegate& inSessionDelegate,
        const std::wstring& certStoreName,
        const std::wstring& certSubjectName);
    ~RUDPSessionBroker();

	RUDPSessionBroker(const RUDPSessionBroker&) = delete;
	RUDPSessionBroker& operator=(const RUDPSessionBroker&) = delete;
	RUDPSessionBroker(RUDPSessionBroker&&) = delete;
	RUDPSessionBroker& operator=(RUDPSessionBroker&&) = delete;

public:
    [[nodiscard]]
	bool Start(PortType listenPort, const std::string& rudpSessionIP);
	void Stop();

private:
	void RunSessionBrokerThread(const std::stop_token& stopToken, const std::string& rudpSessionIP);

	[[nodiscard]]
	bool OpenSessionBrokerSocket(PortType listenPort);
	void CloseListenSocket();

    [[nodiscard]]
    RUDPSession* ReserveSession(OUT NetBuffer& sendBuffer, const std::string& rudpServerIP) const;
	[[nodiscard]]
	static CONNECT_RESULT_CODE InitReserveSession(OUT RUDPSession& session);

private:
	[[nodiscard]]
	bool InitSessionCrypto(OUT RUDPSession& session) const;
	[[nodiscard]]
	bool GenerateSessionKey(OUT RUDPSession& session) const;
	[[nodiscard]]
	bool GenerateSaltKey(OUT RUDPSession& session) const;

private:
    void SetSessionInfoToBuffer(const RUDPSession& session, const std::string& rudpServerIP, OUT NetBuffer& buffer) const;
	[[nodiscard]]
	bool SendSessionInfoToClient(const SOCKET& clientSocket, OUT NetBuffer& sendBuffer);
	[[nodiscard]]
	static bool SendAll(const SOCKET& socket, const char* sendBuffer, const size_t sendSize);

private:
	MultiSocketRUDPCore& core;
	ISessionDelegate& sessionDelegate;
	TLSHelper::TLSHelperServer tlsHelper;

	SOCKET sessionBrokerListenSocket = INVALID_SOCKET;
	std::jthread sessionBrokerThread{};

	bool isRunning{};
};