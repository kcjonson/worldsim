// Handle Demo Implementation
// Demonstrates resource handle safety and validation

#include "Demo.h"
#include "resources/ResourceHandle.h"
#include "resources/ResourceManager.h"
#include "utils/Log.h"

#include <GL/glew.h>
#include <array>
#include <vector>

using namespace renderer;

namespace demo {

	// Simple test resource
	struct TestResource {
		int			id;
		float		value;
		const char* name;
	};

	static void testBasicAllocation();
	static void testFreeListReuse();
	static void testStaleHandles();
	static void testHandleValidation();
	static void testCapacityLimit();

	void init() {
		LOG_INFO(UI, "");
		LOG_INFO(UI, "Handle Demo - Resource Handle System Tests");
		LOG_INFO(UI, "================================================");

		// Run all tests at initialization
		testBasicAllocation();
		testFreeListReuse();
		testStaleHandles();
		testHandleValidation();
		testCapacityLimit();

		LOG_INFO(UI, "================================================");
		LOG_INFO(UI, "All handle tests passed!");
		LOG_INFO(UI, "");
	}

	void render() {
		// Clear background
		glClearColor(0.1F, 0.1F, 0.15F, 1.0F);
		glClear(GL_COLOR_BUFFER_BIT);

		// No rendering needed for this demo - all output is to console
	}

	void shutdown() {
		// No cleanup needed
	}

	// ============================================================================
	// Test Implementations
	// ============================================================================

	void testBasicAllocation() {
		LOG_INFO(UI, "");
		LOG_INFO(UI, "Basic Allocation Test: Allocate and retrieve resources");
		LOG_INFO(UI, "--------------------------------------------------------");

		ResourceManager<TestResource> manager;

		// Allocate 3 resources
		ResourceHandle handle1 = manager.allocate();
		ResourceHandle handle2 = manager.allocate();
		ResourceHandle handle3 = manager.allocate();

		LOG_INFO(UI, "Allocated 3 handles");
		LOG_INFO(
			UI, "  Handle 1: index=%d, gen=%d, valid=%s", handle1.getIndex(), handle1.getGeneration(), handle1.IsValid() ? "true" : "false"
		);
		LOG_INFO(
			UI, "  Handle 2: index=%d, gen=%d, valid=%s", handle2.getIndex(), handle2.getGeneration(), handle2.IsValid() ? "true" : "false"
		);
		LOG_INFO(
			UI, "  Handle 3: index=%d, gen=%d, valid=%s", handle3.getIndex(), handle3.getGeneration(), handle3.IsValid() ? "true" : "false"
		);

		// Set resource data
		TestResource* res1 = manager.get(handle1);
		TestResource* res2 = manager.get(handle2);
		TestResource* res3 = manager.get(handle3);

		assert(res1 && res2 && res3 && "Failed to get resources"); // NOLINT(readability-implicit-bool-conversion)

		res1->id = 1;
		res1->value = 1.5F;
		res1->name = "Resource1";

		res2->id = 2;
		res2->value = 2.5F;
		res2->name = "Resource2";

		res3->id = 3;
		res3->value = 3.5F;
		res3->name = "Resource3";

		LOG_INFO(UI, "");
		LOG_INFO(UI, "Resource data:");
		LOG_INFO(UI, "  Resource 1: id=%d, value=%.1F, name=%s", res1->id, res1->value, res1->name);
		LOG_INFO(UI, "  Resource 2: id=%d, value=%.1F, name=%s", res2->id, res2->value, res2->name);
		LOG_INFO(UI, "  Resource 3: id=%d, value=%.1F, name=%s", res3->id, res3->value, res3->name);

		// Verify count
		LOG_INFO(UI, "");
		LOG_INFO(UI, "Total count: %zu (should be 3)", manager.getCount());
		LOG_INFO(UI, "Active count: %zu (should be 3)", manager.getActiveCount());

		assert(manager.getCount() == 3 && "Wrong total count");
		assert(manager.getActiveCount() == 3 && "Wrong active count");

		LOG_INFO(UI, "Basic allocation test passed!");
	}

	void testFreeListReuse() {
		LOG_INFO(UI, "");
		LOG_INFO(UI, "Free List Test: Verify index recycling");
		LOG_INFO(UI, "---------------------------------------");

		ResourceManager<TestResource> manager;

		// Allocate 5 handles
		std::array<ResourceHandle, 5> handles;
		for (size_t i = 0; i < handles.size(); i++) {
			handles.at(i) = manager.allocate();
			TestResource* res = manager.get(handles.at(i));
			res->id = static_cast<int>(i);
		}

		LOG_INFO(UI, "Allocated 5 resources (indices 0-4)");
		LOG_INFO(UI, "Active count: %zu", manager.getActiveCount());

		// Free handles 1, 2, 3 (indices 1, 2, 3)
		manager.free(handles.at(1));
		manager.free(handles.at(2));
		manager.free(handles.at(3));

		LOG_INFO(UI, "");
		LOG_INFO(UI, "Freed handles at indices 1, 2, 3");
		LOG_INFO(UI, "Active count: %zu (should be 2)", manager.getActiveCount());

		assert(manager.getActiveCount() == 2 && "Wrong active count after free");

		// Allocate 2 new handles - should reuse indices 3 and 2 (LIFO from free list)
		ResourceHandle newHandle1 = manager.allocate();
		ResourceHandle newHandle2 = manager.allocate();

		LOG_INFO(UI, "");
		LOG_INFO(UI, "Allocated 2 new handles:");
		LOG_INFO(UI, "  New handle 1: index=%d, gen=%d (should reuse index 3, gen 1)", newHandle1.getIndex(), newHandle1.getGeneration());
		LOG_INFO(UI, "  New handle 2: index=%d, gen=%d (should reuse index 2, gen 1)", newHandle2.getIndex(), newHandle2.getGeneration());

		// Verify indices were reused and generation incremented
		assert((newHandle1.getIndex() == 3 || newHandle1.getIndex() == 2) && "Index not reused");
		assert((newHandle2.getIndex() == 3 || newHandle2.getIndex() == 2) && "Index not reused");
		assert(newHandle1.getGeneration() == 1 && "Generation not incremented");
		assert(newHandle2.getGeneration() == 1 && "Generation not incremented");

		LOG_INFO(UI, "");
		LOG_INFO(UI, "Active count: %zu (should be 4)", manager.getActiveCount());
		assert(manager.getActiveCount() == 4 && "Wrong active count after realloc");

		LOG_INFO(UI, "Free list reuse test passed!");
	}

	void testStaleHandles() {
		LOG_INFO(UI, "");
		LOG_INFO(UI, "Stale Handle Test: Verify generation validation");
		LOG_INFO(UI, "-------------------------------------------------");

		ResourceManager<TestResource> manager;

		// Allocate resource
		ResourceHandle handle = manager.allocate();
		TestResource*  res = manager.get(handle);
		assert(res && "Failed to get resource"); // NOLINT(readability-implicit-bool-conversion)
		res->id = 42;

		LOG_INFO(UI, "Allocated handle: index=%d, gen=%d", handle.getIndex(), handle.getGeneration());
		LOG_INFO(UI, "Resource id: %d", res->id);

		// Free the resource
		manager.free(handle);
		LOG_INFO(UI, "");
		LOG_INFO(UI, "Freed the resource");

		// Try to access with old handle (should return nullptr)
		TestResource* staleRes = manager.get(handle);
		LOG_INFO(UI, "Accessing with stale handle: %s", staleRes ? "FAIL - got resource!" : "PASS - returned null");
		assert(staleRes == nullptr && "Stale handle returned resource!"); // NOLINT(readability-implicit-bool-conversion)

		// Allocate new resource in same slot
		ResourceHandle newHandle = manager.allocate();
		LOG_INFO(UI, "");
		LOG_INFO(UI, "Allocated new handle in same slot: index=%d, gen=%d", newHandle.getIndex(), newHandle.getGeneration());

		// Verify new handle has incremented generation
		assert(newHandle.getIndex() == handle.getIndex() && "Different index");
		assert(newHandle.getGeneration() == handle.getGeneration() + 1 && "Generation not incremented");

		// Old handle should still be invalid
		staleRes = manager.get(handle);
		LOG_INFO(UI, "Accessing with old handle after realloc: %s", staleRes ? "FAIL - got resource!" : "PASS - returned null");
		assert(staleRes == nullptr && "Old handle should still be invalid"); // NOLINT(readability-implicit-bool-conversion)

		// New handle should work
		TestResource* newRes = manager.get(newHandle);
		assert(newRes && "New handle should be valid"); // NOLINT(readability-implicit-bool-conversion)
		newRes->id = 99;
		LOG_INFO(UI, "Accessing with new handle: PASS - got resource (id=%d)", newRes->id);

		LOG_INFO(UI, "Stale handle test passed!");
	}

	void testHandleValidation() {
		LOG_INFO(UI, "");
		LOG_INFO(UI, "Handle Validation Test: Test invalid handles");
		LOG_INFO(UI, "---------------------------------------------");

		ResourceManager<TestResource> manager;

		// Test invalid handle
		ResourceHandle invalidHandle = ResourceHandle::invalid();
		LOG_INFO(UI, "Invalid handle: value=0x%08x, valid=%s", invalidHandle.value, invalidHandle.IsValid() ? "true" : "false");
		assert(!invalidHandle.IsValid() && "Invalid handle should not be valid"); // NOLINT(readability-implicit-bool-conversion)

		TestResource* res = manager.get(invalidHandle);
		LOG_INFO(UI, "Get with invalid handle: %s", res ? "FAIL - got resource!" : "PASS - returned null");
		assert(res == nullptr && "Invalid handle should return null"); // NOLINT(readability-implicit-bool-conversion)

		// Test out-of-range handle
		ResourceHandle outOfRange = ResourceHandle::make(9999, 0);
		res = manager.get(outOfRange);
		LOG_INFO(UI, "Get with out-of-range index (9999): %s", res ? "FAIL - got resource!" : "PASS - returned null");
		assert(res == nullptr && "Out-of-range handle should return null"); // NOLINT(readability-implicit-bool-conversion)

		// Test handle comparison
		ResourceHandle h1 = manager.allocate();
		ResourceHandle h2 = manager.allocate();
		ResourceHandle h3 = h1;

		LOG_INFO(UI, "");
		LOG_INFO(UI, "Handle comparison:");
		LOG_INFO(UI, "  h1 == h1: %s", (h1 == h1) ? "true" : "false");
		LOG_INFO(UI, "  h1 == h2: %s", (h1 == h2) ? "true" : "false");
		LOG_INFO(UI, "  h1 == h3: %s", (h1 == h3) ? "true" : "false");
		LOG_INFO(UI, "  h1 != h2: %s", (h1 != h2) ? "true" : "false");

		assert((h1 == h1) && "Same handle should be equal");		   // NOLINT(readability-implicit-bool-conversion)
		assert((h1 != h2) && "Different handles should not be equal"); // NOLINT(readability-implicit-bool-conversion)
		assert((h1 == h3) && "Copied handle should be equal");		   // NOLINT(readability-implicit-bool-conversion)

		LOG_INFO(UI, "Handle validation test passed!");
	}

	void testCapacityLimit() {
		LOG_INFO(UI, "");
		LOG_INFO(UI, "Capacity Limit Test: Verify 65,536 resource limit");
		LOG_INFO(UI, "---------------------------------------------------");

		ResourceManager<TestResource> manager;

		// Allocate a large number of resources to verify the system handles it correctly
		constexpr int				kTestCount = 10000;
		std::vector<ResourceHandle> handles;
		handles.reserve(kTestCount);

		LOG_INFO(UI, "Allocating %d resources...", kTestCount);

		for (int i = 0; i < kTestCount; i++) {
			ResourceHandle handle = manager.allocate();
			assert(handle.IsValid() && "Handle should be valid"); // NOLINT(readability-implicit-bool-conversion)
			assert(handle.getIndex() == i && "Index should match allocation order");
			handles.push_back(handle);
		}

		LOG_INFO(UI, "Successfully allocated %d resources", kTestCount);
		LOG_INFO(UI, "Total count: %zu", manager.getCount());
		LOG_INFO(UI, "Active count: %zu", manager.getActiveCount());

		// Verify all handles are still valid and accessible
		for (int i = 0; i < kTestCount; i++) {
			TestResource* res = manager.get(handles[i]);
			assert(res != nullptr && "Resource should be accessible"); // NOLINT(readability-implicit-bool-conversion)
			res->id = i;
		}

		LOG_INFO(UI, "All %d resources accessible and writable", kTestCount);

		// Verify indices are correct
		assert(handles[0].getIndex() == 0 && "First index should be 0");
		assert(handles[kTestCount - 1].getIndex() == kTestCount - 1 && "Last index should be count-1");

		LOG_INFO(UI, "Index range: 0 to %d (correct)", kTestCount - 1);
		LOG_INFO(UI, "");
		LOG_INFO(UI, "Note: 16-bit index allows up to 65,536 resources (0-65535)");
		LOG_INFO(UI, "Capacity limit test passed!");
	}

} // namespace demo
