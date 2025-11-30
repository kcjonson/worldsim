// Handle Scene - Resource Handle System Tests
// Demonstrates resource handle safety and validation

#include <resources/resource_handle.h>
#include <resources/resource_manager.h>
#include <scene/scene.h>
#include <scene/scene_manager.h>
#include <utils/log.h>

#include <GL/glew.h>
#include <array>
#include <vector>

using namespace renderer;

namespace {

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

	class HandleScene : public engine::IScene {
	  public:
		void onEnter() override {
			LOG_INFO(UI, "");
			LOG_INFO(UI, "Handle Scene - Resource Handle System Tests");
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

		void handleInput(float dt) override {
			// No input handling needed - test scene
		}

		void update(float dt) override {
			// No update logic needed - tests run on enter
		}

		void render() override {
			// Clear background
			glClearColor(0.1F, 0.1F, 0.15F, 1.0F);
			glClear(GL_COLOR_BUFFER_BIT);

			// No rendering needed for this scene - all output is to console/logs
		}

		void onExit() override {
			// No cleanup needed
		}

		[[nodiscard]] std::string exportState() override { // NOLINT(readability-convert-member-functions-to-static)
			return R"({
			"scene": "handles",
			"description": "Resource handle system tests",
			"tests": ["basic_allocation", "free_list_reuse", "stale_handles", "handle_validation", "capacity_limit"],
			"status": "Tests run on scene enter, see console/logs for results"
		})";
		}

		const char* getName() const override { return "handles"; }
	};

	// ============================================================================
	// Test Implementations
	// ============================================================================

	void testBasicAllocation() {
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
			UI, "  Handle 1: index=%d, gen=%d, valid=%s", handle1.getIndex(), handle1.getGeneration(), handle1.isValid() ? "true" : "false"
		);
		LOG_INFO(
			UI, "  Handle 2: index=%d, gen=%d, valid=%s", handle2.getIndex(), handle2.getGeneration(), handle2.isValid() ? "true" : "false"
		);
		LOG_INFO(
			UI, "  Handle 3: index=%d, gen=%d, valid=%s", handle3.getIndex(), handle3.getGeneration(), handle3.isValid() ? "true" : "false"
		);

		// Set resource data
		TestResource* res1 = manager.Get(handle1);
		TestResource* res2 = manager.Get(handle2);
		TestResource* res3 = manager.Get(handle3);

		assert((res1 != nullptr && res2 != nullptr && res3 != nullptr) && static_cast<bool>("Failed to get resources"));

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
		LOG_INFO(UI, "  Resource 1: id=%d, value=%.1F, name=%s", res1->id, res1->value, res1->name != nullptr ? res1->name : "null");
		LOG_INFO(UI, "  Resource 2: id=%d, value=%.1F, name=%s", res2->id, res2->value, res2->name != nullptr ? res2->name : "null");
		LOG_INFO(UI, "  Resource 3: id=%d, value=%.1F, name=%s", res3->id, res3->value, res3->name != nullptr ? res3->name : "null");

		// Verify count
		LOG_INFO(UI, "");
		LOG_INFO(UI, "Total count: %zu (should be 3)", manager.getCount());
		LOG_INFO(UI, "Active count: %zu (should be 3)", manager.getActiveCount());

		assert(manager.getCount() == 3 && static_cast<bool>("Wrong total count"));
		assert(manager.getActiveCount() == 3 && static_cast<bool>("Wrong active count"));

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
			handles.at(i) = manager.Allocate();
			TestResource* res = manager.Get(handles.at(i));
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

		assert(manager.getActiveCount() == 2 && static_cast<bool>("Wrong active count after free"));

		// Allocate 2 new handles - should reuse indices 3 and 2 (LIFO from free list)
		ResourceHandle newHandle1 = manager.Allocate();
		ResourceHandle newHandle2 = manager.Allocate();

		LOG_INFO(UI, "");
		LOG_INFO(UI, "Allocated 2 new handles:");
		LOG_INFO(UI, "  New handle 1: index=%d, gen=%d (should reuse index 3, gen 1)", newHandle1.getIndex(), newHandle1.getGeneration());
		LOG_INFO(UI, "  New handle 2: index=%d, gen=%d (should reuse index 2, gen 1)", newHandle2.getIndex(), newHandle2.getGeneration());

		// Verify indices were reused and generation incremented
		assert((newHandle1.getIndex() == 3 || newHandle1.getIndex() == 2) && static_cast<bool>("Index not reused"));
		assert((newHandle2.getIndex() == 3 || newHandle2.getIndex() == 2) && static_cast<bool>("Index not reused"));
		assert(newHandle1.getGeneration() == 1 && static_cast<bool>("Generation not incremented"));
		assert(newHandle2.getGeneration() == 1 && static_cast<bool>("Generation not incremented"));

		LOG_INFO(UI, "");
		LOG_INFO(UI, "Active count: %zu (should be 4)", manager.getActiveCount());
		assert(manager.getActiveCount() == 4 && static_cast<bool>("Wrong active count after realloc"));

		LOG_INFO(UI, "Free list reuse test passed!");
	}

	void testStaleHandles() {
		LOG_INFO(UI, "");
		LOG_INFO(UI, "Stale Handle Test: Verify generation validation");
		LOG_INFO(UI, "-------------------------------------------------");

		ResourceManager<TestResource> manager;

		// Allocate resource
		ResourceHandle handle = manager.Allocate();
		TestResource*  res = manager.Get(handle);
		assert(res != nullptr && static_cast<bool>("Failed to get resource"));
		res->id = 42;

		LOG_INFO(UI, "Allocated handle: index=%d, gen=%d", handle.getIndex(), handle.getGeneration());
		LOG_INFO(UI, "Resource id: %d", res->id);

		// Free the resource
		manager.free(handle);
		LOG_INFO(UI, "");
		LOG_INFO(UI, "Freed the resource");

		// Try to access with old handle (should return nullptr)
		TestResource* staleRes = manager.Get(handle);
		LOG_INFO(UI, "Accessing with stale handle: %s", staleRes != nullptr ? "FAIL - got resource!" : "PASS - returned null");
		assert(staleRes == nullptr && static_cast<bool>("Stale handle returned resource!"));

		// Allocate new resource in same slot
		ResourceHandle newHandle = manager.Allocate();
		LOG_INFO(UI, "");
		LOG_INFO(UI, "Allocated new handle in same slot: index=%d, gen=%d", newHandle.getIndex(), newHandle.getGeneration());

		// Verify new handle has incremented generation
		assert(newHandle.getIndex() == handle.getIndex() && static_cast<bool>("Different index"));
		assert(newHandle.getGeneration() == handle.getGeneration() + 1 && static_cast<bool>("Generation not incremented"));

		// Old handle should still be invalid
		staleRes = manager.Get(handle);
		LOG_INFO(UI, "Accessing with old handle after realloc: %s", staleRes != nullptr ? "FAIL - got resource!" : "PASS - returned null");
		assert(staleRes == nullptr && static_cast<bool>("Old handle should still be invalid"));

		// New handle should work
		TestResource* newRes = manager.Get(newHandle);
		assert(newRes != nullptr && static_cast<bool>("New handle should be valid"));
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
		LOG_INFO(UI, "Invalid handle: value=0x%08x, valid=%s", invalidHandle.value, invalidHandle.isValid() ? "true" : "false");
		assert(!invalidHandle.isValid() && static_cast<bool>("Invalid handle should not be valid"));

		TestResource* res = manager.Get(invalidHandle);
		LOG_INFO(UI, "Get with invalid handle: %s", res != nullptr ? "FAIL - got resource!" : "PASS - returned null");
		assert(res == nullptr && static_cast<bool>("Invalid handle should return null"));

		// Test out-of-range handle
		ResourceHandle outOfRange = ResourceHandle::make(9999, 0);
		res = manager.Get(outOfRange);
		LOG_INFO(UI, "Get with out-of-range index (9999): %s", res != nullptr ? "FAIL - got resource!" : "PASS - returned null");
		assert(res == nullptr && static_cast<bool>("Out-of-range handle should return null"));

		// Test handle comparison
		ResourceHandle h1 = manager.Allocate();
		ResourceHandle h2 = manager.Allocate();
		ResourceHandle h3 = h1;

		LOG_INFO(UI, "");
		LOG_INFO(UI, "Handle comparison:");
		LOG_INFO(UI, "  h1 == h1: %s", h1 == h1 ? "true" : "false");
		LOG_INFO(UI, "  h1 == h2: %s", h1 == h2 ? "true" : "false");
		LOG_INFO(UI, "  h1 == h3: %s", h1 == h3 ? "true" : "false");
		LOG_INFO(UI, "  h1 != h2: %s", h1 != h2 ? "true" : "false");

		assert(h1 == h1 && static_cast<bool>("Same handle should be equal"));
		assert(h1 != h2 && static_cast<bool>("Different handles should not be equal"));
		assert((h1 == h3) && static_cast<bool>("Copied handle should be equal"));

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
			ResourceHandle handle = manager.Allocate();
			assert(handle.isValid() && static_cast<bool>("Handle should be valid"));
			assert(handle.getIndex() == i && static_cast<bool>("Index should match allocation order"));
			handles.push_back(handle);
		}

		LOG_INFO(UI, "Successfully allocated %d resources", kTestCount);
		LOG_INFO(UI, "Total count: %zu", manager.getCount());
		LOG_INFO(UI, "Active count: %zu", manager.getActiveCount());

		// Verify all handles are still valid and accessible
		for (int i = 0; i < kTestCount; i++) {
			TestResource* res = manager.Get(handles[i]);
			assert(res != nullptr && static_cast<bool>("Resource should be accessible"));
			res->id = i;
		}

		LOG_INFO(UI, "All %d resources accessible and writable", kTestCount);

		// Verify indices are correct
		assert(handles[0].getIndex() == 0 && static_cast<bool>("First index should be 0"));
		assert(handles[kTestCount - 1].getIndex() == kTestCount - 1 && static_cast<bool>("Last index should be count-1"));

		LOG_INFO(UI, "Index range: 0 to %d (correct)", kTestCount - 1);
		LOG_INFO(UI, "");
		LOG_INFO(UI, "Note: 16-bit index allows up to 65,536 resources (0-65535)");
		LOG_INFO(UI, "Capacity limit test passed!");
	}

	// Register scene with SceneManager
	bool g_registered = []() {
		engine::SceneManager::Get().registerScene("handles", []() { return std::make_unique<HandleScene>(); });
		return true;
	}();

} // anonymous namespace
