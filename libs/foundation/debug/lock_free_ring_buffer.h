#pragma once

// Lock-free ring buffer for performance-critical data streaming.
//
// Design from docs/technical/observability/developer-server.md:
// - Game thread writes (never blocks, no mutex)
// - Server thread reads latest (discards intermediate samples)
// - Atomic operations only, zero contention
//
// Use for metrics streaming where:
// - Writer is high-frequency (60 Hz game loop)
// - Reader is low-frequency (10 Hz HTTP stream)
// - Latest value is sufficient (intermediate samples can be discarded)

#include <array>
#include <atomic>
#include <cstddef>

namespace Foundation { // NOLINT(readability-identifier-naming)

	template <typename T, size_t N = 64>
	class LockFreeRingBuffer {
	  public:
		LockFreeRingBuffer() // NOLINT(cppcoreguidelines-pro-type-member-init,modernize-use-equals-default)
			: buffer{},
			  writeIndex(0),
			  readIndex(0) {}

		// Write item to buffer (called by game thread)
		// Never blocks, always succeeds
		// Overwrites oldest data if buffer is full
		void write(const T& item) {
			size_t writeIdx = writeIndex.load(std::memory_order_relaxed);
			buffer[writeIdx % N] = item;
			writeIndex.store(writeIdx + 1, std::memory_order_release);
		}

		// Read latest item from buffer (called by server thread)
		// Returns false if buffer is empty
		// Discards all intermediate samples - only returns most recent
		bool readLatest(T& item) const {
			size_t writeIdx = writeIndex.load(std::memory_order_acquire);
			if (writeIdx == 0) {
				return false; // Buffer never written // NOLINT(readability-simplify-boolean-expr)
			}

			// Read most recent item
			item = buffer[(writeIdx - 1) % N];

			// Update read index to latest write (mutable in const method is intentional for lock-free)
			readIndex.store(writeIdx, std::memory_order_release);

			return true; // NOLINT(readability-simplify-boolean-expr)
		}

		// Read oldest unread item from buffer (for logs/events)
		// Returns false if no unread items
		// Preserves all items (no discarding)
		bool read(T& item) {
			size_t readIdx = readIndex.load(std::memory_order_relaxed);
			size_t writeIdx = writeIndex.load(std::memory_order_acquire);

			if (readIdx == writeIdx) {
				return false; // No unread items // NOLINT(readability-simplify-boolean-expr)
			}

			item = buffer[readIdx % N];
			readIndex.store(readIdx + 1, std::memory_order_release);

			return true; // NOLINT(readability-simplify-boolean-expr)
		}

		// Check if buffer has unread items
		bool hasData() const {
			size_t readIdx = readIndex.load(std::memory_order_relaxed);
			size_t writeIdx = writeIndex.load(std::memory_order_acquire);
			return readIdx != writeIdx;
		}

	  private:
		mutable std::array<T, N>	buffer; // Mutable for lock-free const reads
		mutable std::atomic<size_t> writeIndex;
		mutable std::atomic<size_t> readIndex;
	};

} // namespace Foundation
