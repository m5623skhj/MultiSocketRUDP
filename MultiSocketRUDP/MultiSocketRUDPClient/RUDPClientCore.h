#pragma once
#include "NetServerSerializeBuffer.h"
#include "NetClient.h"
#include "../MultiSocketRUDPServer/BuildConfig.h"
#include "../MultiSocketRUDPServer/CoreType.h"

class RUDPClientCore
{
private:
	RUDPClientCore() = default;
	~RUDPClientCore() = default;
	RUDPClientCore& operator=(const RUDPClientCore&) = delete;
	RUDPClientCore(RUDPClientCore&&) = delete;

public:
	static RUDPClientCore& GetInst();

public:
	bool Start(const std::wstring& optionFilePath);
	void Stop();

#if USE_IOCP_SESSION_BROKER
private:
	class SessionGetter : public CNetClient
	{
	public:
		bool Start(const std::wstring& optionFilePath);

	private:
		virtual void OnConnectionComplete();
		virtual void OnRecv(CNetServerSerializationBuf* recvBuffer);
		virtual void OnSend(int sendsize);

		virtual void OnWorkerThreadBegin();
		virtual void OnWorkerThreadEnd();
		virtual void OnError(st_Error* error);
	};

	SessionGetter sessionGetter;
#else
private:
	bool RunGetSessionFromServer(const std::wstring& optionFilePath);
	bool ReadOptionFile(const std::wstring& optionFilePath);
	bool GetSessionFromServer();

private:
	WCHAR sessionBrokerIP[16]{};
	PortType sessionBrokerPort{};

	SOCKET sessionBrokerSocket{};

#endif
private:
	bool SetTargetSessionInfo(OUT NetBuffer& receivedBuffer);

private:
	std::string serverIp{};
	PortType port{};
	SessionIdType sessionId{};
	std::string sessionKey{};
};