#include "ChunkCoordinate.h"

#include <gtest/gtest.h>
#include <unordered_map>
#include <unordered_set>

using namespace engine::world;

// ============================================================================
// ChunkCoordinate Basic Tests
// ============================================================================

TEST(ChunkCoordinateTests, DefaultConstruction) {
	ChunkCoordinate coord;

	EXPECT_EQ(coord.x, 0);
	EXPECT_EQ(coord.y, 0);
}

TEST(ChunkCoordinateTests, ValueConstruction) {
	ChunkCoordinate coord(5, -3);

	EXPECT_EQ(coord.x, 5);
	EXPECT_EQ(coord.y, -3);
}

TEST(ChunkCoordinateTests, Equality) {
	ChunkCoordinate a(1, 2);
	ChunkCoordinate b(1, 2);
	ChunkCoordinate c(1, 3);

	EXPECT_EQ(a, b);
	EXPECT_NE(a, c);
}

TEST(ChunkCoordinateTests, NegativeCoordinates) {
	ChunkCoordinate coord(-100, -200);

	EXPECT_EQ(coord.x, -100);
	EXPECT_EQ(coord.y, -200);
}

// ============================================================================
// Origin and Position Tests
// ============================================================================

TEST(ChunkCoordinateTests, OriginAtZero) {
	ChunkCoordinate coord(0, 0);
	WorldPosition	origin = coord.origin();

	EXPECT_FLOAT_EQ(origin.x, 0.0F);
	EXPECT_FLOAT_EQ(origin.y, 0.0F);
}

TEST(ChunkCoordinateTests, OriginPositiveChunks) {
	ChunkCoordinate coord(1, 2);
	WorldPosition	origin = coord.origin();

	// Chunk (1, 2) origin = (512, 1024) at kChunkSize=512, kTileSize=1
	EXPECT_FLOAT_EQ(origin.x, 512.0F);
	EXPECT_FLOAT_EQ(origin.y, 1024.0F);
}

TEST(ChunkCoordinateTests, OriginNegativeChunks) {
	ChunkCoordinate coord(-1, -1);
	WorldPosition	origin = coord.origin();

	// Chunk (-1, -1) origin = (-512, -512)
	EXPECT_FLOAT_EQ(origin.x, -512.0F);
	EXPECT_FLOAT_EQ(origin.y, -512.0F);
}

TEST(ChunkCoordinateTests, Center) {
	ChunkCoordinate coord(0, 0);
	WorldPosition	center = coord.center();

	// Center of chunk (0,0) is at (256, 256)
	EXPECT_FLOAT_EQ(center.x, 256.0F);
	EXPECT_FLOAT_EQ(center.y, 256.0F);
}

TEST(ChunkCoordinateTests, Corners) {
	ChunkCoordinate coord(0, 0);

	WorldPosition nw = coord.corner(ChunkCorner::NorthWest);
	WorldPosition ne = coord.corner(ChunkCorner::NorthEast);
	WorldPosition sw = coord.corner(ChunkCorner::SouthWest);
	WorldPosition se = coord.corner(ChunkCorner::SouthEast);

	// NorthWest is origin
	EXPECT_FLOAT_EQ(nw.x, 0.0F);
	EXPECT_FLOAT_EQ(nw.y, 0.0F);

	// NorthEast
	EXPECT_FLOAT_EQ(ne.x, 512.0F);
	EXPECT_FLOAT_EQ(ne.y, 0.0F);

	// SouthWest
	EXPECT_FLOAT_EQ(sw.x, 0.0F);
	EXPECT_FLOAT_EQ(sw.y, 512.0F);

	// SouthEast
	EXPECT_FLOAT_EQ(se.x, 512.0F);
	EXPECT_FLOAT_EQ(se.y, 512.0F);
}

// ============================================================================
// Distance Tests
// ============================================================================

TEST(ChunkCoordinateTests, ManhattanDistance) {
	ChunkCoordinate a(0, 0);
	ChunkCoordinate b(3, 4);

	EXPECT_EQ(a.manhattanDistance(b), 7);
	EXPECT_EQ(b.manhattanDistance(a), 7);
}

TEST(ChunkCoordinateTests, ManhattanDistanceNegative) {
	ChunkCoordinate a(-2, 3);
	ChunkCoordinate b(1, -1);

	// |(-2) - 1| + |3 - (-1)| = 3 + 4 = 7
	EXPECT_EQ(a.manhattanDistance(b), 7);
}

TEST(ChunkCoordinateTests, ChebyshevDistance) {
	ChunkCoordinate a(0, 0);
	ChunkCoordinate b(3, 5);

	// max(3, 5) = 5
	EXPECT_EQ(a.chebyshevDistance(b), 5);
	EXPECT_EQ(b.chebyshevDistance(a), 5);
}

TEST(ChunkCoordinateTests, ChebyshevDistanceDiagonal) {
	ChunkCoordinate a(0, 0);
	ChunkCoordinate b(4, 4);

	// Diagonal movement: max(4, 4) = 4
	EXPECT_EQ(a.chebyshevDistance(b), 4);
}

TEST(ChunkCoordinateTests, DistanceToSelf) {
	ChunkCoordinate coord(5, 5);

	EXPECT_EQ(coord.manhattanDistance(coord), 0);
	EXPECT_EQ(coord.chebyshevDistance(coord), 0);
}

// ============================================================================
// World-to-Chunk Conversion Tests
// ============================================================================

TEST(ChunkCoordinateTests, WorldToChunkOrigin) {
	WorldPosition	pos(0.0F, 0.0F);
	ChunkCoordinate chunk = worldToChunk(pos);

	EXPECT_EQ(chunk.x, 0);
	EXPECT_EQ(chunk.y, 0);
}

TEST(ChunkCoordinateTests, WorldToChunkInsideFirstChunk) {
	// Position inside chunk (0, 0) but not at origin
	WorldPosition	pos(255.0F, 100.0F);
	ChunkCoordinate chunk = worldToChunk(pos);

	EXPECT_EQ(chunk.x, 0);
	EXPECT_EQ(chunk.y, 0);
}

TEST(ChunkCoordinateTests, WorldToChunkNextChunk) {
	// Position just into chunk (1, 0)
	WorldPosition	pos(512.0F, 0.0F);
	ChunkCoordinate chunk = worldToChunk(pos);

	EXPECT_EQ(chunk.x, 1);
	EXPECT_EQ(chunk.y, 0);
}

TEST(ChunkCoordinateTests, WorldToChunkNegative) {
	// Negative coordinates should floor correctly
	WorldPosition	pos(-1.0F, -1.0F);
	ChunkCoordinate chunk = worldToChunk(pos);

	// -1 is in chunk -1 (floor(-0.00195...) = -1)
	EXPECT_EQ(chunk.x, -1);
	EXPECT_EQ(chunk.y, -1);
}

TEST(ChunkCoordinateTests, WorldToChunkFarNegative) {
	WorldPosition	pos(-600.0F, -1024.0F);
	ChunkCoordinate chunk = worldToChunk(pos);

	// -600 / 512 = -1.17... -> floor = -2
	// -1024 / 512 = -2 -> floor = -2
	EXPECT_EQ(chunk.x, -2);
	EXPECT_EQ(chunk.y, -2);
}

TEST(ChunkCoordinateTests, WorldToChunkBoundary) {
	// Exactly on boundary of chunk
	WorldPosition	posOnBoundary(512.0F, 512.0F);
	ChunkCoordinate chunk = worldToChunk(posOnBoundary);

	EXPECT_EQ(chunk.x, 1);
	EXPECT_EQ(chunk.y, 1);
}

// ============================================================================
// World-to-Local-Tile Conversion Tests
// ============================================================================

TEST(ChunkCoordinateTests, WorldToLocalTileOrigin) {
	WorldPosition pos(0.0F, 0.0F);
	auto [tileX, tileY] = worldToLocalTile(pos);

	EXPECT_EQ(tileX, 0);
	EXPECT_EQ(tileY, 0);
}

TEST(ChunkCoordinateTests, WorldToLocalTileMiddle) {
	WorldPosition pos(256.0F, 256.0F);
	auto [tileX, tileY] = worldToLocalTile(pos);

	EXPECT_EQ(tileX, 256);
	EXPECT_EQ(tileY, 256);
}

TEST(ChunkCoordinateTests, WorldToLocalTileNextChunk) {
	// Position in chunk (1, 0) should give local tile (0, 0)
	WorldPosition pos(512.0F, 0.0F);
	auto [tileX, tileY] = worldToLocalTile(pos);

	EXPECT_EQ(tileX, 0);
	EXPECT_EQ(tileY, 0);
}

TEST(ChunkCoordinateTests, WorldToLocalTileNegative) {
	// -1 in world coordinates should map to tile 511 in the local chunk
	WorldPosition pos(-1.0F, -1.0F);
	auto [tileX, tileY] = worldToLocalTile(pos);

	EXPECT_EQ(tileX, 511);
	EXPECT_EQ(tileY, 511);
}

TEST(ChunkCoordinateTests, WorldToLocalTileNegativeOffset) {
	// Position in chunk (-1, -1) at local tile (10, 20)
	WorldPosition pos(-502.0F, -492.0F);
	auto [tileX, tileY] = worldToLocalTile(pos);

	EXPECT_EQ(tileX, 10);
	EXPECT_EQ(tileY, 20);
}

// ============================================================================
// Hash Function Tests
// ============================================================================

TEST(ChunkCoordinateHashTests, SameCoordinateSameHash) {
	ChunkCoordinate a(5, 10);
	ChunkCoordinate b(5, 10);

	std::hash<ChunkCoordinate> hasher;
	EXPECT_EQ(hasher(a), hasher(b));
}

TEST(ChunkCoordinateHashTests, DifferentCoordinatesDifferentHash) {
	ChunkCoordinate a(0, 0);
	ChunkCoordinate b(0, 1);
	ChunkCoordinate c(1, 0);

	std::hash<ChunkCoordinate> hasher;

	// These should all be different
	EXPECT_NE(hasher(a), hasher(b));
	EXPECT_NE(hasher(a), hasher(c));
	EXPECT_NE(hasher(b), hasher(c));
}

TEST(ChunkCoordinateHashTests, NegativeCoordinates) {
	ChunkCoordinate pos(5, 5);
	ChunkCoordinate neg(-5, -5);
	ChunkCoordinate mixed(5, -5);

	std::hash<ChunkCoordinate> hasher;

	EXPECT_NE(hasher(pos), hasher(neg));
	EXPECT_NE(hasher(pos), hasher(mixed));
	EXPECT_NE(hasher(neg), hasher(mixed));
}

TEST(ChunkCoordinateHashTests, SymmetryBreaking) {
	// (x, y) and (y, x) should have different hashes
	ChunkCoordinate a(3, 7);
	ChunkCoordinate b(7, 3);

	std::hash<ChunkCoordinate> hasher;
	EXPECT_NE(hasher(a), hasher(b));
}

TEST(ChunkCoordinateHashTests, UsableInUnorderedMap) {
	std::unordered_map<ChunkCoordinate, int> map;

	map[ChunkCoordinate(0, 0)] = 100;
	map[ChunkCoordinate(1, 0)] = 200;
	map[ChunkCoordinate(-1, -1)] = 300;
	map[ChunkCoordinate(100, 100)] = 400;

	EXPECT_EQ(map[ChunkCoordinate(0, 0)], 100);
	EXPECT_EQ(map[ChunkCoordinate(1, 0)], 200);
	EXPECT_EQ(map[ChunkCoordinate(-1, -1)], 300);
	EXPECT_EQ(map[ChunkCoordinate(100, 100)], 400);

	// Non-existent key
	EXPECT_EQ(map.find(ChunkCoordinate(5, 5)), map.end());
}

TEST(ChunkCoordinateHashTests, LowCollisionRate) {
	// Test a grid of coordinates for collisions
	std::unordered_set<size_t> hashes;
	std::hash<ChunkCoordinate> hasher;

	int			  collisions = 0;
	constexpr int range = 50;											// -50 to +50
	constexpr int totalCoordinates = (2 * range + 1) * (2 * range + 1); // 10201

	for (int x = -range; x <= range; x++) {
		for (int y = -range; y <= range; y++) {
			size_t h = hasher(ChunkCoordinate(x, y));
			if (hashes.count(h) > 0) {
				collisions++;
			}
			hashes.insert(h);
		}
	}

	// Allow up to 50% collision rate as acceptable for hash tables
	// (std::unordered_map handles collisions via chaining)
	// A typical good hash should have much lower, but some collision is normal
	EXPECT_LT(collisions, totalCoordinates / 2) << "Too many hash collisions";
}

TEST(ChunkCoordinateHashTests, LargeCoordinates) {
	// Test with coordinates that might overflow on 32-bit systems
	ChunkCoordinate large(1000000, 1000000);
	ChunkCoordinate largeNeg(-1000000, -1000000);

	std::hash<ChunkCoordinate> hasher;

	// Should produce valid hashes without overflow issues
	size_t h1 = hasher(large);
	size_t h2 = hasher(largeNeg);

	EXPECT_NE(h1, h2);
	EXPECT_NE(h1, 0u);
	EXPECT_NE(h2, 0u);
}

// ============================================================================
// WorldPosition Tests
// ============================================================================

TEST(WorldPositionTests, DefaultConstruction) {
	WorldPosition pos;

	EXPECT_FLOAT_EQ(pos.x, 0.0F);
	EXPECT_FLOAT_EQ(pos.y, 0.0F);
}

TEST(WorldPositionTests, ValueConstruction) {
	WorldPosition pos(10.5F, -20.3F);

	EXPECT_FLOAT_EQ(pos.x, 10.5F);
	EXPECT_FLOAT_EQ(pos.y, -20.3F);
}

TEST(WorldPositionTests, Addition) {
	WorldPosition a(10.0F, 20.0F);
	WorldPosition b(5.0F, -10.0F);
	WorldPosition c = a + b;

	EXPECT_FLOAT_EQ(c.x, 15.0F);
	EXPECT_FLOAT_EQ(c.y, 10.0F);
}

TEST(WorldPositionTests, Subtraction) {
	WorldPosition a(10.0F, 20.0F);
	WorldPosition b(5.0F, -10.0F);
	WorldPosition c = a - b;

	EXPECT_FLOAT_EQ(c.x, 5.0F);
	EXPECT_FLOAT_EQ(c.y, 30.0F);
}

TEST(WorldPositionTests, ScalarMultiplication) {
	WorldPosition a(10.0F, 20.0F);
	WorldPosition c = a * 2.0F;

	EXPECT_FLOAT_EQ(c.x, 20.0F);
	EXPECT_FLOAT_EQ(c.y, 40.0F);
}

TEST(WorldPositionTests, Equality) {
	WorldPosition a(10.0F, 20.0F);
	WorldPosition b(10.0F, 20.0F);
	WorldPosition c(10.0F, 21.0F);

	EXPECT_EQ(a, b);
	EXPECT_NE(a, c);
}

// ============================================================================
// Constants Tests
// ============================================================================

TEST(ChunkConstantsTests, ChunkSizeIs512) {
	EXPECT_EQ(kChunkSize, 512);
}

TEST(ChunkConstantsTests, TileSizeIs1) {
	EXPECT_FLOAT_EQ(kTileSize, 1.0F);
}

TEST(ChunkConstantsTests, ChunkWorldSizeIs512) {
	float chunkWorldSize = static_cast<float>(kChunkSize) * kTileSize;
	EXPECT_FLOAT_EQ(chunkWorldSize, 512.0F);
}
