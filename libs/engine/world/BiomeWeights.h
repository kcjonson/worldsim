#pragma once

// BiomeWeights - Percentage-based biome blending for tiles.
// Supports future transition zones where tiles blend between biomes.
// A tile at a grassland-forest boundary might be "70% grassland, 30% forest".

#include "world/Biome.h"

#include <algorithm>
#include <array>
#include <cstddef>

namespace engine::world {

/// Biome blend weights - supports multi-biome tiles.
/// Fixed array is cache-friendly and avoids heap allocations.
/// 8 biomes × 4 bytes = 32 bytes per tile (acceptable overhead).
struct BiomeWeights {
	std::array<float, static_cast<size_t>(Biome::Count)> weights{};

	/// Get weight for a specific biome (0.0 = absent, 1.0 = fully present)
	[[nodiscard]] float get(Biome biome) const { return weights[static_cast<size_t>(biome)]; }

	/// Set weight for a specific biome
	void set(Biome biome, float weight) { weights[static_cast<size_t>(biome)] = weight; }

	/// Create single-biome weights (100% one biome, 0% all others)
	[[nodiscard]] static BiomeWeights single(Biome biome) {
		BiomeWeights bw;
		bw.set(biome, 1.0F);
		return bw;
	}

	/// Get primary (dominant) biome - the one with highest weight.
	/// Returns Biome::Grassland when all weights are zero (default behavior).
	[[nodiscard]] Biome primary() const {
		Biome best = Biome::Grassland;
		float bestWeight = 0.0F;
		for (size_t i = 0; i < weights.size(); ++i) {
			if (weights[i] > bestWeight) {
				bestWeight = weights[i];
				best = static_cast<Biome>(i);
			}
		}
		return best;
	}

	/// Get secondary biome - second highest weight, for ecotone blending.
	/// Returns primary() if only one biome has weight (pure tile).
	[[nodiscard]] Biome secondary() const {
		Biome best = Biome::Grassland;
		Biome secondBest = Biome::Grassland;
		float bestWeight = 0.0F;
		float secondBestWeight = 0.0F;
		for (size_t i = 0; i < weights.size(); ++i) {
			if (weights[i] > bestWeight) {
				secondBestWeight = bestWeight;
				secondBest = best;
				bestWeight = weights[i];
				best = static_cast<Biome>(i);
			} else if (weights[i] > secondBestWeight) {
				secondBestWeight = weights[i];
				secondBest = static_cast<Biome>(i);
			}
		}
		// If no secondary has weight, return primary
		return (secondBestWeight > 0.0F) ? secondBest : best;
	}

	/// Get the weight of the primary biome (0.0-1.0).
	[[nodiscard]] float primaryWeight() const {
		float bestWeight = 0.0F;
		for (float w : weights) {
			if (w > bestWeight) {
				bestWeight = w;
			}
		}
		return bestWeight;
	}

	/// Check if a biome is present (weight > 0)
	[[nodiscard]] bool has(Biome biome) const { return weights[static_cast<size_t>(biome)] > 0.0F; }

	/// Normalize weights to sum to 1.0 (for blended tiles)
	void normalize() {
		float sum = 0.0F;
		for (float w : weights) {
			sum += w;
		}
		if (sum > 0.0F) {
			for (float& w : weights) {
				w /= sum;
			}
		}
	}

	/// Get total of all weights (useful for validation)
	[[nodiscard]] float total() const {
		float sum = 0.0F;
		for (float w : weights) {
			sum += w;
		}
		return sum;
	}
};

}  // namespace engine::world
