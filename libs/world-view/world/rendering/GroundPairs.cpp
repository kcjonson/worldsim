#include "world/rendering/GroundPairs.h"

#include <algorithm>
#include <cassert>

namespace worldview {

	namespace {

		/// Surface id by D13 level, 0 (Mud) to 8 (Snow).
		std::array<size_t, kLandLevels> surfacesByLevel() {
			std::array<size_t, kLandLevels> byLevel{};
			for (size_t id = 0; id < world::kSurfaceCount; ++id) {
				const int level = world::surfaceStackLevel(static_cast<uint8_t>(id));
				if (level > 0) {
					byLevel[static_cast<size_t>(level - 1)] = id;
				}
			}
			return byLevel;
		}

	} // namespace

	GroundPairTable buildGroundPairTable(const std::array<assets::GroundTransitions, world::kSurfaceCount>& bySurface) {
		const std::array<size_t, kLandLevels> byLevel = surfacesByLevel();
		GroundPairTable						  table{};
		for (size_t upper = 1; upper < kLandLevels; ++upper) {
			for (size_t lower = 0; lower < upper; ++lower) {
				table[upper * kLandLevels + lower] = bySurface[byLevel[upper]][byLevel[lower]];
			}
		}
		return table;
	}

	void packGroundPairs(
		const GroundPairTable& table, const std::array<float, world::kSurfaceCount>& mipLevels, float metersPerPixel, std::span<float, kLandPairFloats> out
	) {
		const std::array<size_t, kLandLevels> byLevel = surfacesByLevel();
		std::fill(out.begin(), out.end(), 0.0F);
		for (size_t upper = 1; upper < kLandLevels; ++upper) {
			for (size_t lower = 0; lower < upper; ++lower) {
				const size_t					pair = upper * kLandLevels + lower;
				const assets::GroundTransition& t = table[pair];
				const float						mip = std::max(mipLevels[byLevel[upper]], mipLevels[byLevel[lower]]);
				const float						window = std::max(t.softness, mip * kLandWindowPerMip);
				const float						width = std::max(t.widthM, metersPerPixel);
				assert(window > 0.0F && width > 0.0F);
				float* v = out.data() + pair * kLandPairVec4s * 4;
				v[0] = 0.5F / width;
				v[1] = 0.5F + 0.5F * t.offsetM / width;
				v[2] = 0.5F / window;
				v[3] = 0.25F * t.breakup / window;
				v[4] = t.shadow;
				v[5] = t.stain;
				v[6] = t.ridge;
				v[7] = t.tint;
			}
		}
	}

} // namespace worldview
