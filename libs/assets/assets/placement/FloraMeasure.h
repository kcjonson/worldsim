#pragma once

// Measures the trees in a world rect: count and density per def, canopy cover, and
// expected wood. The dev flora view (DevCommandHandler) is a thin caller; the maths
// is here so it is testable without a renderer or a running game.
//
// A tree is a placed static with isTallOccluder set, the renderer's own tall/short
// rule (kShortFloraMaxHeight), whose def is in the kTreeGroup placement group. Other
// tall occluders (reeds, big rocks) are counted apart, outside the totals and the
// cover. Canopy cover is the union of the trees' silhouettes
// rasterised on a kFloraCellSizeMeters grid, sampled at cell centres, so crowns
// that overlap are counted once. Trees of a def with age classes are also counted
// per age class, from PlacedEntity::traits.

#include "assets/placement/SpatialIndex.h" // PlacedEntity

#include <polygon/Polygon.h> // geometry::Ring

#include <glm/vec2.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace assets {

	struct AssetDefinition;

	// 0.1 m: a mature oak crown (1.6 m) spans 16 cells, so the cover of a 100 m square
	// moves by well under 0.1 points from where cell edges fall.
	inline constexpr float kFloraCellSizeMeters = 0.1F;

	// The placement group every tree def declares.
	inline constexpr const char* kTreeGroup = "trees";

	// Largest rect measured in one call: each side at most 500 m, and at most 25 million
	// cells in all, counted as the grid counts them (floraAxisCells). A thin rect still
	// gets a cell per axis, so its area alone does not bound the grid.
	inline constexpr float	  kFloraMaxSpanMeters = 500.0F;
	inline constexpr int64_t kFloraMaxCells = 25000000;

	// Largest world coordinate (either axis, either sign) a flora query accepts: 2^24 m, where
	// a float stops holding whole metres, so the 1 m tile grid is no longer addressable. It is
	// 32,768 chunks out, far inside int32 for world::worldToChunk, and leaves room for the
	// crown padding and the near search half-size added to a query before it is gathered.
	inline constexpr float kFloraMaxCoordMeters = 16777216.0F;

	// Most trees one nearest-of-age query lists.
	inline constexpr size_t kFloraNearestMax = 20;

	struct FloraRect {
		float minX = 0.0F;
		float minY = 0.0F;
		float maxX = 0.0F;
		float maxY = 0.0F;

		[[nodiscard]] double area() const { return static_cast<double>(maxX - minX) * static_cast<double>(maxY - minY); }
	};

	// What the measure needs from a def: whether it is a tree (isTreeDef), its silhouette
	// rings in the template-local millimetre frame (AssetSilhouette::rings), the wood
	// one tree yields, and its age class names in index order (empty when it has none).
	struct FloraDefTraits {
		bool							   isTree = false;
		const std::vector<geometry::Ring>* rings = nullptr;
		double							   woodPerTree = 0.0;
		std::vector<std::string>		   ageClassNames;
	};

	struct FloraAgeMeasure {
		std::string name;
		uint32_t	count = 0;
		double		perThousandM2 = 0.0;
	};

	struct FloraDefMeasure {
		std::string defName;
		uint32_t	count = 0;
		double		perThousandM2 = 0.0;
		double		woodPerTree = 0.0;
		double		woodPerThousandM2 = 0.0;
		std::vector<FloraAgeMeasure> ageClasses; // one per age class of the def, in its order; empty when it has none
	};

	// One tree a nearest-of-age query found.
	struct FloraTreeSighting {
		std::string defName;
		glm::vec2	position{0.0F, 0.0F};
		std::string ageClass;
		uint16_t	variant = 0;
		double		distance = 0.0;
	};

	struct FloraMeasurement {
		FloraRect rect;
		double	  rectAreaM2 = 0.0;
		// The part of the rect inside loaded chunks; densities and cover are over this.
		double						 measuredAreaM2 = 0.0;
		uint32_t					 treeCount = 0;
		double						 treesPerThousandM2 = 0.0;
		double						 canopyCover = 0.0; // fraction of the measured area, 0 to 1
		double						 woodPerThousandM2 = 0.0;
		std::vector<FloraDefMeasure> defs;				 // trees, by defName
		std::vector<FloraDefMeasure> otherTallOccluders; // the rest, by defName; not in the totals or the cover
	};

	// The cells a grid spends on one axis of `spanMeters`: the ceil of span over the cell
	// size, and at least one. The cover grid and floraRectWithinLimits both count with this.
	[[nodiscard]] int64_t floraAxisCells(float spanMeters);

	// Whether the point is finite and within kFloraMaxCoordMeters of the origin on both axes.
	// Check it before converting a query position to a chunk.
	[[nodiscard]] bool floraPointWithinWorld(float x, float y);

	// Whether `rect` can be measured: both corners within the world (floraPointWithinWorld),
	// above zero on both axes (so not inverted), each side at most kFloraMaxSpanMeters, and
	// at most kFloraMaxCells cells in all. Check it before gathering anything for the rect.
	[[nodiscard]] bool floraRectWithinLimits(const FloraRect& rect);

	// Whether the def is in the kTreeGroup placement group.
	[[nodiscard]] bool isTreeDef(const AssetDefinition& def);

	// The wood one tree of this def yields: the midpoint of its resource pool when it has
	// one, else of its per-harvest amount; 0 when it does not yield Wood.
	[[nodiscard]] double woodPerTree(const AssetDefinition& def);

	// Measures `entities` over `rect`. `loadedAreas` are the disjoint squares placement
	// holds data for (loaded chunks); only their overlap with the rect is measured.
	// Entities that are not tall occluders are skipped. A tall occluder is counted when
	// its position is inside the rect and a loaded area, in `defs` when its def is a
	// tree and in `otherTallOccluders` otherwise. Every tree passed adds its crown to the
	// cover, so pass the trees near the rect, not only in it. A def missing from
	// `traits` is not a tree. A rect that fails floraRectWithinLimits is a precondition
	// violation: it allocates nothing and returns a default-constructed (empty)
	// measurement, never a truncated one.
	[[nodiscard]] FloraMeasurement measureFlora(
		const FloraRect&									   rect,
		const std::vector<FloraRect>&						   loadedAreas,
		const std::vector<const PlacedEntity*>&				   entities,
		const std::unordered_map<std::string, FloraDefTraits>& traits
	);

	// The trees among `entities` whose age class is named `ageClass`, nearest to `near` first,
	// at most `limit`. With `defName` set, only trees of that def. A tree is what measureFlora
	// counts as one.
	[[nodiscard]] std::vector<FloraTreeSighting> nearestTreesOfAge(
		glm::vec2											   near,
		const std::string&									   ageClass,
		const std::string&									   defName,
		size_t											   limit,
		const std::vector<const PlacedEntity*>&				   entities,
		const std::unordered_map<std::string, FloraDefTraits>& traits
	);

} // namespace assets
