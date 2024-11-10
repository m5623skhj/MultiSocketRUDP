#include "PreCompile.h"
#include "MultiSocketClientCore.h"

MultiSocketClientCore& MultiSocketClientCore::GetInst()
{
	static MultiSocketClientCore instance;
	return instance;
}