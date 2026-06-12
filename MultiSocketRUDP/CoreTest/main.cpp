#include "PreCompile.h"
#include "GoogleTest.h"
#include <iostream>

int main(int argc, char** argv)
{
	if (GTestHelper::StartTest(argc, argv) == false)
    {
    	std::cout << "---------------------" << '\n';
		std::cout << "GTest failed" << '\n';
    	std::cout << "---------------------" << '\n' << '\n' << '\n';
    	return 1;
    }

    std::cout << "---------------------" << '\n';
    std::cout << "GTest successes" << '\n';
    std::cout << "---------------------" << '\n' << '\n' << '\n';
	return 0;
}
