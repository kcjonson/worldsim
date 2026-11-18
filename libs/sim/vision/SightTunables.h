#pragma once

// The sight tunables (sight-softening.md 9), one runtime struct whose defaults are the start values.
// VisionSystem owns it and clamps every change with clampedToRanges; the placed occluder index, the
// seen record, and the cover read it from there. Each field is a slider on the Dev Tools tab.

#include <visibility/ForestDepth.h>

#include <cstdint>
#include <span>

namespace vision {

	// The most `lead` is taken as, so `reach` (sight plus lead) stays under 2^16 mm.
	constexpr std::int64_t kSightLeadMostMm = 8000;

	struct SightTunables {
		std::int64_t wallOpaqueLeastDiameterMm	 = 600;
		std::int64_t placedOpaqueLeastDiameterMm = 1400;
		std::int64_t softShadowRayWidthMm		 = 3000;
		std::int64_t softShadowStrengthPercent	 = 50;
		std::int64_t shadowEdgeWidthMm			 = 1200;
		std::int64_t shadowWideningMostDegrees	 = 10;
		std::int64_t shadowFinMostDegrees		 = 15;
		std::int64_t shadowCornerEaseMm			 = 1500;
		std::int64_t forestCapMm				 = 800;
		std::int64_t forestWindowSteps			 = 8;
		std::int64_t forestNearMm				 = 10000;
		std::int64_t forestFloorPercent			 = 67;
		std::int64_t forestSlopeMm				 = 1000;
		std::int64_t forestFullPerimeterMm		 = 3142;
		std::int64_t forestArrivalMs			 = 2000;
		std::int64_t sightFeatherMm				 = 1500;
		std::int64_t softLegToleranceMm			 = 500;
		std::int64_t rimEaseMm					 = 400;
		// Its start is the whole of `reach`; whoever reads it takes the least of it and the reach.
		std::int64_t drawnShadowReachMm	  = kDrawnShadowWholeReachMm;
		std::int64_t drawnShadowRebuildMm = 500;

		// A colonist's sight (kDefaultSightRadius, checked in SightTunables.cpp).
		static constexpr std::int64_t kColonistSightMm = 30000;
		// A colonist's sight plus kSightLeadMostMm: no reach is longer.
		static constexpr std::int64_t kDrawnShadowWholeReachMm = kColonistSightMm + kSightLeadMostMm;

		[[nodiscard]] geometry::ForestTunables forest() const;

		bool operator==(const SightTunables&) const = default;
	};

	// A field's name, as sight-softening.md 9 names it, and its fixed bounds.
	struct SightTunableRange {
		const char*				   name;
		std::int64_t SightTunables::* field;
		std::int64_t			   least;
		std::int64_t			   most;
	};

	// Every field with its fixed bounds, in the struct's order. forestNearMm's upper bound is a
	// colonist's sight (kDefaultSightRadius).
	std::span<const SightTunableRange> sightTunableRanges();

	// Whether the sim tunables, those the sight shape is built from (the kind thresholds and forest
	// depth's), are the same in both. The rest are the record's and the cover's.
	bool sameShapeTunables(const SightTunables& first, const SightTunables& second);

	// `tunables` with every field clamped to its fixed bounds, then softShadowRayWidthMm raised to at
	// least 1 mm more than the larger kind threshold, so a threshold slider pushes the ray width up
	// rather than being held down by it.
	SightTunables clampedToRanges(const SightTunables& tunables);

} // namespace vision
