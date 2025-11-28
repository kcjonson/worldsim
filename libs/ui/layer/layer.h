#pragma once

#include <cstdint>

// LayerHandle - Safe reference to a child in the component tree
//
// Uses generational index pattern: 16-bit index + 16-bit generation
// When a child is removed, generation increments, invalidating old handles.

namespace UI {

struct LayerHandle {
	uint32_t value{kInvalidHandle};

	static constexpr uint32_t kInvalidHandle = 0xFFFFFFFF;

	[[nodiscard]] bool IsValid() const { return value != kInvalidHandle; }

	[[nodiscard]] uint16_t GetIndex() const {
		return static_cast<uint16_t>(value & 0xFFFF);
	}

	[[nodiscard]] uint16_t GetGeneration() const {
		return static_cast<uint16_t>(value >> 16);
	}

	static LayerHandle Make(uint16_t index, uint16_t generation) {
		return {(static_cast<uint32_t>(generation) << 16) | index};
	}

	static LayerHandle Invalid() { return {kInvalidHandle}; }

	bool operator==(const LayerHandle& other) const { return value == other.value; }
	bool operator!=(const LayerHandle& other) const { return value != other.value; }
};

} // namespace UI
