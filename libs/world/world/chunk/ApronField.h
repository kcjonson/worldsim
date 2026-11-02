#pragma once

// ApronField - the ring of tiles surrounding a chunk (kApronTiles wide on every
// side), computed as if by the chunk's own neighbors so the terrain-polygon
// builder (a later task) sees the same tiles a neighbor chunk would compute for
// itself, right up to the chunk border (see
// docs/technical/organic-terrain/terrain-polygons-architecture.md D4).
//
// Built entirely from the generating chunk's own data (its neighborhood corner
// lattice, plus a hydrology-only result carrying its extended-AABB river/pond
// gather); it never waits on a neighbor Chunk existing. Apron tiles are raw
// Chunk::computeTileFrom output; a reader that needs their final surface (the
// render tiles, D16) runs TilePostProcessor::finalSurface on them.

#include "world/chunk/Chunk.h"
#include "world/chunk/ChunkCoordinate.h"
#include "world/chunk/ChunkSampleResult.h"
#include "world/chunk/TileBiomes.h"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace world {

/// A generating chunk's 3x3 neighborhood (D4): the biome and elevation of every
/// tile its generation reads, each chunk's computed as that chunk computes its
/// own, from the sample's corner lattice. The chunk itself (0, 0) reads the
/// sample's own corners. Each chunk's ChunkBiomes is built on first use. One per
/// generation call: not thread-safe.
class ChunkNeighborhood {
  public:
	/// `sample` must carry the neighborhood corner lattice
	/// (fillNeighborhoodCorners) and outlive this.
	ChunkNeighborhood(ChunkCoordinate coord, const ChunkSampleResult& sample, uint64_t worldSeed);

	[[nodiscard]] ChunkCoordinate coord() const { return center; }
	[[nodiscard]] uint64_t worldSeed() const { return seed; }

	/// The biomes of the chunk at offset (dx, dy), each in [-1, 1].
	[[nodiscard]] const ChunkBiomes& biomes(int32_t dx, int32_t dy);

	/// Elevation, meters, of local tile (localX, localY) of the chunk at offset
	/// (dx, dy): one plane a chunk through its corners.
	[[nodiscard]] float tileElevation(int32_t dx, int32_t dy, uint16_t localX, uint16_t localY) const;

	/// The biome of world tile (tx, ty), the one its owning chunk's own
	/// generate() computes there. (tx, ty) must lie in the neighborhood.
	[[nodiscard]] TileBiome tileBiomeAt(int64_t tx, int64_t ty);

	/// isBiomeWater (D5) of world tile (tx, ty), as tileBiomeAt, without the pick
	/// where the owning chunk weighs no water biome.
	[[nodiscard]] bool isBiomeWaterAt(int64_t tx, int64_t ty);

  private:
	struct Owner {
		int32_t	 dx = 0;
		int32_t	 dy = 0;
		uint16_t localX = 0;
		uint16_t localY = 0;
	};
	[[nodiscard]] Owner ownerOf(int64_t tx, int64_t ty) const;

	ChunkCoordinate						  center;
	const ChunkSampleResult&			  sample;
	uint64_t							  seed;
	std::array<std::optional<ChunkBiomes>, 9> chunks;
};

/// The apron ring of tiles around a chunk: the kExtendedSize x kExtendedSize
/// extended region minus the chunk's own kChunkSize x kChunkSize square.
class ApronField {
  public:
	/// Build the apron of the neighborhood's chunk, each tile's biome and
	/// elevation from the neighbor that owns it. `hydrology` supplies
	/// riverHalfWidthAt/pondDepthAt for every apron tile regardless of which
	/// neighbor conceptually owns it (river/pond geometry is world-position-based
	/// and doesn't care); it is normally the generating chunk's rasterHydrology()
	/// result, gathered over the extended AABB (D4).
	[[nodiscard]] static ApronField build(ChunkNeighborhood& neighborhood, const ChunkSampleResult& hydrology);

	/// Tile at extended-region coordinates (ex, ey), each in [0, kExtendedSize),
	/// excluding the interior [kApronTiles, kApronTiles + kChunkSize) square (that
	/// is the chunk's own tiles; use ExtendedTiles below to read across both).
	[[nodiscard]] const TileData& tileAt(int32_t ex, int32_t ey) const;

  private:
	// Four strips covering the ring without double-storing the corners: top and
	// bottom span the full extended width, left and right span only the chunk's
	// own height (their corners are already in top/bottom).
	std::vector<TileData> m_top;    // kExtendedSize x kApronTiles
	std::vector<TileData> m_bottom; // kExtendedSize x kApronTiles
	std::vector<TileData> m_left;   // kApronTiles x kChunkSize
	std::vector<TileData> m_right;  // kApronTiles x kChunkSize
};

/// Read-only view over a chunk's own tiles plus its apron, indexed by extended-
/// region coordinates (ex, ey) in [0, kExtendedSize) x [0, kExtendedSize). The
/// accessor TerrainPolygonBuilder (a later task) uses to sample the full
/// extended field without caring which store a given tile came from.
class ExtendedTiles {
  public:
	ExtendedTiles(const Chunk& chunk, const ApronField& apron) : m_chunk(chunk), m_apron(apron) {}

	[[nodiscard]] const TileData& at(int32_t ex, int32_t ey) const;

  private:
	const Chunk& m_chunk;
	const ApronField& m_apron;
};

} // namespace world
