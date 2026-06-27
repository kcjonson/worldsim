#pragma once

// Lock-free ring buffer for streaming game data to the debug server.
//
// Single game-thread WRITER (never blocks, no mutex, overwrites the oldest entry when full).
// Server-thread READERS never contend:
//   - readLatest(): newest value only (metrics -- intermediate samples are discardable).
//   - writeCursor() + peekAt(): each reader keeps its OWN absolute position and reads forward, so
//     MANY readers (e.g. several log-stream clients) each receive every item independently -- there
//     is no shared read cursor for them to fight over and starve each other.
// Atomic operations only, zero contention.

#include <array>
#include <atomic>
#include <cstddef>

namespace Foundation { // NOLINT(readability-identifier-naming)

	template <typename T, size_t N = 64>
	class LockFreeRingBuffer {
	  public:
		LockFreeRingBuffer() // NOLINT(cppcoreguidelines-pro-type-member-init,modernize-use-equals-default)
			: buffer{},
			  writeIndex(0) {}

		static constexpr size_t capacity() {
			return N;
		}

		// Write item (game thread). Never blocks; overwrites the oldest item when full.
		void write(const T& item) {
			size_t writeIdx = writeIndex.load(std::memory_order_relaxed);
			buffer[writeIdx % N] = item;
			writeIndex.store(writeIdx + 1, std::memory_order_release);
		}

		// Most recent item -- for metrics, where the latest value is sufficient and intermediate
		// samples can be discarded. False if nothing has been written yet.
		bool readLatest(T& item) const {
			size_t writeIdx = writeIndex.load(std::memory_order_acquire);
			if (writeIdx == 0) {
				return false; // NOLINT(readability-simplify-boolean-expr)
			}
			item = buffer[(writeIdx - 1) % N];
			return true; // NOLINT(readability-simplify-boolean-expr)
		}

		// Total number of items ever written = the absolute write cursor. A reader tracks its own
		// position and reads forward to this via peekAt, so readers don't share a cursor. Entries
		// more than N behind the cursor have been overwritten; the caller skips ahead past them.
		size_t writeCursor() const {
			return writeIndex.load(std::memory_order_acquire);
		}

		// Non-consuming read of absolute index `idx` (0-based across all writes ever). False when
		// idx has not been written yet (idx >= writeCursor()). The caller must not pass an idx more
		// than N behind writeCursor() -- those slots are already overwritten; skip to writeCursor()-N.
		bool peekAt(size_t idx, T& item) const {
			if (idx >= writeIndex.load(std::memory_order_acquire)) {
				return false; // NOLINT(readability-simplify-boolean-expr)
			}
			item = buffer[idx % N];
			return true; // NOLINT(readability-simplify-boolean-expr)
		}

	  private:
		mutable std::array<T, N>	buffer; // Mutable for lock-free const reads
		mutable std::atomic<size_t> writeIndex;
	};

} // namespace Foundation
