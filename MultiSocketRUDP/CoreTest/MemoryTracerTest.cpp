#include "PreCompile.h"
#include <gtest/gtest.h>

#include "MemoryTracer.h"

#include <array>
#include <thread>

class MemoryTracerTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		MemoryTracer::Clear();
		MemoryTracer::Enable();
	}

	void TearDown() override
	{
		MemoryTracer::Clear();
		MemoryTracer::Enable();
	}
};

TEST_F(MemoryTracerTest, EnableAndDisableControlTracking)
{
	int object{};
	MemoryTracer::Disable();

	MemoryTracer::TrackObject(&object, "disabled", __FILE__, __LINE__);
	EXPECT_FALSE(MemoryTracer::IsEnabled());
	EXPECT_EQ(MemoryTracer::GetActiveObjectCount(), 0u);

	MemoryTracer::Enable();
	MemoryTracer::TrackObject(&object, "enabled", __FILE__, __LINE__);
	EXPECT_TRUE(MemoryTracer::IsEnabled());
	EXPECT_EQ(MemoryTracer::GetActiveObjectCount(), 1u);
}

TEST_F(MemoryTracerTest, NullPointersAreIgnored)
{
	MemoryTracer::TrackObject(nullptr, "null", __FILE__, __LINE__);
	MemoryTracer::UntrackObject(nullptr, __FILE__, __LINE__);
	MemoryTracer::AddNote(nullptr, "ignored");

	EXPECT_EQ(MemoryTracer::GetActiveObjectCount(), 0u);
}

TEST_F(MemoryTracerTest, TrackNoteAndUntrackProduceDeterministicHistory)
{
	int object{};
	MemoryTracer::TrackObject(&object, "TrackedObject", "MemoryTracerTest.cpp", 42);
	MemoryTracer::AddNote(&object, "first");
	MemoryTracer::AddNote(&object, "second");
	EXPECT_EQ(MemoryTracer::GetActiveObjectCount(), 1u);

	testing::internal::CaptureStdout();
	MemoryTracer::GetObjectHistory(&object);
	const std::string activeHistory = testing::internal::GetCapturedStdout();
	EXPECT_NE(activeHistory.find("Object: TrackedObject"), std::string::npos);
	EXPECT_NE(activeHistory.find("Tracked at: MemoryTracerTest.cpp:42"), std::string::npos);
	EXPECT_NE(activeHistory.find("Note: first | second"), std::string::npos);
	EXPECT_NE(activeHistory.find("Status: ACTIVE"), std::string::npos);

	MemoryTracer::UntrackObject(&object, "MemoryTracerTest.cpp", 50);
	EXPECT_EQ(MemoryTracer::GetActiveObjectCount(), 0u);

	testing::internal::CaptureStdout();
	MemoryTracer::GetObjectHistory(&object);
	const std::string freedHistory = testing::internal::GetCapturedStdout();
	EXPECT_NE(freedHistory.find("Untracked at: MemoryTracerTest.cpp:50"), std::string::npos);
	EXPECT_NE(freedHistory.find("Lifetime:"), std::string::npos);
}

TEST_F(MemoryTracerTest, RetrackingSameAddressReplacesHistoryWithoutIncreasingActiveCount)
{
	int object{};
	MemoryTracer::TrackObject(&object, "FirstObject", "first.cpp", 1);
	MemoryTracer::UntrackObject(&object, "first.cpp", 2);
	MemoryTracer::TrackObject(&object, "SecondObject", "second.cpp", 3);

	EXPECT_EQ(MemoryTracer::GetActiveObjectCount(), 1u);
	testing::internal::CaptureStdout();
	MemoryTracer::GetObjectHistory(&object);
	const std::string history = testing::internal::GetCapturedStdout();
	EXPECT_EQ(history.find("FirstObject"), std::string::npos);
	EXPECT_NE(history.find("SecondObject"), std::string::npos);
	EXPECT_NE(history.find("Status: ACTIVE"), std::string::npos);
}

TEST_F(MemoryTracerTest, LeakReportAndThreadStatisticsContainActiveCounts)
{
	int first{};
	int second{};
	MemoryTracer::TrackObject(&first, "First", __FILE__, __LINE__);
	MemoryTracer::TrackObject(&second, "Second", __FILE__, __LINE__);

	testing::internal::CaptureStdout();
	MemoryTracer::GenerateReport();
	const std::string report = testing::internal::GetCapturedStdout();
	EXPECT_NE(report.find("Total tracked objects: 2"), std::string::npos);
	EXPECT_NE(report.find("Active objects: 2"), std::string::npos);

	testing::internal::CaptureStdout();
	MemoryTracer::GetThreadStatistics();
	const std::string statistics = testing::internal::GetCapturedStdout();
	EXPECT_NE(statistics.find(": 2 active objects"), std::string::npos);
}

TEST_F(MemoryTracerTest, ConcurrentTrackingAndUntrackingLeavesNoActiveObjects)
{
	std::array<int, 8> objects{};
	std::array<std::jthread, 8> threads;

	for (size_t index = 0; index < threads.size(); ++index)
	{
		threads[index] = std::jthread([&, index]()
		{
			MemoryTracer::TrackObject(&objects[index], "ConcurrentObject", __FILE__, __LINE__);
			MemoryTracer::AddNote(&objects[index], std::to_string(index));
			MemoryTracer::UntrackObject(&objects[index], __FILE__, __LINE__);
		});
	}
	for (auto& thread : threads)
	{
		thread.join();
	}

	EXPECT_EQ(MemoryTracer::GetActiveObjectCount(), 0u);
}

TEST_F(MemoryTracerTest, ClearRemovesActiveAndFreedHistory)
{
	int object{};
	MemoryTracer::TrackObject(&object, "Object", __FILE__, __LINE__);
	MemoryTracer::UntrackObject(&object, __FILE__, __LINE__);

	MemoryTracer::Clear();

	EXPECT_EQ(MemoryTracer::GetActiveObjectCount(), 0u);
	testing::internal::CaptureStdout();
	MemoryTracer::GetObjectHistory(&object);
	const std::string history = testing::internal::GetCapturedStdout();
	EXPECT_NE(history.find("Object not found in tracker"), std::string::npos);
}
