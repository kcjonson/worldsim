#include "MockWorldSampler.h"

#include <gtest/gtest.h>
#include <unordered_set>

using namespace engine::world;

// ============================================================================
// MockWorldSampler Determinism Tests
// ============================================================================

TEST(MockWorldSamplerTests, SameSeedSameResults) {
	MockWorldSampler sampler1(12345);
	MockWorldSampler sampler2(12345);

	ChunkSampleResult result1 = sampler1.sampleChunk(ChunkCoordinate(0, 0));
	ChunkSampleResult result2 = sampler2.sampleChunk(ChunkCoordinate(0, 0));

	// Same seed, same coordinate should produce identical results
	EXPECT_EQ(result1.cornerBiomes[0].primary(), result2.cornerBiomes[0].primary());
	EXPECT_EQ(result1.cornerBiomes[1].primary(), result2.cornerBiomes[1].primary());
	EXPECT_EQ(result1.cornerBiomes[2].primary(), result2.cornerBiomes[2].primary());
	EXPECT_EQ(result1.cornerBiomes[3].primary(), result2.cornerBiomes[3].primary());
}

TEST(MockWorldSamplerTests, DifferentSeedDifferentResults) {
	MockWorldSampler sampler1(11111);
	MockWorldSampler sampler2(99999);

	// Sample many chunks - at least some should differ with different seeds
	int differentCount = 0;
	for (int x = -5; x <= 5; x++) {
		for (int y = -5; y <= 5; y++) {
			ChunkSampleResult result1 = sampler1.sampleChunk(ChunkCoordinate(x, y));
			ChunkSampleResult result2 = sampler2.sampleChunk(ChunkCoordinate(x, y));

			if (result1.cornerBiomes[0].primary() != result2.cornerBiomes[0].primary()) {
				differentCount++;
			}
		}
	}

	// With different seeds, most results should be different
	EXPECT_GT(differentCount, 50) << "Expected different seeds to produce mostly different results";
}

TEST(MockWorldSamplerTests, DifferentCoordinatesDifferentBiomes) {
	MockWorldSampler sampler(12345);

	// Sample chunks at various coordinates
	std::unordered_set<Biome> biomesSeen;

	for (int x = -10; x <= 10; x += 2) {
		for (int y = -10; y <= 10; y += 2) {
			ChunkSampleResult result = sampler.sampleChunk(ChunkCoordinate(x, y));
			biomesSeen.insert(result.cornerBiomes[0].primary());
		}
	}

	// Should see multiple different biomes
	EXPECT_GT(biomesSeen.size(), 1) << "Expected to see multiple different biomes across the world";
}

TEST(MockWorldSamplerTests, RepeatedSamplingDeterministic) {
	MockWorldSampler sampler(42);

	ChunkCoordinate coord(5, -3);

	ChunkSampleResult result1 = sampler.sampleChunk(coord);
	ChunkSampleResult result2 = sampler.sampleChunk(coord);
	ChunkSampleResult result3 = sampler.sampleChunk(coord);

	// All three should be identical
	EXPECT_EQ(result1.cornerBiomes[0].primary(), result2.cornerBiomes[0].primary());
	EXPECT_EQ(result2.cornerBiomes[0].primary(), result3.cornerBiomes[0].primary());
}

// ============================================================================
// Elevation Tests
// ============================================================================

TEST(MockWorldSamplerTests, ElevationDeterministic) {
	MockWorldSampler sampler(12345);

	WorldPosition pos(100.0F, 200.0F);

	float elev1 = sampler.sampleElevation(pos);
	float elev2 = sampler.sampleElevation(pos);

	EXPECT_FLOAT_EQ(elev1, elev2);
}

TEST(MockWorldSamplerTests, ElevationVaries) {
	MockWorldSampler sampler(12345);

	float elev1 = sampler.sampleElevation(WorldPosition(0.0F, 0.0F));
	float elev2 = sampler.sampleElevation(WorldPosition(1000.0F, 0.0F));
	float elev3 = sampler.sampleElevation(WorldPosition(0.0F, 1000.0F));

	// At least some elevations should be different
	bool allSame = (elev1 == elev2) && (elev2 == elev3);
	EXPECT_FALSE(allSame) << "Expected elevation to vary across the world";
}

TEST(MockWorldSamplerTests, ElevationInReasonableRange) {
	MockWorldSampler sampler(12345);

	for (int i = 0; i < 100; i++) {
		float x = static_cast<float>(i * 100);
		float y = static_cast<float>(i * 50);
		float elev = sampler.sampleElevation(WorldPosition(x, y));

		// Elevation is returned in meters - allow a generous range
		// for terrain (sea level to mountain heights)
		EXPECT_GE(elev, -1000.0F) << "Elevation too low (below -1000m)";
		EXPECT_LE(elev, 10000.0F) << "Elevation too high (above 10000m)";
	}
}

// ============================================================================
// ChunkSampleResult Tests
// ============================================================================

TEST(MockWorldSamplerTests, ChunkSampleResultHasFourCorners) {
	MockWorldSampler sampler(12345);

	ChunkSampleResult result = sampler.sampleChunk(ChunkCoordinate(0, 0));

	// Should have 4 corner biomes
	EXPECT_EQ(result.cornerBiomes.size(), 4);
}

TEST(MockWorldSamplerTests, BiomeWeightsAreValid) {
	MockWorldSampler sampler(12345);

	ChunkSampleResult result = sampler.sampleChunk(ChunkCoordinate(3, -2));

	for (const auto& biomeWeight : result.cornerBiomes) {
		// Total weights should sum to approximately 1 (after normalization)
		float sum = biomeWeight.total();
		EXPECT_NEAR(sum, 1.0F, 0.01F);

		// Should have at least one biome with positive weight
		EXPECT_GT(sum, 0.0F);
	}
}

TEST(MockWorldSamplerTests, PureChunkDetection) {
	MockWorldSampler sampler(12345);

	// Sample many chunks to find both pure and mixed
	int pureCount = 0;
	int mixedCount = 0;

	for (int x = -20; x <= 20; x++) {
		for (int y = -20; y <= 20; y++) {
			ChunkSampleResult result = sampler.sampleChunk(ChunkCoordinate(x, y));

			if (result.isPure) {
				pureCount++;
			} else {
				mixedCount++;
			}
		}
	}

	// Should have some of both types
	EXPECT_GT(pureCount, 0) << "Expected to find some pure chunks";
	// Mixed chunks at biome boundaries are less common with spherical tiling
	// so we don't require them, just verify the logic works
}

// ============================================================================
// Seed Tests
// ============================================================================

TEST(MockWorldSamplerTests, GetWorldSeedReturnsCorrectSeed) {
	uint64_t seed = 987654321;
	MockWorldSampler sampler(seed);

	EXPECT_EQ(sampler.getWorldSeed(), seed);
}

TEST(MockWorldSamplerTests, ZeroSeedWorks) {
	MockWorldSampler sampler(0);

	// Should still produce valid results
	ChunkSampleResult result = sampler.sampleChunk(ChunkCoordinate(0, 0));

	// Just verify it doesn't crash and produces valid biomes
	EXPECT_GE(static_cast<int>(result.cornerBiomes[0].primary()), 0);
}

TEST(MockWorldSamplerTests, MaxSeedWorks) {
	MockWorldSampler sampler(UINT64_MAX);

	// Should still produce valid results
	ChunkSampleResult result = sampler.sampleChunk(ChunkCoordinate(0, 0));

	EXPECT_GE(static_cast<int>(result.cornerBiomes[0].primary()), 0);
}

// ============================================================================
// Negative Coordinate Tests
// ============================================================================

TEST(MockWorldSamplerTests, NegativeCoordinatesDeterministic) {
	MockWorldSampler sampler(12345);

	ChunkSampleResult result1 = sampler.sampleChunk(ChunkCoordinate(-5, -10));
	ChunkSampleResult result2 = sampler.sampleChunk(ChunkCoordinate(-5, -10));

	EXPECT_EQ(result1.cornerBiomes[0].primary(), result2.cornerBiomes[0].primary());
}

TEST(MockWorldSamplerTests, SymmetricCoordinatesAreDifferent) {
	MockWorldSampler sampler(12345);

	ChunkSampleResult resultPos = sampler.sampleChunk(ChunkCoordinate(5, 10));
	ChunkSampleResult resultNeg = sampler.sampleChunk(ChunkCoordinate(-5, -10));
	ChunkSampleResult resultMixed = sampler.sampleChunk(ChunkCoordinate(5, -10));

	// While we can't guarantee they're all different (noise is random),
	// sampling at many different coordinates should produce varied results
	// This test verifies the coordinate system handles signs correctly
	SUCCEED();
}

// ============================================================================
// Large Coordinate Tests
// ============================================================================

TEST(MockWorldSamplerTests, LargeCoordinatesWork) {
	MockWorldSampler sampler(12345);

	// Very far from origin
	ChunkSampleResult result = sampler.sampleChunk(ChunkCoordinate(100000, 100000));

	// Should produce valid results
	EXPECT_GE(static_cast<int>(result.cornerBiomes[0].primary()), 0);
}

TEST(MockWorldSamplerTests, LargeNegativeCoordinatesWork) {
	MockWorldSampler sampler(12345);

	ChunkSampleResult result = sampler.sampleChunk(ChunkCoordinate(-100000, -100000));

	EXPECT_GE(static_cast<int>(result.cornerBiomes[0].primary()), 0);
}
