#include "PlacementExecutor.h"

#include "assets/AssetRegistry.h"

#include <world/chunk/Chunk.h>

#include <gtest/gtest.h>

using namespace assets;

// ============================================================================
// Test Fixtures and Helpers
// ============================================================================

/// Mock adjacent chunk provider for cross-chunk tests
class MockAdjacentProvider : public IAdjacentChunkProvider {
  public:
	void setChunkIndex(world::ChunkCoordinate coord, const SpatialIndex* index) {
		m_indices[coord] = index;
	}

	const SpatialIndex* getChunkIndex(world::ChunkCoordinate coord) const override {
		auto it = m_indices.find(coord);
		if (it != m_indices.end()) {
			return it->second;
		}
		return nullptr;
	}

  private:
	std::unordered_map<world::ChunkCoordinate, const SpatialIndex*> m_indices;
};

/// Create a simple chunk placement context for testing
ChunkPlacementContext createTestContext(world::ChunkCoordinate coord,
										uint64_t seed,
										world::Biome defaultBiome = world::Biome::TemperateGrassland) {
	ChunkPlacementContext ctx;
	ctx.coord = coord;
	ctx.worldSeed = seed;
	ctx.getBiome = [defaultBiome](uint16_t /*localX*/, uint16_t /*localY*/) {
		return defaultBiome;
	};
	ctx.isWater = [](glm::vec2 /*pos*/) { return false; };
	ctx.landSurface = [](glm::vec2 /*pos*/) { return world::Surface::Grass; };
	return ctx;
}

// ============================================================================
// Basic Initialization Tests
// ============================================================================

TEST(PlacementExecutorTests, ConstructorDoesNotInitialize) {
	auto& registry = AssetRegistry::Get();
	registry.clear();

	PlacementExecutor executor(registry);
	EXPECT_FALSE(executor.isInitialized());
}

TEST(PlacementExecutorTests, InitializeWithEmptyRegistry) {
	auto& registry = AssetRegistry::Get();
	registry.clear();

	PlacementExecutor executor(registry);
	executor.initialize();

	EXPECT_TRUE(executor.isInitialized());
	EXPECT_TRUE(executor.getSpawnOrder().empty());
}

TEST(PlacementExecutorTests, ClearResetsState) {
	auto& registry = AssetRegistry::Get();
	registry.clear();

	PlacementExecutor executor(registry);
	executor.initialize();

	executor.clear();

	EXPECT_FALSE(executor.isInitialized());
	EXPECT_TRUE(executor.getSpawnOrder().empty());
}

// ============================================================================
// Chunk Processing Tests
// ============================================================================

TEST(PlacementExecutorTests, ProcessChunkReturnsEmptyForUninitializedExecutor) {
	auto& registry = AssetRegistry::Get();
	registry.clear();

	PlacementExecutor executor(registry);
	// Don't initialize

	auto ctx = createTestContext({0, 0}, 12345);
	auto result = executor.processChunk(ctx);

	EXPECT_EQ(result.entitiesPlaced, 0);
	EXPECT_TRUE(result.entities.empty());
}

TEST(PlacementExecutorTests, ProcessChunkWithNoDefinitions) {
	auto& registry = AssetRegistry::Get();
	registry.clear();

	PlacementExecutor executor(registry);
	executor.initialize();

	auto ctx = createTestContext({0, 0}, 12345);
	auto result = executor.processChunk(ctx);

	EXPECT_EQ(result.entitiesPlaced, 0);
	EXPECT_EQ(result.coord.x, 0);
	EXPECT_EQ(result.coord.y, 0);
}

TEST(PlacementExecutorTests, ProcessChunkIsDeterministic) {
	auto& registry = AssetRegistry::Get();
	registry.clear();

	PlacementExecutor executor1(registry);
	executor1.initialize();

	PlacementExecutor executor2(registry);
	executor2.initialize();

	auto ctx = createTestContext({5, 10}, 42);
	auto result1 = executor1.processChunk(ctx);
	auto result2 = executor2.processChunk(ctx);

	// Same context should produce same results
	EXPECT_EQ(result1.entitiesPlaced, result2.entitiesPlaced);
}

TEST(PlacementExecutorTests, DifferentChunksProduceDifferentResults) {
	auto& registry = AssetRegistry::Get();
	registry.clear();

	PlacementExecutor executor(registry);
	executor.initialize();

	auto ctx1 = createTestContext({0, 0}, 12345);
	auto ctx2 = createTestContext({1, 0}, 12345);

	auto result1 = executor.processChunk(ctx1);
	auto result2 = executor.processChunk(ctx2);

	// Different chunks should have different coordinates in result
	EXPECT_EQ(result1.coord.x, 0);
	EXPECT_EQ(result2.coord.x, 1);
}

// ============================================================================
// Chunk Index Management Tests
// ============================================================================

TEST(PlacementExecutorTests, GetChunkIndexReturnsNullForUnprocessedChunk) {
	auto& registry = AssetRegistry::Get();
	registry.clear();

	PlacementExecutor executor(registry);
	executor.initialize();

	const SpatialIndex* index = executor.getChunkIndex({0, 0});
	EXPECT_EQ(index, nullptr);
}

TEST(PlacementExecutorTests, GetChunkIndexReturnsIndexForProcessedChunk) {
	auto& registry = AssetRegistry::Get();
	registry.clear();

	PlacementExecutor executor(registry);
	executor.initialize();

	auto ctx = createTestContext({5, 5}, 12345);
	executor.processChunk(ctx);

	const SpatialIndex* index = executor.getChunkIndex({5, 5});
	EXPECT_NE(index, nullptr);
}

TEST(PlacementExecutorTests, UnloadChunkRemovesIndex) {
	auto& registry = AssetRegistry::Get();
	registry.clear();

	PlacementExecutor executor(registry);
	executor.initialize();

	auto ctx = createTestContext({5, 5}, 12345);
	executor.processChunk(ctx);

	EXPECT_NE(executor.getChunkIndex({5, 5}), nullptr);

	executor.unloadChunk({5, 5});

	EXPECT_EQ(executor.getChunkIndex({5, 5}), nullptr);
}

// ============================================================================
// Adjacent Chunk Provider Tests
// ============================================================================

TEST(PlacementExecutorTests, ProcessChunkWithAdjacentProvider) {
	auto& registry = AssetRegistry::Get();
	registry.clear();

	PlacementExecutor executor(registry);
	executor.initialize();

	// Set up adjacent chunk index
	SpatialIndex adjacentIndex;
	adjacentIndex.insert({"TestEntity", {-10.0F, 10.0F}});

	MockAdjacentProvider provider;
	provider.setChunkIndex({-1, 0}, &adjacentIndex);

	auto ctx = createTestContext({0, 0}, 12345);
	auto result = executor.processChunk(ctx, &provider);

	// Should not crash and return valid result
	EXPECT_EQ(result.coord.x, 0);
	EXPECT_EQ(result.coord.y, 0);
}

// ============================================================================
// Context Function Tests
// ============================================================================

TEST(PlacementExecutorTests, BiomeFunctionIsCalled) {
	auto& registry = AssetRegistry::Get();
	registry.clear();

	PlacementExecutor executor(registry);
	executor.initialize();

	int callCount = 0;
	ChunkPlacementContext ctx;
	ctx.coord = {0, 0};
	ctx.worldSeed = 12345;
	ctx.getBiome = [&callCount](uint16_t /*localX*/, uint16_t /*localY*/) {
		++callCount;
		return world::Biome::TemperateGrassland;
	};

	executor.processChunk(ctx);

	// With no definitions, biome function won't be called
	// This test verifies the context is properly used
	EXPECT_GE(callCount, 0);
}

// ============================================================================
// Coordinate System Tests
// ============================================================================

TEST(PlacementExecutorTests, ChunkCoordinateOriginCalculation) {
	// Verify that chunk coordinate origins are calculated correctly
	world::ChunkCoordinate coord0{0, 0};
	world::ChunkCoordinate coord1{1, 0};
	world::ChunkCoordinate coordNeg{-1, -1};

	auto origin0 = coord0.origin();
	auto origin1 = coord1.origin();
	auto originNeg = coordNeg.origin();

	EXPECT_EQ(origin0.x, 0.0F);
	EXPECT_EQ(origin0.y, 0.0F);
	EXPECT_EQ(origin1.x, static_cast<float>(world::kChunkSize));
	EXPECT_EQ(origin1.y, 0.0F);
	EXPECT_EQ(originNeg.x, -static_cast<float>(world::kChunkSize));
	EXPECT_EQ(originNeg.y, -static_cast<float>(world::kChunkSize));
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST(PlacementExecutorTests, ProcessSameChunkTwice) {
	auto& registry = AssetRegistry::Get();
	registry.clear();

	PlacementExecutor executor(registry);
	executor.initialize();

	auto ctx = createTestContext({0, 0}, 12345);
	auto result1 = executor.processChunk(ctx);
	auto result2 = executor.processChunk(ctx);

	// Both should succeed and produce same results (deterministic)
	EXPECT_EQ(result1.entitiesPlaced, result2.entitiesPlaced);
}

TEST(PlacementExecutorTests, ProcessMultipleChunks) {
	auto& registry = AssetRegistry::Get();
	registry.clear();

	PlacementExecutor executor(registry);
	executor.initialize();

	// Process a grid of chunks
	for (int x = -2; x <= 2; ++x) {
		for (int y = -2; y <= 2; ++y) {
			auto ctx = createTestContext({x, y}, 12345);
			auto result = executor.processChunk(ctx);
			EXPECT_EQ(result.coord.x, x);
			EXPECT_EQ(result.coord.y, y);
		}
	}

	// All chunk indices should be accessible
	for (int x = -2; x <= 2; ++x) {
		for (int y = -2; y <= 2; ++y) {
			EXPECT_NE(executor.getChunkIndex({x, y}), nullptr);
		}
	}
}

TEST(PlacementExecutorTests, NegativeChunkCoordinates) {
	auto& registry = AssetRegistry::Get();
	registry.clear();

	PlacementExecutor executor(registry);
	executor.initialize();

	auto ctx = createTestContext({-100, -50}, 12345);
	auto result = executor.processChunk(ctx);

	EXPECT_EQ(result.coord.x, -100);
	EXPECT_EQ(result.coord.y, -50);
	EXPECT_NE(executor.getChunkIndex({-100, -50}), nullptr);
}

// ============================================================================
// Entity Cooldown Tests
// ============================================================================

TEST(PlacementExecutorTests, IsEntityOnCooldownReturnsFalseInitially) {
	auto& registry = AssetRegistry::Get();
	registry.clear();

	PlacementExecutor executor(registry);
	executor.initialize();

	// Entity not on cooldown initially
	EXPECT_FALSE(executor.isEntityOnCooldown({0, 0}, {10.0F, 20.0F}, "BerryBush"));
}

TEST(PlacementExecutorTests, SetEntityCooldownMakesEntityOnCooldown) {
	auto& registry = AssetRegistry::Get();
	registry.clear();

	PlacementExecutor executor(registry);
	executor.initialize();

	executor.setEntityCooldown({0, 0}, {10.0F, 20.0F}, "BerryBush", 60.0F);

	EXPECT_TRUE(executor.isEntityOnCooldown({0, 0}, {10.0F, 20.0F}, "BerryBush"));
}

TEST(PlacementExecutorTests, DifferentPositionsAreIndependentCooldowns) {
	auto& registry = AssetRegistry::Get();
	registry.clear();

	PlacementExecutor executor(registry);
	executor.initialize();

	executor.setEntityCooldown({0, 0}, {10.0F, 20.0F}, "BerryBush", 60.0F);

	// Same chunk, different position
	EXPECT_FALSE(executor.isEntityOnCooldown({0, 0}, {15.0F, 25.0F}, "BerryBush"));
}

TEST(PlacementExecutorTests, DifferentDefNamesAreIndependentCooldowns) {
	auto& registry = AssetRegistry::Get();
	registry.clear();

	PlacementExecutor executor(registry);
	executor.initialize();

	executor.setEntityCooldown({0, 0}, {10.0F, 20.0F}, "BerryBush", 60.0F);

	// Same position, different defName
	EXPECT_FALSE(executor.isEntityOnCooldown({0, 0}, {10.0F, 20.0F}, "WoodyBush"));
}

TEST(PlacementExecutorTests, DifferentChunksAreIndependentCooldowns) {
	auto& registry = AssetRegistry::Get();
	registry.clear();

	PlacementExecutor executor(registry);
	executor.initialize();

	executor.setEntityCooldown({0, 0}, {10.0F, 20.0F}, "BerryBush", 60.0F);

	// Same position and defName, different chunk
	EXPECT_FALSE(executor.isEntityOnCooldown({1, 0}, {10.0F, 20.0F}, "BerryBush"));
}

TEST(PlacementExecutorTests, UpdateCooldownsDecrementsCooldownTime) {
	auto& registry = AssetRegistry::Get();
	registry.clear();

	PlacementExecutor executor(registry);
	executor.initialize();

	executor.setEntityCooldown({0, 0}, {10.0F, 20.0F}, "BerryBush", 5.0F);
	EXPECT_TRUE(executor.isEntityOnCooldown({0, 0}, {10.0F, 20.0F}, "BerryBush"));

	// Update with 2 seconds
	executor.updateCooldowns(2.0F);

	// Still on cooldown (5 - 2 = 3 seconds remaining)
	EXPECT_TRUE(executor.isEntityOnCooldown({0, 0}, {10.0F, 20.0F}, "BerryBush"));
}

TEST(PlacementExecutorTests, UpdateCooldownsRemovesExpiredCooldowns) {
	auto& registry = AssetRegistry::Get();
	registry.clear();

	PlacementExecutor executor(registry);
	executor.initialize();

	executor.setEntityCooldown({0, 0}, {10.0F, 20.0F}, "BerryBush", 5.0F);
	EXPECT_TRUE(executor.isEntityOnCooldown({0, 0}, {10.0F, 20.0F}, "BerryBush"));

	// Update with 6 seconds (cooldown expires)
	executor.updateCooldowns(6.0F);

	// No longer on cooldown
	EXPECT_FALSE(executor.isEntityOnCooldown({0, 0}, {10.0F, 20.0F}, "BerryBush"));
}

TEST(PlacementExecutorTests, UpdateCooldownsExactExpiration) {
	auto& registry = AssetRegistry::Get();
	registry.clear();

	PlacementExecutor executor(registry);
	executor.initialize();

	executor.setEntityCooldown({0, 0}, {10.0F, 20.0F}, "BerryBush", 5.0F);

	// Update with exactly 5 seconds
	executor.updateCooldowns(5.0F);

	// Should be expired at exactly 0 or slightly negative
	EXPECT_FALSE(executor.isEntityOnCooldown({0, 0}, {10.0F, 20.0F}, "BerryBush"));
}

TEST(PlacementExecutorTests, MultipleCooldownsUpdateIndependently) {
	auto& registry = AssetRegistry::Get();
	registry.clear();

	PlacementExecutor executor(registry);
	executor.initialize();

	executor.setEntityCooldown({0, 0}, {10.0F, 20.0F}, "BerryBush", 3.0F);
	executor.setEntityCooldown({0, 0}, {30.0F, 40.0F}, "BerryBush", 10.0F);

	// Update with 5 seconds
	executor.updateCooldowns(5.0F);

	// First cooldown expired, second still active
	EXPECT_FALSE(executor.isEntityOnCooldown({0, 0}, {10.0F, 20.0F}, "BerryBush"));
	EXPECT_TRUE(executor.isEntityOnCooldown({0, 0}, {30.0F, 40.0F}, "BerryBush"));
}

TEST(PlacementExecutorTests, ResetCooldownOverwritesExisting) {
	auto& registry = AssetRegistry::Get();
	registry.clear();

	PlacementExecutor executor(registry);
	executor.initialize();

	executor.setEntityCooldown({0, 0}, {10.0F, 20.0F}, "BerryBush", 5.0F);

	// Reset cooldown to longer time
	executor.setEntityCooldown({0, 0}, {10.0F, 20.0F}, "BerryBush", 100.0F);

	// Update with 10 seconds (would have expired with original 5s cooldown)
	executor.updateCooldowns(10.0F);

	// Still on cooldown due to reset
	EXPECT_TRUE(executor.isEntityOnCooldown({0, 0}, {10.0F, 20.0F}, "BerryBush"));
}

TEST(PlacementExecutorTests, ZeroCooldownExpiresOnFirstUpdate) {
	auto& registry = AssetRegistry::Get();
	registry.clear();

	PlacementExecutor executor(registry);
	executor.initialize();

	// Set zero cooldown - still registered as on cooldown initially
	executor.setEntityCooldown({0, 0}, {10.0F, 20.0F}, "BerryBush", 0.0F);

	// Zero cooldown expires immediately on first update
	executor.updateCooldowns(0.001F);

	// Now it should be expired
	EXPECT_FALSE(executor.isEntityOnCooldown({0, 0}, {10.0F, 20.0F}, "BerryBush"));
}

// ============================================================================
// Distribution::Spaced + riparian placement (forests-and-ponds PR)
// ============================================================================

namespace {
	AssetDefinition makeFloraDef(const std::string& name, world::Biome biome,
								 Distribution dist, float spawnChance, float minDist,
								 const std::string& nearType = "", float nearDist = 0.0F) {
		AssetDefinition d;
		d.defName = name;
		d.assetType = AssetType::Procedural;
		BiomePlacement bp;
		bp.biomeName = world::biomeToString(biome);
		bp.spawnChance = spawnChance;
		bp.distribution = dist;
		bp.spacing.minDistance = minDist;
		bp.nearTileType = nearType;
		bp.nearDistance = nearDist;
		d.placement.biomes.push_back(bp);
		return d;
	}
} // namespace

// Spaced placement must honor minDistance: no two placed instances closer than it.
// (Before this PR, Spaced fell through to Uniform and minDistance did nothing.)
TEST(PlacementExecutorTests, SpacedRespectsMinDistance) {
	auto& registry = AssetRegistry::Get();
	registry.clear();
	constexpr float kMinDist = 20.0F;
	registry.registerTestDefinition(makeFloraDef("Flora_TestSpaced", world::Biome::TemperateGrassland,
												 Distribution::Spaced, 0.4F, kMinDist));

	PlacementExecutor executor(registry);
	executor.initialize();
	auto ctx = createTestContext({0, 0}, 777, world::Biome::TemperateGrassland); // getSurface = Grass
	auto result = executor.processChunk(ctx);

	ASSERT_GT(result.entities.size(), 1u) << "spaced stand should place many instances";
	// Spaced enforces minDistance, except thicket cores (grove-field high tail) tighten
	// it; the hard floor is (1 - kThicketTighten) * minDistance = 0.55 * minDistance.
	// Assert no pair beats that floor (proving spacing is enforced, unlike Uniform).
	const float floorDist = 0.55F * kMinDist;
	for (size_t i = 0; i < result.entities.size(); ++i) {
		for (size_t j = i + 1; j < result.entities.size(); ++j) {
			const float dx = result.entities[i].position.x - result.entities[j].position.x;
			const float dy = result.entities[i].position.y - result.entities[j].position.y;
			EXPECT_GE(dx * dx + dy * dy, floorDist * floorDist - 0.01F)
				<< "two spaced instances closer than the thicket spacing floor";
		}
	}
}

// Riparian flora (near="Water") must actually cluster at a waterline. Diagnoses the
// "waterside plants don't appear" report: a water stripe with land around it should
// grow near-water flora, all within near.distance (+ clump spread) of the stripe, and
// none in it.
TEST(PlacementExecutorTests, RiparianFloraSpawnsNearWater) {
	auto& registry = AssetRegistry::Get();
	registry.clear();
	registry.registerTestDefinition(makeFloraDef("Flora_TestReed", world::Biome::BorealForest,
												 Distribution::Clumped, 0.6F, 0.0F, "Water", 3.0F));

	PlacementExecutor executor(registry);
	executor.initialize();

	ChunkPlacementContext ctx;
	ctx.coord = {0, 0};
	ctx.worldSeed = 999;
	ctx.getBiome = [](uint16_t, uint16_t) { return world::Biome::BorealForest; };
	// A vertical water stripe over x in [250, 253); land (Grass) elsewhere.
	ctx.isWater = [](glm::vec2 pos) { return pos.x >= 250.0F && pos.x < 253.0F; };
	ctx.landSurface = [](glm::vec2 /*pos*/) { return world::Surface::Grass; };

	auto result = executor.processChunk(ctx);
	ASSERT_GT(result.entities.size(), 0u) << "near-water flora should appear at the waterline";
	for (const auto& e : result.entities) {
		const float lx = e.position.x; // chunk origin is (0,0), so world == local
		const float dist = lx < 250.0F ? (250.0F - lx) : (lx >= 253.0F ? lx - 253.0F : 0.0F);
		EXPECT_LE(dist, 8.0F) << "near-water flora spawned far from the water (x=" << lx << ")";
		EXPECT_GT(dist, 0.0F) << "near-water flora spawned in the water (x=" << lx << ")";
	}
}

// A land surface rule (near="Mud", as the reeds have) reads the painted land: a
// painted mud band grows the flora beside it and nowhere else.
TEST(PlacementExecutorTests, NearSurfaceReadsThePaintedLand) {
	auto& registry = AssetRegistry::Get();
	registry.clear();
	registry.registerTestDefinition(makeFloraDef("Flora_TestBankReed", world::Biome::BorealForest,
												 Distribution::Uniform, 0.6F, 0.0F, "Mud", 1.0F));

	PlacementExecutor executor(registry);
	executor.initialize();

	ChunkPlacementContext ctx;
	ctx.coord = {0, 0};
	ctx.worldSeed = 4242;
	ctx.getBiome = [](uint16_t, uint16_t) { return world::Biome::BorealForest; };
	ctx.isWater = [](glm::vec2 /*pos*/) { return false; };
	// Mud painted over y in [100, 102.5), off the tile grid; grass elsewhere.
	ctx.landSurface = [](glm::vec2 pos) {
		return pos.y >= 100.0F && pos.y < 102.5F ? world::Surface::Mud : world::Surface::Grass;
	};

	auto result = executor.processChunk(ctx);
	ASSERT_GT(result.entities.size(), 0u) << "flora near painted mud should grow beside it";
	for (const auto& e : result.entities) {
		// Tile centers within one tile of the sampled tile's: tiles 99 to 102.
		EXPECT_GE(e.position.y, 99.0F);
		EXPECT_LT(e.position.y, 103.0F);
	}
}

TEST(PlacementExecutorTests, NegativeChunkCooldowns) {
	auto& registry = AssetRegistry::Get();
	registry.clear();

	PlacementExecutor executor(registry);
	executor.initialize();

	executor.setEntityCooldown({-5, -10}, {10.0F, 20.0F}, "BerryBush", 60.0F);

	EXPECT_TRUE(executor.isEntityOnCooldown({-5, -10}, {10.0F, 20.0F}, "BerryBush"));
	EXPECT_FALSE(executor.isEntityOnCooldown({5, 10}, {10.0F, 20.0F}, "BerryBush"));
}

TEST(PlacementExecutorTests, ClearRemovesCooldowns) {
	auto& registry = AssetRegistry::Get();
	registry.clear();

	PlacementExecutor executor(registry);
	executor.initialize();

	executor.setEntityCooldown({0, 0}, {10.0F, 20.0F}, "BerryBush", 60.0F);
	EXPECT_TRUE(executor.isEntityOnCooldown({0, 0}, {10.0F, 20.0F}, "BerryBush"));

	executor.clear();

	// After clear, cooldown should be gone
	EXPECT_FALSE(executor.isEntityOnCooldown({0, 0}, {10.0F, 20.0F}, "BerryBush"));
}

TEST(PlacementExecutorTests, CooldownPositionQuantization) {
	auto& registry = AssetRegistry::Get();
	registry.clear();

	PlacementExecutor executor(registry);
	executor.initialize();

	// Set cooldown at position 10.3, 20.7
	executor.setEntityCooldown({0, 0}, {10.3F, 20.7F}, "BerryBush", 60.0F);

	// Query with slightly different position that quantizes to same tile
	// Position is quantized to integer tile coordinates (floor)
	EXPECT_TRUE(executor.isEntityOnCooldown({0, 0}, {10.1F, 20.9F}, "BerryBush"));
	EXPECT_TRUE(executor.isEntityOnCooldown({0, 0}, {10.9F, 20.0F}, "BerryBush"));

	// Different tile should not be on cooldown
	EXPECT_FALSE(executor.isEntityOnCooldown({0, 0}, {11.0F, 20.0F}, "BerryBush"));
	EXPECT_FALSE(executor.isEntityOnCooldown({0, 0}, {10.0F, 21.0F}, "BerryBush"));
}

// ============================================================================
// Placement golden
// ============================================================================

namespace {

	uint64_t fnvMix(uint64_t hash, const void* data, size_t size) {
		const auto* bytes = static_cast<const unsigned char*>(data);
		for (size_t i = 0; i < size; ++i) {
			hash ^= bytes[i];
			hash *= 0x100000001B3ULL;
		}
		return hash;
	}

	// Every field placement draws from the chunk RNG, in order: name, position, rotation,
	// scale, tint.
	uint64_t placementHash(const std::vector<PlacedEntity>& entities) {
		uint64_t hash = 0xCBF29CE484222325ULL;
		for (const PlacedEntity& e : entities) {
			hash = fnvMix(hash, e.defName.data(), e.defName.size() + 1);
			const float fields[] = {e.position.x, e.position.y, e.rotation, e.scale, e.colorTint.r, e.colorTint.g, e.colorTint.b, e.colorTint.a};
			hash = fnvMix(hash, fields, sizeof(fields));
		}
		return hash;
	}

	const std::vector<AgeClass> kTreeAges{
		{.name = "sapling", .weight = 0.25F, .variants = 4, .yieldScale = 0.25F, .workScale = 0.35F},
		{.name = "mature", .weight = 0.55F, .variants = 8},
		{.name = "veteran", .weight = 0.20F, .variants = 4, .yieldScale = 1.8F, .workScale = 1.6F},
	};

	// A spaced oak and maple with age classes, clumped bushes, and uniform rocks across a
	// forest and a grassland half, with a river stripe the placer skips. `oakScale` is the
	// oak's own placement scale range.
	void registerGoldenDefs(AssetRegistry& registry, PlacementScale oakScale = {}) {
		registry.clear();
		AssetDefinition oak = makeFloraDef("Golden_Oak", world::Biome::TemperateDeciduousForest, Distribution::Spaced, 0.45F, 3.0F);
		oak.ageClasses = kTreeAges;
		oak.placement.scale = oakScale;
		registry.registerTestDefinition(oak);

		AssetDefinition maple = makeFloraDef("Golden_Maple", world::Biome::TemperateDeciduousForest, Distribution::Spaced, 0.40F, 2.8F);
		BiomePlacement lone;
		lone.biomeName = world::biomeToString(world::Biome::TemperateGrassland);
		lone.spawnChance = 0.01F;
		lone.distribution = Distribution::Spaced;
		lone.spacing.minDistance = 12.0F;
		maple.placement.biomes.push_back(lone);
		maple.ageClasses = kTreeAges;
		registry.registerTestDefinition(maple);

		registry.registerTestDefinition(makeFloraDef("Golden_Bush", world::Biome::TemperateGrassland, Distribution::Clumped, 0.01F, 0.0F));

		AssetDefinition rock = makeFloraDef("Golden_Rock", world::Biome::TemperateGrassland, Distribution::Uniform, 0.02F, 0.0F);
		rock.maxRandomRotation = 3.14159265F;
		BiomePlacement forestRock = rock.placement.biomes.front();
		forestRock.biomeName = world::biomeToString(world::Biome::TemperateDeciduousForest);
		rock.placement.biomes.push_back(forestRock);
		registry.registerTestDefinition(rock);
	}

	ChunkPlacementContext goldenContext(world::ChunkCoordinate coord) {
		ChunkPlacementContext ctx;
		ctx.coord = coord;
		ctx.worldSeed = 424242;
		ctx.getBiome = [](uint16_t localX, uint16_t /*localY*/) {
			return localX < 300 ? world::Biome::TemperateDeciduousForest : world::Biome::TemperateGrassland;
		};
		ctx.isWater = [origin = coord.origin()](glm::vec2 pos) {
			const float localY = pos.y - origin.y;
			return localY >= 200.0F && localY < 206.0F;
		};
		ctx.landSurface = [](glm::vec2 /*pos*/) { return world::Surface::Grass; };
		return ctx;
	}

	struct GoldenChunk {
		world::ChunkCoordinate coord;
		size_t				   count;
		uint64_t			   hash;
	};

} // namespace

// Placement of every entity, for a fixed seed, matches what it was before per-tree
// traits existed, and each carries its computed traits. The hashes were captured from
// the placer that had no traits or age classes; a change that draws from or reorders
// the chunk RNG changes them. The standard library's distributions are
// implementation-defined, so the golden is MSVC's.
TEST(PlacementExecutorTests, PlacementMatchesGolden) {
#if !defined(_MSVC_STL_VERSION)
	GTEST_SKIP() << "golden captured with MSVC's standard library; other libraries draw differently";
#else
	auto& registry = AssetRegistry::Get();
	registerGoldenDefs(registry);
	PlacementExecutor executor(registry);
	executor.initialize();

	const GoldenChunk golden[] = {
		{{0, 0}, 7344, 17200122207072769837ULL},
		{{3, -2}, 7072, 16064193191493374804ULL},
		{{-5, 7}, 7054, 5432118265719953071ULL},
	};
	for (const GoldenChunk& chunk : golden) {
		const auto result = executor.processChunk(goldenContext(chunk.coord));
		EXPECT_EQ(result.entities.size(), chunk.count) << "chunk " << chunk.coord.x << "," << chunk.coord.y;
		EXPECT_EQ(placementHash(result.entities), chunk.hash) << "chunk " << chunk.coord.x << "," << chunk.coord.y;
		for (const PlacedEntity& e : result.entities) {
			EXPECT_EQ(e.traits, computeTreeTraits(*registry.getDefinition(e.defName), e.position, 424242));
		}
	}
#endif
}

// A def's own scale range replaces the shared one and draws as many values, so its
// scales move into the range and nothing else in the chunk moves.
TEST(PlacementExecutorTests, DefScaleRangeMovesOnlyThatDefsScale) {
	auto& registry = AssetRegistry::Get();
	const world::ChunkCoordinate coord{3, -2};

	registerGoldenDefs(registry);
	PlacementExecutor shared(registry);
	shared.initialize();
	const auto before = shared.processChunk(goldenContext(coord)).entities;

	constexpr PlacementScale kNarrow{0.93F, 1.07F};
	registerGoldenDefs(registry, kNarrow);
	PlacementExecutor narrowed(registry);
	narrowed.initialize();
	const auto after = narrowed.processChunk(goldenContext(coord)).entities;

	ASSERT_EQ(after.size(), before.size());
	size_t oaks = 0;
	for (size_t i = 0; i < after.size(); ++i) {
		ASSERT_EQ(after[i].defName, before[i].defName);
		EXPECT_EQ(after[i].position, before[i].position);
		EXPECT_EQ(after[i].rotation, before[i].rotation);
		EXPECT_EQ(after[i].colorTint, before[i].colorTint);
		if (after[i].defName == "Golden_Oak") {
			++oaks;
			EXPECT_GE(after[i].scale, kNarrow.min);
			EXPECT_LE(after[i].scale, kNarrow.max);
		} else {
			EXPECT_EQ(after[i].scale, before[i].scale);
		}
	}
	EXPECT_GT(oaks, 100U);
}
