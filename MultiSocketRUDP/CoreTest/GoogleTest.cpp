#include "PreCompile.h"
#include "GoogleTest.h"
#include <googletest-main/googletest/include/gtest/gtest.h>

namespace GTestHelper
{
	bool StartTest(int argc, char** argv)
	{
		testing::InitGoogleTest(&argc, argv);
		return RUN_ALL_TESTS() == 0;
	}
}
