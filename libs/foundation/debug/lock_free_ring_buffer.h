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

namespace Foundation {

template <typename T, size_t N = 64>
class LockFreeRingBuffer {
public:
	LockFreeRingBuffer() : m_writeIndex(0), m_readIndex(0) {}

	// Write item to buffer (called by game thread)
	// Never blocks, always succeeds
	// Overwrites oldest data if buffer is full
	void Write(const T& item) {
		size_t writeIdx = m_writeIndex.load(std::memory_order_relaxed);
		m_buffer[writeIdx % N] = item;
		m_writeIndex.store(writeIdx + 1, std::memory_order_release);
	}

	// Read latest item from buffer (called by server thread)
	// Returns false if buffer is empty
	// Discards all intermediate samples - only returns most recent
	bool ReadLatest(T& item) const {
		size_t writeIdx = m_writeIndex.load(std::memory_order_acquire);
		if (writeIdx == 0)
			return false; // Buffer never written

		// Read most recent item
		item = m_buffer[(writeIdx - 1) % N];

		// Update read index to latest write (mutable in const method is intentional for lock-free)
		m_readIndex.store(writeIdx, std::memory_order_release);

		return true;
	}

	// Read oldest unread item from buffer (for logs/events)
	// Returns false if no unread items
	// Preserves all items (no discarding)
	bool Read(T& item) {
		size_t readIdx = m_readIndex.load(std::memory_order_relaxed);
		size_t writeIdx = m_writeIndex.load(std::memory_order_acquire);

		if (readIdx == writeIdx)
			return false; // No unread items

		item = m_buffer[readIdx % N];
		m_readIndex.store(readIdx + 1, std::memory_order_release);

		return true;
	}

	// Check if buffer has unread items
	bool HasData() const {
		size_t readIdx = m_readIndex.load(std::memory_order_relaxed);
		size_t writeIdx = m_writeIndex.load(std::memory_order_acquire);
		return readIdx != writeIdx;
	}

private:
	mutable std::array<T, N> m_buffer;             // Mutable for lock-free const reads
	mutable std::atomic<size_t> m_writeIndex;
	mutable std::atomic<size_t> m_readIndex;
};

} // namespace Foundation
