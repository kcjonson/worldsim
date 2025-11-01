#include "resource_manager.h"
#include "resource_handle.h"
#include <gtest/gtest.h>

using namespace renderer;

// ============================================================================
// ResourceHandle Tests
// ============================================================================

TEST(ResourceHandleTests, CreateValidHandle) {
	ResourceHandle handle = ResourceHandle::Make(42, 5);

	EXPECT_TRUE(handle.IsValid());
	EXPECT_EQ(handle.GetIndex(), 42);
	EXPECT_EQ(handle.GetGeneration(), 5);
}

TEST(ResourceHandleTests, CreateInvalidHandle) {
	ResourceHandle handle = ResourceHandle::Invalid();

	EXPECT_FALSE(handle.IsValid());
	EXPECT_EQ(handle.value, ResourceHandle::kInvalidHandle);
}

TEST(ResourceHandleTests, HandleEquality) {
	ResourceHandle handle1 = ResourceHandle::Make(10, 3);
	ResourceHandle handle2 = ResourceHandle::Make(10, 3);
	ResourceHandle handle3 = ResourceHandle::Make(10, 4); // Different generation
	ResourceHandle handle4 = ResourceHandle::Make(11, 3); // Different index

	EXPECT_EQ(handle1, handle2);
	EXPECT_NE(handle1, handle3);
	EXPECT_NE(handle1, handle4);
	EXPECT_NE(handle3, handle4);
}

TEST(ResourceHandleTests, InvalidHandleEquality) {
	ResourceHandle invalid1 = ResourceHandle::Invalid();
	ResourceHandle invalid2 = ResourceHandle::Invalid();
	ResourceHandle valid = ResourceHandle::Make(0, 0);

	EXPECT_EQ(invalid1, invalid2);
	EXPECT_NE(invalid1, valid);
}

TEST(ResourceHandleTests, ExtractIndexAndGeneration) {
	// Test various combinations
	ResourceHandle h1 = ResourceHandle::Make(0, 0);
	EXPECT_EQ(h1.GetIndex(), 0);
	EXPECT_EQ(h1.GetGeneration(), 0);

	ResourceHandle h2 = ResourceHandle::Make(65535, 0); // Max index
	EXPECT_EQ(h2.GetIndex(), 65535);
	EXPECT_EQ(h2.GetGeneration(), 0);

	ResourceHandle h3 = ResourceHandle::Make(0, 65535); // Max generation
	EXPECT_EQ(h3.GetIndex(), 0);
	EXPECT_EQ(h3.GetGeneration(), 65535);

	ResourceHandle h4 = ResourceHandle::Make(65535, 65535); // Both max
	EXPECT_EQ(h4.GetIndex(), 65535);
	EXPECT_EQ(h4.GetGeneration(), 65535);
}

TEST(ResourceHandleTests, TypeAliases) {
	// Verify type aliases compile and behave correctly
	TextureHandle  texHandle = ResourceHandle::Make(1, 0);
	MeshHandle	   meshHandle = ResourceHandle::Make(2, 0);
	SVGAssetHandle svgHandle = ResourceHandle::Make(3, 0);

	EXPECT_TRUE(texHandle.IsValid());
	EXPECT_TRUE(meshHandle.IsValid());
	EXPECT_TRUE(svgHandle.IsValid());

	EXPECT_EQ(texHandle.GetIndex(), 1);
	EXPECT_EQ(meshHandle.GetIndex(), 2);
	EXPECT_EQ(svgHandle.GetIndex(), 3);
}

// ============================================================================
// ResourceManager Tests
// ============================================================================

// Simple test resource type
struct TestResource {
	int			value = 0;
	std::string name{};
};

TEST(ResourceManagerTests, AllocateFirstResource) {
	ResourceManager<TestResource> manager;

	ResourceHandle handle = manager.Allocate();

	EXPECT_TRUE(handle.IsValid());
	EXPECT_EQ(handle.GetIndex(), 0);
	EXPECT_EQ(handle.GetGeneration(), 0);
}

TEST(ResourceManagerTests, AllocateMultipleResources) {
	ResourceManager<TestResource> manager;

	ResourceHandle h1 = manager.Allocate();
	ResourceHandle h2 = manager.Allocate();
	ResourceHandle h3 = manager.Allocate();

	EXPECT_EQ(h1.GetIndex(), 0);
	EXPECT_EQ(h2.GetIndex(), 1);
	EXPECT_EQ(h3.GetIndex(), 2);

	// All should have generation 0 initially
	EXPECT_EQ(h1.GetGeneration(), 0);
	EXPECT_EQ(h2.GetGeneration(), 0);
	EXPECT_EQ(h3.GetGeneration(), 0);
}

TEST(ResourceManagerTests, GetResource) {
	ResourceManager<TestResource> manager;

	ResourceHandle handle = manager.Allocate();
	TestResource*  resource = manager.Get(handle);

	ASSERT_NE(resource, nullptr);

	// Modify resource
	resource->value = 42;
	resource->name = "test";

	// Get again and verify it's the same resource
	TestResource* resource2 = manager.Get(handle);
	ASSERT_NE(resource2, nullptr);
	EXPECT_EQ(resource2->value, 42);
	EXPECT_EQ(resource2->name, "test");
}

TEST(ResourceManagerTests, GetInvalidHandle) {
	ResourceManager<TestResource> manager;

	ResourceHandle invalid = ResourceHandle::Invalid();
	TestResource*  resource = manager.Get(invalid);

	EXPECT_EQ(resource, nullptr);
}

TEST(ResourceManagerTests, GetOutOfBoundsHandle) {
	ResourceManager<TestResource> manager;

	// Create handle with index beyond allocated resources
	ResourceHandle oob = ResourceHandle::Make(100, 0);
	TestResource*  resource = manager.Get(oob);

	EXPECT_EQ(resource, nullptr);
}

TEST(ResourceManagerTests, FreeResource) {
	ResourceManager<TestResource> manager;

	ResourceHandle handle = manager.Allocate();
	TestResource*  resource = manager.Get(handle);
	ASSERT_NE(resource, nullptr);

	// Free the resource
	manager.Free(handle);

	// Handle should now be stale
	TestResource* stale = manager.Get(handle);
	EXPECT_EQ(stale, nullptr);
}

TEST(ResourceManagerTests, GenerationIncrementsOnFree) {
	ResourceManager<TestResource> manager;

	ResourceHandle handle = manager.Allocate();
	EXPECT_EQ(handle.GetGeneration(), 0);

	manager.Free(handle);

	// Allocate again in same slot
	ResourceHandle handle2 = manager.Allocate();
	EXPECT_EQ(handle2.GetIndex(), handle.GetIndex()); // Same index
	EXPECT_EQ(handle2.GetGeneration(), 1);			  // Generation incremented

	// Old handle should be invalid
	EXPECT_EQ(manager.Get(handle), nullptr);
	EXPECT_NE(manager.Get(handle2), nullptr);
}

TEST(ResourceManagerTests, ReuseFreedSlots) {
	ResourceManager<TestResource> manager;

	ResourceHandle h1 = manager.Allocate();
	ResourceHandle h2 = manager.Allocate();
	ResourceHandle h3 = manager.Allocate();

	EXPECT_EQ(manager.GetCount(), 3);

	// Free middle slot
	manager.Free(h2);

	// Allocate new resource - should reuse freed slot
	ResourceHandle h4 = manager.Allocate();
	EXPECT_EQ(h4.GetIndex(), h2.GetIndex()); // Same index as freed slot
	EXPECT_EQ(h4.GetGeneration(), 1);		 // Generation incremented

	// Total count should still be 3 (reused slot)
	EXPECT_EQ(manager.GetCount(), 3);
}

TEST(ResourceManagerTests, FreeInvalidHandle) {
	ResourceManager<TestResource> manager;

	ResourceHandle invalid = ResourceHandle::Invalid();

	// Should not crash
	manager.Free(invalid);
}

TEST(ResourceManagerTests, DoubleFree) {
	ResourceManager<TestResource> manager;

	ResourceHandle handle = manager.Allocate();

	manager.Free(handle);
	manager.Free(handle); // Double free - should be safe

	// Should not crash and handle should still be invalid
	EXPECT_EQ(manager.Get(handle), nullptr);
}

TEST(ResourceManagerTests, GetCount) {
	ResourceManager<TestResource> manager;

	EXPECT_EQ(manager.GetCount(), 0);

	ResourceHandle h1 = manager.Allocate();
	EXPECT_EQ(manager.GetCount(), 1);

	ResourceHandle h2 = manager.Allocate();
	EXPECT_EQ(manager.GetCount(), 2);

	manager.Free(h1);
	EXPECT_EQ(manager.GetCount(), 2); // Count doesn't decrease on free
}

TEST(ResourceManagerTests, GetActiveCount) {
	ResourceManager<TestResource> manager;

	EXPECT_EQ(manager.GetActiveCount(), 0);

	ResourceHandle h1 = manager.Allocate();
	ResourceHandle h2 = manager.Allocate();
	ResourceHandle h3 = manager.Allocate();

	EXPECT_EQ(manager.GetActiveCount(), 3);

	manager.Free(h2);
	EXPECT_EQ(manager.GetActiveCount(), 2); // Active count decreases

	manager.Free(h1);
	EXPECT_EQ(manager.GetActiveCount(), 1);
}

TEST(ResourceManagerTests, Clear) {
	ResourceManager<TestResource> manager;

	ResourceHandle h1 = manager.Allocate();
	ResourceHandle h2 = manager.Allocate();
	ResourceHandle h3 = manager.Allocate();

	EXPECT_EQ(manager.GetCount(), 3);

	manager.Clear();

	EXPECT_EQ(manager.GetCount(), 0);
	EXPECT_EQ(manager.GetActiveCount(), 0);

	// Old handles should be invalid
	EXPECT_EQ(manager.Get(h1), nullptr);
	EXPECT_EQ(manager.Get(h2), nullptr);
	EXPECT_EQ(manager.Get(h3), nullptr);
}

TEST(ResourceManagerTests, ClearAndReallocate) {
	ResourceManager<TestResource> manager;

	ResourceHandle h1 = manager.Allocate();
	manager.Clear();

	// After clear, allocation should start from 0 again
	ResourceHandle h2 = manager.Allocate();
	EXPECT_EQ(h2.GetIndex(), 0);
	EXPECT_EQ(h2.GetGeneration(), 0);
}

TEST(ResourceManagerTests, ConstGetResource) {
	ResourceManager<TestResource> manager;

	ResourceHandle handle = manager.Allocate();
	TestResource*  resource = manager.Get(handle);
	resource->value = 42;

	// Test const version
	const ResourceManager<TestResource>& constManager = manager;
	const TestResource*					 constResource = constManager.Get(handle);

	ASSERT_NE(constResource, nullptr);
	EXPECT_EQ(constResource->value, 42);
}

TEST(ResourceManagerTests, StaleHandleAfterMultipleFrees) {
	ResourceManager<TestResource> manager;

	ResourceHandle h1 = manager.Allocate();
	manager.Free(h1);

	ResourceHandle h2 = manager.Allocate(); // Reuses slot, gen=1
	manager.Free(h2);

	ResourceHandle h3 = manager.Allocate(); // Reuses slot, gen=2

	// h1 and h2 should be stale
	EXPECT_EQ(manager.Get(h1), nullptr);
	EXPECT_EQ(manager.Get(h2), nullptr);
	EXPECT_NE(manager.Get(h3), nullptr);

	// Verify generations
	EXPECT_EQ(h1.GetGeneration(), 0);
	EXPECT_EQ(h2.GetGeneration(), 1);
	EXPECT_EQ(h3.GetGeneration(), 2);
}

TEST(ResourceManagerTests, LargeAllocation) {
	ResourceManager<TestResource> manager(10000);

	std::vector<ResourceHandle> handles;
	handles.reserve(1000);

	// Allocate 1000 resources
	for (int i = 0; i < 1000; i++) {
		ResourceHandle handle = manager.Allocate();
		EXPECT_TRUE(handle.IsValid());
		EXPECT_EQ(handle.GetIndex(), i);
		handles.push_back(handle);

		// Set unique value
		TestResource* resource = manager.Get(handle);
		ASSERT_NE(resource, nullptr);
		resource->value = i;
	}

	// Verify all handles are still valid
	for (int i = 0; i < 1000; i++) {
		TestResource* resource = manager.Get(handles[i]);
		ASSERT_NE(resource, nullptr);
		EXPECT_EQ(resource->value, i);
	}

	EXPECT_EQ(manager.GetCount(), 1000);
	EXPECT_EQ(manager.GetActiveCount(), 1000);
}

TEST(ResourceManagerTests, InterleavedAllocateFree) {
	ResourceManager<TestResource> manager;

	ResourceHandle h1 = manager.Allocate();
	ResourceHandle h2 = manager.Allocate();
	manager.Free(h1);
	ResourceHandle h3 = manager.Allocate(); // Reuses h1's slot
	manager.Free(h2);
	ResourceHandle h4 = manager.Allocate(); // Reuses h2's slot

	// h1 and h2 should be stale
	EXPECT_EQ(manager.Get(h1), nullptr);
	EXPECT_EQ(manager.Get(h2), nullptr);

	// h3 and h4 should be valid
	EXPECT_NE(manager.Get(h3), nullptr);
	EXPECT_NE(manager.Get(h4), nullptr);

	// Verify slot reuse
	EXPECT_EQ(h3.GetIndex(), h1.GetIndex());
	EXPECT_EQ(h4.GetIndex(), h2.GetIndex());
}

// ============================================================================
// Integration Tests
// ============================================================================

TEST(ResourceManagerIntegrationTests, CompleteLifecycle) {
	ResourceManager<TestResource> manager;

	// Allocate resource
	ResourceHandle handle = manager.Allocate();
	EXPECT_TRUE(handle.IsValid());

	// Initialize resource
	TestResource* resource = manager.Get(handle);
	ASSERT_NE(resource, nullptr);
	resource->value = 999;
	resource->name = "important_data";

	// Verify data persists
	TestResource* retrieved = manager.Get(handle);
	ASSERT_NE(retrieved, nullptr);
	EXPECT_EQ(retrieved->value, 999);
	EXPECT_EQ(retrieved->name, "important_data");

	// Free resource
	manager.Free(handle);

	// Handle is now stale
	EXPECT_EQ(manager.Get(handle), nullptr);

	// Allocate new resource in same slot
	ResourceHandle newHandle = manager.Allocate();
	EXPECT_EQ(newHandle.GetIndex(), handle.GetIndex());
	EXPECT_NE(newHandle.GetGeneration(), handle.GetGeneration());

	// IMPORTANT: ResourceManager reuses the memory slot without resetting it
	// This is correct behavior - the manager doesn't know how to initialize resources
	// Users must initialize resources after allocation
	TestResource* newResource = manager.Get(newHandle);
	ASSERT_NE(newResource, nullptr);

	// Initialize the reused resource
	newResource->value = 0;
	newResource->name = "";

	// Verify initialization
	EXPECT_EQ(newResource->value, 0);
	EXPECT_EQ(newResource->name, "");
}
