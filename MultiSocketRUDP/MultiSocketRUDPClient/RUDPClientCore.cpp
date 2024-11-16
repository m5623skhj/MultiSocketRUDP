#include "PreCompile.h"
#include "RUDPClientCore.h"

RUDPClientCore& RUDPClientCore::GetInst()
{
	static RUDPClientCore instance;
	return instance;
}

bool RUDPClientCore::Start(const std::wstring& optionFilePath)
{
#if USE_IOCP_SESSION_BROKER
	if (not sessionGetter.Start(optionFilePath))
	{
		return false;
	}
#else
	if (not RunGetSessionFromServer(optionFilePath))
	{
		return false;
	}
#endif

	// TODO : Connect to rudp server
	// TODO : Run udp client thread

	return true;
}

void RUDPClientCore::Stop()
{
}
