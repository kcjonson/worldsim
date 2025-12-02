// Arena Scene - Memory Arena Performance Tests
// Demonstrates memory arena performance and correctness

#include <math/Types.h>
#include <memory/Arena.h>
#include <scene/Scene.h>
#include <scene/SceneManager.h>
#include <utils/Log.h>

#include <GL/glew.h>
#include <chrono>
#include <memory>
#include <vector>

using namespace foundation;

namespace {

constexpr const char* kSceneName = "arena";

static void testPerformance();
	static void testAlignment();
	static void testCapacity();
	static void testScoped();

	class ArenaScene : public engine::IScene {
	  public:
		void onEnter() override {
			LOG_INFO(UI, "");
			LOG_INFO(UI, "Arena Scene - Memory Arena Performance Tests");
			LOG_INFO(UI, "================================================");

			// Run all tests at initialization
			testPerformance();
			testAlignment();
			testCapacity();
			testScoped();

			LOG_INFO(UI, "================================================");
			LOG_INFO(UI, "All arena tests passed!");
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

		std::string exportState() override { // NOLINT(readability-convert-member-functions-to-static)
			return R"({
			"scene": "arena",
			"description": "Memory arena performance tests",
			"tests": ["performance", "alignment", "capacity", "scoped"],
			"status": "Tests run on scene enter, see console/logs for results"
		})";
		}

		const char* getName() const override { return kSceneName; }
	};

// ============================================================================
	// Test Implementations
	// ============================================================================

	void testPerformance() {
		LOG_INFO(UI, "");
		LOG_INFO(UI, "Performance Test: Arena vs Standard Allocation");
		LOG_INFO(UI, "-----------------------------------------------");

		constexpr int kIterations = 10000;

		// Test 1: Arena allocation
		Arena arena(static_cast<size_t>(1024) * 1024); // 1 MB
		auto  arenaStart = std::chrono::high_resolution_clock::now();

		for (int i = 0; i < kIterations; i++) {
			auto* vec = arena.allocate<Foundation::Vec2>();
			vec->x = static_cast<float>(i);
			vec->y = static_cast<float>(i * 2);
		}

		auto arenaEnd = std::chrono::high_resolution_clock::now();
		auto arenaDuration = std::chrono::duration_cast<std::chrono::microseconds>(arenaEnd - arenaStart);

		LOG_INFO(UI, "Arena: Allocated %d Vec2 objects in %lld microseconds", kIterations, arenaDuration.count());

		// Reset arena for reuse test
		arena.reset();
		LOG_INFO(UI, "Arena: Reset to 0 bytes used (instant)");

		// Test 2: Standard allocation (using unique_ptr for RAII)
		std::vector<std::unique_ptr<Foundation::Vec2>> pointers;
		pointers.reserve(kIterations);

		auto stdStart = std::chrono::high_resolution_clock::now();

		for (int i = 0; i < kIterations; i++) {
			auto vec = std::make_unique<Foundation::Vec2>();
			vec->x = static_cast<float>(i);
			vec->y = static_cast<float>(i * 2);
			pointers.push_back(std::move(vec));
		}

		auto stdEnd = std::chrono::high_resolution_clock::now();
		auto stdDuration = std::chrono::duration_cast<std::chrono::microseconds>(stdEnd - stdStart);

		// Cleanup standard allocations
		auto cleanupStart = std::chrono::high_resolution_clock::now();
		pointers.clear(); // Force deallocation
		auto cleanupEnd = std::chrono::high_resolution_clock::now();
		auto cleanupDuration = std::chrono::duration_cast<std::chrono::microseconds>(cleanupEnd - cleanupStart);

		LOG_INFO(UI, "Standard: Allocated %d Vec2 objects in %lld microseconds", kIterations, stdDuration.count());
		LOG_INFO(UI, "Standard: Freed %d Vec2 objects in %lld microseconds", kIterations, cleanupDuration.count());

		// Calculate speedup
		long long totalStd = stdDuration.count() + cleanupDuration.count();
		double	  speedup = static_cast<double>(totalStd) / static_cast<double>(arenaDuration.count());

		LOG_INFO(UI, "");
		LOG_INFO(UI, "Result: Arena is %.1fx faster than standard allocation!", speedup);
	}

	void testAlignment() {
		LOG_INFO(UI, "");
		LOG_INFO(UI, "Alignment Test: Verify correct alignment for different types");
		LOG_INFO(UI, "-------------------------------------------------------------");

		Arena arena(1024);

		// Test alignment for different types
		struct Aligned1 {
			uint8_t data{};
		};
		struct Aligned4 {
			uint32_t data{};
		};
		struct Aligned8 {
			uint64_t data{};
		};
		struct Aligned16 {
			double data[2]{};
		};

		auto* a1 = arena.allocate<Aligned1>();
		auto* a4 = arena.allocate<Aligned4>();
		auto* a8 = arena.allocate<Aligned8>();
		auto* a16 = arena.allocate<Aligned16>();

		bool align1OK = (reinterpret_cast<uintptr_t>(a1) % alignof(Aligned1)) == 0;
		bool align4OK = (reinterpret_cast<uintptr_t>(a4) % alignof(Aligned4)) == 0;
		bool align8OK = (reinterpret_cast<uintptr_t>(a8) % alignof(Aligned8)) == 0;
		bool align16OK = (reinterpret_cast<uintptr_t>(a16) % alignof(Aligned16)) == 0;

		LOG_INFO(UI, "1-byte alignment: %s", align1OK ? "PASS" : "FAIL");
		LOG_INFO(UI, "4-byte alignment: %s", align4OK ? "PASS" : "FAIL");
		LOG_INFO(UI, "8-byte alignment: %s", align8OK ? "PASS" : "FAIL");
		LOG_INFO(UI, "16-byte alignment: %s", align16OK ? "PASS" : "FAIL");

		assert((align1OK && align4OK && align8OK && align16OK) && static_cast<bool>("Alignment test failed"));
		LOG_INFO(UI, "All alignment tests passed!");
	}

	void testCapacity() {
		LOG_INFO(UI, "");
		LOG_INFO(UI, "Capacity Test: Fill arena and verify tracking");
		LOG_INFO(UI, "-----------------------------------------------");

		constexpr size_t kArenaSize = 1024; // 1 KB
		Arena			 arena(kArenaSize);

		LOG_INFO(UI, "Arena size: %zu bytes", arena.getSize());
		LOG_INFO(UI, "Arena used: %zu bytes", arena.getUsed());
		LOG_INFO(UI, "Arena remaining: %zu bytes", arena.getRemaining());

		// Fill most of the arena
		constexpr int kAllocCount = 100;
		for (int i = 0; i < kAllocCount; i++) {
			arena.allocate<uint64_t>();
		}

		LOG_INFO(UI, "");
		LOG_INFO(UI, "After %d allocations:", kAllocCount);
		LOG_INFO(UI, "Arena used: %zu bytes", arena.getUsed());
		LOG_INFO(UI, "Arena remaining: %zu bytes", arena.getRemaining());

		// Reset and verify
		arena.reset();
		LOG_INFO(UI, "");
		LOG_INFO(UI, "After reset:");
		LOG_INFO(UI, "Arena used: %zu bytes (should be 0)", arena.getUsed());
		LOG_INFO(UI, "Arena remaining: %zu bytes (should be %zu)", arena.getRemaining(), kArenaSize);

		assert(arena.getUsed() == 0 && static_cast<bool>("Reset failed"));
		assert(arena.getRemaining() == kArenaSize && static_cast<bool>("Reset failed"));
		LOG_INFO(UI, "Capacity test passed!");
	}

	void testScoped() {
		LOG_INFO(UI, "");
		LOG_INFO(UI, "Scoped Test: RAII arena with checkpoint restoration");
		LOG_INFO(UI, "----------------------------------------------------");

		Arena arena(1024);

		// Allocate before scope to verify pre-scope allocations remain valid after checkpoint restoration
		int* data1 = arena.allocate<int>();
		*data1 = 42;
		size_t usedBefore = arena.getUsed();

		LOG_INFO(UI, "Arena used before scope: %zu bytes (allocated int with value 42)", usedBefore);

		{
			ScopedArena scoped(arena);

			// Allocate within scope
			for (int i = 0; i < 10; i++) {
				scoped.allocate<Foundation::Vec2>();
			}

			LOG_INFO(UI, "Arena used inside scope: %zu bytes", arena.getUsed());
		}

		// Should restore to usedBefore, NOT 0
		LOG_INFO(UI, "Arena used after scope: %zu bytes (should be %zu)", arena.getUsed(), usedBefore);

		assert(arena.getUsed() == usedBefore && static_cast<bool>("ScopedArena did not restore checkpoint"));

		// Verify data1 is still valid and has correct value
		assert(*data1 == 42 && static_cast<bool>("Pre-scope allocation was invalidated!"));

		LOG_INFO(UI, "Pre-scope allocation still valid with correct value (42)");
		LOG_INFO(UI, "Scoped arena test passed!");
	}

} // anonymous namespace

// Export factory and name for scene registry
namespace ui_sandbox::scenes {
	std::unique_ptr<engine::IScene> createArenaScene() { return std::make_unique<ArenaScene>(); }
	const char* getArenaSceneName() { return kSceneName; }
}
