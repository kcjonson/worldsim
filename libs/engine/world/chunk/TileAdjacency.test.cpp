#include "world/chunk/TileAdjacency.h"

#include <gtest/gtest.h>

using namespace engine::world::TileAdjacency;

TEST(TileAdjacency, HardEdgeMaskByFamily) {
	// Surface ids follow enum: Soil(0)=Ground, Dirt(1)=Ground, Sand(2)=Ground, Rock(3)=Rock, Water(4)=Water
	uint64_t adj = 0;
	setNeighbor(adj, N, 4);  // Water
	setNeighbor(adj, E, 3);  // Rock
	setNeighbor(adj, S, 0);  // Ground
	setNeighbor(adj, W, 1);  // Ground
	setNeighbor(adj, NE, 4); // Water
	setNeighbor(adj, NW, 0); // Ground
	setNeighbor(adj, SE, 3); // Rock
	setNeighbor(adj, SW, 1); // Ground

	uint8_t mask = getHardEdgeMaskByFamily(adj, 0); // thisSurface = Soil (Ground)

	// N, E, NE, SE should be hard (Water/Rock families differ); S/W/SW/NW remain soft
	EXPECT_NE(mask & (1 << N), 0);
	EXPECT_NE(mask & (1 << E), 0);
	EXPECT_EQ(mask & (1 << S), 0);
	EXPECT_EQ(mask & (1 << W), 0);
	EXPECT_NE(mask & (1 << NE), 0);
	EXPECT_EQ(mask & (1 << NW), 0);
	EXPECT_NE(mask & (1 << SE), 0);
	EXPECT_EQ(mask & (1 << SW), 0);
}
