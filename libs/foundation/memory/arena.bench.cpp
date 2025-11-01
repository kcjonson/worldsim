#include "arena.h"
#include <benchmark/benchmark.h>
#include <cstdlib>

using namespace foundation;

// ============================================================================
// Arena vs malloc Comparison Benchmarks
// ============================================================================

// Benchmark: Single allocation with malloc
static void BM_MallocSingleAllocation(benchmark::State& state) {
	size_t size = state.range(0);

	for (auto _ : state) {
		void* ptr = malloc(size);
		benchmark::DoNotOptimize(ptr);
		free(ptr);
	}

	state.SetBytesProcessed(state.iterations() * size);
}
BENCHMARK(BM_MallocSingleAllocation)->Range(8, 8 << 10);

// Benchmark: Single allocation with Arena
static void BM_ArenaSingleAllocation(benchmark::State& state) {
	size_t size = state.range(0);
	Arena  arena(1024 * 1024); // 1MB arena

	for (auto _ : state) {
		arena.Reset();
		void* ptr = arena.Allocate(size);
		benchmark::DoNotOptimize(ptr);
	}

	state.SetBytesProcessed(state.iterations() * size);
}
BENCHMARK(BM_ArenaSingleAllocation)->Range(8, 8 << 10);

// ============================================================================
// Batch Allocations (Common Game Engine Pattern)
// ============================================================================

// Benchmark: 1000 small allocations with malloc
static void BM_MallocBatchSmallAllocations(benchmark::State& state) {
	constexpr int	 kAllocCount = 1000;
	constexpr size_t kAllocSize = 64;

	for (auto _ : state) {
		void* ptrs[kAllocCount];
		for (int i = 0; i < kAllocCount; ++i) { // NOLINT(modernize-loop-convert)
			ptrs[i] = malloc(kAllocSize);		// NOLINT(cppcoreguidelines-pro-bounds-constant-array-index)
			benchmark::DoNotOptimize(ptrs[i]);	// NOLINT(cppcoreguidelines-pro-bounds-constant-array-index)
		}
		for (int i = 0; i < kAllocCount; ++i) { // NOLINT(modernize-loop-convert)
			free(ptrs[i]);						// NOLINT(cppcoreguidelines-pro-bounds-constant-array-index)
		}
	}

	state.SetBytesProcessed(state.iterations() * kAllocCount * kAllocSize);
	state.SetItemsProcessed(state.iterations() * kAllocCount);
}
BENCHMARK(BM_MallocBatchSmallAllocations);

// Benchmark: 1000 small allocations with Arena
static void BM_ArenaBatchSmallAllocations(benchmark::State& state) {
	constexpr int	 kAllocCount = 1000;
	constexpr size_t kAllocSize = 64;

	Arena arena(kAllocCount * kAllocSize * 2); // Ensure enough space

	for (auto _ : state) {
		arena.Reset();
		for (int i = 0; i < kAllocCount; ++i) {
			void* ptr = arena.Allocate(kAllocSize);
			benchmark::DoNotOptimize(ptr);
		}
	}

	state.SetBytesProcessed(state.iterations() * kAllocCount * kAllocSize);
	state.SetItemsProcessed(state.iterations() * kAllocCount);
}
BENCHMARK(BM_ArenaBatchSmallAllocations);

// ============================================================================
// Type-Safe Allocation Benchmarks
// ============================================================================

struct SmallStruct {
	int	  a; // NOLINT(readability-identifier-naming)
	float b; // NOLINT(readability-identifier-naming)
};

struct LargeStruct {
	double data[16]; // NOLINT(readability-identifier-naming)
	int	   id;		 // NOLINT(readability-identifier-naming)
};

// Benchmark: Allocate structs with malloc
static void BM_MallocStructAllocation(benchmark::State& state) {
	for (auto _ : state) {
		auto* ptr = static_cast<SmallStruct*>(malloc(sizeof(SmallStruct)));
		benchmark::DoNotOptimize(ptr);
		free(ptr);
	}
}
BENCHMARK(BM_MallocStructAllocation);

// Benchmark: Allocate structs with Arena
static void BM_ArenaStructAllocation(benchmark::State& state) {
	Arena arena(1024 * 1024);

	for (auto _ : state) {
		arena.Reset();
		SmallStruct* ptr = arena.Allocate<SmallStruct>();
		benchmark::DoNotOptimize(ptr);
	}
}
BENCHMARK(BM_ArenaStructAllocation);

// ============================================================================
// Array Allocation Benchmarks
// ============================================================================

// Benchmark: Array allocation with malloc
static void BM_MallocArrayAllocation(benchmark::State& state) {
	int count = state.range(0);

	for (auto _ : state) {
		int* arr = static_cast<int*>(malloc(sizeof(int) * count));
		benchmark::DoNotOptimize(arr);
		free(arr);
	}

	state.SetItemsProcessed(state.iterations() * count);
}
BENCHMARK(BM_MallocArrayAllocation)->Range(8, 1024);

// Benchmark: Array allocation with Arena
static void BM_ArenaArrayAllocation(benchmark::State& state) {
	int	  count = state.range(0);
	Arena arena(1024 * 1024);

	for (auto _ : state) {
		arena.Reset();
		int* arr = arena.AllocateArray<int>(count);
		benchmark::DoNotOptimize(arr);
	}

	state.SetItemsProcessed(state.iterations() * count);
}
BENCHMARK(BM_ArenaArrayAllocation)->Range(8, 1024);

// ============================================================================
// Alignment Benchmarks
// ============================================================================

// Benchmark: Arena allocation with default alignment (8 bytes)
static void BM_ArenaDefaultAlignment(benchmark::State& state) {
	Arena arena(1024 * 1024);

	for (auto _ : state) {
		arena.Reset();
		for (int i = 0; i < 100; ++i) {
			void* ptr = arena.Allocate(64, 8);
			benchmark::DoNotOptimize(ptr);
		}
	}

	state.SetItemsProcessed(state.iterations() * 100);
}
BENCHMARK(BM_ArenaDefaultAlignment);

// Benchmark: Arena allocation with 16-byte alignment
static void BM_Arena16ByteAlignment(benchmark::State& state) {
	Arena arena(1024 * 1024);

	for (auto _ : state) {
		arena.Reset();
		for (int i = 0; i < 100; ++i) {
			void* ptr = arena.Allocate(64, 16);
			benchmark::DoNotOptimize(ptr);
		}
	}

	state.SetItemsProcessed(state.iterations() * 100);
}
BENCHMARK(BM_Arena16ByteAlignment);

// Benchmark: Arena allocation with 64-byte alignment (cache line)
static void BM_Arena64ByteAlignment(benchmark::State& state) {
	Arena arena(1024 * 1024);

	for (auto _ : state) {
		arena.Reset();
		for (int i = 0; i < 100; ++i) {
			void* ptr = arena.Allocate(64, 64);
			benchmark::DoNotOptimize(ptr);
		}
	}

	state.SetItemsProcessed(state.iterations() * 100);
}
BENCHMARK(BM_Arena64ByteAlignment);

// ============================================================================
// Reset Performance Benchmarks
// ============================================================================

// Benchmark: Arena Reset performance
static void BM_ArenaReset(benchmark::State& state) {
	Arena arena(1024 * 1024);

	for (auto _ : state) {
		// Fill arena with allocations
		for (int i = 0; i < 100; ++i) {
			arena.Allocate(1024);
		}

		// Benchmark the reset
		arena.Reset();
		benchmark::DoNotOptimize(arena.GetUsed());
	}
}
BENCHMARK(BM_ArenaReset);

// Benchmark: Checkpoint/Restore performance
static void BM_ArenaCheckpointRestore(benchmark::State& state) {
	Arena arena(1024 * 1024);

	for (auto _ : state) {
		arena.Reset();

		// Some initial allocations
		arena.Allocate(1024);
		size_t checkpoint = arena.GetUsed();

		// More allocations
		for (int i = 0; i < 50; ++i) {
			arena.Allocate(512);
		}

		// Benchmark the restore
		arena.RestoreCheckpoint(checkpoint);
		benchmark::DoNotOptimize(arena.GetUsed());
	}
}
BENCHMARK(BM_ArenaCheckpointRestore);

// ============================================================================
// FrameArena Benchmarks
// ============================================================================

// Benchmark: FrameArena typical frame usage
static void BM_FrameArenaFrameUsage(benchmark::State& state) {
	FrameArena arena(1024 * 1024);

	for (auto _ : state) {
		// Simulate frame allocations
		for (int i = 0; i < 50; ++i) {
			void* ptr = arena.Allocate(256);
			benchmark::DoNotOptimize(ptr);
		}

		// Type-safe allocations
		for (int i = 0; i < 20; ++i) {
			SmallStruct* s = arena.Allocate<SmallStruct>();
			benchmark::DoNotOptimize(s);
		}

		// Array allocations
		for (int i = 0; i < 10; ++i) {
			int* arr = arena.AllocateArray<int>(32);
			benchmark::DoNotOptimize(arr);
		}

		// Frame end - reset
		arena.ResetFrame();
	}

	state.SetItemsProcessed(state.iterations() * 80); // 50 + 20 + 10
}
BENCHMARK(BM_FrameArenaFrameUsage);

// ============================================================================
// ScopedArena Benchmarks
// ============================================================================

// Benchmark: ScopedArena allocation pattern
static void BM_ScopedArenaPattern(benchmark::State& state) {
	Arena arena(1024 * 1024);

	for (auto _ : state) {
		arena.Reset();

		{
			ScopedArena scoped(arena);

			// Temporary allocations within scope
			for (int i = 0; i < 20; ++i) {
				void* ptr = scoped.Allocate(128);
				benchmark::DoNotOptimize(ptr);
			}

			// Destructor will restore checkpoint
		}

		benchmark::DoNotOptimize(arena.GetUsed());
	}

	state.SetItemsProcessed(state.iterations() * 20);
}
BENCHMARK(BM_ScopedArenaPattern);

// Benchmark: Nested ScopedArena pattern
static void BM_ScopedArenaNested(benchmark::State& state) {
	Arena arena(1024 * 1024);

	for (auto _ : state) {
		arena.Reset();

		{
			ScopedArena scoped1(arena);
			scoped1.Allocate(256);

			{
				ScopedArena scoped2(arena);
				scoped2.Allocate(512);

				{
					ScopedArena scoped3(arena);
					scoped3.Allocate(1024);
				}
			}
		}

		benchmark::DoNotOptimize(arena.GetUsed());
	}
}
BENCHMARK(BM_ScopedArenaNested);

// ============================================================================
// Real-World Game Engine Patterns
// ============================================================================

// Benchmark: Simulated UI layout pass (many small allocations)
static void BM_SimulatedUILayout(benchmark::State& state) {
	FrameArena arena(1024 * 1024);

	for (auto _ : state) {
		// Simulate UI element data allocations
		for (int i = 0; i < 100; ++i) {
			struct UIElementData {
				float x, y, width, height; // NOLINT(readability-identifier-naming)
				int	  id;				   // NOLINT(readability-identifier-naming)
				char  text[32];			   // NOLINT(readability-identifier-naming)
			};

			UIElementData* elem = arena.Allocate<UIElementData>();
			benchmark::DoNotOptimize(elem);
		}

		// Simulate temporary string buffers
		for (int i = 0; i < 50; ++i) {
			char* buffer = arena.AllocateArray<char>(128);
			benchmark::DoNotOptimize(buffer);
		}

		// Frame end
		arena.ResetFrame();
	}

	state.SetItemsProcessed(state.iterations() * 150);
}
BENCHMARK(BM_SimulatedUILayout);

// Benchmark: Simulated particle system update (batch allocations)
static void BM_SimulatedParticleUpdate(benchmark::State& state) {
	Arena arena(1024 * 1024);

	for (auto _ : state) {
		arena.Reset();

		struct Particle {
			float x, y, z;	  // NOLINT(readability-identifier-naming)
			float vx, vy, vz; // NOLINT(readability-identifier-naming)
			float life;		  // NOLINT(readability-identifier-naming)
		};

		// Allocate particle batch
		constexpr int kParticleCount = 1000;
		Particle*	  particles = arena.AllocateArray<Particle>(kParticleCount);
		benchmark::DoNotOptimize(particles);

		// Allocate temporary buffers for sorting
		int*   indices = arena.AllocateArray<int>(kParticleCount);
		float* distances = arena.AllocateArray<float>(kParticleCount);
		benchmark::DoNotOptimize(indices);
		benchmark::DoNotOptimize(distances);
	}

	state.SetItemsProcessed(state.iterations() * 1000);
}
BENCHMARK(BM_SimulatedParticleUpdate);
