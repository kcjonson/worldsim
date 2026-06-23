#include "world/chunk/TilePostProcessor.h"

#include "world/chunk/Chunk.h" // TileData, Surface

#include <gtest/gtest.h>

#include <array>
#include <memory>

using namespace engine::world;

namespace {
constexpr size_t tileIdx(int x, int y) {
	return static_cast<size_t>(y) * kChunkSize + static_cast<size_t>(x);
}
} // namespace

// A pond or oasis in a desert/beach sits on Sand; its bank must still turn to Mud so
// the riparian flora keyed on near="Mud" (reeds, bankside bushes) can seat. Without
// Sand in the mud whitelist those water bodies get no bank and grow nothing -- the
// oasis becomes barren. Regression guard for that fix.
TEST(TilePostProcessor, SandBankBecomesMudNextToWater) {
	auto tiles = std::make_unique<std::array<TileData, kChunkSize * kChunkSize>>();
	for (auto& t : *tiles) {
		t.surface = Surface::Sand; // an all-sand (desert) chunk
	}

	const int wx = 100;
	const int wy = 100;
	(*tiles)[tileIdx(wx, wy)].surface = Surface::Water; // a small water body

	TilePostProcessor::process(*tiles, /*seed*/ 1234u);

	// Wave 1 (cardinally adjacent to water) is always mud -- on sand, not just grass.
	EXPECT_EQ((*tiles)[tileIdx(wx + 1, wy)].surface, Surface::Mud);
	EXPECT_EQ((*tiles)[tileIdx(wx - 1, wy)].surface, Surface::Mud);
	EXPECT_EQ((*tiles)[tileIdx(wx, wy + 1)].surface, Surface::Mud);
	EXPECT_EQ((*tiles)[tileIdx(wx, wy - 1)].surface, Surface::Mud);

	// Sand far from any water stays sand; the water tile itself is untouched.
	EXPECT_EQ((*tiles)[tileIdx(300, 300)].surface, Surface::Sand);
	EXPECT_EQ((*tiles)[tileIdx(wx, wy)].surface, Surface::Water);
}
