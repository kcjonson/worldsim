#include "Chunk.h"

#include "world/chunk/TileAdjacency.h"
#include "world/chunk/TilePostProcessor.h"
#include "world/generation/BiomeDispatcher.h"

#include <cmath>

namespace engine::world {

	Chunk::Chunk(ChunkCoordinate coord, ChunkSampleResult biomeData, uint64_t worldSeed)
		: m_coord(coord),
		  m_biomeData(std::move(biomeData)),
		  m_worldSeed(worldSeed),
		  m_tiles{},
		  m_generationComplete(false) {
		touch();
	}

	void Chunk::generate() {
		// Pre-compute all tiles in the chunk
		for (uint16_t y = 0; y < kChunkSize; ++y) {
			for (uint16_t x = 0; x < kChunkSize; ++x) {
				m_tiles[y * kChunkSize + x] = computeTile(x, y);
			}
		}

		// Post-process tiles: generate mud near water, compute adjacency
		TilePostProcessor::process(m_tiles, m_worldSeed);

		// Cache shore tiles (land tiles adjacent to water) for VisionSystem
		// This avoids iterating all tiles every frame during vision updates
		computeShoreTiles();

		// Pre-compute rendering data (adjacency masks, neighbors) for ChunkRenderer
		// This avoids per-frame extraction of adjacency data during rendering
		computeRenderData();

		// Mark generation complete (release semantics for thread safety)
		m_generationComplete.store(true, std::memory_order_release);
	}

	void Chunk::computeShoreTiles() {
		m_shoreTiles.clear();

		constexpr uint8_t kWaterSurfaceId = static_cast<uint8_t>(Surface::Water);

		for (uint16_t y = 0; y < kChunkSize; ++y) {
			for (uint16_t x = 0; x < kChunkSize; ++x) {
				const auto& tile = m_tiles[y * kChunkSize + x];

				// Skip water tiles - we want land tiles adjacent to water
				if (tile.surface == Surface::Water) {
					continue;
				}

				// Check if this land tile has water in any cardinal direction
				if (TileAdjacency::hasAdjacentSurface(tile.adjacency, kWaterSurfaceId)) {
					m_shoreTiles.emplace_back(x, y);
				}
			}
		}

		// Shrink to fit to minimize memory usage
		m_shoreTiles.shrink_to_fit();
	}

	void Chunk::computeRenderData() {
		for (uint16_t y = 0; y < kChunkSize; ++y) {
			for (uint16_t x = 0; x < kChunkSize; ++x) {
				size_t		idx = y * kChunkSize + x;
				const auto& tile = m_tiles[idx];
				auto&		render = m_renderData[idx];

				uint8_t surfaceId = static_cast<uint8_t>(tile.surface);
				render.surfaceId = surfaceId;

				// Pre-compute edge and corner masks
				render.edgeMask = TileAdjacency::getEdgeMaskByStack(tile.adjacency, surfaceId);
				render.cornerMask = TileAdjacency::getCornerMaskByStack(tile.adjacency, surfaceId);
				render.hardEdgeMask = TileAdjacency::getHardEdgeMaskByFamily(tile.adjacency, surfaceId);

				// Pre-extract all neighbor surface IDs
				render.neighborN = TileAdjacency::getNeighbor(tile.adjacency, TileAdjacency::N);
				render.neighborE = TileAdjacency::getNeighbor(tile.adjacency, TileAdjacency::E);
				render.neighborS = TileAdjacency::getNeighbor(tile.adjacency, TileAdjacency::S);
				render.neighborW = TileAdjacency::getNeighbor(tile.adjacency, TileAdjacency::W);
				render.neighborNW = TileAdjacency::getNeighbor(tile.adjacency, TileAdjacency::NW);
				render.neighborNE = TileAdjacency::getNeighbor(tile.adjacency, TileAdjacency::NE);
				render.neighborSE = TileAdjacency::getNeighbor(tile.adjacency, TileAdjacency::SE);
				render.neighborSW = TileAdjacency::getNeighbor(tile.adjacency, TileAdjacency::SW);
			}
		}
	}

	const TileData& Chunk::getTile(uint16_t localX, uint16_t localY) const {
		return m_tiles[localY * kChunkSize + localX];
	}

	void Chunk::setAdjacency(uint16_t localX, uint16_t localY, uint64_t adjacency) {
		m_tiles[localY * kChunkSize + localX].adjacency = adjacency;
	}

	TileData Chunk::computeTile(uint16_t localX, uint16_t localY) const {
		TileData tile;

		// Get biome weights from pre-computed sample data
		BiomeWeights biomeWeights = m_biomeData.getTileBiome(localX, localY);

		// Store primary and secondary biomes with blend weight
		tile.primaryBiome = biomeWeights.primary();
		tile.secondaryBiome = biomeWeights.secondary();

		// Convert float weight (0.0-1.0) to uint8_t (0-255)
		float primaryWeight = biomeWeights.primaryWeight();
		tile.biomeBlend = static_cast<uint8_t>(std::min(255.0F, primaryWeight * 255.0F));

		// Get elevation from interpolation (convert meters to centimeters, clamped to uint16_t)
		float elevMeters = m_biomeData.getTileElevation(localX, localY);
		float elevCm = elevMeters * 100.0F;
		tile.elevation = static_cast<uint16_t>(std::clamp(elevCm, 0.0F, 65535.0F));

		// Select surface type based on primary biome (uses spatial clustering)
		tile.surface = selectSurface(tile.primaryBiome, localX, localY);

		// Generate deterministic moisture from hash
		uint32_t		hash = tileHash(m_coord, localX, localY, m_worldSeed);
		constexpr float kNormalize = 1.0F / static_cast<float>(UINT32_MAX);
		float			moistureBase = static_cast<float>(hash) * kNormalize;

		// Adjust moisture based on biome
		if (tile.primaryBiome == Biome::Desert) {
			moistureBase *= 0.2F;
		} else if (tile.primaryBiome == Biome::Wetland || tile.primaryBiome == Biome::Ocean) {
			moistureBase = 0.8F + moistureBase * 0.2F;
		}

		// Convert to uint8_t (0-255)
		tile.moisture = static_cast<uint8_t>(std::min(255.0F, moistureBase * 255.0F));

		tile.attributes = 0; // Reserved for future use
		tile.adjacency = 0;	 // Computed by TilePostProcessor after all tiles generated

		return tile;
	}

	Surface Chunk::selectSurface(Biome biome, uint16_t localX, uint16_t localY) const {
		// Delegate to biome-specific generators via dispatcher
		generation::GenerationContext ctx{
			.chunkCoord = m_coord,
			.localX = localX,
			.localY = localY,
			.worldSeed = m_worldSeed,
			.biome = biome,
			.elevation = m_biomeData.getTileElevation(localX, localY)
		};

		return generation::BiomeDispatcher::generate(ctx).surface;
	}

	float Chunk::smoothstep(float t) {
		// Hermite interpolation: 3t² - 2t³
		return t * t * (3.0F - 2.0F * t);
	}

	float Chunk::valueNoise(float x, float y, uint64_t seed) const {
		// Get integer grid coordinates
		auto	x0 = static_cast<int32_t>(std::floor(x));
		auto	y0 = static_cast<int32_t>(std::floor(y));
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
		float			n00 = static_cast<float>(tileHash({x0, y0}, 0, 0, seed)) * kNormalize;
		float			n10 = static_cast<float>(tileHash({x1, y0}, 0, 0, seed)) * kNormalize;
		float			n01 = static_cast<float>(tileHash({x0, y1}, 0, 0, seed)) * kNormalize;
		float			n11 = static_cast<float>(tileHash({x1, y1}, 0, 0, seed)) * kNormalize;

		// Bilinear interpolation
		float nx0 = n00 * (1.0F - sx) + n10 * sx;
		float nx1 = n01 * (1.0F - sx) + n11 * sx;
		return nx0 * (1.0F - sy) + nx1 * sy;
	}

	float Chunk::fractalNoise(float x, float y, uint64_t seed, int octaves, float persistence) const {
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

	uint32_t Chunk::tileHash(ChunkCoordinate chunk, uint16_t localX, uint16_t localY, uint64_t seed) {
		// Combine all coordinates into a deterministic hash
		uint64_t h = seed;
		h ^= static_cast<uint64_t>(chunk.x) * 0x9E3779B97F4A7C15ULL;
		h ^= static_cast<uint64_t>(chunk.y) * 0xC6A4A7935BD1E995ULL;
		h ^= static_cast<uint64_t>(localX) * 0x85EBCA6B;
		h ^= static_cast<uint64_t>(localY) * 0xC2B2AE35;
		h ^= h >> 33;
		h *= 0xFF51AFD7ED558CCDULL;
		h ^= h >> 33;
		return static_cast<uint32_t>(h);
	}

	Foundation::Color Chunk::getBiomeColor(Biome biome) {
		switch (biome) {
			case Biome::Grassland:
				return Foundation::Color(0.29F, 0.49F, 0.25F, 1.0F); // #4a7c3f

			case Biome::Forest:
				return Foundation::Color(0.18F, 0.35F, 0.12F, 1.0F); // #2d5a1f

			case Biome::Desert:
				return Foundation::Color(0.82F, 0.71F, 0.47F, 1.0F); // #d1b578

			case Biome::Tundra:
				return Foundation::Color(0.75F, 0.78F, 0.80F, 1.0F); // #c0c7cc

			case Biome::Wetland:
				return Foundation::Color(0.25F, 0.42F, 0.35F, 1.0F); // #406b59

			case Biome::Mountain:
				return Foundation::Color(0.42F, 0.42F, 0.42F, 1.0F); // #6b6b6b

			case Biome::Beach:
				return Foundation::Color(0.77F, 0.64F, 0.35F, 1.0F); // #c4a35a

			case Biome::Ocean:
				return Foundation::Color(0.10F, 0.30F, 0.48F, 1.0F); // #1a4c7a

			default:
				return Foundation::Color(0.5F, 0.5F, 0.5F, 1.0F);
		}
	}

	Foundation::Color Chunk::getSurfaceColor(Surface surface) {
		switch (surface) {
			case Surface::Grass:
				return Foundation::Color(0.29F, 0.49F, 0.25F, 1.0F); // #4a7c3f

			case Surface::Dirt:
				return Foundation::Color(0.45F, 0.35F, 0.25F, 1.0F);

			case Surface::Sand:
				return Foundation::Color(0.82F, 0.71F, 0.47F, 1.0F);

			case Surface::Rock:
				return Foundation::Color(0.42F, 0.42F, 0.42F, 1.0F);

			case Surface::Water:
				return Foundation::Color(0.10F, 0.30F, 0.48F, 1.0F);

			case Surface::Snow:
				return Foundation::Color(0.95F, 0.97F, 1.0F, 1.0F);

			case Surface::Mud:
				return Foundation::Color(0.35F, 0.25F, 0.15F, 1.0F); // Darker brown than Dirt

			case Surface::GrassTall:
				return Foundation::Color(0.25F, 0.45F, 0.22F, 1.0F); // Slightly darker green

			case Surface::GrassShort:
				return Foundation::Color(0.35F, 0.45F, 0.28F, 1.0F); // Yellow-green, drier

			case Surface::GrassMeadow:
				return Foundation::Color(0.22F, 0.42F, 0.20F, 1.0F); // Lush deep green

			default:
				return Foundation::Color(0.5F, 0.5F, 0.5F, 1.0F);
		}
	}

} // namespace engine::world
