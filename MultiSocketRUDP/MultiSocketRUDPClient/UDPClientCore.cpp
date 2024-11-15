#include "PreCompile.h"
#include "UDPClientCore.h"

UDPClientCore& UDPClientCore::GetInst()
{
	static UDPClientCore instance;
	return instance;
}