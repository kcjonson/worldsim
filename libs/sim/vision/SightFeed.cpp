#include "SightFeed.h"

#include <predicates/Predicates.h>
#include <visibility/ForestShape.h>
#include <visibility/SightClip.h>
#include <visibility/Visibility.h>

#include <algorithm>
#include <cassert>
#include <cmath>

namespace vision {

	using geometry::Vec2i64;

	namespace {

		constexpr std::int64_t kRebuildStepMm = 500;
		constexpr double	   kJumpMm		  = kSeenJumpMeters * 1000.0;
		// The walker the leg tolerance is scaled for: 2 m/s for vision's five frames at 60 a second.
		constexpr double kToleranceWalkMm = 2000.0 * 5.0 / 60.0;
		// A walk that divides the rebuild step exactly takes that many intervals, not one more.
		constexpr double kWholeIntervalSlack = 1e-9;

		static_assert(3 * geometry::kSightSides <= kSoftLegMostVertices, "a new leg's ring of three shapes always fits");

		double lengthOf(const Vec2i64& offset) {
			const double x = static_cast<double>(offset.x);
			const double y = static_cast<double>(offset.y);
			return std::sqrt(x * x + y * y);
		}

		double crossOf(const std::array<double, 2>& first, double secondX, double secondY) { return first[0] * secondY - first[1] * secondX; }

		// A leg's ranges: the build's at the slack radius (sight-softening.md 6.3).
		SightRanges legRangesOf(const SightRanges& buildRanges) {
			SightRanges ranges;
			for (std::size_t side = 0; side < geometry::kSightSides; ++side) {
				ranges[side] = std::max(geometry::kForestShapeLeastRangeMm, buildRanges[side] - kSeenSlackMm);
			}
			return ranges;
		}

	} // namespace

	std::int64_t SightFeed::legToleranceMm(std::int64_t toleranceMm, double timeScale) {
		if (timeScale <= 0.0) {
			return toleranceMm;
		}
		const auto spacing = [](double walkMm) {
			return walkMm * std::ceil(static_cast<double>(kRebuildStepMm) / walkMm - kWholeIntervalSlack);
		};
		return std::llround(static_cast<double>(toleranceMm) * spacing(kToleranceWalkMm * timeScale) / spacing(kToleranceWalkMm));
	}

	// Side k of a forest shape lies at range x kSightCosHalfSide / 2^8 along a normal of length
	// kSightSideNormalScale (2^22), so at range x cos(pi / kSightSides) millimetres: a range of
	// offset x 2^30 / kSightCosHalfSide, rounded down, puts it at or inside the offset.
	SightRanges SightFeed::rangesOfOffsets(const SideOffsets& offsets) {
		SightRanges ranges{};
		for (std::size_t side = 0; side < geometry::kSightSides; ++side) {
			ranges[side] = std::clamp(
				offsets[side] * geometry::kSightDirectionScale / geometry::kSightCosHalfSide, geometry::kForestShapeLeastRangeMm, geometry::kForestRadiusLimitMm - 1
			);
		}
		return ranges;
	}

	void SightFeed::beginLeg(Leg& leg, const Vec2i64& at, const SightRanges& ranges) {
		leg		   = Leg{};
		leg.open   = true;
		leg.start  = at;
		leg.end	   = at;
		leg.least  = ranges;
		leg.most   = ranges;
		leg.ranges = ranges;
	}

	// The leg may end at `at` when every position held since it began stays within kSeenSlackMm of
	// the line from its start to `at` (its direction is in the wedge) and none lay more than
	// kSeenSlackMm farther from the start than `at` does.
	bool SightFeed::holds(const Leg& leg, const Vec2i64& at) {
		const Vec2i64 offset = at - leg.start;
		const double  length = lengthOf(offset);
		if (leg.farthestMm > length + static_cast<double>(kSeenSlackMm)) {
			return false;
		}
		if (!leg.hasWedge || length == 0.0) {
			return true;
		}
		const double offsetX = static_cast<double>(offset.x);
		const double offsetY = static_cast<double>(offset.y);
		return crossOf(leg.wedgeLow, offsetX, offsetY) >= 0.0 && crossOf(leg.wedgeHigh, offsetX, offsetY) <= 0.0;
	}

	// Holds `at`: it is the leg's new end, and, once it lies farther from the start than the slack,
	// the wedge narrows to the directions whose line passes within the slack of it.
	void SightFeed::hold(Leg& leg, const Vec2i64& at) {
		leg.end				 = at;
		const Vec2i64 offset = at - leg.start;
		const double  length = lengthOf(offset);
		leg.farthestMm		 = std::max(leg.farthestMm, length);
		const double slack	 = static_cast<double>(kSeenSlackMm);
		if (length <= slack) {
			return;
		}
		const double				alongX = static_cast<double>(offset.x) / length;
		const double				alongY = static_cast<double>(offset.y) / length;
		const double				sine   = slack / length;
		const double				cosine = std::sqrt(length * length - slack * slack) / length;
		const std::array<double, 2> low{alongX * cosine + alongY * sine, alongY * cosine - alongX * sine};
		const std::array<double, 2> high{alongX * cosine - alongY * sine, alongY * cosine + alongX * sine};
		if (!leg.hasWedge) {
			leg.hasWedge  = true;
			leg.wedgeLow  = low;
			leg.wedgeHigh = high;
			return;
		}
		if (crossOf(leg.wedgeLow, low[0], low[1]) > 0.0) {
			leg.wedgeLow = low;
		}
		if (crossOf(leg.wedgeHigh, high[0], high[1]) < 0.0) {
			leg.wedgeHigh = high;
		}
	}

	bool SightFeed::follow(SeenArea& seen, SeenArea::LegId id, const Vec2i64& at, const SightBuild& build, std::int64_t toleranceMm, std::int64_t rimEaseMm) {
		const auto walkedMm = placed ? static_cast<std::int64_t>(std::ceil(lengthOf(at - lastPosition))) : 0;
		placed				= true;
		lastPosition		= at;
		const bool newBuild = !builtOnce || lastBuild != build.buildRevision;
		if (newBuild && builtOnce) {
			rangeChange = 0;
			for (std::size_t side = 0; side < geometry::kSightSides; ++side) {
				rangeChange = std::max(rangeChange, std::abs((*build.ranges)[side] - lastBuildRanges[side]));
			}
			greatestRangeChange = std::max(greatestRangeChange, rangeChange);
		}
		lastBuildRanges = *build.ranges;
		builtOnce		= true;
		lastBuild		= build.buildRevision;
		// A jump opens a leg with no inherited shape, so nothing in the record holds the ease's start where
		// he lands: the ease starts again from the newest build's shape, which his new leg holds.
		if (static_cast<double>(walkedMm) > kJumpMm) {
			ease.valid = false;
		}
		if (build.forest) {
			followEase(build, rimEaseMm, walkedMm);
		}
		const SightRanges ranges = legRangesOf(*build.ranges);
		if (build.polygon == nullptr) {
			dropWallSight();
			followLeg(seen, id, at, ranges, newBuild, toleranceMm);
			return false;
		}
		// The polygon path ends his leg, run on to here with its own ranges first while he holds its line.
		if (leg.open) {
			if (holds(leg, at) && seen.setOpenLeg(id, leg.start, at, leg.ranges, std::nullopt)) {
				hold(leg, at);
			}
			closeLeg(seen, id, LegEnd::Path);
		}
		const bool newPolygon = build.polygonRevision != polygonSeen;
		if (wallSight) {
			const bool resting = at == stretch.end;
			followStretch(at);
			if (resting && !newPolygon) {
				enterPolygonAt(seen, at);
			}
		}
		if (newPolygon) {
			registerPolygon(seen, build);
		}
		return newPolygon;
	}

	void SightFeed::stop(SeenArea& seen, SeenArea::LegId id) {
		closeLeg(seen, id, LegEnd::Count);
		dropWallSight();
	}

	void SightFeed::leave(SeenArea& seen, SeenArea::LegId id) {
		enterPolygonAt(seen, stretch.end);
		closeLeg(seen, id, LegEnd::Count);
	}

	void SightFeed::followLeg(SeenArea& seen, SeenArea::LegId id, const Vec2i64& at, const SightRanges& ranges, bool newBuild, std::int64_t toleranceMm) {
		// The shape drawn this frame, which the next leg holds first after a turn, the spread, or the
		// ring's vertices (sight-softening.md 6.2).
		const std::optional<SeenArea::InheritedShape> drawn =
			ease.valid ? std::optional(SeenArea::InheritedShape{at, rangesOfOffsets(eased())}) : std::nullopt;
		if (!leg.open) {
			beginLeg(leg, at, ranges);
			openLeg(seen, id, at, std::nullopt);
			return;
		}
		if (lengthOf(at - leg.end) > kJumpMm) {
			closeLeg(seen, id, LegEnd::Jump);
			beginLeg(leg, at, ranges);
			openLeg(seen, id, at, std::nullopt);
			return;
		}
		bool spread = false;
		for (std::size_t side = 0; newBuild && side < geometry::kSightSides; ++side) {
			spread = spread || std::max(leg.most[side], ranges[side]) - std::min(leg.least[side], ranges[side]) > toleranceMm;
		}
		if (spread) {
			closeLeg(seen, id, LegEnd::Spread);
			beginLeg(leg, at, ranges);
			openLeg(seen, id, at, drawn);
			return;
		}
		if (!holds(leg, at)) {
			const Vec2i64 turnedAt = leg.end;
			closeLeg(seen, id, LegEnd::Turn);
			beginLeg(leg, turnedAt, ranges);
			hold(leg, at);
			openLeg(seen, id, at, drawn);
			return;
		}
		hold(leg, at);
		for (std::size_t side = 0; side < geometry::kSightSides; ++side) {
			leg.least[side] = std::min(leg.least[side], ranges[side]);
			leg.most[side]	= std::max(leg.most[side], ranges[side]);
		}
		leg.ranges = ranges;
		if (!seen.setOpenLeg(id, leg.start, at, leg.ranges, std::nullopt)) {
			closeLeg(seen, id, LegEnd::Vertices);
			beginLeg(leg, at, ranges);
			openLeg(seen, id, at, drawn);
		}
	}

	// A new leg's ring is the hull of its start's shape, his position's, and the inherited one, three
	// forest shapes of at most kSightSides vertices each, which always fit (the static_assert above).
	void SightFeed::openLeg(SeenArea& seen, SeenArea::LegId id, const Vec2i64& at, const std::optional<SeenArea::InheritedShape>& inherited) {
		++opened;
		[[maybe_unused]] const bool joined = seen.setOpenLeg(id, leg.start, at, leg.ranges, inherited);
		assert(joined);
	}

	// LegEnd::Count closes a leg for no reason of its own: his sight shrank, or he is gone.
	void SightFeed::closeLeg(SeenArea& seen, SeenArea::LegId id, LegEnd reason) {
		if (!leg.open) {
			return;
		}
		seen.closeLeg(id);
		leg.open = false;
		if (reason != LegEnd::Count) {
			++endedBy[static_cast<std::size_t>(reason)];
		}
	}

	// The sweep runs along the walk, so it stays within the slack of where he went: a turn is a corner
	// of the walk. A shape built before a jump says nothing about what the walls hide from here, and no
	// sweep crosses the jump.
	void SightFeed::followStretch(const Vec2i64& at) {
		if (lengthOf(at - stretch.end) > kJumpMm) {
			dropWallSight();
			return;
		}
		if (!holds(stretch, at)) {
			const Vec2i64 turnedAt = stretch.end;
			walk.push_back(turnedAt);
			beginLeg(stretch, turnedAt, stretch.ranges);
		}
		hold(stretch, at);
	}

	void SightFeed::dropWallSight() {
		wallSight = false;
		walk.clear();
	}

	// Where he rests or his walk ends away from vision's last build, a shape built there goes in, so
	// the ground he sees from there is not left to the next build.
	void SightFeed::enterPolygonAt(SeenArea& seen, const Vec2i64& at) {
		if (!wallSight || walk.empty() || walk.front() == at) {
			return;
		}
		const SightRanges rim = walkStartRanges;
		seen.queueSightAt(at, sightMm, rim, sweepWanted ? std::move(walk) : std::vector<Vec2i64>{}, rim);
		walk.assign(1, at);
		beginLeg(stretch, at, rim);
	}

	// Vision's shape cut to its rim, the forest shape of his sight round its build, goes into the record,
	// with his walk from the build before it for the sweep when an opaque wall is in reach: past a placed
	// body's shadow alone the seen edge steps with vision's builds (sight-softening.md 6.6).
	void SightFeed::registerPolygon(SeenArea& seen, const SightBuild& build) {
		const SightRanges&	 rim	 = *build.ranges;
		std::vector<Vec2i64> inSight = geometry::starClippedToConvex(*build.polygon, build.eye, rimOf(build.eye, rim));
		sweepWanted					 = build.opaqueWallInReach;
		if (geometry::simpleAround(inSight, build.eye)) {
			seen.queueSightPolygon(std::move(inSight), build.eye, build.sightMm, rim, sweepWanted ? std::move(walk) : std::vector<Vec2i64>{}, walkStartRanges);
		}
		walk.assign(1, build.eye);
		walkStartRanges = rim;
		beginLeg(stretch, build.eye, rim);
		sightMm		= build.sightMm;
		wallSight	= true;
		polygonSeen = build.polygonRevision;
	}

	// The ease's state is integers: a frame's walk rounded up, so `walkedMm` is never less than his
	// straight distance from where it started, and a start rounded down, 1 mm less when he has walked
	// since the last start, which pays for that start's position rounding to the millimetre
	// (sight-softening.md 6.3). Standing still, a restart takes the drawn shape as it is.
	void SightFeed::followEase(const SightBuild& build, std::int64_t rimEaseMm, std::int64_t walkedMm) {
		ease.walkedMm += walkedMm;
		if (ease.valid && ease.buildRevision == build.buildRevision) {
			return;
		}
		const std::int64_t inMm = build.sightMm - (drawnVisibleRadiusMm(build.sightMm) - rimEaseMm);
		SightRanges		   drawnRanges{};
		for (std::size_t side = 0; side < geometry::kSightSides; ++side) {
			drawnRanges[side] = std::max(geometry::kForestShapeLeastRangeMm, (*build.ranges)[side] - inMm);
		}
		const geometry::Ring forest = geometry::forestShape({0, 0}, drawnRanges);
		SideOffsets			 newest{};
		for (std::size_t side = 0; side < geometry::kSightSides; ++side) {
			std::int64_t support = 0;
			for (std::size_t vertex = 0; vertex < forest.size(); ++vertex) {
				const std::int64_t along = geometry::kSightSideNormals[side][0] * forest[vertex].x + geometry::kSightSideNormals[side][1] * forest[vertex].y;
				support					 = vertex == 0 ? along : std::max(support, along);
			}
			newest[side] = support / geometry::kSightSideNormalScale - (support % geometry::kSightSideNormalScale < 0 ? 1 : 0);
		}
		if (ease.valid) {
			const std::int64_t rounding = ease.walkedMm > 0 ? 1 : 0;
			ease.start					= eased();
			for (std::int64_t& offset : ease.start) {
				offset -= rounding;
			}
			ease.walkedMm = 0;
		} else {
			ease.start	  = newest;
			ease.walkedMm = rimEaseMm;
		}
		ease.newest		   = newest;
		ease.rimEaseMm	   = rimEaseMm;
		ease.buildRevision = build.buildRevision;
		ease.valid		   = true;
	}

	SightFeed::SideOffsets SightFeed::eased() const {
		const double blend = std::min(1.0, static_cast<double>(ease.walkedMm) / static_cast<double>(ease.rimEaseMm));
		SideOffsets	 drawn{};
		for (std::size_t side = 0; side < geometry::kSightSides; ++side) {
			drawn[side] = static_cast<std::int64_t>(std::floor((1.0 - blend) * static_cast<double>(ease.start[side]) + blend * static_cast<double>(ease.newest[side])));
		}
		return drawn;
	}

	std::optional<SightFeed::SideOffsets> SightFeed::drawnOffsets() const {
		if (!ease.valid) {
			return std::nullopt;
		}
		return eased();
	}

} // namespace vision
