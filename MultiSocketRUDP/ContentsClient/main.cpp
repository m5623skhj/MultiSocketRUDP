#include "PreCompile.h"
#include "TestClient.h"
#include <format>
#include <ranges>

int main()
{
	constexpr int clientSize = 20;
	std::list<int> stoppedClientList;

	std::vector<std::pair<TestClient*, bool>> clients;
	clients.reserve(clientSize);

	for (int i = 0; i < clientSize; ++i)
	{
		clients.emplace_back(new TestClient(), false);
	}

	for (auto& client : clients)
	{
		if (not client.first->Start(L"ClientOptionFile/CoreOption.txt", L"ClientOptionFile/SessionGetterOption.txt", true))
		{
			client.first->Stop();
			return 0;
		}

		client.second = true;
	}
	std::cout << "Exit : ESC" << '\n';

	srand(static_cast<unsigned int>(time(nullptr)));
	while (true)
	{
		constexpr int randomSize = 300;
		Sleep(1000);

		if (const auto targetListItem = rand() % randomSize; targetListItem < clientSize)
		{
			if (clients[targetListItem].second == true)
			{
				clients[targetListItem].first->Stop();
				clients[targetListItem].second = false;
				stoppedClientList.emplace_back(targetListItem);
				std::cout << std::format("Client stopped. Current connected client index : {} / count : {}\n", targetListItem, clientSize - stoppedClientList.size());
			}
		}

		if (GetAsyncKeyState(VK_ESCAPE) & 0x8000)
		{
			for (auto& client : clients)
			{
				if (client.second == true && client.first->IsConnected())
				{
					client.first->Stop();
					client.second = false;
				}
			}
			break;
		}

		if (rand() % 30 == 1)
		{
			if (stoppedClientList.empty() == true)
			{
				continue;
			}

			const auto restartIndex = stoppedClientList.front();
			if (not clients[restartIndex].first->Start(L"ClientOptionFile/CoreOption.txt", L"ClientOptionFile/SessionGetterOption.txt", true))
			{
				clients[restartIndex].second = false;
				std::cout << "Client Start() failed after stop" << '\n';
				return 0;
			}

			clients[restartIndex].second = true;
			stoppedClientList.pop_front();
			std::cout << std::format("Client restarted. Current connected client index : {} / count : {}\n", restartIndex, clientSize - stoppedClientList.size());
		}
	}

	for (const auto& key : clients | std::views::keys)
	{
		delete key;
	}
	std::cout << "All client stopped" << '\n';

	return 0;
}
