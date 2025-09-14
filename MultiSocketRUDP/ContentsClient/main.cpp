#include "PreCompile.h"
#include "TestClient.h"

int main()
{
	TestClient client;

	if (not client.Start(L"ClientOptionFile/CoreOption.txt", L"ClientOptionFile/SessionGetterOption.txt", true))
	{
		client.Stop();
		return 0;
	}
	std::cout << "Exit : ESC" << '\n';

	while (true)
	{
		Sleep(1000);

		if (GetAsyncKeyState(VK_ESCAPE) & 0x8000 || not client.IsConnected())
		{
			client.Stop();
			break;
		}
	}

	return 0;
}