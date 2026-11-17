#pragma once

// Forest depth (sight-softening.md 4): the placed rings in a direction shorten how far an observer
// sees that way, as a smooth density down to a floor, taken at each side of the sight polygon. In
// integers throughout, with no trigonometry, so every toolchain gives the same ranges from the same
// input (ForestDepth.test.cpp pins them).

#include "../core/Vec2i64.h"
#include "SightPolygon.h"

#include <array>
#include <cstdint>
#include <span>

namespace geometry {

	// One whole in the integer fractions forest depth works in.
	constexpr std::int64_t kFractionOne = 65536;

	// Sight radii, shape radii, and so every offset from the eye stay below this, which 4.4's bounds
	// assume.
	constexpr std::int64_t kForestRadiusLimitMm = std::int64_t{1} << 16;

	// What forest depth reads of the sight tunables, named as sight-softening.md 9 names them, with
	// its start values. Each field must lie within its range there.
	struct ForestTunables {
		std::int64_t forestCapMm = 800;
		std::int64_t forestWindowSteps = 8;
		std::int64_t forestNearMm = 10000;
		std::int64_t forestFloorPercent = 67;
		std::int64_t forestSlopeMm = 1000;
		std::int64_t forestFullPerimeterMm = 3142;
		std::int64_t forestArrivalMs = 2000;
	};

	// A placed ring as forest depth reads it. `widthFactor` and `arrival` are fractions of
	// kFractionOne (forestWidthFactor, forestArrivalFraction).
	struct ForestBody {
		Vec2i64		 centre;
		std::int64_t widthFactor = kFractionOne;
		std::int64_t arrival = kFractionOne;
	};

	// One value a side of the sight polygon, side k running from vertex k to vertex k + 1.
	using SightSideValues = std::array<std::int64_t, kSightSides>;

	// min(1, perimeter / forestFullPerimeterMm) as a fraction.
	std::int64_t forestWidthFactor(std::int64_t perimeterMm, const ForestTunables& tunables);

	// How far a ring that arrived at `arrivedMs` has come into forest depth by `clockMs`, as a
	// fraction: 0 at its arrival, whole forestArrivalMs after it.
	std::int64_t forestArrivalFraction(std::int64_t clockMs, std::int64_t arrivedMs, const ForestTunables& tunables);

	// The least range on any side: sightMm x forestFloorPercent / 100.
	std::int64_t forestFloorMm(std::int64_t sightMm, const ForestTunables& tunables);

	// Each side's summed depth before the cap and the slope limit: every body with its centre within
	// `sightMm` of `eye` spends its capped, width-, distance-, and arrival-weighted depth over a tent
	// forestWindowSteps sides wide either side of its direction.
	SightSideValues
	forestRawDepth(const Vec2i64& eye, std::int64_t sightMm, std::span<const ForestBody> bodies, const ForestTunables& tunables);

	// Raw depth capped at sightMm less the floor, then slope limited so no side's depth exceeds a
	// neighbour's by more than forestSlopeMm.
	SightSideValues forestLimitedDepth(const SightSideValues& rawDepth, std::int64_t sightMm, const ForestTunables& tunables);

	// Each side's range: `radiusMm` (the sight radius, or a polygon path's reach) less its limited
	// depth.
	SightSideValues forestRanges(
		const Vec2i64&				eye,
		std::int64_t				sightMm,
		std::int64_t				radiusMm,
		std::span<const ForestBody> bodies,
		const ForestTunables&		tunables
	);

} // namespace geometry
