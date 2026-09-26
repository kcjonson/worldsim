#include "Chunk.h"

#include "world/chunk/ApronField.h"
#include "world/chunk/TerrainPolygonBuilder.h"
#include "world/chunk/TileAdjacency.h"
#include "world/chunk/TilePostProcessor.h"
#include "world/generation/BiomeDispatcher.h"

#include <algorithm>
#include <array>
#include <utility>
#include <vector>

namespace engine::world {

	namespace {
		// Cosmetic water depth kept as tile data (D1); the renderer paints depth from
		// the distance field instead. 255 = deepest.
		constexpr uint8_t kDeepWaterDepth = 255;

		// Map a channel's full width (meters) to a depth byte: narrow streams
		// shallow, wide rivers deep. A floor keeps even the thinnest stream
		// distinct from a shallow lake edge.
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
		// Pre-compute all tiles in the chunk. The tile raster (here and in the apron
		// below) reads only the segments near the chunk via this hydrology-only
		// result; the builder reads the whole gather off m_biomeData directly.
		const ChunkSampleResult raster = m_biomeData.rasterHydrology(m_coord);
		for (uint16_t y = 0; y < kChunkSize; ++y) {
			for (uint16_t x = 0; x < kChunkSize; ++x) {
				m_tiles[y * kChunkSize + x] = computeTile(x, y, raster);
			}
		}

		// Terrain polygon rings from the raw tiles plus the apron (D4, D11 order),
		// then their distance field (D10): before post-processing, so these tiles
		// match what a neighbor's apron computes for them. The apron is discarded
		// once the rings are built.
		{
			const ApronField	apron = ApronField::build(m_coord, m_biomeData, raster, m_worldSeed);
			const ExtendedTiles extended(*this, apron);
			NeighborhoodGrids	neighborhood(m_biomeData);
			setTerrainPolygons(TerrainPolygonBuilder::build(
				m_coord,
				m_worldSeed,
				[&extended](int32_t ex, int32_t ey) -> const TileData& { return extended.at(ex, ey); },
				[this, &neighborhood](int64_t tx, int64_t ty) { return isBiomeWater(neighborhood.primaryBiomeAt(m_coord, tx, ty)); },
				m_biomeData.riverSegments,
				m_biomeData.pondBlobs
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
		// Paint surfaces first: land paints as itself, a Water tile as its bed, the
		// surface of the nearest land tile in the chunk (multi-source BFS in scan
		// order, 8-connected so a bed follows a diagonal shore). Only the bed within
		// a tile or two of the shore is ever seen, where the smoothed waterline puts
		// land over a Water tile; beyond that the shader's water covers it.
		constexpr uint8_t kWater	 = static_cast<uint8_t>(Surface::Water);
		constexpr uint8_t kNoBed	 = 0xFF;
		constexpr size_t  kSide		 = static_cast<size_t>(kChunkSize);
		constexpr size_t  kTileCount = kSide * kSide;
		std::vector<uint32_t> frontier;
		for (size_t idx = 0; idx < kTileCount; ++idx) {
			const uint8_t surface		 = static_cast<uint8_t>(m_tiles[idx].surface);
			m_renderData[idx].surfaceId = surface == kWater ? kNoBed : surface;
			if (surface != kWater) {
				frontier.push_back(static_cast<uint32_t>(idx));
			}
		}
		for (size_t head = 0; head < frontier.size(); ++head) {
			const uint32_t idx	   = frontier[head];
			const int32_t  x	   = static_cast<int32_t>(idx % kSide);
			const int32_t  y	   = static_cast<int32_t>(idx / kSide);
			const uint8_t  surface = m_renderData[idx].surfaceId;
			for (int32_t dy = -1; dy <= 1; ++dy) {
				for (int32_t dx = -1; dx <= 1; ++dx) {
					const int32_t nx = x + dx;
					const int32_t ny = y + dy;
					if (nx < 0 || ny < 0 || nx >= kChunkSize || ny >= kChunkSize) {
						continue;
					}
					const size_t n = static_cast<size_t>(ny) * kSide + static_cast<size_t>(nx);
					if (m_renderData[n].surfaceId == kNoBed) {
						m_renderData[n].surfaceId = surface;
						frontier.push_back(static_cast<uint32_t>(n));
					}
				}
			}
		}
		// A chunk with no land at all: its bed is never seen, sand is as good as any.
		for (size_t idx = 0; idx < kTileCount; ++idx) {
			if (m_renderData[idx].surfaceId == kNoBed) {
				m_renderData[idx].surfaceId = static_cast<uint8_t>(Surface::Sand);
			}
		}

		for (uint16_t y = 0; y < kChunkSize; ++y) {
			for (uint16_t x = 0; x < kChunkSize; ++x) {
				setRenderAdjacency(x, y, m_tiles[static_cast<size_t>(y) * kSide + x].adjacency);
			}
		}
	}

	void Chunk::setRenderAdjacency(uint16_t localX, uint16_t localY, uint64_t adjacency) {
		constexpr uint8_t kWater = static_cast<uint8_t>(Surface::Water);
		static constexpr std::array<std::pair<int32_t, int32_t>, TileAdjacency::kDirectionCount> kOffsets{{
			{-1, -1}, // NW
			{-1, 0},  // W
			{-1, 1},  // SW
			{0, 1},	  // S
			{1, 1},	  // SE
			{1, 0},	  // E
			{1, -1},  // NE
			{0, -1},  // N
		}};

		constexpr size_t  kSide	 = static_cast<size_t>(kChunkSize);
		auto&		  render	= m_renderData[static_cast<size_t>(localY) * kSide + localX];
		const uint8_t surfaceId = render.surfaceId;
		uint64_t	  paint		= 0;
		for (int dir = 0; dir < TileAdjacency::kDirectionCount; ++dir) {
			const auto	  d	 = static_cast<TileAdjacency::Direction>(dir);
			const int32_t nx = static_cast<int32_t>(localX) + kOffsets[static_cast<size_t>(dir)].first;
			const int32_t ny = static_cast<int32_t>(localY) + kOffsets[static_cast<size_t>(dir)].second;
			uint8_t		  neighbor = TileAdjacency::getNeighbor(adjacency, d);
			if (nx >= 0 && ny >= 0 && nx < kChunkSize && ny < kChunkSize) {
				neighbor = m_renderData[static_cast<size_t>(ny) * kSide + static_cast<size_t>(nx)].surfaceId;
			} else if (neighbor == kWater) {
				neighbor = surfaceId;
			}
			TileAdjacency::setNeighbor(paint, d, neighbor);
		}

		render.edgeMask		= TileAdjacency::getEdgeMaskByStack(paint, surfaceId);
		render.cornerMask	= TileAdjacency::getCornerMaskByStack(paint, surfaceId);
		render.hardEdgeMask = TileAdjacency::getHardEdgeMaskByFamily(paint, surfaceId);
		render.neighborN	= TileAdjacency::getNeighbor(paint, TileAdjacency::N);
		render.neighborE	= TileAdjacency::getNeighbor(paint, TileAdjacency::E);
		render.neighborS	= TileAdjacency::getNeighbor(paint, TileAdjacency::S);
		render.neighborW	= TileAdjacency::getNeighbor(paint, TileAdjacency::W);
		render.neighborNW	= TileAdjacency::getNeighbor(paint, TileAdjacency::NW);
		render.neighborNE	= TileAdjacency::getNeighbor(paint, TileAdjacency::NE);
		render.neighborSE	= TileAdjacency::getNeighbor(paint, TileAdjacency::SE);
		render.neighborSW	= TileAdjacency::getNeighbor(paint, TileAdjacency::SW);
	}

	const TileData& Chunk::getTile(uint16_t localX, uint16_t localY) const {
		return m_tiles[localY * kChunkSize + localX];
	}

	void Chunk::setAdjacency(uint16_t localX, uint16_t localY, uint64_t adjacency) {
		m_tiles[localY * kChunkSize + localX].adjacency = adjacency;
		setRenderAdjacency(localX, localY, adjacency);
		m_renderDataVersion.fetch_add(1, std::memory_order_release);
	}

	TileData Chunk::computeTile(uint16_t localX, uint16_t localY, const ChunkSampleResult& hydrology) const {
		return computeTileFrom({
			.coord = m_coord,
			.localX = localX,
			.localY = localY,
			.biomeWeights = m_biomeData.getTileBiome(localX, localY),
			.elevationMeters = m_biomeData.getTileElevation(localX, localY),
			.hydrology = &hydrology,
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

		// Water depth byte (cosmetic tile data, not drawn). Biome water
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
		m_terrainDistanceField = TerrainDistanceField::bake(m_terrainPolygons, m_coord);
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
