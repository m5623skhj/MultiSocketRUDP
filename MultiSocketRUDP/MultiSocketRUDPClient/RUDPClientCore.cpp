#include "PreCompile.h"
#include "RUDPClientCore.h"

RUDPClientCore& RUDPClientCore::GetInst()
{
	static RUDPClientCore instance;
	return instance;
}