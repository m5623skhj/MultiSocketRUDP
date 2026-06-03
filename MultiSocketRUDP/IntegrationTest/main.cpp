#include <Windows.h>

#include <gtest/gtest.h>
#include <iostream>

int main(int argc, char** argv)
{
	std::cout.setf(std::ios::unitbuf);
	testing::InitGoogleTest(&argc, argv);
	const int result = RUN_ALL_TESTS();
	ExitProcess(static_cast<UINT>(result));
	return result;
}
