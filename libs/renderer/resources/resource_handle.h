#pragma once

// Resource Handle System
//
// Safe 32-bit handles for resource management instead of raw pointers.
// Handles combine index (16-bit) + generation (16-bit) for validation.
//
// Benefits:
// - Detects stale/dangling references via generation check
// - Supports hot-reloading (reload asset, handle stays valid)
// - Compact (4 bytes vs 8-byte pointer)
// - Serializable (save/load as 32-bit number)
//
// Use cases:
// - Textures, meshes, shaders
// - SVG assets and rasterized caches
// - Any resource that can be hot-reloaded

#include <cstdint>

namespace renderer {

	// 32-bit handle: 16-bit index + 16-bit generation
	struct ResourceHandle {
		uint32_t value;

		static constexpr uint32_t kInvalidHandle = 0xFFFFFFFF;

		// Check if handle is valid
		bool IsValid() const { return value != kInvalidHandle; }

		// Extract index (lower 16 bits)
		uint16_t GetIndex() const { return value & 0xFFFF; }

		// Extract generation (upper 16 bits)
		uint16_t GetGeneration() const { return value >> 16; }

		// Create handle from index and generation
		static ResourceHandle Make(uint16_t index, uint16_t generation) { return {(static_cast<uint32_t>(generation) << 16) | index}; }

		// Create invalid handle
		static ResourceHandle Invalid() { return {kInvalidHandle}; }

		// Comparison operators
		bool operator==(ResourceHandle other) const { return value == other.value; }

		bool operator!=(ResourceHandle other) const { return value != other.value; }
	};

	// Type-safe handle aliases
	using TextureHandle = ResourceHandle;
	using MeshHandle = ResourceHandle;
	using SVGAssetHandle = ResourceHandle;

} // namespace renderer
