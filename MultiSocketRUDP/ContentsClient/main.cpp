#include "PreCompile.h"
#include "TestClient.h"

int main()
{
	if (not TestClient::GetInst().Start(L"ClientOptionFile/CoreOption.txt", L"ClientOptionFile/SessionGetterOption.txt"))
	{
		TestClient::GetInst().Stop();
		return 0;
	}
	std::cout << "Exit : ESC" << std::endl;

	while (true)
	{
		Sleep(1000);

		if (GetAsyncKeyState(VK_ESCAPE) & 0x8000 || not TestClient::GetInst().IsConnected())
		{
			TestClient::GetInst().Stop();
			break;
		}
	}

	return 0;
}