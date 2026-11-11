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
		constexpr uint64_t kSeed = 0x5EED5EED12345678ULL;

		// A chunk's macro texture for a canopy cover given as a function of the texel's world meters.
		template <typename CoverAt> std::vector<uint8_t> fill(ChunkCoordinate coord, CoverAt coverAt) {
			std::vector<uint8_t> rgba(kGroundMacroBytes);
			std::vector<float>	 cover(static_cast<size_t>(kGroundMacroSide) * kGroundMacroSide);
			for (int32_t j = 0; j < kGroundMacroSide; ++j) {
				for (int32_t i = 0; i < kGroundMacroSide; ++i) {
					cover[(static_cast<size_t>(j) * kGroundMacroSide) + static_cast<size_t>(i)] =
						coverAt((coord.x * kChunkSize) + (i * kGroundMacroMetersPerTexel), (coord.y * kChunkSize) + (j * kGroundMacroMetersPerTexel));
				}
			}
			fillGroundMacro(coord, kSeed, cover, rgba);
			return rgba;
		}

		std::vector<uint8_t> fill(ChunkCoordinate coord) {
			return fill(coord, [](int32_t, int32_t) { return 0.0F; });
		}

		float fullCover(int32_t /*x*/, int32_t /*y*/) {
			return 1.0F;
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

	// The corner texel is shared by four chunks: (0, 0)'s last is (1, 1)'s first, and its first is
	// (-1, -1)'s last, every channel and the floor included.
	TEST(GroundMacroTest, DiagonalNeighborsShareTheCornerTexel) {
		const std::vector<uint8_t> self = fill({0, 0}, fullCover);
		const std::vector<uint8_t> after = fill({1, 1}, fullCover);
		const std::vector<uint8_t> before = fill({-1, -1}, fullCover);
		const int32_t			   last = kGroundMacroSide - 1;
		EXPECT_TRUE(std::equal(texel(self.data(), last, last), texel(self.data(), last, last) + 4, texel(after.data(), 0, 0)));
		EXPECT_TRUE(std::equal(texel(self.data(), 0, 0), texel(self.data(), 0, 0) + 4, texel(before.data(), last, last)));
	}

	// The cover differs on each side of a border (a light canopy west of x = -100, inside the chunk
	// west of the border at x = 0, a closed one east of it, and another cover north of y = 0), and the
	// shared texels still agree: each is the field at its own position, whichever chunk fills it.
	TEST(GroundMacroTest, BorderTexelsAgreeWhereTheCoverDiffersAcrossIt) {
		const auto coverAt = [](int32_t x, int32_t y) {
			if (y < 0) {
				return 0.5F;
			}
			return x < -100 ? 0.3F : 1.0F;
		};
		const std::vector<uint8_t> west = fill({-1, 0}, coverAt);
		const std::vector<uint8_t> east = fill({0, 0}, coverAt);
		const std::vector<uint8_t> north = fill({0, -1}, coverAt);
		const int32_t			   last = kGroundMacroSide - 1;
		size_t					   floor = 0;
		for (int32_t k = 0; k < kGroundMacroSide; ++k) {
			EXPECT_EQ(texel(west.data(), last, k)[3], texel(east.data(), 0, k)[3]) << "row " << k;
			EXPECT_EQ(texel(north.data(), k, last)[3], texel(east.data(), k, 0)[3]) << "column " << k;
			floor += texel(east.data(), 0, k)[3] > 0 ? 1 : 0;
		}
		EXPECT_GT(floor, 0U) << "the border carries some floor, so the comparison isn't all zeros";
	}

	// Under a closed canopy a glade still has no floor: wherever the grove field is at or below the
	// floor's edge, alpha is exactly 0.
	TEST(GroundMacroTest, AGladeUnderFullCoverHasNoFloor) {
		const ChunkCoordinate	   coord{-2, 1};
		const std::vector<uint8_t> rgba = fill(coord, fullCover);
		size_t					   glade = 0;
		for (int32_t j = 0; j < kGroundMacroSide; ++j) {
			for (int32_t i = 0; i < kGroundMacroSide; ++i) {
				const auto x = static_cast<float>((coord.x * kChunkSize) + (i * kGroundMacroMetersPerTexel));
				const auto y = static_cast<float>((coord.y * kChunkSize) + (j * kGroundMacroMetersPerTexel));
				if (grove::groveValue(x, y, kSeed) <= grove::kFloorEdgeLo) {
					ASSERT_EQ(texel(rgba.data(), i, j)[3], 0) << i << ", " << j;
					++glade;
				}
			}
		}
		EXPECT_GT(glade, 0U) << "the chunk holds a glade";
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
