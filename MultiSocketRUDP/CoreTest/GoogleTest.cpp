#include "GoogleTest.h"
#include <googletest-main/googletest/include/gtest/gtest.h>

namespace GTestHelper
{
	bool StartTest()
	{
		testing::InitGoogleTest();
		return RUN_ALL_TESTS() == 0;
	}
}