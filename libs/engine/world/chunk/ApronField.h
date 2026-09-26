#pragma once

// ApronField - the ring of tiles surrounding a chunk (kApronTiles wide on every
// side), computed as if by the chunk's own neighbors so the terrain-polygon
// builder (a later task) sees the same tiles a neighbor chunk would compute for
// itself, right up to the chunk border (see
// docs/technical/organic-terrain/terrain-polygons-architecture.md D4).
//
// Built entirely from the generating chunk's own ChunkSampleResult (its
// neighborhood corner lattice plus its extended-AABB river/pond gather); it never
// waits on a neighbor Chunk existing. Apron tiles are raw Chunk::computeTileFrom
// output: no mud post-process, no adjacency (TilePostProcessor never runs on
// them).

#include "world/chunk/Chunk.h"
#include "world/chunk/ChunkCoordinate.h"
#include "world/chunk/ChunkSampleResult.h"

#include <array>
#include <cstdint>
#include <vector>

namespace engine::world {

/// The biome/elevation grids of a chunk's 3x3 neighborhood, each rebuilt from the
/// sample data's neighborhood corner lattice exactly as that neighbor builds its
/// own (D4), lazily, on first use. The chunk itself (0, 0) reads `sampleData`.
/// One per generation call: not thread-safe.
class NeighborhoodGrids {
  public:
	/// `sampleData` must carry the neighborhood corner lattice
	/// (fillNeighborhoodCorners) and outlive this.
	explicit NeighborhoodGrids(const ChunkSampleResult& sampleData);

	/// Grid of the neighbor at offset (dx, dy), each in [-1, 1].
	[[nodiscard]] const ChunkSampleResult& grid(int32_t dx, int32_t dy);

	/// Primary biome of world tile (tx, ty), the tile the owning chunk's own
	/// generate() computes there. (tx, ty) must lie in the neighborhood of `coord`.
	[[nodiscard]] Biome primaryBiomeAt(ChunkCoordinate coord, int64_t tx, int64_t ty);

  private:
	const ChunkSampleResult&	   m_sampleData;
	std::vector<ChunkSampleResult> m_grids; // on the heap: nine are ~330 KB, too much for a worker's stack
	std::array<bool, 9>			   m_built{};
};

/// The apron ring of tiles around a chunk: the kExtendedSize x kExtendedSize
/// extended region minus the chunk's own kChunkSize x kChunkSize square.
class ApronField {
  public:
	/// Build the apron for `coord` from `sampleData`, which must already carry the
	/// neighborhood corner lattice (fillNeighborhoodCorners) and the
	/// extended-AABB river/pond gather (D4). `sampleData` is normally the
	/// generating chunk's own ChunkSampleResult.
	[[nodiscard]] static ApronField build(ChunkCoordinate coord, const ChunkSampleResult& sampleData, uint64_t worldSeed);

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

} // namespace engine::world
