// The macro texture's shared border: the tile shader reads it bilinearly, so a chunk's last column and
// row must be its neighbors' first, or every chunk border would show a seam.

#include "world/chunk/GroundMacro.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

namespace world {

	namespace {
		std::vector<uint8_t> fill(ChunkCoordinate coord) {
			std::vector<uint8_t> rgba(kGroundMacroBytes);
			fillGroundMacro(coord, 0x5EED5EED12345678ULL, rgba);
			return rgba;
		}

		const uint8_t* texel(const std::vector<uint8_t>& rgba, int32_t i, int32_t j) {
			return rgba.data() + (static_cast<size_t>(j) * kGroundMacroSide + static_cast<size_t>(i)) * 4;
		}
	} // namespace

	TEST(GroundMacroTest, NeighborsShareTheirBorderTexels) {
		const ChunkCoordinate	   here{-1, 2};
		const std::vector<uint8_t> self = fill(here);
		const std::vector<uint8_t> east = fill({here.x + 1, here.y});
		const std::vector<uint8_t> south = fill({here.x, here.y + 1});
		const int32_t			   last = kGroundMacroSide - 1;
		for (int32_t k = 0; k < kGroundMacroSide; ++k) {
			EXPECT_TRUE(std::equal(texel(self, last, k), texel(self, last, k) + 4, texel(east, 0, k))) << "row " << k;
			EXPECT_TRUE(std::equal(texel(self, k, last), texel(self, k, last) + 4, texel(south, k, 0))) << "column " << k;
		}

		// Each channel varies, and the value channels sit around their 0.5 center.
		for (int c = 0; c < 3; ++c) {
			uint8_t lo = 255;
			uint8_t hi = 0;
			double	sum = 0.0;
			for (size_t t = 0; t < self.size(); t += 4) {
				lo = std::min(lo, self[t + static_cast<size_t>(c)]);
				hi = std::max(hi, self[t + static_cast<size_t>(c)]);
				sum += self[t + static_cast<size_t>(c)];
			}
			EXPECT_GT(hi - lo, 64) << "channel " << c;
			EXPECT_NEAR(sum / static_cast<double>(self.size() / 4), 127.5, 24.0) << "channel " << c;
		}
	}

} // namespace world
