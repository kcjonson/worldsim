#include "SightTunables.h"

#include <ecs/components/Memory.h>

#include <algorithm>
#include <array>

namespace vision {

	static_assert(SightTunables::kColonistSightMm == static_cast<std::int64_t>(ecs::kDefaultSightRadius * 1000.0F));
	static_assert(SightTunables::kDrawnShadowWholeReachMm < geometry::kForestRadiusLimitMm, "reach stays under 2^16 mm");

	namespace {
		constexpr std::array kRanges = {
			SightTunableRange{"wallOpaqueLeastDiameterMm", &SightTunables::wallOpaqueLeastDiameterMm, 100, 2000},
			SightTunableRange{"placedOpaqueLeastDiameterMm", &SightTunables::placedOpaqueLeastDiameterMm, 100, 2000},
			SightTunableRange{"softShadowRayWidthMm", &SightTunables::softShadowRayWidthMm, 101, 10000},
			SightTunableRange{"softShadowStrengthPercent", &SightTunables::softShadowStrengthPercent, 0, 100},
			SightTunableRange{"shadowEdgeWidthMm", &SightTunables::shadowEdgeWidthMm, 0, 5000},
			SightTunableRange{"shadowWideningMostDegrees", &SightTunables::shadowWideningMostDegrees, 0, 45},
			SightTunableRange{"shadowFinMostDegrees", &SightTunables::shadowFinMostDegrees, 0, 45},
			SightTunableRange{"shadowCornerEaseMm", &SightTunables::shadowCornerEaseMm, 100, 5000},
			SightTunableRange{"forestCapMm", &SightTunables::forestCapMm, 0, 5000},
			SightTunableRange{"forestWindowSteps", &SightTunables::forestWindowSteps, 1, 32},
			SightTunableRange{"forestNearMm", &SightTunables::forestNearMm, 1000, SightTunables::kColonistSightMm},
			SightTunableRange{"forestFloorPercent", &SightTunables::forestFloorPercent, 1, 100},
			SightTunableRange{"forestSlopeMm", &SightTunables::forestSlopeMm, 0, 10000},
			SightTunableRange{"forestFullPerimeterMm", &SightTunables::forestFullPerimeterMm, 100, 10000},
			SightTunableRange{"forestArrivalMs", &SightTunables::forestArrivalMs, 100, 10000},
			SightTunableRange{"sightFeatherMm", &SightTunables::sightFeatherMm, 100, 5000},
			SightTunableRange{"softLegToleranceMm", &SightTunables::softLegToleranceMm, 50, 5000},
			SightTunableRange{"rimEaseMm", &SightTunables::rimEaseMm, 100, 450},
			SightTunableRange{"drawnShadowReachMm", &SightTunables::drawnShadowReachMm, 0, SightTunables::kDrawnShadowWholeReachMm},
			SightTunableRange{"drawnShadowRebuildMm", &SightTunables::drawnShadowRebuildMm, 0, 1000},
		};
	} // namespace

	geometry::ForestTunables SightTunables::forest() const {
		return {
			.forestCapMm		   = forestCapMm,
			.forestWindowSteps	   = forestWindowSteps,
			.forestNearMm		   = forestNearMm,
			.forestFloorPercent	   = forestFloorPercent,
			.forestSlopeMm		   = forestSlopeMm,
			.forestFullPerimeterMm = forestFullPerimeterMm,
			.forestArrivalMs	   = forestArrivalMs,
		};
	}

	std::span<const SightTunableRange> sightTunableRanges() { return kRanges; }

	SightTunables clampedToRanges(const SightTunables& tunables) {
		SightTunables clamped = tunables;
		for (const SightTunableRange& range : kRanges) {
			clamped.*range.field = std::clamp(clamped.*range.field, range.least, range.most);
		}
		const std::int64_t largerThreshold = std::max(clamped.wallOpaqueLeastDiameterMm, clamped.placedOpaqueLeastDiameterMm);
		clamped.softShadowRayWidthMm	   = std::max(clamped.softShadowRayWidthMm, largerThreshold + 1);
		return clamped;
	}

} // namespace vision
