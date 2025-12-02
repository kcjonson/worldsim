#include "SpatialIndex.h"

#include <gtest/gtest.h>
#include <unordered_set>

using namespace engine::assets;

// ============================================================================
// Basic Operations Tests
// ============================================================================

TEST(SpatialIndexTests, EmptyIndex) {
	SpatialIndex index;

	EXPECT_EQ(index.size(), 0);
}

TEST(SpatialIndexTests, InsertSingleEntity) {
	SpatialIndex index;
	index.insert({"Tree", {10.0F, 20.0F}});

	EXPECT_EQ(index.size(), 1);
}

TEST(SpatialIndexTests, InsertMultipleEntities) {
	SpatialIndex index;
	index.insert({"Tree", {10.0F, 20.0F}});
	index.insert({"Flower", {15.0F, 25.0F}});
	index.insert({"Grass", {100.0F, 200.0F}});

	EXPECT_EQ(index.size(), 3);
}

TEST(SpatialIndexTests, ClearIndex) {
	SpatialIndex index;
	index.insert({"Tree", {10.0F, 20.0F}});
	index.insert({"Flower", {15.0F, 25.0F}});

	EXPECT_EQ(index.size(), 2);

	index.clear();

	EXPECT_EQ(index.size(), 0);
}

// ============================================================================
// Query Radius Tests (All Types)
// ============================================================================

TEST(SpatialIndexTests, QueryRadiusEmpty) {
	SpatialIndex index;

	auto results = index.queryRadius({0.0F, 0.0F}, 10.0F);
	EXPECT_TRUE(results.empty());
}

TEST(SpatialIndexTests, QueryRadiusFindsNearby) {
	SpatialIndex index;
	index.insert({"Tree", {10.0F, 10.0F}});

	auto results = index.queryRadius({10.0F, 10.0F}, 1.0F);
	EXPECT_EQ(results.size(), 1);
	EXPECT_EQ(results[0]->defName, "Tree");
}

TEST(SpatialIndexTests, QueryRadiusExcludesFarAway) {
	SpatialIndex index;
	index.insert({"Tree", {100.0F, 100.0F}});

	auto results = index.queryRadius({0.0F, 0.0F}, 10.0F);
	EXPECT_TRUE(results.empty());
}

TEST(SpatialIndexTests, QueryRadiusBoundaryInclusive) {
	SpatialIndex index;
	index.insert({"Tree", {10.0F, 0.0F}}); // Exactly at radius distance

	auto results = index.queryRadius({0.0F, 0.0F}, 10.0F);
	EXPECT_EQ(results.size(), 1);
}

TEST(SpatialIndexTests, QueryRadiusMultipleResults) {
	SpatialIndex index;
	index.insert({"Tree1", {5.0F, 0.0F}});
	index.insert({"Tree2", {-5.0F, 0.0F}});
	index.insert({"Tree3", {0.0F, 5.0F}});
	index.insert({"TreeFar", {100.0F, 100.0F}});

	auto results = index.queryRadius({0.0F, 0.0F}, 10.0F);
	EXPECT_EQ(results.size(), 3);
}

// ============================================================================
// Query Radius by DefName Tests
// ============================================================================

TEST(SpatialIndexTests, QueryRadiusByDefNameFindsCorrect) {
	SpatialIndex index;
	index.insert({"Tree", {5.0F, 0.0F}});
	index.insert({"Flower", {0.0F, 5.0F}});
	index.insert({"Tree", {-5.0F, 0.0F}});

	auto results = index.queryRadius({0.0F, 0.0F}, 10.0F, "Tree");
	EXPECT_EQ(results.size(), 2);
	for (const auto* entity : results) {
		EXPECT_EQ(entity->defName, "Tree");
	}
}

TEST(SpatialIndexTests, QueryRadiusByDefNameNoMatch) {
	SpatialIndex index;
	index.insert({"Tree", {5.0F, 0.0F}});

	auto results = index.queryRadius({0.0F, 0.0F}, 10.0F, "Flower");
	EXPECT_TRUE(results.empty());
}

// ============================================================================
// Query Radius by Group (DefName Set) Tests
// ============================================================================

TEST(SpatialIndexTests, QueryRadiusGroupFindsMultipleTypes) {
	SpatialIndex index;
	index.insert({"Oak", {5.0F, 0.0F}});
	index.insert({"Pine", {0.0F, 5.0F}});
	index.insert({"Flower", {-5.0F, 0.0F}});

	std::unordered_set<std::string> trees = {"Oak", "Pine"};
	auto results = index.queryRadiusGroup({0.0F, 0.0F}, 10.0F, trees);

	EXPECT_EQ(results.size(), 2);
}

TEST(SpatialIndexTests, QueryRadiusGroupExcludesNonMembers) {
	SpatialIndex index;
	index.insert({"Oak", {5.0F, 0.0F}});
	index.insert({"Flower", {0.0F, 5.0F}});

	std::unordered_set<std::string> trees = {"Oak", "Pine"};
	auto results = index.queryRadiusGroup({0.0F, 0.0F}, 10.0F, trees);

	EXPECT_EQ(results.size(), 1);
	EXPECT_EQ(results[0]->defName, "Oak");
}

// ============================================================================
// HasNearby Tests
// ============================================================================

TEST(SpatialIndexTests, HasNearbyReturnsTrueWhenPresent) {
	SpatialIndex index;
	index.insert({"Tree", {5.0F, 0.0F}});

	EXPECT_TRUE(index.hasNearby({0.0F, 0.0F}, 10.0F, "Tree"));
}

TEST(SpatialIndexTests, HasNearbyReturnsFalseWhenAbsent) {
	SpatialIndex index;
	index.insert({"Tree", {100.0F, 100.0F}});

	EXPECT_FALSE(index.hasNearby({0.0F, 0.0F}, 10.0F, "Tree"));
}

TEST(SpatialIndexTests, HasNearbyReturnsFalseForWrongType) {
	SpatialIndex index;
	index.insert({"Tree", {5.0F, 0.0F}});

	EXPECT_FALSE(index.hasNearby({0.0F, 0.0F}, 10.0F, "Flower"));
}

TEST(SpatialIndexTests, HasNearbyReturnsFalseOnEmpty) {
	SpatialIndex index;

	EXPECT_FALSE(index.hasNearby({0.0F, 0.0F}, 10.0F, "Tree"));
}

// ============================================================================
// HasNearbyGroup Tests
// ============================================================================

TEST(SpatialIndexTests, HasNearbyGroupReturnsTrueWhenMemberPresent) {
	SpatialIndex index;
	index.insert({"Oak", {5.0F, 0.0F}});

	std::unordered_set<std::string> trees = {"Oak", "Pine"};
	EXPECT_TRUE(index.hasNearbyGroup({0.0F, 0.0F}, 10.0F, trees));
}

TEST(SpatialIndexTests, HasNearbyGroupReturnsFalseWhenNoMember) {
	SpatialIndex index;
	index.insert({"Flower", {5.0F, 0.0F}});

	std::unordered_set<std::string> trees = {"Oak", "Pine"};
	EXPECT_FALSE(index.hasNearbyGroup({0.0F, 0.0F}, 10.0F, trees));
}

// ============================================================================
// Cell Size Tests
// ============================================================================

TEST(SpatialIndexTests, CustomCellSize) {
	SpatialIndex index(10.0F); // 10-tile cells
	index.insert({"Tree", {5.0F, 5.0F}});
	index.insert({"Flower", {15.0F, 5.0F}});

	// Should find tree within radius of first cell
	auto results = index.queryRadius({0.0F, 0.0F}, 10.0F);
	EXPECT_EQ(results.size(), 1);
}

// ============================================================================
// Negative Coordinate Tests
// ============================================================================

TEST(SpatialIndexTests, NegativeCoordinates) {
	SpatialIndex index;
	index.insert({"Tree", {-10.0F, -10.0F}});

	auto results = index.queryRadius({-10.0F, -10.0F}, 5.0F);
	EXPECT_EQ(results.size(), 1);
}

TEST(SpatialIndexTests, CrossOriginQuery) {
	SpatialIndex index;
	index.insert({"Tree1", {-5.0F, 0.0F}});
	index.insert({"Tree2", {5.0F, 0.0F}});
	index.insert({"Tree3", {0.0F, -5.0F}});
	index.insert({"Tree4", {0.0F, 5.0F}});

	auto results = index.queryRadius({0.0F, 0.0F}, 10.0F);
	EXPECT_EQ(results.size(), 4);
}

// ============================================================================
// Large Scale Tests
// ============================================================================

TEST(SpatialIndexTests, ManyEntitiesPerformance) {
	SpatialIndex index;

	// Insert a grid of entities
	for (int x = 0; x < 100; x++) {
		for (int y = 0; y < 100; y++) {
			index.insert({"Grass", {static_cast<float>(x), static_cast<float>(y)}});
		}
	}

	EXPECT_EQ(index.size(), 10000);

	// Query should only check nearby cells, not all entities
	auto results = index.queryRadius({50.0F, 50.0F}, 5.0F);
	// Should find entities in a circle of radius 5 around (50, 50)
	EXPECT_GT(results.size(), 0);
	EXPECT_LT(results.size(), 100); // Much fewer than total
}

// ============================================================================
// Edge Case Tests
// ============================================================================

TEST(SpatialIndexTests, ZeroRadius) {
	SpatialIndex index;
	index.insert({"Tree", {0.0F, 0.0F}});

	// Zero radius should still find entity at exact position
	auto results = index.queryRadius({0.0F, 0.0F}, 0.0F);
	EXPECT_EQ(results.size(), 1);
}

TEST(SpatialIndexTests, VeryLargeRadius) {
	SpatialIndex index;
	index.insert({"Tree1", {0.0F, 0.0F}});
	index.insert({"Tree2", {1000.0F, 1000.0F}});
	index.insert({"Tree3", {-1000.0F, -1000.0F}});

	auto results = index.queryRadius({0.0F, 0.0F}, 2000.0F);
	EXPECT_EQ(results.size(), 3);
}

TEST(SpatialIndexTests, EntitiesAtSamePosition) {
	SpatialIndex index;
	index.insert({"Tree1", {10.0F, 10.0F}});
	index.insert({"Tree2", {10.0F, 10.0F}});
	index.insert({"Flower", {10.0F, 10.0F}});

	auto results = index.queryRadius({10.0F, 10.0F}, 1.0F);
	EXPECT_EQ(results.size(), 3);
}
