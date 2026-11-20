#include "ForestDepth.h"

#include <algorithm>
#include <cassert>
#include <cstdlib>

namespace geometry {

	namespace {

		constexpr std::int64_t kSides = static_cast<std::int64_t>(kSightSides);

		// A table direction at scale 2^30 crossed with an offset under 2^16 mm stays under 2^47, and
		// the place along a step, that cross product times kFractionOne, under 2^63.
		static_assert(kSightDirectionScale == std::int64_t{1} << 30 && kForestRadiusLimitMm <= std::int64_t{1} << 16);
		static_assert(kFractionOne == std::int64_t{1} << 16);

		std::int64_t crossWithDirection(std::size_t index, const Vec2i64& offset) {
			const auto& [cosine, sine] = kSightDirections[index % kSightSides];
			return cosine * offset.y - sine * offset.x;
		}

		// The offset's direction as a place in the table: index x kFractionOne + the fraction of the
		// step to the next direction, `index` the direction at or before the offset's. Increases
		// counter-clockwise from +x through [0, kSightSides x kFractionOne).
		std::int64_t tablePosition(const Vec2i64& offset) {
			const std::size_t  low	  = sightDirectionAtOrBefore(offset);
			const std::int64_t before = crossWithDirection(low, offset);
			const std::int64_t after = -crossWithDirection(low + 1, offset);
			assert(before >= 0 && after > 0);
			return static_cast<std::int64_t>(low) * kFractionOne + before * kFractionOne / (before + after);
		}

		// min(1, d^2 / near^2) x (1 - d^2 / sight^2) as a fraction, d the distance to the centre;
		// 0 at and past the sight radius.
		std::int64_t distanceWeight(std::int64_t distanceSquared, std::int64_t sightMm, std::int64_t nearMm) {
			const std::int64_t sightSquared = sightMm * sightMm;
			if (distanceSquared >= sightSquared) {
				return 0;
			}
			const std::int64_t rise = std::min(kFractionOne, distanceSquared * kFractionOne / (nearMm * nearMm));
			const std::int64_t fall = (sightSquared - distanceSquared) * kFractionOne / sightSquared;
			return rise * fall / kFractionOne;
		}

		struct TunableRange {
			std::int64_t ForestTunables::* field;
			std::int64_t				   least;
			std::int64_t				   most;
		};

		// Each tunable's range, sight-softening.md 9.
		std::array<TunableRange, 7> tunableRanges(std::int64_t sightMm) {
			return {{
				{&ForestTunables::forestCapMm, 0, 5000},
				{&ForestTunables::forestWindowSteps, 1, 32},
				{&ForestTunables::forestNearMm, 1000, std::max<std::int64_t>(sightMm, 1000)},
				{&ForestTunables::forestFloorPercent, 1, 100},
				{&ForestTunables::forestSlopeMm, 0, 10000},
				{&ForestTunables::forestFullPerimeterMm, 100, 10000},
				{&ForestTunables::forestArrivalMs, 100, 10000},
			}};
		}

		void assertWithinRanges(std::int64_t sightMm, [[maybe_unused]] const ForestTunables& tunables) {
			assert(sightMm > 0 && sightMm < kForestRadiusLimitMm);
			for ([[maybe_unused]] const TunableRange& range : tunableRanges(sightMm)) {
				assert(tunables.*range.field >= range.least && tunables.*range.field <= range.most);
			}
		}

	} // namespace

	std::size_t sightDirectionAtOrBefore(const Vec2i64& offset) {
		std::size_t low	 = inUpperHalfTurn(offset) ? 0 : kSightSides / 2;
		std::size_t high = low + kSightSides / 2 - 1;
		while (low < high) {
			const std::size_t middle = (low + high + 1) / 2;
			if (crossWithDirection(middle, offset) >= 0) {
				low = middle;
			} else {
				high = middle - 1;
			}
		}
		return low;
	}

	ForestTunables clampedToRanges(const ForestTunables& tunables, std::int64_t sightMm) {
		ForestTunables clamped = tunables;
		for (const TunableRange& range : tunableRanges(sightMm)) {
			clamped.*range.field = std::clamp(clamped.*range.field, range.least, range.most);
		}
		return clamped;
	}

	std::int64_t forestWidthFactor(std::int64_t perimeterMm, const ForestTunables& tunables) {
		assert(perimeterMm >= 0);
		if (perimeterMm >= tunables.forestFullPerimeterMm) {
			return kFractionOne;
		}
		return perimeterMm * kFractionOne / tunables.forestFullPerimeterMm;
	}

	std::int64_t forestArrivalFraction(std::int64_t clockMs, std::int64_t arrivedMs, const ForestTunables& tunables) {
		const std::int64_t elapsedMs = clockMs - arrivedMs;
		if (elapsedMs <= 0) {
			return 0;
		}
		if (elapsedMs >= tunables.forestArrivalMs) {
			return kFractionOne;
		}
		return elapsedMs * kFractionOne / tunables.forestArrivalMs;
	}

	std::int64_t forestFloorMm(std::int64_t sightMm, const ForestTunables& tunables) {
		return sightMm * tunables.forestFloorPercent / 100;
	}

	SightSideValues
	forestRawDepth(const Vec2i64& eye, std::int64_t sightMm, std::span<const ForestBody> bodies, const ForestTunables& tunables) {
		assertWithinRanges(sightMm, tunables);
		SightSideValues	   depthMm{};
		const std::int64_t windowUnits = tunables.forestWindowSteps * kFractionOne;
		for (const ForestBody& body : bodies) {
			assert(body.widthFactor >= 0 && body.widthFactor <= kFractionOne);
			assert(body.arrival >= 0 && body.arrival <= kFractionOne);
			const Vec2i64 offset = body.centre - eye;
			if (std::abs(offset.x) >= sightMm || std::abs(offset.y) >= sightMm) {
				continue;
			}
			const std::int64_t distanceSquared = offset.x * offset.x + offset.y * offset.y;
			const std::int64_t weightMm = tunables.forestCapMm * body.widthFactor / kFractionOne *
										  distanceWeight(distanceSquared, sightMm, tunables.forestNearMm) / kFractionOne * body.arrival /
										  kFractionOne;
			if (weightMm == 0) {
				continue;
			}
			const std::int64_t position = tablePosition(offset);
			const std::int64_t first = position / kFractionOne - tunables.forestWindowSteps;
			for (std::int64_t side = first; side <= first + 2 * tunables.forestWindowSteps; ++side) {
				const std::int64_t offsetUnits = std::abs(side * kFractionOne + kFractionOne / 2 - position);
				if (offsetUnits < windowUnits) {
					const auto wrapped = static_cast<std::size_t>((side % kSides + kSides) % kSides);
					depthMm[wrapped] += weightMm * (windowUnits - offsetUnits) / windowUnits;
				}
			}
		}
		return depthMm;
	}

	SightSideValues forestLimitedDepth(const SightSideValues& rawDepth, std::int64_t sightMm, const ForestTunables& tunables) {
		assertWithinRanges(sightMm, tunables);
		const std::int64_t capMm = sightMm - forestFloorMm(sightMm, tunables);
		SightSideValues	   depthMm{};
		for (std::size_t side = 0; side < kSightSides; ++side) {
			depthMm[side] = std::min(rawDepth[side], capMm);
		}
		for (std::int64_t index = 1; index < 2 * kSides; ++index) {
			const auto side = static_cast<std::size_t>(index % kSides);
			const auto before = static_cast<std::size_t>((index - 1) % kSides);
			depthMm[side] = std::min(depthMm[side], depthMm[before] + tunables.forestSlopeMm);
		}
		for (std::int64_t index = 2 * kSides - 2; index >= 0; --index) {
			const auto side = static_cast<std::size_t>(index % kSides);
			const auto after = static_cast<std::size_t>((index + 1) % kSides);
			depthMm[side] = std::min(depthMm[side], depthMm[after] + tunables.forestSlopeMm);
		}
		return depthMm;
	}

	SightSideValues forestRanges(
		const Vec2i64&				eye,
		std::int64_t				sightMm,
		std::int64_t				radiusMm,
		std::span<const ForestBody> bodies,
		const ForestTunables&		tunables
	) {
		assert(radiusMm > sightMm - forestFloorMm(sightMm, tunables) && radiusMm < kForestRadiusLimitMm);
		const SightSideValues depthMm = forestLimitedDepth(forestRawDepth(eye, sightMm, bodies, tunables), sightMm, tunables);
		SightSideValues		  rangeMm{};
		for (std::size_t side = 0; side < kSightSides; ++side) {
			rangeMm[side] = radiusMm - depthMm[side];
		}
		return rangeMm;
	}

} // namespace geometry
