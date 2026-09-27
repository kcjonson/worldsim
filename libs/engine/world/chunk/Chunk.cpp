#include "Chunk.h"

#include "world/chunk/ApronField.h"
#include "world/chunk/SurfaceField.h"
#include "world/chunk/TerrainPolygonBuilder.h"
#include "world/chunk/TerrainPolygonQuery.h"
#include "world/chunk/TileAdjacency.h"
#include "world/chunk/TilePostProcessor.h"
#include "world/generation/BiomeDispatcher.h"

#include <algorithm>
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
		// match what a neighbor's apron computes for them. The apron lives on to
		// fill the render tiles' apron below.
		const ApronField	apron = ApronField::build(m_coord, m_biomeData, raster, m_worldSeed);
		const ExtendedTiles extended(*this, apron);
		{
			NeighborhoodGrids neighborhood(m_biomeData);
			setTerrainPolygons(TerrainPolygonBuilder::build(
				m_coord,
				m_worldSeed,
				[&extended](int32_t ex, int32_t ey) -> const TileData& { return extended.at(ex, ey); },
				[this, &neighborhood](int64_t tx, int64_t ty) { return isBiomeWater(neighborhood.primaryBiomeAt(m_coord, tx, ty)); },
				m_biomeData.riverSegments,
				m_biomeData.pondBlobs
			));
		}

		// Final surfaces, point-bar sand then mud by distance to water (D11, D12),
		// then adjacency. The shore points came with the polygons.
		const TerrainPolygonQuery terrain(m_terrainPolygons);
		TilePostProcessor::process(m_tiles, {.coord = m_coord, .terrain = &terrain, .worldSeed = m_worldSeed});

		computeRenderData(extended, terrain);

		m_renderDataVersion.fetch_add(1, std::memory_order_release);

		// Mark generation complete (release semantics for thread safety)
		m_generationComplete.store(true, std::memory_order_release);
	}

	void Chunk::computeRenderData(const ExtendedTiles& extended, const TerrainPolygonQuery& terrain) {
		// buildRenderTiles reads paint surfaces over the render window grown by the
		// interior reach, and paintSurfaces reads tiles one bed reach further; all of
		// it lies in the apron. The apron's raw tiles get the final surface their own
		// chunk gives them (D11), so they are the neighbors' own tiles.
		constexpr int32_t kPaintMargin	 = kRenderApronTiles + kInteriorReachTiles;
		constexpr int32_t kSurfaceMargin = kRenderSurfaceReachTiles;
		static_assert(kSurfaceMargin <= kApronTiles, "the render tiles must be built from the apron");
		constexpr int32_t kPaintSide   = kChunkSize + 2 * kPaintMargin;
		constexpr int32_t kSurfaceSide = kChunkSize + 2 * kSurfaceMargin;

		const int64_t		 originX = static_cast<int64_t>(m_coord.x) * kChunkSize - kSurfaceMargin;
		const int64_t		 originY = static_cast<int64_t>(m_coord.y) * kChunkSize - kSurfaceMargin;
		std::vector<uint8_t> surfaces(static_cast<size_t>(kSurfaceSide) * kSurfaceSide);
		for (int32_t y = 0; y < kSurfaceSide; ++y) {
			for (int32_t x = 0; x < kSurfaceSide; ++x) {
				const int32_t	ex	 = kApronTiles - kSurfaceMargin + x;
				const int32_t	ey	 = kApronTiles - kSurfaceMargin + y;
				const TileData& tile = extended.at(ex, ey);
				const bool		own	 = ex >= kApronTiles && ey >= kApronTiles && ex < kApronTiles + kChunkSize && ey < kApronTiles + kChunkSize;
				const Surface	surface =
					own ? tile.surface
						: TilePostProcessor::finalSurface(
							  {.raw = tile.surface, .tileX = originX + x, .tileY = originY + y, .terrain = &terrain, .worldSeed = m_worldSeed}
						  );
				surfaces[static_cast<size_t>(y) * kSurfaceSide + static_cast<size_t>(x)] = static_cast<uint8_t>(surface);
			}
		}
		const std::vector<uint8_t>		  paint = paintSurfaces(surfaces, kPaintSide, kPaintSide);
		const std::vector<TileRenderData> tiles = buildRenderTiles(paint, kRenderTilesSide, kRenderTilesSide);
		std::copy(tiles.begin(), tiles.end(), m_renderData.begin());
	}

	const TileData& Chunk::getTile(uint16_t localX, uint16_t localY) const {
		return m_tiles[localY * kChunkSize + localX];
	}

	void Chunk::setAdjacency(uint16_t localX, uint16_t localY, uint64_t adjacency) {
		m_tiles[localY * kChunkSize + localX].adjacency = adjacency;
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
