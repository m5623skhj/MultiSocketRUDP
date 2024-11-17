#pragma once
#include "NetServerSerializeBuffer.h"
#include "NetClient.h"
#include "../MultiSocketRUDPServer/BuildConfig.h"
#include "../MultiSocketRUDPServer/CoreType.h"
#include <thread>
#include <mutex>
#include <array>
#include "Queue.h"

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

	bool IsStopped();
	bool IsConnected();

private:
	bool ConnectToServer();

private:
	bool isStopped{};
	bool isConnected{};

#pragma region SessionBroker
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

#pragma endregion SessionBroker

#pragma region RUDP
private:
	void RunThreads();
	void RunRecvThread();
	void RunSendThread();

	void DoSend();

private:
	SOCKET rudpSocket{};
	sockaddr_in serverAddr{};

	std::thread recvThread{};
	std::thread sendThread{};
	std::array<HANDLE, 2> sendEventHandles{};

#pragma endregion RUDP

public:
	unsigned int GetRemainPacketSize();
	NetBuffer* GetReceivedPacket();
	void SendPacket(OUT NetBuffer& packet);

private:
	void EncodePacket(OUT NetBuffer& packet);

private:
	CListBaseQueue<NetBuffer*> recvBufferQueue;
	CListBaseQueue<NetBuffer*> sendBufferQueue;
	std::mutex sendBufferQueueLock;
};