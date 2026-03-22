#pragma once

void EnableANSI()
{
	HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
	DWORD dwMode = 0;
	GetConsoleMode(hOut, &dwMode);
	dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
	SetConsoleMode(hOut, dwMode);
}

void MoveCursor(int x, int y)
{
	std::cout << "\033[" << y << ";" << x << "H";
}

void HideCursor()
{
	HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
	CONSOLE_CURSOR_INFO cursorInfo;
	GetConsoleCursorInfo(hOut, &cursorInfo);
	cursorInfo.bVisible = FALSE;
	SetConsoleCursorInfo(hOut, &cursorInfo);
}

constexpr int UI_HEIGHT = 10;
constexpr int LOG_HEIGHT = 20;

std::mutex coutLock;
std::deque<std::string> logBuffer;

struct ServerStats
{
	std::atomic<int> player{ 0 };
	std::atomic<int> connected{ 0 };
	std::atomic<int> disconnected{ 0 };
	std::atomic<int> retrans{ 0 };
	std::atomic<int> tps{ 0 };
	std::atomic<int> error{ 0 };
};

ServerStats serverStats;

void UpdateUI(int consoleStartCursorX)
{
	std::lock_guard<std::mutex> lock(coutLock);

	MoveCursor(consoleStartCursorX, 2);
	std::cout << serverStats.player.load() << "    ";

	MoveCursor(consoleStartCursorX, 3);
	std::cout << serverStats.connected.load() << "    ";

	MoveCursor(consoleStartCursorX, 4);
	std::cout << serverStats.disconnected.load() << "    ";

	MoveCursor(consoleStartCursorX, 5);
	std::cout << serverStats.retrans.load() << "    ";

	MoveCursor(consoleStartCursorX, 6);
	std::cout << serverStats.tps.load() << "    ";

	MoveCursor(consoleStartCursorX, 7);
	std::cout << serverStats.error.load() << "    ";

	std::cout.flush();
}

void DrawUI()
{
	std::lock_guard<std::mutex> lock(coutLock);
	MoveCursor(1, 1);

	std::cout << "------------------- Server -------------------\n";
	std::cout << "* Player count :\n";
	std::cout << "* Num of connect count :\n";
	std::cout << "* Num of disconnect count :\n";
	std::cout << "* Disconnected by retransmission :\n";
	std::cout << "* TPS :\n";
	std::cout << "* Num of Error :\n\n\n";
	std::cout << "* Process stop : esc\n";
	std::cout << "-------------------- Log ---------------------\n";
}

void RenderLogs()
{
	for (int i = 0; i < LOG_HEIGHT; ++i)
	{
		MoveCursor(1, UI_HEIGHT + 1 + i);

		if (i < (int)logBuffer.size())
		{
			std::cout << logBuffer[i] << "                                        ";
		}
		else
		{
			std::cout << " ";
		}
	}

	MoveCursor(1, UI_HEIGHT + LOG_HEIGHT + 1);
}

void Log(const std::string& msg)
{
	std::lock_guard<std::mutex> lock(coutLock);

	if (logBuffer.size() >= LOG_HEIGHT)
	{
		logBuffer.pop_front();
	}

	logBuffer.push_back(msg);

	RenderLogs();
}
