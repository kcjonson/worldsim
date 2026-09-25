#include "Chunk.h"

#include "world/chunk/ApronField.h"
#include "world/chunk/TerrainPolygonBuilder.h"
#include "world/chunk/TileAdjacency.h"
#include "world/chunk/TilePostProcessor.h"
#include "world/generation/BiomeDispatcher.h"

#include <algorithm>

namespace engine::world {

	namespace {
		// Rendered water depth (cosmetic) packed into TileData::attributes and read
		// by the tile shader. 255 = deepest.
		constexpr uint8_t kDeepWaterDepth = 255;

		// Map a channel's full width (meters) to a depth byte: narrow streams read
		// shallow (light), wide rivers deep. A floor keeps even the thinnest stream
		// visually distinct from a shallow lake edge.
		uint8_t waterDepthFromWidth(float fullWidthMeters) {
			constexpr float kShallowAt = 1.5F;   // <= this is fully shallow (a trickle)
			constexpr float kDeepAt = 14.0F;     // >= this reads fully deep; keeps stream-vs-river contrast
			constexpr float kMinDepth = 80.0F;   // shallowest stream still distinct from land
			float t = std::clamp((fullWidthMeters - kShallowAt) / (kDeepAt - kShallowAt), 0.0F, 1.0F);
			t = t * t * (3.0F - 2.0F * t); // smoothstep
			float d = kMinDepth + t * (255.0F - kMinDepth);
			return static_cast<uint8_t>(std::clamp(d, 0.0F, 255.0F));
		}
	} // namespace

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

		// Terrain polygon rings from the raw tiles plus the apron (D4, D11 order):
		// before post-processing, so these tiles match what a neighbor's apron
		// computes for them. The apron is discarded once the rings are built.
		{
			const ApronField	apron = ApronField::build(m_coord, m_biomeData, m_worldSeed);
			const ExtendedTiles extended(*this, apron);
			setTerrainPolygons(TerrainPolygonBuilder::build(
				m_coord, m_worldSeed, [&extended](int32_t ex, int32_t ey) -> const TileData& { return extended.at(ex, ey); }
			));
		}

		// Post-process tiles: generate mud near water, compute adjacency
		TilePostProcessor::process(m_tiles, m_worldSeed);

		// Cache shore tiles (land tiles adjacent to water) for VisionSystem
		// This avoids iterating all tiles every frame during vision updates
		computeShoreTiles();

		// Pre-compute rendering data (adjacency masks, neighbors) for ChunkRenderer
		// This avoids per-frame extraction of adjacency data during rendering
		computeRenderData();

		m_renderDataVersion.fetch_add(1, std::memory_order_release);

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

		// Shrink to fit to minimize memory usage. Note: this is a non-binding request
		// but in practice chunks typically have 50-200 shore tiles (~400-1600 bytes),
		// so the potential excess capacity per chunk is small and bounded.
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
				render.waterDepth = tile.waterDepth; // cosmetic depth for the water shader

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
		size_t idx = localY * kChunkSize + localX;
		m_tiles[idx].adjacency = adjacency;

		// Update pre-computed render data to match new adjacency
		const auto& tile = m_tiles[idx];
		auto&		render = m_renderData[idx];
		uint8_t		surfaceId = static_cast<uint8_t>(tile.surface);

		render.surfaceId = surfaceId;
		render.waterDepth = tile.waterDepth; // keep cosmetic depth in sync
		render.edgeMask = TileAdjacency::getEdgeMaskByStack(adjacency, surfaceId);
		render.cornerMask = TileAdjacency::getCornerMaskByStack(adjacency, surfaceId);
		render.hardEdgeMask = TileAdjacency::getHardEdgeMaskByFamily(adjacency, surfaceId);
		render.neighborN = TileAdjacency::getNeighbor(adjacency, TileAdjacency::N);
		render.neighborE = TileAdjacency::getNeighbor(adjacency, TileAdjacency::E);
		render.neighborS = TileAdjacency::getNeighbor(adjacency, TileAdjacency::S);
		render.neighborW = TileAdjacency::getNeighbor(adjacency, TileAdjacency::W);
		render.neighborNW = TileAdjacency::getNeighbor(adjacency, TileAdjacency::NW);
		render.neighborNE = TileAdjacency::getNeighbor(adjacency, TileAdjacency::NE);
		render.neighborSE = TileAdjacency::getNeighbor(adjacency, TileAdjacency::SE);
		render.neighborSW = TileAdjacency::getNeighbor(adjacency, TileAdjacency::SW);

		m_renderDataVersion.fetch_add(1, std::memory_order_release);
	}

	TileData Chunk::computeTile(uint16_t localX, uint16_t localY) const {
		return computeTileFrom({
			.coord = m_coord,
			.localX = localX,
			.localY = localY,
			.biomeWeights = m_biomeData.getTileBiome(localX, localY),
			.elevationMeters = m_biomeData.getTileElevation(localX, localY),
			.hydrology = &m_biomeData,
			.worldSeed = m_worldSeed,
		});
	}

	TileData Chunk::computeTileFrom(const TileComputeArgs& args) {
		TileData tile;

		// Store primary and secondary biomes with blend weight
		tile.primaryBiome = args.biomeWeights.primary();
		tile.secondaryBiome = args.biomeWeights.secondary();

		// Convert float weight (0.0-1.0) to uint8_t (0-255)
		float primaryWeight = args.biomeWeights.primaryWeight();
		tile.biomeBlend = static_cast<uint8_t>(std::min(255.0F, primaryWeight * 255.0F));

		// Elevation in meters -> centimeters, clamped to uint16_t
		float elevCm = args.elevationMeters * 100.0F;
		tile.elevation = static_cast<uint16_t>(std::clamp(elevCm, 0.0F, 65535.0F));

		// Select surface type based on primary biome (uses spatial clustering)
		tile.surface = selectSurfaceFor(args.coord, tile.primaryBiome, args.localX, args.localY, args.elevationMeters, args.worldSeed);

		// Water depth byte (cosmetic; the shader tints water by it). Biome water
		// (ocean/lake/wetland) reads deep; river channels set depth from their width
		// below so streams render shallow and trunks deep.
		uint8_t depth = (tile.surface == Surface::Water) ? kDeepWaterDepth : 0;

		// World position of this tile (meters), shared by the water overrides.
		const WorldPosition origin = args.coord.origin();
		const double worldXMeters = static_cast<double>(origin.x) + static_cast<double>(args.localX) * static_cast<double>(kTileSize);
		const double worldYMeters = static_cast<double>(origin.y) + static_cast<double>(args.localY) * static_cast<double>(kTileSize);

		// River channels from the coarse 3D drainage graph override the biome
		// surface. Continuous across chunk seams: the channel geometry is a
		// deterministic function of world position, gathered per chunk (extended by
		// the apron, so apron tiles see the same channels a neighbor chunk would).
		if (args.hydrology != nullptr && !args.hydrology->riverSegments.empty()) {
			const float halfWidth = args.hydrology->riverHalfWidthAt(worldXMeters, worldYMeters);
			if (halfWidth > 0.0F) {
				tile.surface = Surface::Water;
				depth = waterDepthFromWidth(2.0F * halfWidth);
			}
		}

		// Sparse hydrology-driven ponds (and desert oases) turn land to water, after
		// rivers so a channel crossing a pond cell keeps its river; existing water
		// (river/ocean/lake) is left untouched.
		if (args.hydrology != nullptr && !args.hydrology->pondBlobs.empty() && tile.surface != Surface::Water) {
			const uint8_t pondDepth = args.hydrology->pondDepthAt(worldXMeters, worldYMeters);
			if (pondDepth > 0) {
				tile.surface = Surface::Water;
				depth = pondDepth;
			}
		}

		// Generate deterministic moisture from hash
		uint32_t		hash = tileHash(args.coord, args.localX, args.localY, args.worldSeed);
		constexpr float kNormalize = 1.0F / static_cast<float>(UINT32_MAX);
		float			moistureBase = static_cast<float>(hash) * kNormalize;

		// Adjust moisture based on biome
		if (tile.primaryBiome == Biome::HotDesert || tile.primaryBiome == Biome::ColdDesert ||
		    tile.primaryBiome == Biome::SemiDesert || tile.primaryBiome == Biome::XericShrubland ||
		    tile.primaryBiome == Biome::PolarDesert) {
			moistureBase *= 0.2F;
		} else if (tile.primaryBiome == Biome::TemperateWetland || tile.primaryBiome == Biome::TropicalWetland ||
		           tile.primaryBiome == Biome::Ocean || tile.primaryBiome == Biome::Lake) {
			moistureBase = 0.8F + moistureBase * 0.2F;
		}

		// Convert to uint8_t (0-255)
		tile.moisture = static_cast<uint8_t>(std::min(255.0F, moistureBase * 255.0F));

		tile.waterDepth = depth;
		tile.adjacency = 0;	 // Computed by TilePostProcessor after all tiles generated

		return tile;
	}

	Surface Chunk::selectSurfaceFor(ChunkCoordinate coord, Biome biome, uint16_t localX, uint16_t localY,
	                                 float elevationMeters, uint64_t worldSeed) {
		// Delegate to biome-specific generators via dispatcher
		generation::GenerationContext ctx{
			.chunkCoord = coord,
			.localX = localX,
			.localY = localY,
			.worldSeed = worldSeed,
			.biome = biome,
			.elevation = elevationMeters
		};

		return generation::BiomeDispatcher::generate(ctx).surface;
	}

	void Chunk::setTerrainPolygons(ChunkTerrainPolygons polygons) {
		polygons.version = m_terrainPolygons.version + 1;
		m_terrainPolygons = std::move(polygons);
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
			case Biome::TemperateGrassland:
			case Biome::TropicalSavanna:
			case Biome::AlpineGrassland:
				return Foundation::Color(0.29F, 0.49F, 0.25F, 1.0F);

			case Biome::TemperateDeciduousForest:
			case Biome::TropicalRainforest:
			case Biome::TropicalSeasonalForest:
			case Biome::TemperateRainforest:
			case Biome::BorealForest:
			case Biome::MontaneForest:
				return Foundation::Color(0.18F, 0.35F, 0.12F, 1.0F);

			case Biome::HotDesert:
			case Biome::ColdDesert:
			case Biome::SemiDesert:
			case Biome::XericShrubland:
				return Foundation::Color(0.82F, 0.71F, 0.47F, 1.0F);

			case Biome::ArcticTundra:
			case Biome::AlpineTundra:
			case Biome::PolarDesert:
				return Foundation::Color(0.75F, 0.78F, 0.80F, 1.0F);

			case Biome::TemperateWetland:
			case Biome::TropicalWetland:
				return Foundation::Color(0.25F, 0.42F, 0.35F, 1.0F);

			case Biome::Beach:
				return Foundation::Color(0.77F, 0.64F, 0.35F, 1.0F);

			case Biome::Ocean:
			case Biome::Lake:
				return Foundation::Color(0.10F, 0.30F, 0.48F, 1.0F);

			case Biome::Count:
				return Foundation::Color(0.5F, 0.5F, 0.5F, 1.0F);
		}
		return Foundation::Color(0.5F, 0.5F, 0.5F, 1.0F);
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
