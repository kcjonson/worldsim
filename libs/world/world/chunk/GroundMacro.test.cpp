// The macro texture's shared border: the tile shader reads it bilinearly, so a chunk's last column and
// row must be its neighbors' first, or every chunk border would show a seam.

#include "world/chunk/Chunk.h"
#include "world/chunk/GroundMacro.h"
#include "world/chunk/MockWorldSampler.h"
#include "world/generation/GroveField.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <vector>

namespace world {

	namespace {
		std::vector<uint8_t> fill(ChunkCoordinate coord) {
			std::vector<uint8_t>	 rgba(kGroundMacroBytes);
			const std::vector<float> cover(static_cast<size_t>(kGroundMacroSide) * kGroundMacroSide, 0.0F);
			fillGroundMacro(coord, 0x5EED5EED12345678ULL, cover, rgba);
			return rgba;
		}

		const uint8_t* texel(const uint8_t* rgba, int32_t i, int32_t j) {
			return rgba + (static_cast<size_t>(j) * kGroundMacroSide + static_cast<size_t>(i)) * 4;
		}
	} // namespace

	TEST(GroundMacroTest, NeighborsShareTheirBorderTexels) {
		const ChunkCoordinate	   here{-1, 2};
		const std::vector<uint8_t> self = fill(here);
		const std::vector<uint8_t> east = fill({here.x + 1, here.y});
		const std::vector<uint8_t> south = fill({here.x, here.y + 1});
		const int32_t			   last = kGroundMacroSide - 1;
		for (int32_t k = 0; k < kGroundMacroSide; ++k) {
			EXPECT_TRUE(std::equal(texel(self.data(), last, k), texel(self.data(), last, k) + 4, texel(east.data(), 0, k))) << "row " << k;
			EXPECT_TRUE(std::equal(texel(self.data(), k, last), texel(self.data(), k, last) + 4, texel(south.data(), k, 0))) << "column " << k;
		}

		// Each channel varies, and the value channels sit around their 0.5 center; with no canopy
		// cover there is no forest floor.
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
		for (size_t t = 3; t < self.size(); t += 4) {
			ASSERT_EQ(self[t], 0) << "texel " << t / 4;
		}
	}

	// The forest floor in alpha, from chunks generated independently the way ChunkManager does it, each
	// texel's cover looked up by its own tile's biome: neighbors agree on every shared texel, and the
	// floor both lies somewhere and opens into glades somewhere.
	TEST(GroundMacroTest, ForestFloorMatchesAcrossChunkBorders) {
		grove::CanopyCover cover;
		for (size_t b = 0; b < cover.byBiome.size(); ++b) {
			cover.byBiome[b] = 0.25F + 0.75F * static_cast<float>(b % 4) / 3.0F;
		}
		const MockWorldSampler sampler(12345);
		auto				   generated = [&](ChunkCoordinate coord) {
			  auto chunk = std::make_unique<Chunk>(coord, sampler.sampleChunk(coord), sampler.getWorldSeed(), cover);
			  chunk->generate();
			  return chunk;
		};
		const auto	  self = generated({0, 0});
		const auto	  east = generated({1, 0});
		const auto	  south = generated({0, 1});
		const int32_t last = kGroundMacroSide - 1;
		for (int32_t k = 0; k < kGroundMacroSide; ++k) {
			EXPECT_EQ(texel(self->groundMacro(), last, k)[3], texel(east->groundMacro(), 0, k)[3]) << "row " << k;
			EXPECT_EQ(texel(self->groundMacro(), k, last)[3], texel(south->groundMacro(), k, 0)[3]) << "column " << k;
		}

		size_t open = 0;
		size_t floor = 0;
		for (size_t t = 3; t < kGroundMacroBytes; t += 4) {
			open += self->groundMacro()[t] == 0 ? 1 : 0;
			floor += self->groundMacro()[t] >= 128 ? 1 : 0;
		}
		EXPECT_GT(open, kGroundMacroBytes / 4 / 20);
		EXPECT_GT(floor, kGroundMacroBytes / 4 / 20);
	}

} // namespace world
