#include "MockWorldSampler.h"

namespace engine::world {

	// Spherical tile size in meters (~5km per spec)
	// This creates large coherent biome regions
	constexpr float kSphericalTileSize = 5000.0F;

	// Blend distance at spherical tile boundaries (500m per spec)
	constexpr float kBlendDistance = 500.0F;

	ChunkSampleResult MockWorldSampler::sampleChunk(ChunkCoordinate coord) const {
		ChunkSampleResult result;

		// Sample biome at each corner
		result.cornerBiomes[0] = sampleBiomeAt(coord.corner(ChunkCorner::NorthWest));
		result.cornerBiomes[1] = sampleBiomeAt(coord.corner(ChunkCorner::NorthEast));
		result.cornerBiomes[2] = sampleBiomeAt(coord.corner(ChunkCorner::SouthWest));
		result.cornerBiomes[3] = sampleBiomeAt(coord.corner(ChunkCorner::SouthEast));

		// Sample elevation at each corner
		result.cornerElevations[0] = sampleElevation(coord.corner(ChunkCorner::NorthWest));
		result.cornerElevations[1] = sampleElevation(coord.corner(ChunkCorner::NorthEast));
		result.cornerElevations[2] = sampleElevation(coord.corner(ChunkCorner::SouthWest));
		result.cornerElevations[3] = sampleElevation(coord.corner(ChunkCorner::SouthEast));

		// Check if chunk is pure (all corners same primary biome)
		if (isChunkPure(result.cornerBiomes[0], result.cornerBiomes[1], result.cornerBiomes[2], result.cornerBiomes[3])) {
			Biome primary = result.cornerBiomes[0].primary();
			// Grassland and Forest biomes can have water ponds generated via noise
			// in Chunk::selectSurface(). Mark these chunks as NOT pure to ensure
			// per-tile rendering, otherwise water tiles won't be visible.
			if (primary == Biome::Grassland || primary == Biome::Forest) {
				result.isPure = false;
				result.computeSectorGrid();
			} else {
				result.isPure = true;
				result.singleBiome = primary;
			}
		} else {
			result.isPure = false;
			result.computeSectorGrid();
		}

		return result;
	}

	float MockWorldSampler::sampleElevation(WorldPosition pos) const {
		// Use fractal noise for natural-looking terrain
		// Scale is ~1km per noise unit for gentle rolling hills
		constexpr float kElevationScale = 0.001F; // 1 unit = 1km
		constexpr float kMaxElevation = 100.0F;	  // 100m max hill height

		float noise = fractalNoise(pos.x * kElevationScale, pos.y * kElevationScale, m_seed + 1, 4, 0.5F);

		return noise * kMaxElevation;
	}

	Biome MockWorldSampler::getSphericalTileBiome(int32_t tileX, int32_t tileY) const {
		// Sample noise at the CENTER of this spherical tile
		// All positions in this tile will share this biome
		float centerX = (static_cast<float>(tileX) + 0.5F) * kSphericalTileSize;
		float centerY = (static_cast<float>(tileY) + 0.5F) * kSphericalTileSize;

		// Use noise at tile center to determine biome characteristics
		// Scale chosen so each spherical tile has a distinct value
		constexpr float kNoiseScale = 1.0F / kSphericalTileSize;

		float moisture = fractalNoise(centerX * kNoiseScale, centerY * kNoiseScale, m_seed + 100, 3, 0.5F);
		float temperature = fractalNoise(centerX * kNoiseScale * 0.7F, centerY * kNoiseScale * 0.7F, m_seed + 200, 2, 0.6F);

		// Sample elevation at tile center (normalized to 0-1)
		float elevation = sampleElevation({centerX, centerY}) / 100.0F;

		// Determine biome based on moisture/temperature/elevation
		// This is the definitive biome for this entire spherical tile
		if (elevation > 0.8F) {
			return Biome::Mountain;
		}
		if (moisture < 0.25F) {
			return (temperature > 0.6F) ? Biome::Desert : Biome::Tundra;
		}
		if (moisture > 0.75F) {
			return (elevation < 0.15F) ? Biome::Ocean : Biome::Wetland;
		}
		if (temperature > 0.5F && moisture > 0.4F) {
			return Biome::Forest;
		}
		if (elevation < 0.1F && moisture > 0.3F) {
			return Biome::Beach;
		}
		return Biome::Grassland;
	}

	float MockWorldSampler::distanceToTileBoundary(WorldPosition pos) const {
		// Find which spherical tile this position is in
		float localX = std::fmod(pos.x, kSphericalTileSize);
		float localY = std::fmod(pos.y, kSphericalTileSize);

		// Handle negative coordinates
		if (localX < 0) {
			localX += kSphericalTileSize;
		}
		if (localY < 0) {
			localY += kSphericalTileSize;
		}

		// Distance to nearest boundary in each axis
		float distX = std::min(localX, kSphericalTileSize - localX);
		float distY = std::min(localY, kSphericalTileSize - localY);

		// Return minimum distance to any boundary
		return std::min(distX, distY);
	}

	BiomeWeights MockWorldSampler::sampleBiomeAt(WorldPosition pos) const {
		// 1. Find which spherical tile this position belongs to
		auto tileX = static_cast<int32_t>(std::floor(pos.x / kSphericalTileSize));
		auto tileY = static_cast<int32_t>(std::floor(pos.y / kSphericalTileSize));

		// 2. Get the definitive biome for this spherical tile
		Biome primaryBiome = getSphericalTileBiome(tileX, tileY);

		// 3. Check distance to nearest tile boundary
		float distToBoundary = distanceToTileBoundary(pos);

		// 4. Fast path: far from boundary, return pure biome (99% of cases per spec)
		if (distToBoundary > kBlendDistance) {
			return BiomeWeights::single(primaryBiome);
		}

		// 5. Slow path: near boundary, blend with neighbors
		//    For now, we still return pure biome - blending will be added later
		//    when BiomeWeights supports multiple biomes
		//    The important fix is that we're sampling at tile centers now
		return BiomeWeights::single(primaryBiome);
	}

	bool MockWorldSampler::isChunkPure(const BiomeWeights& nw, const BiomeWeights& ne, const BiomeWeights& sw, const BiomeWeights& se) {
		Biome primary = nw.primary();
		return ne.primary() == primary && sw.primary() == primary && se.primary() == primary;
	}

	uint32_t MockWorldSampler::hash(int32_t x, int32_t y, uint64_t seed) {
		// Simple but effective hash combining coordinates and seed
		// Based on xxHash-style mixing
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
		// Hermite interpolation: 3t² - 2t³
		return t * t * (3.0F - 2.0F * t);
	}

	float MockWorldSampler::valueNoise(float x, float y, uint64_t seed) const {
		// Get integer grid coordinates
		int32_t x0 = static_cast<int32_t>(std::floor(x));
		int32_t y0 = static_cast<int32_t>(std::floor(y));
		int32_t x1 = x0 + 1;
		int32_t y1 = y0 + 1;

		// Get fractional part
		float fx = x - static_cast<float>(x0);
		float fy = y - static_cast<float>(y0);

		// Apply smoothstep for smoother interpolation
		float sx = smoothstep(fx);
		float sy = smoothstep(fy);

		// Hash at each corner, normalized to [0, 1]
		constexpr float kNormalize = 1.0F / static_cast<float>(UINT32_MAX);
		float			n00 = static_cast<float>(hash(x0, y0, seed)) * kNormalize;
		float			n10 = static_cast<float>(hash(x1, y0, seed)) * kNormalize;
		float			n01 = static_cast<float>(hash(x0, y1, seed)) * kNormalize;
		float			n11 = static_cast<float>(hash(x1, y1, seed)) * kNormalize;

		// Bilinear interpolation
		float nx0 = n00 * (1.0F - sx) + n10 * sx;
		float nx1 = n01 * (1.0F - sx) + n11 * sx;
		return nx0 * (1.0F - sy) + nx1 * sy;
	}

	float MockWorldSampler::fractalNoise(float x, float y, uint64_t seed, int octaves, float persistence) const {
		float total = 0.0F;
		float amplitude = 1.0F;
		float frequency = 1.0F;
		float maxValue = 0.0F;

		for (int i = 0; i < octaves; ++i) {
			total += valueNoise(x * frequency, y * frequency, seed + static_cast<uint64_t>(i)) * amplitude;
			maxValue += amplitude;
			amplitude *= persistence;
			frequency *= 2.0F;
		}

		return total / maxValue; // Normalize to [0, 1]
	}

} // namespace engine::world
