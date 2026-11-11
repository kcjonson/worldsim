#pragma once

// The generation seed of a mesh variant: a hash of its seed group and its index. Ground tiles
// hash their def's <seedGroup>; tree templates hash the def and age class (AssetRegistry::
// generationSeed). Neighboring variants land far apart, so a generator that seeds from it
// alone never repeats a layout between variants or groups.

#include <cstdint>
#include <string_view>

namespace assets {

	[[nodiscard]] inline uint32_t variantSeed(std::string_view seedGroup, uint32_t variant) {
		uint32_t h = 2166136261U; // FNV-1a over the group
		for (const char c : seedGroup) {
			h ^= static_cast<uint8_t>(c);
			h *= 16777619U;
		}
		h ^= (variant + 1U) * 0x9E3779B9U;
		h ^= h >> 16;
		h *= 0x85EBCA6BU;
		h ^= h >> 13;
		h *= 0xC2B2AE35U;
		h ^= h >> 16;
		return h;
	}

} // namespace assets
