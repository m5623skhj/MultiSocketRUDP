#include "PreCompile.h"
#include "RUDPClientCore.h"

int main()
{
	if (not RUDPClientCore::GetInst().Start(L"OptionFile/CoreOption.txt"))
	{
		std::cout << "Core start failed" << std::endl;
		return 0;
	}

	while (true)
	{
		Sleep(1000);

		if (GetAsyncKeyState(VK_ESCAPE) & 0x8000)
		{
			RUDPClientCore::GetInst().Stop();
			break;
		}
	}
	std::cout << "Server stop" << std::endl;

	return 0;
}