#pragma once

// Memory Arena Allocators
//
// Fast linear allocators for temporary data. Allocate many small objects quickly,
// then free everything at once by resetting the arena.
//
// Performance: 10-100x faster than malloc/new for temporary allocations
//
// Use cases:
// - Per-frame temporary data (UI layout, debug rendering)
// - Chunk generation scratch space
// - Algorithm temporary buffers
// - String building
//
// IMPORTANT: Arenas do NOT call destructors! Only use for POD types or manage cleanup manually.

#include <cstdint>
#include <cstdlib>
#include <cassert>

namespace foundation {

// Core linear allocator
class Arena {
public:
	explicit Arena(size_t size)
		: m_size(size)
		, m_used(0)
	{
		m_buffer = static_cast<uint8_t*>(malloc(size));
		assert(m_buffer && "Arena allocation failed");
	}

	~Arena() {
		free(m_buffer);
	}

	// Allocate memory from arena with alignment
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

	// Type-safe allocate single object
	template<typename T>
	T* Allocate() {
		return static_cast<T*>(Allocate(sizeof(T), alignof(T)));
	}

	// Type-safe allocate array
	template<typename T>
	T* AllocateArray(size_t count) {
		return static_cast<T*>(Allocate(sizeof(T) * count, alignof(T)));
	}

	// Reset arena (free all at once)
	void Reset() {
		m_used = 0;
	}

	// Restore arena to a previous checkpoint
	void RestoreCheckpoint(size_t checkpoint) {
		assert(checkpoint <= m_used && "Invalid checkpoint");
		m_used = checkpoint;
	}

	// Get current usage
	size_t GetUsed() const { return m_used; }
	size_t GetSize() const { return m_size; }
	size_t GetRemaining() const { return m_size - m_used; }

private:
	uint8_t* m_buffer;
	size_t   m_size;
	size_t   m_used;

	// Non-copyable
	Arena(const Arena&) = delete;
	Arena& operator=(const Arena&) = delete;
};

// Frame arena - designed for per-frame temporary data
// Resets at the end of each frame
class FrameArena {
public:
	explicit FrameArena(size_t size) : m_arena(size) {}

	template<typename T>
	T* Allocate() {
		return m_arena.Allocate<T>();
	}

	template<typename T>
	T* AllocateArray(size_t count) {
		return m_arena.AllocateArray<T>(count);
	}

	void* Allocate(size_t size, size_t alignment = 8) {
		return m_arena.Allocate(size, alignment);
	}

	// Reset at end of frame
	void ResetFrame() {
		m_arena.Reset();
	}

	// Get current usage
	size_t GetUsed() const { return m_arena.GetUsed(); }
	size_t GetSize() const { return m_arena.GetSize(); }
	size_t GetRemaining() const { return m_arena.GetRemaining(); }

private:
	Arena m_arena;
};

// Scoped arena - RAII wrapper that resets arena on destruction
// Useful for temporary allocations within a scope
class ScopedArena {
public:
	explicit ScopedArena(Arena& arena)
		: m_arena(arena)
		, m_checkpoint(arena.GetUsed())
	{}

	~ScopedArena() {
		// Restore to checkpoint (undo all allocations made within this scope)
		m_arena.RestoreCheckpoint(m_checkpoint);
	}

	template<typename T>
	T* Allocate() {
		return m_arena.Allocate<T>();
	}

	template<typename T>
	T* AllocateArray(size_t count) {
		return m_arena.AllocateArray<T>(count);
	}

	void* Allocate(size_t size, size_t alignment = 8) {
		return m_arena.Allocate(size, alignment);
	}

private:
	Arena& m_arena;
	size_t m_checkpoint;

	// Non-copyable
	ScopedArena(const ScopedArena&) = delete;
	ScopedArena& operator=(const ScopedArena&) = delete;
};

} // namespace foundation
