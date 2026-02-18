#include "PreCompile.h"
#include "GoogleTest.h"
#include <iostream>

int main()
{
	if (GTestHelper::StartTest() == false)
    {
    	std::cout << "---------------------" << '\n';
		std::cout << "GTest failed" << '\n';
    	std::cout << "---------------------" << '\n' << '\n' << '\n';
    	return 0;
    }

    std::cout << "---------------------" << '\n';
    std::cout << "GTest successes" << '\n';
    std::cout << "---------------------" << '\n' << '\n' << '\n';
}