#pragma once

// TerrainPolygonQuery - point queries over one chunk's terrain polygons
// (docs/technical/organic-terrain/terrain-polygons-architecture.md D1, D3, D11):
// how far a point is from water, the nearest point on the shore, and whether a
// point is in water.
//
// It reads the chunk's unclipped `rings`. They cover the extended region (chunk
// plus apron), and every ring edge within the lattice cell next to a border is
// identical in both chunks (D4, TerrainPolygonSeamsTest), so a chunk's own set
// answers for every point of the chunk and several meters past it without
// touching a neighbor.
//
// The shore is every ring edge but the synthetic (D4) and fordable-cut (D7)
// ones. Water is D9's classification with fordable channels included: even-odd
// over Waterline rings, union over Channel and Pond rings, synthetic edges
// counted (they close rings). Both use the edge index the builder stores with
// the polygons, so a query costs a few cells, not the whole ring set.

#include "world/chunk/TerrainPolygons.h"

#include <contour/ClipRing.h>
#include <core/Vec2i64.h>

#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

namespace engine::world {

class TerrainPolygonQuery {
  public:
	/// Index cell side: the pin lattice (TerrainPolygonBuilder::kPinLatticeMm).
	static constexpr int64_t kCellMm = 16000;
	static constexpr double kUnbounded = std::numeric_limits<double>::infinity();

	/// Bucket `rings` over `area` (the extended region they lie in).
	/// TerrainPolygonBuilder stores the result in ChunkTerrainPolygons::edgeIndex.
	[[nodiscard]] static TerrainEdgeIndex buildIndex(const std::vector<TerrainRing>& rings, const geometry::RectMm& area);

	explicit TerrainPolygonQuery(const ChunkTerrainPolygons& terrainPolygons)
		: polygons(terrainPolygons) {}

	/// Distance in mm from `point` to water: 0 in water, else to the nearest
	/// shore edge. Shore farther than `searchMm` is not looked for: a land point
	/// with none that close gets kUnbounded.
	[[nodiscard]] double distanceToWaterMm(const geometry::Vec2i64& point, double searchMm = kUnbounded) const;

	/// The point of the shore nearest `point`, rounded to the mm; nullopt when
	/// the rings have no shore edge. From a point in water it can land on a bank
	/// submerged in another water body (a channel running into a lake).
	[[nodiscard]] std::optional<geometry::Vec2i64> nearestShorePoint(const geometry::Vec2i64& point) const;

	/// Whether `point` is in water. Outside the indexed area it is not.
	[[nodiscard]] bool isInsideWater(const geometry::Vec2i64& point) const;

  private:
	struct ShoreHit {
		double distanceMm = 0.0;
		const TerrainEdgeIndex::Edge* edge = nullptr;
		double t = 0.0;
	};

	[[nodiscard]] std::optional<ShoreHit> nearestShore(const geometry::Vec2i64& point, double searchMm) const;

	const ChunkTerrainPolygons& polygons;
};

} // namespace engine::world
