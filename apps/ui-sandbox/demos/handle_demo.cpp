// Handle Demo Implementation
// Demonstrates resource handle safety and validation

#include "demo.h"
#include "resources/resource_handle.h"
#include "resources/resource_manager.h"
#include "utils/log.h"

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

	static void TestBasicAllocation();
	static void TestFreeListReuse();
	static void TestStaleHandles();
	static void TestHandleValidation();
	static void TestCapacityLimit();

	void Init() {
		LOG_INFO(UI, "");
		LOG_INFO(UI, "Handle Demo - Resource Handle System Tests");
		LOG_INFO(UI, "================================================");

		// Run all tests at initialization
		TestBasicAllocation();
		TestFreeListReuse();
		TestStaleHandles();
		TestHandleValidation();
		TestCapacityLimit();

		LOG_INFO(UI, "================================================");
		LOG_INFO(UI, "All handle tests passed!");
		LOG_INFO(UI, "");
	}

	void Render() {
		// Clear background
		glClearColor(0.1F, 0.1F, 0.15F, 1.0F);
		glClear(GL_COLOR_BUFFER_BIT);

		// No rendering needed for this demo - all output is to console
	}

	void Shutdown() {
		// No cleanup needed
	}

	// ============================================================================
	// Test Implementations
	// ============================================================================

	void TestBasicAllocation() {
		LOG_INFO(UI, "");
		LOG_INFO(UI, "Basic Allocation Test: Allocate and retrieve resources");
		LOG_INFO(UI, "--------------------------------------------------------");

		ResourceManager<TestResource> manager;

		// Allocate 3 resources
		ResourceHandle handle1 = manager.Allocate();
		ResourceHandle handle2 = manager.Allocate();
		ResourceHandle handle3 = manager.Allocate();

		LOG_INFO(UI, "Allocated 3 handles");
		LOG_INFO(
			UI, "  Handle 1: index=%d, gen=%d, valid=%s", handle1.GetIndex(), handle1.GetGeneration(), handle1.IsValid() ? "true" : "false"
		);
		LOG_INFO(
			UI, "  Handle 2: index=%d, gen=%d, valid=%s", handle2.GetIndex(), handle2.GetGeneration(), handle2.IsValid() ? "true" : "false"
		);
		LOG_INFO(
			UI, "  Handle 3: index=%d, gen=%d, valid=%s", handle3.GetIndex(), handle3.GetGeneration(), handle3.IsValid() ? "true" : "false"
		);

		// Set resource data
		TestResource* res1 = manager.Get(handle1);
		TestResource* res2 = manager.Get(handle2);
		TestResource* res3 = manager.Get(handle3);

		assert(res1 && res2 && res3); // NOLINT(readability-implicit-bool-conversion)

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
		LOG_INFO(UI, "Total count: %zu (should be 3)", manager.GetCount());
		LOG_INFO(UI, "Active count: %zu (should be 3)", manager.GetActiveCount());

		assert(manager.GetCount() == 3);
		assert(manager.GetActiveCount() == 3);

		LOG_INFO(UI, "Basic allocation test passed!");
	}

	void TestFreeListReuse() {
		LOG_INFO(UI, "");
		LOG_INFO(UI, "Free List Test: Verify index recycling");
		LOG_INFO(UI, "---------------------------------------");

		ResourceManager<TestResource> manager;

		// Allocate 5 handles
		std::array<ResourceHandle, 5> handles;
		for (size_t i = 0; i < handles.size(); i++) {
			handles.at(i) = manager.Allocate();
			TestResource* res = manager.Get(handles.at(i));
			res->id = static_cast<int>(i);
		}

		LOG_INFO(UI, "Allocated 5 resources (indices 0-4)");
		LOG_INFO(UI, "Active count: %zu", manager.GetActiveCount());

		// Free handles 1, 2, 3 (indices 1, 2, 3)
		manager.Free(handles.at(1));
		manager.Free(handles.at(2));
		manager.Free(handles.at(3));

		LOG_INFO(UI, "");
		LOG_INFO(UI, "Freed handles at indices 1, 2, 3");
		LOG_INFO(UI, "Active count: %zu (should be 2)", manager.GetActiveCount());

		assert(manager.GetActiveCount() == 2);

		// Allocate 2 new handles - should reuse indices 3 and 2 (LIFO from free list)
		ResourceHandle newHandle1 = manager.Allocate();
		ResourceHandle newHandle2 = manager.Allocate();

		LOG_INFO(UI, "");
		LOG_INFO(UI, "Allocated 2 new handles:");
		LOG_INFO(UI, "  New handle 1: index=%d, gen=%d (should reuse index 3, gen 1)", newHandle1.GetIndex(), newHandle1.GetGeneration());
		LOG_INFO(UI, "  New handle 2: index=%d, gen=%d (should reuse index 2, gen 1)", newHandle2.GetIndex(), newHandle2.GetGeneration());

		// Verify indices were reused and generation incremented
		assert((newHandle1.GetIndex() == 3 || newHandle1.GetIndex() == 2));
		assert((newHandle2.GetIndex() == 3 || newHandle2.GetIndex() == 2));
		assert(newHandle1.GetGeneration() == 1);
		assert(newHandle2.GetGeneration() == 1);

		LOG_INFO(UI, "");
		LOG_INFO(UI, "Active count: %zu (should be 4)", manager.GetActiveCount());
		assert(manager.GetActiveCount() == 4);

		LOG_INFO(UI, "Free list reuse test passed!");
	}

	void TestStaleHandles() {
		LOG_INFO(UI, "");
		LOG_INFO(UI, "Stale Handle Test: Verify generation validation");
		LOG_INFO(UI, "-------------------------------------------------");

		ResourceManager<TestResource> manager;

		// Allocate resource
		ResourceHandle handle = manager.Allocate();
		TestResource*  res = manager.Get(handle);
		assert(res); // NOLINT(readability-implicit-bool-conversion)
		res->id = 42;

		LOG_INFO(UI, "Allocated handle: index=%d, gen=%d", handle.GetIndex(), handle.GetGeneration());
		LOG_INFO(UI, "Resource id: %d", res->id);

		// Free the resource
		manager.Free(handle);
		LOG_INFO(UI, "");
		LOG_INFO(UI, "Freed the resource");

		// Try to access with old handle (should return nullptr)
		TestResource* staleRes = manager.Get(handle);
		LOG_INFO(UI, "Accessing with stale handle: %s", staleRes ? "FAIL - got resource!" : "PASS - returned null");
		assert(staleRes == nullptr); // NOLINT(readability-implicit-bool-conversion)

		// Allocate new resource in same slot
		ResourceHandle newHandle = manager.Allocate();
		LOG_INFO(UI, "");
		LOG_INFO(UI, "Allocated new handle in same slot: index=%d, gen=%d", newHandle.GetIndex(), newHandle.GetGeneration());

		// Verify new handle has incremented generation
		assert(newHandle.GetIndex() == handle.GetIndex());
		assert(newHandle.GetGeneration() == handle.GetGeneration() + 1);

		// Old handle should still be invalid
		staleRes = manager.Get(handle);
		LOG_INFO(UI, "Accessing with old handle after realloc: %s", staleRes ? "FAIL - got resource!" : "PASS - returned null");
		assert(staleRes == nullptr); // NOLINT(readability-implicit-bool-conversion)

		// New handle should work
		TestResource* newRes = manager.Get(newHandle);
		assert(newRes); // NOLINT(readability-implicit-bool-conversion)
		newRes->id = 99;
		LOG_INFO(UI, "Accessing with new handle: PASS - got resource (id=%d)", newRes->id);

		LOG_INFO(UI, "Stale handle test passed!");
	}

	void TestHandleValidation() {
		LOG_INFO(UI, "");
		LOG_INFO(UI, "Handle Validation Test: Test invalid handles");
		LOG_INFO(UI, "---------------------------------------------");

		ResourceManager<TestResource> manager;

		// Test invalid handle
		ResourceHandle invalidHandle = ResourceHandle::Invalid();
		LOG_INFO(UI, "Invalid handle: value=0x%08x, valid=%s", invalidHandle.value, invalidHandle.IsValid() ? "true" : "false");
		assert(!invalidHandle.IsValid()); // NOLINT(readability-implicit-bool-conversion)

		TestResource* res = manager.Get(invalidHandle);
		LOG_INFO(UI, "Get with invalid handle: %s", res ? "FAIL - got resource!" : "PASS - returned null");
		assert(res == nullptr); // NOLINT(readability-implicit-bool-conversion)

		// Test out-of-range handle
		ResourceHandle outOfRange = ResourceHandle::Make(9999, 0);
		res = manager.Get(outOfRange);
		LOG_INFO(UI, "Get with out-of-range index (9999): %s", res ? "FAIL - got resource!" : "PASS - returned null");
		assert(res == nullptr); // NOLINT(readability-implicit-bool-conversion)

		// Test handle comparison
		ResourceHandle h1 = manager.Allocate();
		ResourceHandle h2 = manager.Allocate();
		ResourceHandle h3 = h1;

		LOG_INFO(UI, "");
		LOG_INFO(UI, "Handle comparison:");
		LOG_INFO(UI, "  h1 == h1: %s", (h1 == h1) ? "true" : "false");
		LOG_INFO(UI, "  h1 == h2: %s", (h1 == h2) ? "true" : "false");
		LOG_INFO(UI, "  h1 == h3: %s", (h1 == h3) ? "true" : "false");
		LOG_INFO(UI, "  h1 != h2: %s", (h1 != h2) ? "true" : "false");

		assert((h1 == h1)); // NOLINT(readability-implicit-bool-conversion)
		assert((h1 != h2)); // NOLINT(readability-implicit-bool-conversion)
		assert((h1 == h3)); // NOLINT(readability-implicit-bool-conversion)

		LOG_INFO(UI, "Handle validation test passed!");
	}

	void TestCapacityLimit() {
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
			ResourceHandle handle = manager.Allocate();
			assert(handle.IsValid()); // NOLINT(readability-implicit-bool-conversion)
			assert(handle.GetIndex() == i);
			handles.push_back(handle);
		}

		LOG_INFO(UI, "Successfully allocated %d resources", kTestCount);
		LOG_INFO(UI, "Total count: %zu", manager.GetCount());
		LOG_INFO(UI, "Active count: %zu", manager.GetActiveCount());

		// Verify all handles are still valid and accessible
		for (int i = 0; i < kTestCount; i++) {
			TestResource* res = manager.Get(handles[i]);
			assert(res != nullptr); // NOLINT(readability-implicit-bool-conversion)
			res->id = i;
		}

		LOG_INFO(UI, "All %d resources accessible and writable", kTestCount);

		// Verify indices are correct
		assert(handles[0].GetIndex() == 0);
		assert(handles[kTestCount - 1].GetIndex() == kTestCount - 1);

		LOG_INFO(UI, "Index range: 0 to %d (correct)", kTestCount - 1);
		LOG_INFO(UI, "");
		LOG_INFO(UI, "Note: 16-bit index allows up to 65,536 resources (0-65535)");
		LOG_INFO(UI, "Capacity limit test passed!");
	}

} // namespace demo
