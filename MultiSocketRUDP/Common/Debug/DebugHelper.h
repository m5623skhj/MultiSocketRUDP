#pragma once

#if defined(_DEBUG)
#include <crtdbg.h>

void EnableCrtDebug()
{
	// Enable memory leak detection and check on every allocation/deallocation
	int flags = _CrtSetDbgFlag(_CRTDBG_REPORT_FLAG);
	flags |= _CRTDBG_ALLOC_MEM_DF;
	flags |= _CRTDBG_LEAK_CHECK_DF;
	flags |= _CRTDBG_CHECK_ALWAYS_DF;
	_CrtSetDbgFlag(flags);

	// Send CRT report output to the debugger
	_CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_DEBUG);
	_CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_DEBUG);
	_CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_DEBUG);
}
#endif
