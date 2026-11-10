#pragma once

// Measures the trees in a world rect: count and density per def, canopy cover, and
// expected wood. The dev flora view (DevCommandHandler) is a thin caller; the maths
// is here so it is testable without a renderer or a running game.
//
// A tree is a placed static with isTallOccluder set, the renderer's own tall/short
// rule (kShortFloraMaxHeight). Canopy cover is the union of the trees' silhouettes
// rasterised on a kFloraCellSizeMeters grid, sampled at cell centres, so crowns
// that overlap are counted once.

#include "assets/placement/SpatialIndex.h" // PlacedEntity

#include <polygon/Polygon.h> // geometry::Ring

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace assets {

	struct AssetDefinition;

	// 0.1 m: a mature oak crown (1.6 m) spans 16 cells, so the cover of a 100 m square
	// moves by well under 0.1 points from where cell edges fall.
	inline constexpr float kFloraCellSizeMeters = 0.1F;

	// Largest rect measured in one call (500 m x 500 m, 25 million cells).
	inline constexpr double kFloraMaxRectAreaM2 = 250000.0;

	struct FloraRect {
		float minX = 0.0F;
		float minY = 0.0F;
		float maxX = 0.0F;
		float maxY = 0.0F;

		[[nodiscard]] double area() const { return static_cast<double>(maxX - minX) * static_cast<double>(maxY - minY); }
	};

	// What the measure needs from a def: its silhouette rings in the template-local
	// millimetre frame (AssetSilhouette::rings) and the wood one tree yields.
	struct FloraDefTraits {
		const std::vector<geometry::Ring>* rings = nullptr;
		double							   woodPerTree = 0.0;
	};

	struct FloraDefMeasure {
		std::string defName;
		uint32_t	count = 0;
		double		perThousandM2 = 0.0;
		double		woodPerTree = 0.0;
		double		woodPerThousandM2 = 0.0;
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
		std::vector<FloraDefMeasure> defs; // by defName
	};

	// The wood one tree of this def yields: the midpoint of its resource pool when it has
	// one, else of its per-harvest amount; 0 when it does not yield Wood.
	[[nodiscard]] double woodPerTree(const AssetDefinition& def);

	// Measures `entities` over `rect`. `loadedAreas` are the disjoint squares placement
	// holds data for (loaded chunks); only their overlap with the rect is measured.
	// Entities that are not tall occluders are skipped. A tree counts toward the def
	// totals when its position is inside the rect and a loaded area; every tree passed
	// adds its crown to the cover, so pass the trees near the rect, not only in it.
	// A def missing from `traits` counts with no crown and no wood.
	[[nodiscard]] FloraMeasurement measureFlora(
		const FloraRect&									   rect,
		const std::vector<FloraRect>&						   loadedAreas,
		const std::vector<const PlacedEntity*>&				   entities,
		const std::unordered_map<std::string, FloraDefTraits>& traits
	);

} // namespace assets
