#include "MockWorldSampler.h"

namespace engine::world {

	constexpr float kSphericalTileSize = 5000.0F;
	constexpr float kBlendDistance     = 500.0F;

	ChunkSampleResult MockWorldSampler::sampleChunk(ChunkCoordinate coord) const {
		ChunkSampleResult result;

		result.cornerBiomes[0] = sampleBiomeAt(coord.corner(ChunkCorner::NorthWest));
		result.cornerBiomes[1] = sampleBiomeAt(coord.corner(ChunkCorner::NorthEast));
		result.cornerBiomes[2] = sampleBiomeAt(coord.corner(ChunkCorner::SouthWest));
		result.cornerBiomes[3] = sampleBiomeAt(coord.corner(ChunkCorner::SouthEast));

		result.cornerElevations[0] = sampleElevation(coord.corner(ChunkCorner::NorthWest));
		result.cornerElevations[1] = sampleElevation(coord.corner(ChunkCorner::NorthEast));
		result.cornerElevations[2] = sampleElevation(coord.corner(ChunkCorner::SouthWest));
		result.cornerElevations[3] = sampleElevation(coord.corner(ChunkCorner::SouthEast));

		result.computeSectorGrid();
		return result;
	}

	float MockWorldSampler::sampleElevation(WorldPosition pos) const {
		constexpr float kElevationScale = 0.001F;
		constexpr float kMaxElevation   = 100.0F;
		float noise = fractalNoise(pos.x * kElevationScale, pos.y * kElevationScale, m_seed + 1, 4, 0.5F);
		return noise * kMaxElevation;
	}

	Biome MockWorldSampler::getSphericalTileBiome(int32_t tileX, int32_t tileY) const {
		float centerX = (static_cast<float>(tileX) + 0.5F) * kSphericalTileSize;
		float centerY = (static_cast<float>(tileY) + 0.5F) * kSphericalTileSize;

		constexpr float kNoiseScale = 1.0F / kSphericalTileSize;
		float moisture    = fractalNoise(centerX * kNoiseScale,        centerY * kNoiseScale,        m_seed + 100, 3, 0.5F);
		float temperature = fractalNoise(centerX * kNoiseScale * 0.7F, centerY * kNoiseScale * 0.7F, m_seed + 200, 2, 0.6F);
		float elevation   = sampleElevation({centerX, centerY}) / 100.0F;

		// Mapping from old 8-value logic to new 21-value taxonomy.
		// Thresholds are unchanged — only the returned enum value changes.
		if (elevation > 0.8F)                                return Biome::AlpineTundra;       // was Mountain
		if (moisture < 0.25F)  return (temperature > 0.6F)  ? Biome::HotDesert                // was Desert
		                                                     : Biome::ArcticTundra;            // was Tundra
		if (moisture > 0.75F)  return (elevation < 0.15F)   ? Biome::Ocean                    // was Ocean
		                                                     : Biome::TemperateWetland;        // was Wetland
		if (temperature > 0.5F && moisture > 0.4F)          return Biome::TemperateDeciduousForest; // was Forest
		if (elevation < 0.1F && moisture > 0.3F)            return Biome::Beach;              // was Beach
		return Biome::TemperateGrassland;                                                      // was Grassland
	}

	float MockWorldSampler::distanceToTileBoundary(WorldPosition pos) const {
		float localX = std::fmod(pos.x, kSphericalTileSize);
		float localY = std::fmod(pos.y, kSphericalTileSize);
		if (localX < 0) localX += kSphericalTileSize;
		if (localY < 0) localY += kSphericalTileSize;
		float distX = std::min(localX, kSphericalTileSize - localX);
		float distY = std::min(localY, kSphericalTileSize - localY);
		return std::min(distX, distY);
	}

	BiomeWeights MockWorldSampler::sampleBiomeAt(WorldPosition pos) const {
		auto tileX = static_cast<int32_t>(std::floor(pos.x / kSphericalTileSize));
		auto tileY = static_cast<int32_t>(std::floor(pos.y / kSphericalTileSize));
		Biome primaryBiome = getSphericalTileBiome(tileX, tileY);
		float distToBoundary = distanceToTileBoundary(pos);
		if (distToBoundary > kBlendDistance)
			return BiomeWeights::single(primaryBiome);
		// Slow path: pure biome for now (blending added in M4).
		return BiomeWeights::single(primaryBiome);
	}

	uint32_t MockWorldSampler::hash(int32_t x, int32_t y, uint64_t seed) {
		uint64_t h = seed;
		h ^= static_cast<uint64_t>(x) * 0x9E3779B97F4A7C15ULL;
		h ^= static_cast<uint64_t>(y) * 0xC6A4A7935BD1E995ULL;
		h ^= h >> 33;
		h *= 0xFF51AFD7ED558CCDULL;
		h ^= h >> 33;
		h *= 0xC4CEB9FE1A85EC53ULL;
		h ^= h >> 33;
		return static_cast<uint32_t>(h);
	}

	float MockWorldSampler::smoothstep(float t) {
		return t * t * (3.0F - 2.0F * t);
	}

	float MockWorldSampler::valueNoise(float x, float y, uint64_t seed) const {
		int32_t x0 = static_cast<int32_t>(std::floor(x));
		int32_t y0 = static_cast<int32_t>(std::floor(y));
		int32_t x1 = x0 + 1;
		int32_t y1 = y0 + 1;
		float fx = x - static_cast<float>(x0);
		float fy = y - static_cast<float>(y0);
		float sx = smoothstep(fx);
		float sy = smoothstep(fy);
		constexpr float kNormalize = 1.0F / static_cast<float>(UINT32_MAX);
		float n00 = static_cast<float>(hash(x0, y0, seed)) * kNormalize;
		float n10 = static_cast<float>(hash(x1, y0, seed)) * kNormalize;
		float n01 = static_cast<float>(hash(x0, y1, seed)) * kNormalize;
		float n11 = static_cast<float>(hash(x1, y1, seed)) * kNormalize;
		float nx0 = n00 * (1.0F - sx) + n10 * sx;
		float nx1 = n01 * (1.0F - sx) + n11 * sx;
		return nx0 * (1.0F - sy) + nx1 * sy;
	}

	float MockWorldSampler::fractalNoise(float x, float y, uint64_t seed, int octaves, float persistence) const {
		float total    = 0.0F;
		float amplitude = 1.0F;
		float frequency = 1.0F;
		float maxValue  = 0.0F;
		for (int i = 0; i < octaves; ++i) {
			total     += valueNoise(x * frequency, y * frequency, seed + static_cast<uint64_t>(i)) * amplitude;
			maxValue  += amplitude;
			amplitude *= persistence;
			frequency *= 2.0F;
		}
		return total / maxValue;
	}

} // namespace engine::world
