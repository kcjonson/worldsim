// Arena Scene - Memory Arena Performance Tests
// Demonstrates memory arena performance and correctness

#include <math/types.h>
#include <memory/arena.h>
#include <scene/scene.h>
#include <scene/scene_manager.h>
#include <utils/log.h>

#include <GL/glew.h>
#include <chrono>
#include <vector>

using namespace foundation;

namespace {

	static void TestPerformance();
	static void TestAlignment();
	static void TestCapacity();
	static void TestScoped();

	class ArenaScene : public engine::IScene {
	  public:
		void OnEnter() override {
			LOG_INFO(UI, "");
			LOG_INFO(UI, "Arena Scene - Memory Arena Performance Tests");
			LOG_INFO(UI, "================================================");

			// Run all tests at initialization
			TestPerformance();
			TestAlignment();
			TestCapacity();
			TestScoped();

			LOG_INFO(UI, "================================================");
			LOG_INFO(UI, "All arena tests passed!");
			LOG_INFO(UI, "");
		}

		void HandleInput(float dt) override {
			// No input handling needed - test scene
		}

		void Update(float dt) override {
			// No update logic needed - tests run on enter
		}

		void Render() override {
			// Clear background
			glClearColor(0.1F, 0.1F, 0.15F, 1.0F);
			glClear(GL_COLOR_BUFFER_BIT);

			// No rendering needed for this scene - all output is to console/logs
		}

		void OnExit() override {
			// No cleanup needed
		}

		std::string ExportState() override {
			return R"({
			"scene": "arena",
			"description": "Memory arena performance tests",
			"tests": ["performance", "alignment", "capacity", "scoped"],
			"status": "Tests run on scene enter, see console/logs for results"
		})";
		}

		const char* GetName() const override { return "arena"; }
	};

	// ============================================================================
	// Test Implementations
	// ============================================================================

	void TestPerformance() {
		LOG_INFO(UI, "");
		LOG_INFO(UI, "Performance Test: Arena vs Standard Allocation");
		LOG_INFO(UI, "-----------------------------------------------");

		constexpr int kIterations = 10000;

		// Test 1: Arena allocation
		Arena arena(1024 * 1024); // 1 MB
		auto  arenaStart = std::chrono::high_resolution_clock::now();

		for (int i = 0; i < kIterations; i++) {
			Foundation::Vec2* vec = arena.Allocate<Foundation::Vec2>();
			vec->x = static_cast<float>(i);
			vec->y = static_cast<float>(i * 2);
		}

		auto arenaEnd = std::chrono::high_resolution_clock::now();
		auto arenaDuration = std::chrono::duration_cast<std::chrono::microseconds>(arenaEnd - arenaStart);

		LOG_INFO(UI, "Arena: Allocated %d Vec2 objects in %lld microseconds", kIterations, arenaDuration.count());

		// Reset arena for reuse test
		arena.Reset();
		LOG_INFO(UI, "Arena: Reset to 0 bytes used (instant)");

		// Test 2: Standard allocation
		std::vector<Foundation::Vec2*> pointers;
		pointers.reserve(kIterations);

		auto stdStart = std::chrono::high_resolution_clock::now();

		for (int i = 0; i < kIterations; i++) {
			Foundation::Vec2* vec = new Foundation::Vec2();
			vec->x = static_cast<float>(i);
			vec->y = static_cast<float>(i * 2);
			pointers.push_back(vec);
		}

		auto stdEnd = std::chrono::high_resolution_clock::now();
		auto stdDuration = std::chrono::duration_cast<std::chrono::microseconds>(stdEnd - stdStart);

		// Cleanup standard allocations
		auto cleanupStart = std::chrono::high_resolution_clock::now();
		for (Foundation::Vec2* vec : pointers) {
			delete vec;
		}
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

	void TestAlignment() {
		LOG_INFO(UI, "");
		LOG_INFO(UI, "Alignment Test: Verify correct alignment for different types");
		LOG_INFO(UI, "-------------------------------------------------------------");

		Arena arena(1024);

		// Test alignment for different types
		struct Aligned1 {
			uint8_t data;
		};
		struct Aligned4 {
			uint32_t data;
		};
		struct Aligned8 {
			uint64_t data;
		};
		struct Aligned16 {
			double data[2];
		};

		Aligned1*  a1 = arena.Allocate<Aligned1>();
		Aligned4*  a4 = arena.Allocate<Aligned4>();
		Aligned8*  a8 = arena.Allocate<Aligned8>();
		Aligned16* a16 = arena.Allocate<Aligned16>();

		bool align1OK = (reinterpret_cast<uintptr_t>(a1) % alignof(Aligned1)) == 0;
		bool align4OK = (reinterpret_cast<uintptr_t>(a4) % alignof(Aligned4)) == 0;
		bool align8OK = (reinterpret_cast<uintptr_t>(a8) % alignof(Aligned8)) == 0;
		bool align16OK = (reinterpret_cast<uintptr_t>(a16) % alignof(Aligned16)) == 0;

		LOG_INFO(UI, "1-byte alignment: %s", align1OK ? "PASS" : "FAIL");
		LOG_INFO(UI, "4-byte alignment: %s", align4OK ? "PASS" : "FAIL");
		LOG_INFO(UI, "8-byte alignment: %s", align8OK ? "PASS" : "FAIL");
		LOG_INFO(UI, "16-byte alignment: %s", align16OK ? "PASS" : "FAIL");

		assert(align1OK && align4OK && align8OK && align16OK && "Alignment test failed");
		LOG_INFO(UI, "All alignment tests passed!");
	}

	void TestCapacity() {
		LOG_INFO(UI, "");
		LOG_INFO(UI, "Capacity Test: Fill arena and verify tracking");
		LOG_INFO(UI, "-----------------------------------------------");

		constexpr size_t kArenaSize = 1024; // 1 KB
		Arena			 arena(kArenaSize);

		LOG_INFO(UI, "Arena size: %zu bytes", arena.GetSize());
		LOG_INFO(UI, "Arena used: %zu bytes", arena.GetUsed());
		LOG_INFO(UI, "Arena remaining: %zu bytes", arena.GetRemaining());

		// Fill most of the arena
		constexpr int kAllocCount = 100;
		for (int i = 0; i < kAllocCount; i++) {
			arena.Allocate<uint64_t>();
		}

		LOG_INFO(UI, "");
		LOG_INFO(UI, "After %d allocations:", kAllocCount);
		LOG_INFO(UI, "Arena used: %zu bytes", arena.GetUsed());
		LOG_INFO(UI, "Arena remaining: %zu bytes", arena.GetRemaining());

		// Reset and verify
		arena.Reset();
		LOG_INFO(UI, "");
		LOG_INFO(UI, "After reset:");
		LOG_INFO(UI, "Arena used: %zu bytes (should be 0)", arena.GetUsed());
		LOG_INFO(UI, "Arena remaining: %zu bytes (should be %zu)", arena.GetRemaining(), kArenaSize);

		assert(arena.GetUsed() == 0 && "Reset failed");
		assert(arena.GetRemaining() == kArenaSize && "Reset failed");
		LOG_INFO(UI, "Capacity test passed!");
	}

	void TestScoped() {
		LOG_INFO(UI, "");
		LOG_INFO(UI, "Scoped Test: RAII arena with checkpoint restoration");
		LOG_INFO(UI, "----------------------------------------------------");

		Arena arena(1024);

		// Allocate before scope to verify pre-scope allocations remain valid after checkpoint restoration
		int* data1 = arena.Allocate<int>();
		*data1 = 42;
		size_t usedBefore = arena.GetUsed();

		LOG_INFO(UI, "Arena used before scope: %zu bytes (allocated int with value 42)", usedBefore);

		{
			ScopedArena scoped(arena);

			// Allocate within scope
			for (int i = 0; i < 10; i++) {
				scoped.Allocate<Foundation::Vec2>();
			}

			LOG_INFO(UI, "Arena used inside scope: %zu bytes", arena.GetUsed());
		}

		// Should restore to usedBefore, NOT 0
		LOG_INFO(UI, "Arena used after scope: %zu bytes (should be %zu)", arena.GetUsed(), usedBefore);

		assert(arena.GetUsed() == usedBefore && "ScopedArena did not restore checkpoint");

		// Verify data1 is still valid and has correct value
		assert(*data1 == 42 && "Pre-scope allocation was invalidated!");

		LOG_INFO(UI, "Pre-scope allocation still valid with correct value (42)");
		LOG_INFO(UI, "Scoped arena test passed!");
	}

	// Register scene with SceneManager
	static bool s_registered = []() {
		engine::SceneManager::Get().RegisterScene("arena", []() { return std::make_unique<ArenaScene>(); });
		return true;
	}();

} // anonymous namespace
