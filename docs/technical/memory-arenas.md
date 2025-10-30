# Memory Arena Allocators

Created: 2025-10-12
Last Updated: 2025-10-12
Status: Active
Priority: **Implement Soon** (before chunk/tile systems)

## What Is a Memory Arena?

A memory arena (or linear allocator) is a large block of memory you allocate once, then hand out pieces from it by bumping a pointer forward. At the end of a scope (frame, chunk generation, etc.), you reset the pointer and reuse the whole block.

**Think of it like:** A notepad where you write on it, then erase everything and start over.

## The Problem

```cpp
// Generating 10,000 tiles - slow!
for (int i = 0; i < 10000; i++) {
	Tile* tile = new Tile();  // Calls malloc (slow)
	GenerateTile(tile);
	tiles.push_back(tile);
}

// Later, cleanup
for (Tile* tile : tiles) {
	delete tile;  // Calls free (slow)
}

// Problems:
// - 10,000 malloc calls = slow
// - 10,000 free calls = slow
// - Memory fragmentation
// - Cache misses (tiles scattered in memory)
```

## The Solution

```cpp
// Allocate arena once (1 MB)
FrameArena arena(1024 * 1024);

// Generate 10,000 tiles - fast!
for (int i = 0; i < 10000; i++) {
	Tile* tile = arena.Allocate<Tile>();  // Just bump pointer (fast!)
	GenerateTile(tile);
	tiles.push_back(tile);
}

// Cleanup entire arena at once
arena.Reset();  // Instant! Just reset pointer

// Benefits:
// - 1 allocation instead of 10,000
// - Instant cleanup
// - Tiles stored together (cache-friendly)
// - No fragmentation
```

## Why It Matters

### Performance
- **10-100x faster** than new/delete in loops
- **Cache-friendly**: Data allocated together stays together
- **No fragmentation**: Memory stays clean

### Use Cases in World-Sim
1. **Per-frame temporary data**: UI layout calculations, debug draw
2. **Chunk generation**: Temporary noise buffers, intermediate tile data
3. **Tile processing**: Variation calculations, blending operations
4. **String building**: Temporary formatted strings

## Implementation

```cpp
// libs/foundation/memory/arena.h
#pragma once

#include <cstdint>
#include <cstdlib>
#include <cassert>

namespace foundation {

class Arena {
public:
	Arena(size_t size)
		: m_size(size)
		, m_used(0)
	{
		m_buffer = static_cast<uint8_t*>(malloc(size));
		assert(m_buffer && "Arena allocation failed");
	}

	~Arena() {
		free(m_buffer);
	}

	// Allocate memory from arena
	void* Allocate(size_t size, size_t alignment = 8) {
		// Align pointer
		size_t aligned = (m_used + alignment - 1) & ~(alignment - 1);

		// Check capacity
		if (aligned + size > m_size) {
			assert(false && "Arena out of memory");
			return nullptr;
		}

		void* ptr = m_buffer + aligned;
		m_used = aligned + size;
		return ptr;
	}

	// Type-safe allocate
	template<typename T>
	T* Allocate() {
		return static_cast<T*>(Allocate(sizeof(T), alignof(T)));
	}

	// Allocate array
	template<typename T>
	T* AllocateArray(size_t count) {
		return static_cast<T*>(Allocate(sizeof(T) * count, alignof(T)));
	}

	// Reset arena (free all at once)
	void Reset() {
		m_used = 0;
	}

	// Get current usage
	size_t GetUsed() const { return m_used; }
	size_t GetSize() const { return m_size; }

private:
	uint8_t* m_buffer;
	size_t   m_size;
	size_t   m_used;

	// Non-copyable
	Arena(const Arena&) = delete;
	Arena& operator=(const Arena&) = delete;
};

// Frame arena - resets each frame
class FrameArena {
public:
	FrameArena(size_t size) : m_arena(size) {}

	template<typename T>
	T* Allocate() { return m_arena.Allocate<T>(); }

	template<typename T>
	T* AllocateArray(size_t count) { return m_arena.AllocateArray<T>(count); }

	void ResetFrame() { m_arena.Reset(); }

private:
	Arena m_arena;
};

// Scoped arena - resets when destroyed
class ScopedArena {
public:
	ScopedArena(Arena& arena) : m_arena(arena), m_checkpoint(arena.GetUsed()) {}
	~ScopedArena() { m_arena.Reset(); /* Could restore checkpoint instead */ }

	template<typename T>
	T* Allocate() { return m_arena.Allocate<T>(); }

private:
	Arena& m_arena;
	size_t m_checkpoint;
};

} // namespace foundation
```

## Usage Examples

### Frame Temporary Data
```cpp
FrameArena g_frameArena(1024 * 1024);  // 1 MB per frame

void GameLoop() {
	while (running) {
		// Allocate temporary data
		int* tempBuffer = g_frameArena.AllocateArray<int>(1000);
		char* debugText = g_frameArena.AllocateArray<char>(256);

		Update(tempBuffer, debugText);
		Render();

		// Reset at end of frame
		g_frameArena.ResetFrame();
	}
}
```

### Chunk Generation
```cpp
void GenerateChunk(ChunkCoord coord) {
	Arena chunkArena(512 * 1024);  // 512 KB for chunk gen

	// Allocate temporary noise buffers
	float* heightMap = chunkArena.AllocateArray<float>(64 * 64);
	float* moistureMap = chunkArena.AllocateArray<float>(64 * 64);

	GenerateNoise(heightMap, moistureMap);

	// Allocate temporary tile data
	TileDraft* tileDrafts = chunkArena.AllocateArray<TileDraft>(64 * 64);
	ProcessTiles(heightMap, moistureMap, tileDrafts);

	// Finalize to actual storage
	FinalizeChunk(coord, tileDrafts);

	// Arena destructor frees everything
}
```

### Scoped Allocations
```cpp
void ProcessComplexTile(Tile* tile, Arena& arena) {
	ScopedArena scope(arena);  // Will reset on exit

	// Temporary calculations
	float* weights = scope.AllocateArray<float>(100);
	VariationParam* params = scope.AllocateArray<VariationParam>(50);

	CalculateWeights(tile, weights);
	GenerateVariations(weights, params);
	ApplyVariations(tile, params);

	// scope destructor resets arena automatically
}
```

## When to Use Arenas

### DO Use For
- Per-frame temporary allocations
- Chunk/tile generation temporary data
- String building and formatting
- Algorithm scratch space
- Any "allocate, use, throw away all at once" pattern

### DON'T Use For
- Long-lived objects (entities, resources)
- Objects with complex destruction needs
- Objects with unpredictable lifetimes
- When you need to free individual objects

## Best Practices

```cpp
// Good: Arena for predictable lifetime
void RenderFrame() {
	FrameArena arena(1MB);
	DebugLine* lines = arena.AllocateArray<DebugLine>(1000);
	// Use lines...
	// Arena resets at end of frame
}

// Bad: Arena for unpredictable lifetime
void LoadAssets() {
	Arena arena(10MB);
	Texture* tex = arena.Allocate<Texture>();  // BAD! Lives forever
	// Arena can't reset until all textures unloaded
}

// Good: Size arena appropriately
Arena chunkArena(512KB);  // Based on profiling

// Bad: Tiny arena that runs out
Arena tinyArena(1KB);
float* huge = arena.AllocateArray<float>(10000);  // ASSERT!
```

## Performance Characteristics

**Allocation Speed:**
- `new`/`malloc`: ~50-200 cycles
- Arena: ~5-10 cycles (just pointer arithmetic)
- **10-40x faster**

**Deallocation Speed:**
- `delete`/`free`: ~50-200 cycles each
- Arena reset: 1 cycle (set pointer to 0)
- **Effectively instant** for thousands of objects

**Memory Usage:**
- Arena: Fixed overhead (buffer + metadata)
- Trade: Pre-allocate more than needed, but faster and simpler

## Trade-offs

**Pros:**
- Extremely fast allocation/deallocation
- Cache-friendly (sequential memory)
- Simple implementation
- No fragmentation

**Cons:**
- Can't free individual allocations
- Must know lifetime upfront
- Pre-allocates memory (may waste some)
- Destructors not called (only for POD data)

**Decision:** Essential for performance in chunk/tile systems.

## Related Documentation

- Tech: [Resource Handles](./resource-handles.md)
- Code: `libs/foundation/memory/arena.h` (once implemented)

## Notes

**Destructors:** Arenas don't call destructors! Only use for simple types or manage cleanup manually.

**Thread Safety:** Basic arena is not thread-safe. Use one arena per thread or add synchronization.

**Size Tuning:** Profile to find right arena sizes. Too small = asserts, too large = wasted memory.
