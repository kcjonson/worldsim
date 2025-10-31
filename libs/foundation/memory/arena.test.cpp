#include "arena.h"
#include <gtest/gtest.h>

using namespace foundation;

// ============================================================================
// Arena Tests
// ============================================================================

TEST(ArenaTests, BasicAllocation) {
	Arena arena(1024);

	void* ptr = arena.Allocate(128);
	EXPECT_NE(ptr, nullptr);
	EXPECT_EQ(arena.GetUsed(), 128);
}

TEST(ArenaTests, MultipleAllocations) {
	Arena arena(1024);

	void* ptr1 = arena.Allocate(64);
	void* ptr2 = arena.Allocate(128);
	void* ptr3 = arena.Allocate(256);

	EXPECT_NE(ptr1, nullptr);
	EXPECT_NE(ptr2, nullptr);
	EXPECT_NE(ptr3, nullptr);

	// Verify pointers are different
	EXPECT_NE(ptr1, ptr2);
	EXPECT_NE(ptr2, ptr3);
	EXPECT_NE(ptr1, ptr3);

	// Verify usage tracking
	EXPECT_EQ(arena.GetUsed(), 64 + 128 + 256);
}

TEST(ArenaTests, Alignment) {
	Arena arena(1024);

	// Default 8-byte alignment
	void* ptr1 = arena.Allocate(1);
	EXPECT_EQ(reinterpret_cast<uintptr_t>(ptr1) % 8, 0);

	void* ptr2 = arena.Allocate(1);
	EXPECT_EQ(reinterpret_cast<uintptr_t>(ptr2) % 8, 0);

	// Custom 16-byte alignment
	void* ptr3 = arena.Allocate(1, 16);
	EXPECT_EQ(reinterpret_cast<uintptr_t>(ptr3) % 16, 0);
}

TEST(ArenaTests, TypeSafeAllocation) {
	Arena arena(1024);

	// Allocate single object
	int* intPtr = arena.Allocate<int>();
	EXPECT_NE(intPtr, nullptr);
	EXPECT_EQ(reinterpret_cast<uintptr_t>(intPtr) % alignof(int), 0);

	// Verify we can write to it
	*intPtr = 42;
	EXPECT_EQ(*intPtr, 42);

	// Allocate struct
	struct TestStruct {
		double x;
		int	   y;
	};
	TestStruct* structPtr = arena.Allocate<TestStruct>();
	EXPECT_NE(structPtr, nullptr);
	EXPECT_EQ(reinterpret_cast<uintptr_t>(structPtr) % alignof(TestStruct), 0);

	structPtr->x = 3.14;
	structPtr->y = 100;
	EXPECT_DOUBLE_EQ(structPtr->x, 3.14);
	EXPECT_EQ(structPtr->y, 100);
}

TEST(ArenaTests, ArrayAllocation) {
	Arena arena(1024);

	// Allocate array
	int* arr = arena.AllocateArray<int>(10);
	EXPECT_NE(arr, nullptr);
	EXPECT_EQ(reinterpret_cast<uintptr_t>(arr) % alignof(int), 0);

	// Verify we can write to it
	for (int i = 0; i < 10; ++i) {
		arr[i] = i * 10;
	}

	for (int i = 0; i < 10; ++i) {
		EXPECT_EQ(arr[i], i * 10);
	}

	// Verify usage
	EXPECT_GE(arena.GetUsed(), sizeof(int) * 10);
}

TEST(ArenaTests, Reset) {
	Arena arena(1024);

	// Allocate some memory
	arena.Allocate(256);
	arena.Allocate(128);
	EXPECT_EQ(arena.GetUsed(), 256 + 128);

	// Reset
	arena.Reset();
	EXPECT_EQ(arena.GetUsed(), 0);

	// Should be able to allocate again
	void* ptr = arena.Allocate(512);
	EXPECT_NE(ptr, nullptr);
	EXPECT_EQ(arena.GetUsed(), 512);
}

TEST(ArenaTests, Checkpoint) {
	Arena arena(1024);

	// First allocation
	arena.Allocate(128);
	size_t checkpoint = arena.GetUsed();
	EXPECT_EQ(checkpoint, 128);

	// More allocations
	arena.Allocate(256);
	arena.Allocate(64);
	EXPECT_EQ(arena.GetUsed(), 128 + 256 + 64);

	// Restore checkpoint
	arena.RestoreCheckpoint(checkpoint);
	EXPECT_EQ(arena.GetUsed(), 128);

	// Should be able to allocate from restored position
	arena.Allocate(100);
	EXPECT_EQ(arena.GetUsed(), 128 + 100);
}

TEST(ArenaTests, Metrics) {
	Arena arena(1024);

	EXPECT_EQ(arena.GetSize(), 1024);
	EXPECT_EQ(arena.GetUsed(), 0);
	EXPECT_EQ(arena.GetRemaining(), 1024);

	arena.Allocate(256);

	EXPECT_EQ(arena.GetSize(), 1024);
	EXPECT_EQ(arena.GetUsed(), 256);
	EXPECT_EQ(arena.GetRemaining(), 1024 - 256);

	arena.Allocate(512);

	EXPECT_EQ(arena.GetSize(), 1024);
	EXPECT_EQ(arena.GetUsed(), 256 + 512);
	EXPECT_EQ(arena.GetRemaining(), 1024 - 256 - 512);
}

TEST(ArenaTests, OutOfMemory) {
	Arena arena(128);

	// Fill arena
	void* ptr1 = arena.Allocate(64);
	void* ptr2 = arena.Allocate(64);
	EXPECT_NE(ptr1, nullptr);
	EXPECT_NE(ptr2, nullptr);

	// This should fail (arena is full)
	// In debug builds, this will assert and crash
	// We use EXPECT_DEATH to verify the assertion fires
#ifdef NDEBUG
	// Release mode - returns nullptr
	void* ptr3 = arena.Allocate(64);
	EXPECT_EQ(ptr3, nullptr);
#else
	// Debug mode - asserts
	EXPECT_DEATH(arena.Allocate(64), "Arena out of memory");
#endif
}

// ============================================================================
// FrameArena Tests
// ============================================================================

TEST(FrameArenaTests, BasicAllocation) {
	FrameArena arena(1024);

	void* ptr = arena.Allocate(128);
	EXPECT_NE(ptr, nullptr);
	EXPECT_EQ(arena.GetUsed(), 128);
}

TEST(FrameArenaTests, TypeSafeAllocation) {
	FrameArena arena(1024);

	int* intPtr = arena.Allocate<int>();
	EXPECT_NE(intPtr, nullptr);

	*intPtr = 123;
	EXPECT_EQ(*intPtr, 123);
}

TEST(FrameArenaTests, ArrayAllocation) {
	FrameArena arena(1024);

	float* arr = arena.AllocateArray<float>(20);
	EXPECT_NE(arr, nullptr);

	for (int i = 0; i < 20; ++i) {
		arr[i] = static_cast<float>(i) * 0.5F;
	}

	for (int i = 0; i < 20; ++i) {
		EXPECT_FLOAT_EQ(arr[i], static_cast<float>(i) * 0.5F);
	}
}

TEST(FrameArenaTests, ResetFrame) {
	FrameArena arena(1024);

	// Allocate memory
	arena.Allocate(256);
	arena.Allocate(128);
	EXPECT_EQ(arena.GetUsed(), 256 + 128);

	// Reset frame
	arena.ResetFrame();
	EXPECT_EQ(arena.GetUsed(), 0);

	// Should be able to allocate again
	void* ptr = arena.Allocate(512);
	EXPECT_NE(ptr, nullptr);
	EXPECT_EQ(arena.GetUsed(), 512);
}

TEST(FrameArenaTests, Metrics) {
	FrameArena arena(2048);

	EXPECT_EQ(arena.GetSize(), 2048);
	EXPECT_EQ(arena.GetUsed(), 0);
	EXPECT_EQ(arena.GetRemaining(), 2048);

	arena.Allocate(512);

	EXPECT_EQ(arena.GetSize(), 2048);
	EXPECT_EQ(arena.GetUsed(), 512);
	EXPECT_EQ(arena.GetRemaining(), 2048 - 512);
}

// ============================================================================
// ScopedArena Tests
// ============================================================================

TEST(ScopedArenaTests, RAIIRestore) {
	Arena arena(1024);

	// Initial allocation
	arena.Allocate(128);
	EXPECT_EQ(arena.GetUsed(), 128);

	{
		// Scoped allocations
		ScopedArena scoped(arena);
		scoped.Allocate(256);
		EXPECT_EQ(arena.GetUsed(), 128 + 256);

		scoped.Allocate(64);
		EXPECT_EQ(arena.GetUsed(), 128 + 256 + 64);

		// Destructor will restore checkpoint here
	}

	// After scope, should be back to 128
	EXPECT_EQ(arena.GetUsed(), 128);
}

TEST(ScopedArenaTests, NestedScopes) {
	Arena arena(1024);

	// Use sizes that are multiples of 8 to avoid alignment padding issues
	arena.Allocate(104);
	EXPECT_EQ(arena.GetUsed(), 104);

	{
		ScopedArena scoped1(arena);
		scoped1.Allocate(200);
		EXPECT_EQ(arena.GetUsed(), 104 + 200);

		{
			ScopedArena scoped2(arena);
			scoped2.Allocate(304);
			EXPECT_EQ(arena.GetUsed(), 104 + 200 + 304);

			// scoped2 destructor restores to 104 + 200
		}

		EXPECT_EQ(arena.GetUsed(), 104 + 200);

		// scoped1 destructor restores to 104
	}

	EXPECT_EQ(arena.GetUsed(), 104);
}

TEST(ScopedArenaTests, TypeSafeAllocation) {
	Arena arena(1024);

	{
		ScopedArena scoped(arena);

		int* intPtr = scoped.Allocate<int>();
		EXPECT_NE(intPtr, nullptr);
		*intPtr = 999;
		EXPECT_EQ(*intPtr, 999);

		double* arr = scoped.AllocateArray<double>(5);
		EXPECT_NE(arr, nullptr);
		for (int i = 0; i < 5; ++i) {
			arr[i] = static_cast<double>(i) * 1.5;
		}

		EXPECT_GT(arena.GetUsed(), 0);
	}

	// All scoped allocations restored
	EXPECT_EQ(arena.GetUsed(), 0);
}

TEST(ScopedArenaTests, AllocationsPersistOutsideScope) {
	Arena arena(1024);

	// Use sizes that are multiples of 8 to avoid alignment padding issues
	void* ptr1 = arena.Allocate(104);
	EXPECT_NE(ptr1, nullptr);

	{
		ScopedArena scoped(arena);
		scoped.Allocate(200);
		EXPECT_EQ(arena.GetUsed(), 104 + 200);
	}

	// Scoped allocations gone, but original allocation persists
	EXPECT_EQ(arena.GetUsed(), 104);

	void* ptr2 = arena.Allocate(56);
	EXPECT_NE(ptr2, nullptr);
	EXPECT_EQ(arena.GetUsed(), 104 + 56);
}
