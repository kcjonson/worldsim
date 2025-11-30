#include "Arena.h"
#include <gtest/gtest.h>

using namespace foundation;

// ============================================================================
// Arena Tests
// ============================================================================

TEST(ArenaTests, BasicAllocation) {
	Arena arena(1024);

	void* ptr = arena.allocate(128);
	EXPECT_NE(ptr, nullptr);
	EXPECT_EQ(arena.getUsed(), 128);
}

TEST(ArenaTests, MultipleAllocations) {
	Arena arena(1024);

	void* ptr1 = arena.allocate(64);
	void* ptr2 = arena.allocate(128);
	void* ptr3 = arena.allocate(256);

	EXPECT_NE(ptr1, nullptr);
	EXPECT_NE(ptr2, nullptr);
	EXPECT_NE(ptr3, nullptr);

	// Verify pointers are different
	EXPECT_NE(ptr1, ptr2);
	EXPECT_NE(ptr2, ptr3);
	EXPECT_NE(ptr1, ptr3);

	// Verify usage tracking
	EXPECT_EQ(arena.getUsed(), 64 + 128 + 256);
}

TEST(ArenaTests, Alignment) {
	Arena arena(1024);

	// Default 8-byte alignment
	void* ptr1 = arena.allocate(1);
	EXPECT_EQ(reinterpret_cast<uintptr_t>(ptr1) % 8, 0);

	void* ptr2 = arena.allocate(1);
	EXPECT_EQ(reinterpret_cast<uintptr_t>(ptr2) % 8, 0);

	// Custom 16-byte alignment
	void* ptr3 = arena.allocate(1, 16);
	EXPECT_EQ(reinterpret_cast<uintptr_t>(ptr3) % 16, 0);
}

TEST(ArenaTests, TypeSafeAllocation) {
	Arena arena(1024);

	// Allocate single object
	int* intPtr = arena.allocate<int>();
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
	TestStruct* structPtr = arena.allocate<TestStruct>();
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
	int* arr = arena.allocateArray<int>(10);
	EXPECT_NE(arr, nullptr);
	EXPECT_EQ(reinterpret_cast<uintptr_t>(arr) % alignof(int), 0);

	// Verify we can write to it
	for (int i = 0; i < 10; ++i) {
		arr[i] = i * 10; // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
	}

	for (int i = 0; i < 10; ++i) {
		EXPECT_EQ(arr[i], i * 10);
	}

	// Verify usage
	EXPECT_GE(arena.getUsed(), sizeof(int) * 10);
}

TEST(ArenaTests, Reset) {
	Arena arena(1024);

	// Allocate some memory
	arena.allocate(256);
	arena.allocate(128);
	EXPECT_EQ(arena.getUsed(), 256 + 128);

	// Reset
	arena.reset();
	EXPECT_EQ(arena.getUsed(), 0);

	// Should be able to allocate again
	void* ptr = arena.allocate(512);
	EXPECT_NE(ptr, nullptr);
	EXPECT_EQ(arena.getUsed(), 512);
}

TEST(ArenaTests, Checkpoint) {
	Arena arena(1024);

	// First allocation
	arena.allocate(128);
	size_t checkpoint = arena.getUsed();
	EXPECT_EQ(checkpoint, 128);

	// More allocations
	arena.allocate(256);
	arena.allocate(64);
	EXPECT_EQ(arena.getUsed(), 128 + 256 + 64);

	// Restore checkpoint
	arena.restoreCheckpoint(checkpoint);
	EXPECT_EQ(arena.getUsed(), 128);

	// Should be able to allocate from restored position
	arena.allocate(100);
	EXPECT_EQ(arena.getUsed(), 128 + 100);
}

TEST(ArenaTests, Metrics) {
	Arena arena(1024);

	EXPECT_EQ(arena.getSize(), 1024);
	EXPECT_EQ(arena.getUsed(), 0);
	EXPECT_EQ(arena.getRemaining(), 1024);

	arena.allocate(256);

	EXPECT_EQ(arena.getSize(), 1024);
	EXPECT_EQ(arena.getUsed(), 256);
	EXPECT_EQ(arena.getRemaining(), 1024 - 256);

	arena.allocate(512);

	EXPECT_EQ(arena.getSize(), 1024);
	EXPECT_EQ(arena.getUsed(), 256 + 512);
	EXPECT_EQ(arena.getRemaining(), 1024 - 256 - 512);
}

TEST(ArenaTests, OutOfMemory) {
	Arena arena(128);

	// Fill arena
	void* ptr1 = arena.allocate(64);
	void* ptr2 = arena.allocate(64);
	EXPECT_NE(ptr1, nullptr);
	EXPECT_NE(ptr2, nullptr);

	// This should fail (arena is full)
	// In debug builds, this will assert and crash
	// We use EXPECT_DEATH to verify the assertion fires
#ifdef NDEBUG
	// Release mode - returns nullptr
	void* ptr3 = arena.allocate(64);
	EXPECT_EQ(ptr3, nullptr);
#else
	// Debug mode - asserts
	EXPECT_DEATH(arena.allocate(64), "Arena out of memory");
#endif
}

// ============================================================================
// FrameArena Tests
// ============================================================================

TEST(FrameArenaTests, BasicAllocation) {
	FrameArena arena(1024);

	void* ptr = arena.allocate(128);
	EXPECT_NE(ptr, nullptr);
	EXPECT_EQ(arena.getUsed(), 128);
}

TEST(FrameArenaTests, TypeSafeAllocation) {
	FrameArena arena(1024);

	int* intPtr = arena.allocate<int>();
	EXPECT_NE(intPtr, nullptr);

	*intPtr = 123;
	EXPECT_EQ(*intPtr, 123);
}

TEST(FrameArenaTests, ArrayAllocation) {
	FrameArena arena(1024);

	float* arr = arena.allocateArray<float>(20);
	EXPECT_NE(arr, nullptr);

	for (int i = 0; i < 20; ++i) {
		arr[i] = static_cast<float>(i) * 0.5F; // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
	}

	for (int i = 0; i < 20; ++i) {
		EXPECT_FLOAT_EQ(arr[i], static_cast<float>(i) * 0.5F); // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
	}
}

TEST(FrameArenaTests, ResetFrame) {
	FrameArena arena(1024);

	// Allocate memory
	arena.allocate(256);
	arena.allocate(128);
	EXPECT_EQ(arena.getUsed(), 256 + 128);

	// Reset frame
	arena.resetFrame();
	EXPECT_EQ(arena.getUsed(), 0);

	// Should be able to allocate again
	void* ptr = arena.allocate(512);
	EXPECT_NE(ptr, nullptr);
	EXPECT_EQ(arena.getUsed(), 512);
}

TEST(FrameArenaTests, Metrics) {
	FrameArena arena(2048);

	EXPECT_EQ(arena.getSize(), 2048);
	EXPECT_EQ(arena.getUsed(), 0);
	EXPECT_EQ(arena.getRemaining(), 2048);

	arena.allocate(512);

	EXPECT_EQ(arena.getSize(), 2048);
	EXPECT_EQ(arena.getUsed(), 512);
	EXPECT_EQ(arena.getRemaining(), 2048 - 512);
}

// ============================================================================
// ScopedArena Tests
// ============================================================================

TEST(ScopedArenaTests, RAIIRestore) {
	Arena arena(1024);

	// Initial allocation
	arena.allocate(128);
	EXPECT_EQ(arena.getUsed(), 128);

	{
		// Scoped allocations
		ScopedArena scoped(arena);
		scoped.allocate(256);
		EXPECT_EQ(arena.getUsed(), 128 + 256);

		scoped.allocate(64);
		EXPECT_EQ(arena.getUsed(), 128 + 256 + 64);

		// Destructor will restore checkpoint here
	}

	// After scope, should be back to 128
	EXPECT_EQ(arena.getUsed(), 128);
}

TEST(ScopedArenaTests, NestedScopes) {
	Arena arena(1024);

	// Use sizes that are multiples of 8 to avoid alignment padding issues
	arena.allocate(104);
	EXPECT_EQ(arena.getUsed(), 104);

	{
		ScopedArena scoped1(arena);
		scoped1.allocate(200);
		EXPECT_EQ(arena.getUsed(), 104 + 200);

		{
			ScopedArena scoped2(arena);
			scoped2.allocate(304);
			EXPECT_EQ(arena.getUsed(), 104 + 200 + 304);

			// scoped2 destructor restores to 104 + 200
		}

		EXPECT_EQ(arena.getUsed(), 104 + 200);

		// scoped1 destructor restores to 104
	}

	EXPECT_EQ(arena.getUsed(), 104);
}

TEST(ScopedArenaTests, TypeSafeAllocation) {
	Arena arena(1024);

	{
		ScopedArena scoped(arena);

		int* intPtr = scoped.allocate<int>();
		EXPECT_NE(intPtr, nullptr);
		*intPtr = 999;
		EXPECT_EQ(*intPtr, 999);

		double* arr = scoped.allocateArray<double>(5);
		EXPECT_NE(arr, nullptr);
		for (int i = 0; i < 5; ++i) {
			arr[i] = static_cast<double>(i) * 1.5; // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
		}

		EXPECT_GT(arena.getUsed(), 0);
	}

	// All scoped allocations restored
	EXPECT_EQ(arena.getUsed(), 0);
}

TEST(ScopedArenaTests, AllocationsPersistOutsideScope) {
	Arena arena(1024);

	// Use sizes that are multiples of 8 to avoid alignment padding issues
	void* ptr1 = arena.allocate(104);
	EXPECT_NE(ptr1, nullptr);

	{
		ScopedArena scoped(arena);
		scoped.allocate(200);
		EXPECT_EQ(arena.getUsed(), 104 + 200);
	}

	// Scoped allocations gone, but original allocation persists
	EXPECT_EQ(arena.getUsed(), 104);

	void* ptr2 = arena.allocate(56);
	EXPECT_NE(ptr2, nullptr);
	EXPECT_EQ(arena.getUsed(), 104 + 56);
}
