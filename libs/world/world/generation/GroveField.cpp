#include "world/generation/GroveField.h"

#include <algorithm>
#include <cmath>

namespace world::grove {

	namespace {

		float hash01(int64_t ix, int64_t iy, uint64_t seed) {
			uint64_t h = seed;
			h ^= static_cast<uint64_t>(ix) * 0x9E3779B97F4A7C15ULL;
			h ^= static_cast<uint64_t>(iy) * 0xC2B2AE3D27D4EB4FULL;
			h ^= h >> 29;
			h *= 0xBF58476D1CE4E5B9ULL;
			h ^= h >> 32;
			return static_cast<float>(static_cast<double>(h >> 11) * (1.0 / 9007199254740992.0));
		}

		// Smooth bilinear value noise in [0,1] at lattice coords (x,y).
		float valueNoise(float x, float y, uint64_t seed) {
			const auto	x0 = static_cast<int64_t>(std::floor(x));
			const auto	y0 = static_cast<int64_t>(std::floor(y));
			const float fx = x - static_cast<float>(x0);
			const float fy = y - static_cast<float>(y0);
			const float sx = fx * fx * (3.0F - 2.0F * fx);
			const float sy = fy * fy * (3.0F - 2.0F * fy);
			const float n00 = hash01(x0, y0, seed);
			const float n10 = hash01(x0 + 1, y0, seed);
			const float n01 = hash01(x0, y0 + 1, seed);
			const float n11 = hash01(x0 + 1, y0 + 1, seed);
			const float nx0 = n00 + (n10 - n00) * sx;
			const float nx1 = n01 + (n11 - n01) * sx;
			return nx0 + (nx1 - nx0) * sy;
		}

	} // namespace

	float smooth01(float a, float b, float v) {
		const float t = std::clamp((v - a) / (b - a), 0.0F, 1.0F);
		return t * t * (3.0F - 2.0F * t);
	}

	float warpedNoise(float worldX, float worldY, float featureMeters, uint64_t seed) {
		float x = worldX / featureMeters;
		float y = worldY / featureMeters;
		// Warp the sample point so outlines are organic, not aligned to the lattice
		// (which reads as square patches).
		const float wx = valueNoise(x * 0.6F + 3.1F, y * 0.6F + 1.7F, seed ^ 0x51U) - 0.5F;
		const float wy = valueNoise(x * 0.6F + 5.2F, y * 0.6F + 9.3F, seed ^ 0x52U) - 0.5F;
		x += wx * 1.1F;
		y += wy * 1.1F;
		// Two octaves so shapes aren't a single grid frequency.
		return 0.65F * valueNoise(x, y, seed) + 0.35F * valueNoise(x * 2.1F + 11.0F, y * 2.1F + 7.0F, seed ^ 0xA3U);
	}

	float groveValue(float worldX, float worldY, uint64_t seed) {
		return warpedNoise(worldX, worldY, kFeatureMeters, seed);
	}

	float thicketWeight(float grove) {
		return smooth01(kThicketBandLo, kThicketBandHi, grove);
	}

	float gladeWeight(float grove) {
		return smooth01(kGladeBandLo, kGladeBandHi, grove);
	}

	float gapWeight(float worldX, float worldY, uint64_t seed) {
		constexpr uint64_t kGapSalt = 0x6A9F1E1DULL;
		return smooth01(kGapBandLo, kGapBandHi, warpedNoise(worldX, worldY, kGapFeatureMeters, seed ^ kGapSalt));
	}

	float canopyWeight(float worldX, float worldY, uint64_t seed, float cover) {
		if (cover <= 0.0F) {
			return 0.0F;
		}
		const float grove = groveValue(worldX, worldY, seed);
		const float edge = smooth01(kFloorEdgeLo, kFloorEdgeHi, grove);
		if (edge <= 0.0F) {
			return 0.0F;
		}
		const float stand = kFloorLoose + (1.0F - kFloorLoose) * gladeWeight(grove);
		const float gap = kFloorInGap + (1.0F - kFloorInGap) * gapWeight(worldX, worldY, seed);
		return cover * edge * stand * gap;
	}

} // namespace world::grove
