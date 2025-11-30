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

#include <cassert>
#include <cstdint>
#include <cstdlib>

namespace foundation {

	// Core linear allocator
	class Arena {
	  public:
		explicit Arena(size_t capacity) // NOLINT(cppcoreguidelines-pro-type-member-init)
			: buffer(nullptr),
			  size(capacity),
			  used(0) {
			buffer = static_cast<uint8_t*>(malloc(capacity));
			assert(buffer && static_cast<bool>("Arena allocation failed: out of memory"));
		}

		~Arena() { free(buffer); }

		// Non-copyable
		Arena(const Arena&) = delete;
		Arena& operator=(const Arena&) = delete;

		// Non-movable (arena owns raw buffer)
		Arena(Arena&&) = delete;
		Arena& operator=(Arena&&) = delete;

		// Allocate memory from arena with alignment
		void* allocate(size_t bytes, size_t alignment = 8) { // NOLINT(readability-convert-member-functions-to-static)
			// Align pointer
			size_t aligned = (used + alignment - 1) & ~(alignment - 1);

			// Check capacity
			if (aligned + bytes > size) {
				assert(false && "Arena out of memory"); // NOLINT(readability-simplify-boolean-expr,readability-implicit-bool-conversion)
				return nullptr;
			}

			void* ptr = buffer + aligned;
			used = aligned + bytes;
			return ptr;
		}

		// Type-safe allocate single object
		template <typename T>
		T* allocate() {
			return static_cast<T*>(allocate(sizeof(T), alignof(T))); // NOLINT(readability-redundant-casting)
		}

		// Type-safe allocate array
		template <typename T>
		T* allocateArray(size_t count) {
			return static_cast<T*>(allocate(sizeof(T) * count, alignof(T)));
		}

		// Reset arena (free all at once)
		void reset() { used = 0; }

		// Restore arena to a previous checkpoint
		void restoreCheckpoint(size_t checkpointPos) { // NOLINT(readability-convert-member-functions-to-static)
			assert(checkpointPos <= used && static_cast<bool>("Invalid checkpoint"));
			used = checkpointPos;
		}

		// Get current usage
		size_t getUsed() const { return used; }
		size_t getSize() const { return size; }
		size_t getRemaining() const { return size - used; }

	  private:
		uint8_t* buffer;
		size_t	 size;
		size_t	 used;
	};

	// Frame arena - designed for per-frame temporary data
	// Resets at the end of each frame
	class FrameArena {
	  public:
		explicit FrameArena(size_t size)
			: arena(size) {}

		template <typename T>
		T* allocate() {
			return arena.allocate<T>();
		}

		template <typename T>
		T* allocateArray(size_t count) {
			return arena.allocateArray<T>(count);
		}

		void* allocate(size_t size, size_t alignment = 8) { return arena.allocate(size, alignment); }

		// Reset at end of frame
		void resetFrame() { arena.reset(); }

		// Get current usage
		size_t getUsed() const { return arena.getUsed(); }
		size_t getSize() const { return arena.getSize(); }
		size_t getRemaining() const { return arena.getRemaining(); }

	  private:
		Arena arena;
	};

	// Scoped arena - RAII wrapper that resets arena on destruction
	// Useful for temporary allocations within a scope
	class ScopedArena {
	  public:
		explicit ScopedArena(Arena& arenaRef)
			: arena(arenaRef),
			  checkpoint(arenaRef.getUsed()) {}

		~ScopedArena() {
			// Restore to checkpoint (undo all allocations made within this scope)
			arena.restoreCheckpoint(checkpoint);
		}

		// Non-copyable
		ScopedArena(const ScopedArena&) = delete;
		ScopedArena& operator=(const ScopedArena&) = delete;

		// Non-movable (holds reference to arena)
		ScopedArena(ScopedArena&&) = delete;
		ScopedArena& operator=(ScopedArena&&) = delete;

		template <typename T>
		T* allocate() {
			return arena.allocate<T>();
		}

		template <typename T>
		T* allocateArray(size_t count) {
			return arena.allocateArray<T>(count);
		}

		void* allocate(size_t size, size_t alignment = 8) { return arena.allocate(size, alignment); }

	  private:
		Arena& arena;
		size_t checkpoint{};
	};

} // namespace foundation
