#include "PreCompile.h"
#include "TestClient.h"

int main()
{
	constexpr int clientSize = 20;

	std::vector<TestClient*> clients;
	clients.reserve(clientSize);

	for (int i = 0; i < clientSize; ++i)
	{
		clients.emplace_back(new TestClient());
	}

	for (const auto& client : clients)
	{
		if (not client->Start(L"ClientOptionFile/CoreOption.txt", L"ClientOptionFile/SessionGetterOption.txt", true))
		{
			client->Stop();
			return 0;
		}
	}
	std::cout << "Exit : ESC" << '\n';

	while (true)
	{
		Sleep(1000);

		if (GetAsyncKeyState(VK_ESCAPE) & 0x8000)
		{
			for (const auto& client : clients)
			{
				if (client->IsConnected())
				{
					client->Stop();
				}
			}
			break;
		}
	}

	for (const auto& client : clients)
	{
		delete client;
	}
	std::cout << "All client stopped" << '\n';

	return 0;
}