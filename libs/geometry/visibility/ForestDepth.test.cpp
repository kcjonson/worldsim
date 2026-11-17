#include "visibility/ForestDepth.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

using geometry::ForestBody;
using geometry::ForestTunables;
using geometry::kFractionOne;
using geometry::kSightSides;
using geometry::SightSideValues;
using geometry::Vec2i64;

namespace {

	constexpr std::int64_t kSightMm = 30000;
	constexpr double	   kPi = 3.14159265358979323846;

	// Padded rings face on (sight-softening.md 2.1): a pine 0.28 m, a mature oak 0.55 m, a veteran
	// oak 0.82 m.
	std::int64_t squareWidthFactor(std::int64_t sideMm) {
		return geometry::forestWidthFactor(4 * sideMm, ForestTunables{});
	}

	std::int64_t maximum(const SightSideValues& values) {
		return *std::max_element(values.begin(), values.end());
	}

	SightSideValues rawDepth(const Vec2i64& eye, const std::vector<ForestBody>& bodies) {
		return geometry::forestRawDepth(eye, kSightMm, bodies, ForestTunables{});
	}

	SightSideValues limitedDepth(const Vec2i64& eye, const std::vector<ForestBody>& bodies) {
		return geometry::forestLimitedDepth(rawDepth(eye, bodies), kSightMm, ForestTunables{});
	}

	// The distance weight of 4.1 in doubles: what one ring's depth straight at it is a fraction of.
	double distanceWeight(double distanceMm) {
		const double nearMm = 10000.0;
		return std::min(1.0, distanceMm * distanceMm / (nearMm * nearMm)) *
			   (1.0 - distanceMm * distanceMm / (static_cast<double>(kSightMm) * kSightMm));
	}

	// A square lattice of trunks `spacingMm` apart covering the sight disc round the origin.
	std::vector<ForestBody> lattice(std::int64_t spacingMm, std::int64_t widthFactor) {
		std::vector<ForestBody> bodies;
		const std::int64_t		cells = kSightMm / spacingMm + 2;
		for (std::int64_t i = -cells; i <= cells; ++i) {
			for (std::int64_t j = -cells; j <= cells; ++j) {
				bodies.push_back({{i * spacingMm, j * spacingMm}, widthFactor, kFractionOne});
			}
		}
		return bodies;
	}

} // namespace

// The width factor is the perimeter against a body 1 m wide on average, and the arrival fraction
// rises over forestArrivalMs from the ring's arrival.
TEST(ForestDepth, WidthAndArrivalFractions) {
	const ForestTunables tunables;
	EXPECT_NEAR(static_cast<double>(squareWidthFactor(280)) / kFractionOne, 0.36, 0.01);
	EXPECT_NEAR(static_cast<double>(squareWidthFactor(550)) / kFractionOne, 0.70, 0.01);
	EXPECT_EQ(squareWidthFactor(820), kFractionOne);
	EXPECT_EQ(geometry::forestWidthFactor(0, tunables), 0);

	EXPECT_EQ(geometry::forestArrivalFraction(5000, 5000, tunables), 0);
	EXPECT_EQ(geometry::forestArrivalFraction(4000, 5000, tunables), 0);
	EXPECT_EQ(geometry::forestArrivalFraction(6000, 5000, tunables), kFractionOne / 2);
	EXPECT_EQ(geometry::forestArrivalFraction(7000, 5000, tunables), kFractionOne);
	EXPECT_EQ(geometry::forestArrivalFraction(std::int64_t{1} << 50, 5000, tunables), kFractionOne);
	EXPECT_EQ(geometry::forestFloorMm(kSightMm, tunables), 20100);
}

// One ring's depth across any side is at most forestCapMm x widthFactor x distanceWeight, greatest
// on the sides either side of its direction and nothing past the tent's window (4.3).
TEST(ForestDepth, LoneBodyIsCappedByItsWeights) {
	const std::int64_t sides = static_cast<std::int64_t>(kSightSides);
	for (const std::int64_t sideMm : {280, 550, 820, 1070}) {
		const std::int64_t widthFactor = squareWidthFactor(sideMm);
		for (const std::int64_t distanceMm : {1000, 2000, 5000, 10000, 20000, 29000}) {
			for (int angleStep = 0; angleStep < 37; ++angleStep) {
				const double  angle = 2.0 * kPi * angleStep / 37.0;
				const Vec2i64 eye{1234567, -7654321};
				const Vec2i64 centre =
					eye + Vec2i64{std::llround(distanceMm * std::cos(angle)), std::llround(distanceMm * std::sin(angle))};
				const SightSideValues depth = rawDepth(eye, {{centre, widthFactor, kFractionOne}});
				const double		  most =
					800.0 * static_cast<double>(widthFactor) / kFractionOne * distanceWeight(static_cast<double>(distanceMm));
				EXPECT_LE(static_cast<double>(maximum(depth)), most + 0.5) << sideMm << " at " << distanceMm;
				EXPECT_GE(static_cast<double>(maximum(depth)), most * 0.9 - 2.0) << sideMm << " at " << distanceMm;

				// Sides more than the window from the ring's direction take nothing.
				const double sidePosition = angle / (2.0 * kPi) * static_cast<double>(sides) - 0.5;
				for (std::int64_t side = 0; side < sides; ++side) {
					double apart = std::fmod(std::abs(static_cast<double>(side) - sidePosition), static_cast<double>(sides));
					apart = std::min(apart, static_cast<double>(sides) - apart);
					if (apart >= 8.0 + 1e-6) {
						EXPECT_EQ(depth[static_cast<std::size_t>(side)], 0) << "side " << side;
					}
				}
			}
		}
	}
}

// Through stands of known density the mean depth matches 4.3's expectation,
// density x widthFactor x forestCapMm x windowAngle x 201 m^2, to 10%, and the two densest stands
// of its table sit at the floor.
TEST(ForestDepth, StandsOfKnownDensityMatchTheTable) {
	struct Stand {
		std::int64_t spacingMm;
		std::int64_t sideMm;
		double		 tableRimMm;
	};
	const double windowAngle = 8.0 * 2.0 * kPi / static_cast<double>(kSightSides);
	for (const Stand stand : {Stand{5000, 550, 26500}, Stand{3000, 280, 24900}, Stand{4000, 820, 22100}, Stand{3000, 550, 20200}}) {
		const std::int64_t			  widthFactor = squareWidthFactor(stand.sideMm);
		const std::vector<ForestBody> bodies = lattice(stand.spacingMm, widthFactor);
		const double				  density = 1.0 / (static_cast<double>(stand.spacingMm) * static_cast<double>(stand.spacingMm) / 1e6);
		const double				  expected = density * static_cast<double>(widthFactor) / kFractionOne * 800.0 * windowAngle * 200.93;
		EXPECT_NEAR(expected, static_cast<double>(kSightMm) - stand.tableRimMm, 0.01 * static_cast<double>(kSightMm));
		for (const Vec2i64 eye : {Vec2i64{1370, 2610}, Vec2i64{-480, 990}, Vec2i64{2210, -1730}}) {
			const SightSideValues depth = rawDepth(eye, bodies);
			double				  sum = 0.0;
			for (const std::int64_t value : depth) {
				sum += static_cast<double>(value);
			}
			EXPECT_NEAR(sum / static_cast<double>(kSightSides), expected, 0.1 * expected)
				<< "spacing " << stand.spacingMm << " side " << stand.sideMm;
		}
	}

	const std::int64_t floorDepthMm = kSightMm - geometry::forestFloorMm(kSightMm, ForestTunables{});
	for (const auto& [spacingMm, sideMm] : {std::pair<std::int64_t, std::int64_t>{2000, 280}, {3000, 820}}) {
		const std::vector<ForestBody> bodies = lattice(spacingMm, squareWidthFactor(sideMm));
		const SightSideValues		  depth = limitedDepth({1370, 2610}, bodies);
		for (const std::int64_t value : depth) {
			EXPECT_GE(value, floorDepthMm * 9 / 10) << "spacing " << spacingMm;
			EXPECT_LE(value, floorDepthMm);
		}
	}
}

// Walking round a veteran oak at 10 m, and past one at 2 m, in 1 cm steps, no side's range changes
// by more than 4.3's bound for the step, moved / (windowAngle x forestNearMm) + 2 x moved /
// forestNearMm of forestCapMm x widthFactor, plus 1 mm.
TEST(ForestDepth, WalkingChangesEachSideByAtMostTheStepBound) {
	const double					  windowAngle = 8.0 * 2.0 * kPi / static_cast<double>(kSightSides);
	const Vec2i64					  oak{-31000, 47000};
	const std::vector<ForestBody>	  bodies{{oak, kFractionOne, kFractionOne}};
	std::vector<std::vector<Vec2i64>> walks(2);
	const int						  roundSteps = 6283;
	for (int step = 0; step <= roundSteps; ++step) {
		const double angle = 2.0 * kPi * step / roundSteps;
		walks[0].push_back(oak + Vec2i64{std::llround(10000.0 * std::cos(angle)), std::llround(10000.0 * std::sin(angle))});
	}
	for (std::int64_t along = -30000; along <= 30000; along += 10) {
		walks[1].push_back(oak + Vec2i64{along, 2000});
	}
	for (const std::vector<Vec2i64>& walk : walks) {
		SightSideValues before = geometry::forestRanges(walk[0], kSightMm, kSightMm, bodies, ForestTunables{});
		for (std::size_t index = 1; index < walk.size(); ++index) {
			const Vec2i64		  moved = walk[index] - walk[index - 1];
			const double		  movedMm = std::sqrt(static_cast<double>(moved.x * moved.x + moved.y * moved.y));
			const double		  bound = 800.0 * (movedMm / (windowAngle * 10000.0) + 2.0 * movedMm / 10000.0) + 1.0;
			const SightSideValues after = geometry::forestRanges(walk[index], kSightMm, kSightMm, bodies, ForestTunables{});
			for (std::size_t side = 0; side < kSightSides; ++side) {
				ASSERT_LE(std::abs(static_cast<double>(after[side] - before[side])), bound) << "step " << index << " side " << side;
			}
			before = after;
		}
	}
}

// A ring coming into sight adds nothing at the rim and rises from there.
TEST(ForestDepth, RingComingIntoSightAddsNothingAtTheRim) {
	const Vec2i64 eye{5000, 5000};
	for (const std::int64_t distanceMm : {30000, 30001, 45000}) {
		const std::vector<ForestBody> bodies{{eye + Vec2i64{distanceMm, 0}, kFractionOne, kFractionOne}};
		EXPECT_EQ(maximum(rawDepth(eye, bodies)), 0) << distanceMm;
	}
	const std::vector<ForestBody> justInside{{eye + Vec2i64{-21213, -21200}, kFractionOne, kFractionOne}};
	EXPECT_LE(maximum(rawDepth(eye, justInside)), 1);
	std::int64_t previous = 0;
	for (std::int64_t distanceMm = 29990; distanceMm >= 25000; distanceMm -= 10) {
		const std::int64_t depth = maximum(rawDepth(eye, {{eye + Vec2i64{0, distanceMm}, kFractionOne, kFractionOne}}));
		EXPECT_GE(depth, previous);
		EXPECT_LE(depth - previous, 1) << distanceMm;
		previous = depth;
	}
}

// A ring still arriving spends its depth in proportion to how far it has come.
TEST(ForestDepth, ArrivalRampsABodysDepth) {
	const Vec2i64		 eye{0, 0};
	const Vec2i64		 centre{7000, 7000};
	const ForestTunables tunables;
	std::int64_t		 previous = -1;
	const std::int64_t	 full = maximum(rawDepth(eye, {{centre, kFractionOne, kFractionOne}}));
	for (std::int64_t elapsedMs = 0; elapsedMs <= 2500; elapsedMs += 100) {
		const std::int64_t arrival = geometry::forestArrivalFraction(10000 + elapsedMs, 10000, tunables);
		const std::int64_t depth = maximum(rawDepth(eye, {{centre, kFractionOne, arrival}}));
		EXPECT_GE(depth, previous);
		EXPECT_NEAR(static_cast<double>(depth), static_cast<double>(full) * std::min<double>(1.0, elapsedMs / 2000.0), 2.0) << elapsedMs;
		previous = depth;
	}
	EXPECT_EQ(maximum(rawDepth(eye, {{centre, kFractionOne, 0}})), 0);
}

// After the slope limit no side's depth exceeds a neighbour's by more than forestSlopeMm, it only
// lowers depth, and every side is within the cap, over seeded random stands and clumps.
TEST(ForestDepth, SlopeLimitHoldsNeighboursWithinTheSlope) {
	std::mt19937		 rng(986);
	const ForestTunables tunables;
	const std::int64_t	 capMm = kSightMm - geometry::forestFloorMm(kSightMm, tunables);
	for (int stand = 0; stand < 60; ++stand) {
		std::vector<ForestBody> bodies;
		const int				count = 20 + static_cast<int>(rng() % 400);
		const std::int64_t		spreadMm = 4000 + static_cast<std::int64_t>(rng() % 60000);
		const Vec2i64			middle{static_cast<std::int64_t>(rng() % 40000) - 20000, static_cast<std::int64_t>(rng() % 40000) - 20000};
		for (int index = 0; index < count; ++index) {
			const Vec2i64 centre = middle + Vec2i64{
												static_cast<std::int64_t>(rng() % spreadMm) - spreadMm / 2,
												static_cast<std::int64_t>(rng() % spreadMm) - spreadMm / 2
											};
			if (centre.x * centre.x + centre.y * centre.y < 500 * 500) {
				continue;
			}
			bodies.push_back({centre, squareWidthFactor(280 + static_cast<std::int64_t>(rng() % 800)), kFractionOne});
		}
		const SightSideValues raw = rawDepth({0, 0}, bodies);
		const SightSideValues limited = geometry::forestLimitedDepth(raw, kSightMm, tunables);
		for (std::size_t side = 0; side < kSightSides; ++side) {
			const std::size_t next = (side + 1) % kSightSides;
			EXPECT_LE(std::abs(limited[side] - limited[next]), tunables.forestSlopeMm) << "stand " << stand << " side " << side;
			EXPECT_LE(limited[side], std::min(raw[side], capMm));
			EXPECT_GE(limited[side], 0);
		}
	}
}

// Fixed input, fixed output: forest depth is integer arithmetic, so every toolchain must give these
// ranges at the defaults for this stand. A toolchain that differs fails here.
TEST(ForestDepth, RangesArePinned) {
	std::mt19937			rng(424242);
	std::vector<ForestBody> bodies;
	for (int index = 0; index < 160; ++index) {
		const Vec2i64	   centre{static_cast<std::int64_t>(rng() % 64000) - 32000, static_cast<std::int64_t>(rng() % 64000) - 32000};
		const std::int64_t arrival = index % 7 == 0 ? static_cast<std::int64_t>(rng() % kFractionOne) : kFractionOne;
		bodies.push_back({centre + Vec2i64{100000, -50000}, squareWidthFactor(280 + static_cast<std::int64_t>(rng() % 900)), arrival});
	}
	const SightSideValues ranges = geometry::forestRanges({100000, -50000}, kSightMm, 32200, bodies, ForestTunables{});
	const SightSideValues pinned{27489, 27705, 27940, 28244, 28642, 29064, 29308, 29357, 29231, 29044, 28814, 28644, 28321,
								 27916, 27636, 27609, 27857, 28187, 28536, 28714, 28929, 29080, 29113, 29007, 28720, 28386,
								 28157, 28111, 28113, 28269, 28549, 28859, 29236, 29615, 29779, 29892, 29950, 30007, 29926,
								 29771, 29585, 29319, 29228, 29030, 28754, 28448, 28277, 28213, 28119, 28159, 28195, 28328,
								 28482, 28508, 28409, 28257, 28149, 27971, 27737, 27423, 27195, 27094, 27168, 27313};
	EXPECT_EQ(ranges, pinned);
}
