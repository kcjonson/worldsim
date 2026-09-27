#pragma once

// TilePostProcessor - a chunk's final tile surfaces.
//
// Runs in Chunk::generate() once the terrain polygons are built. A tile's final
// surface (finalSurface) is its raw surface with point bars turned to Sand (D12)
// and ground near water to Mud (D11), a function of the raw surface, the world
// tile, the terrain polygons, and the world seed alone. So a tile within
// kRenderSurfaceReachTiles of a border, which the neighbor's render tiles read
// (D16), comes out the same from the neighbor's polygons as from its own chunk's
// (D14; see docs/technical/organic-terrain/terrain-polygons-architecture.md D11).

#include "world/chunk/ChunkCoordinate.h"

#include <array>
#include <cstdint>

namespace engine::world {

class TerrainPolygonQuery;
enum class Surface : uint8_t;
struct TileData;

class TilePostProcessor {
  public:
	struct FinalSurfaceArgs {
		Surface raw{};	 ///< the tile's Chunk::computeTileFrom surface (ExtendedTiles / ApronField read it)
		int64_t tileX = 0; ///< world tile index: the tile covers [tileX, tileX + 1) m x [tileY, tileY + 1) m
		int64_t tileY = 0;
		/// Polygons of the chunk whose extended region holds the tile: the owning
		/// chunk, or a neighbor reading it into its apron.
		const TerrainPolygonQuery* terrain = nullptr;
		uint64_t worldSeed = 0;
	};

	/// The tile's post-processed surface: Sand on a point bar (never a Water
	/// tile), else Mud with the probability of its distance-to-water band for
	/// Grass variants, Dirt, and Sand, else the raw surface.
	[[nodiscard]] static Surface finalSurface(const FinalSurfaceArgs& args);

	struct ProcessArgs {
		ChunkCoordinate coord;
		const TerrainPolygonQuery* terrain = nullptr; ///< the chunk's own polygons
		uint64_t worldSeed = 0;
	};

	/// Every tile of the chunk to its finalSurface.
	static void process(std::array<TileData, kChunkSize * kChunkSize>& tiles, const ProcessArgs& args);

	/// Mud by distance to water (D11): a tile whose center is within reachMm of
	/// water turns to Mud with `probability` (the first band that holds it).
	struct MudBand {
		double reachMm;
		double probability;
	};
	static constexpr std::array<MudBand, 3> kMudBands{{{1000.0, 0.95}, {2000.0, 0.80}, {3000.0, 0.65}}};

	/// Mixed into the world seed for the mud roll, so it is not the tile's
	/// moisture hash.
	static constexpr uint64_t kMudSalt = 0x4D55445F524F4C4CULL;
};

} // namespace engine::world
