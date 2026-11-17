#include "SightForestRecord.bench.h"

#include <nav/NavMesh.h>
#include <nav/PatchableMesh.h>
#include <nav/SightCover.h>
#include <nav/SightFrontier.h>
#include <predicates/Predicates.h>
#include <visibility/ForestShape.h>
#include <visibility/SightPolygon.h>
#include <visibility/SightSkirt.h>
#include <visibility/Visibility.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iterator>
#include <numbers>
#include <utility>

namespace sight_bench {

	using geometry::Vec2i64;

	namespace {

		void boxOf(const std::vector<Vec2i64>& ring, Vec2i64& low, Vec2i64& high) {
			low = ring.front();
			high = ring.front();
			for (const Vec2i64& point : ring) {
				low = {std::min(low.x, point.x), std::min(low.y, point.y)};
				high = {std::max(high.x, point.x), std::max(high.y, point.y)};
			}
		}

		bool boxesMeet(const Vec2i64& firstLow, const Vec2i64& firstHigh, const Vec2i64& secondLow, const Vec2i64& secondHigh) {
			return firstLow.x <= secondHigh.x && secondLow.x <= firstHigh.x && firstLow.y <= secondHigh.y && secondLow.y <= firstHigh.y;
		}

		bool allEqual(const SightRanges& ranges) {
			return std::all_of(ranges.begin(), ranges.end(), [&ranges](std::int64_t range) { return range == ranges.front(); });
		}

		std::int64_t mostOf(const SightRanges& ranges) { return *std::max_element(ranges.begin(), ranges.end()); }

		// SeenArea's test, a vertex past the circle kSeenSweepRimBandMm inside the rim, read on each
		// vertex against the range of the side it faces: with every range equal it is SeenArea's.
		bool addsNearRim(const std::vector<std::vector<Vec2i64>>& pieces, const Vec2i64& centre, const SightRanges& ranges) {
			for (const std::vector<Vec2i64>& piece : pieces) {
				for (const Vec2i64& vertex : piece) {
					const Vec2i64 offset = vertex - centre;
					std::size_t	  facing = 0;
					double		  mostAlong = -1e300;
					for (std::size_t side = 0; side < geometry::kSightSides; ++side) {
						const double along = static_cast<double>(geometry::kSightSideNormals[side][0]) * static_cast<double>(offset.x) +
											 static_cast<double>(geometry::kSightSideNormals[side][1]) * static_cast<double>(offset.y);
						if (along > mostAlong) {
							mostAlong = along;
							facing = side;
						}
					}
					const std::int64_t inner = ranges[facing] - vision::kSeenSweepRimBandMm;
					if (offset.x * offset.x + offset.y * offset.y > inner * inner) {
						return true;
					}
				}
			}
			return false;
		}

		// A convex ring's two chains between its lexicographically least and greatest vertices, each in
		// increasing order.
		void appendChains(const std::vector<Vec2i64>& ring, std::vector<Vec2i64>& lower, std::vector<Vec2i64>& upper) {
			const std::size_t count = ring.size();
			const std::size_t least = static_cast<std::size_t>(std::min_element(ring.begin(), ring.end()) - ring.begin());
			const std::size_t most = static_cast<std::size_t>(std::max_element(ring.begin(), ring.end()) - ring.begin());
			for (std::size_t k = least;; k = (k + 1) % count) {
				lower.push_back(ring[k]);
				if (k == most) {
					break;
				}
			}
			for (std::size_t k = least;; k = (k + count - 1) % count) {
				upper.push_back(ring[k]);
				if (k == most) {
					break;
				}
			}
		}

		// One monotone chain of the hull, turning left only. A leg's points lie well under 2^31 mm
		// apart, so the cross products of their differences fit an int64.
		void hullChain(const std::vector<Vec2i64>& sorted, bool forward, std::vector<Vec2i64>& out) {
			const std::size_t first = out.size();
			const std::size_t count = sorted.size();
			for (std::size_t k = 0; k < count; ++k) {
				const Vec2i64& point = sorted[forward ? k : count - 1 - k];
				while (out.size() >= first + 2) {
					const Vec2i64 last = out.back() - out[out.size() - 2];
					const Vec2i64 onward = point - out.back();
					if (last.x * onward.y - last.y * onward.x > 0) {
						break;
					}
					out.pop_back();
				}
				out.push_back(point);
			}
			out.pop_back();
		}

		std::vector<Vec2i64>& movedBy(const std::vector<Vec2i64>& ring, const Vec2i64& offset, std::vector<Vec2i64>& out) {
			out.clear();
			for (const Vec2i64& point : ring) {
				out.push_back(point + offset);
			}
			return out;
		}

		double distanceBetween(const Vec2i64& first, const Vec2i64& second) {
			const Vec2i64 gap = first - second;
			return std::sqrt(geometry::dot(gap, gap).toDouble());
		}

	} // namespace

	SightRanges equalRanges(std::int64_t radiusMm) {
		SightRanges ranges;
		ranges.fill(radiusMm);
		return ranges;
	}

	// Both are integer constructions in offsets from the centre, so moving the centre by a whole
	// millimetre vector moves the ring by it exactly.
	std::vector<Vec2i64> rimOf(const Vec2i64& centre, const SightRanges& ranges) {
		if (allEqual(ranges)) {
			return geometry::sightPolygon(centre, ranges.front());
		}
		return geometry::forestShape(centre, ranges);
	}

	ForestRecord::ForestRecord(const Scheduler& scheduler)
		: scheduler(scheduler),
		  mesh(std::make_unique<geometry::nav::PatchableMesh>()),
		  scratch(std::make_unique<geometry::nav::SightCoverScratch>()),
		  frontierScratch(std::make_unique<geometry::nav::SightFrontierScratch>()) {}

	ForestRecord::~ForestRecord() = default;

	ForestRecord::PendingShape ForestRecord::pendingOf(std::vector<Vec2i64> outline, const Vec2i64& centre, Origin origin) {
		PendingShape shape;
		boxOf(outline, shape.low, shape.high);
		shape.outline = std::move(outline);
		shape.centre = centre;
		shape.origin = origin;
		return shape;
	}

	// The convex hull of two convex counter-clockwise rings: each one's lower chains merged in order
	// and swept once, and the upper the same. `out` may be `first`.
	void ForestRecord::hullOfConvex(const std::vector<Vec2i64>& first, const std::vector<Vec2i64>& second, std::vector<Vec2i64>& out) {
		firstLower.clear();
		firstUpper.clear();
		secondLower.clear();
		secondUpper.clear();
		lower.clear();
		upper.clear();
		appendChains(first, firstLower, firstUpper);
		appendChains(second, secondLower, secondUpper);
		std::merge(firstLower.begin(), firstLower.end(), secondLower.begin(), secondLower.end(), std::back_inserter(lower));
		std::merge(firstUpper.begin(), firstUpper.end(), secondUpper.begin(), secondUpper.end(), std::back_inserter(upper));
		out.clear();
		hullChain(lower, true, out);
		hullChain(upper, false, out);
	}

	bool ForestRecord::covered(const PendingShape& shape) {
		counted.mostTicksToTest = std::max(counted.mostTicksToTest, ticks - shape.queuedTick);
		const bool leg = shape.origin == Origin::Leg;
		counted.legsTested += leg ? 1 : 0;
		if (geometry::nav::sightShapeCovered(*mesh, shape.outline, shape.centre, *scratch)) {
			counted.legsCovered += leg ? 1 : 0;
			return true;
		}
		return false;
	}

	void ForestRecord::enter(const PendingShape& shape) {
		std::vector<Vec2i64> grown;
		if (shape.sightPolygon) {
			grown = geometry::skirtedSightShape(shape.outline, shape.centre, rimOf(shape.centre, shape.ranges), geometry::kSeenSkirtMm);
		}
		if (geometry::nav::sightShapeFrontier(*mesh, grown.empty() ? shape.outline : grown, shape.centre, *frontierScratch, pieces) ==
			geometry::nav::SightFrontier::Covered) {
			counted.legsCovered += shape.origin == Origin::Leg ? 1 : 0;
			return;
		}
		if (!shape.sweepPath.empty() && sightBuilder && addsNearRim(pieces, shape.centre, shape.ranges)) {
			queueSweep(shape);
		}
		for (std::vector<Vec2i64>& piece : pieces) {
			entering.push_back(pendingOf(std::move(piece), shape.centre, shape.origin));
		}
	}

	void ForestRecord::registerPiece() {
		const vision::SeenArea::Box box{entering.front().low, entering.front().high};
		const auto					countStart = std::chrono::steady_clock::now();
		const auto met = static_cast<std::size_t>(std::count_if(registeredBoxes.begin(), registeredBoxes.end(), [&box](const auto& registered) {
			return boxesMeet(box.low, box.high, registered.low, registered.high);
		}));
		counted.ringsMetMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - countStart).count();
		++counted.registrations[static_cast<std::size_t>(entering.front().origin)];
		counted.ringsMetTotal += met;
		counted.ringsMetMost = std::max(counted.ringsMetMost, met);
		geometry::nav::PatchEdits edits;
		edits.adds.push_back(geometry::nav::NavInputPolygon{.ring = std::move(entering.front().outline)});
		entering.pop_front();
		const geometry::nav::PatchReport report = mesh->applyPatch(edits);
		for (const std::int32_t added : report.added) {
			if (added != geometry::nav::kNoRing) {
				registeredBoxes.push_back(box);
			}
		}
		mesh->refreshWidths();
	}

	// SeenArea::queueSweep with each point's rim ranges interpolated between the walk's two builds by
	// where it lies along the walk (sight-softening.md 6.4), held in by the inset of the nearer build,
	// `apart` taking the most its ranges stray from that build's as well as its distance.
	void ForestRecord::queueSweep(const PendingShape& shape) {
		const std::vector<Vec2i64>& walk = shape.sweepPath;
		const Vec2i64&				to = shape.centre;
		const double				halfSideAngle = std::numbers::pi / static_cast<double>(geometry::kSightSides);
		double						walkLength = 0.0;
		for (std::size_t corner = 0; corner < walk.size(); ++corner) {
			walkLength += distanceBetween(walk[corner], corner + 1 < walk.size() ? walk[corner + 1] : to);
		}
		double walked = 0.0;
		auto   queuePoint = [&](const Vec2i64& point, double along) {
			  const double		 fraction = walkLength > 0.0 ? std::clamp(along / walkLength, 0.0, 1.0) : 0.0;
			  const bool		 nearStart = distanceBetween(point, walk.front()) <= distanceBetween(point, to);
			  const SightRanges& nearer = nearStart ? shape.walkStartRanges : shape.ranges;
			  SightRanges		 interpolated;
			  std::int64_t		 strays = 0;
			  for (std::size_t side = 0; side < geometry::kSightSides; ++side) {
				  const double start = static_cast<double>(shape.walkStartRanges[side]);
				  interpolated[side] = static_cast<std::int64_t>(std::floor(start + (static_cast<double>(shape.ranges[side]) - start) * fraction));
				  strays = std::max(strays, std::abs(interpolated[side] - nearer[side]));
			  }
			  const double apart = std::min(distanceBetween(point, walk.front()), distanceBetween(point, to)) + static_cast<double>(strays);
			  const double inset = halfSideAngle * apart + apart * apart / (2.0 * static_cast<double>(mostOf(interpolated)));
			  const auto   insetMm = static_cast<std::int64_t>(std::ceil(inset)) + 1;
			  PendingShape sweep;
			  sweep.centre = point;
			  for (std::size_t side = 0; side < geometry::kSightSides; ++side) {
				  sweep.ranges[side] = interpolated[side] - insetMm;
			  }
			  sweeps.push_back(std::move(sweep));
		};
		for (std::size_t corner = 0; corner < walk.size(); ++corner) {
			const Vec2i64&	   from = walk[corner];
			const Vec2i64&	   until = corner + 1 < walk.size() ? walk[corner + 1] : to;
			const Vec2i64	   along = until - from;
			const double	   stretch = distanceBetween(until, from);
			const std::int64_t steps = static_cast<std::int64_t>(std::ceil(stretch / static_cast<double>(vision::kSeenSweepSpacingMm)));
			if (corner > 0) {
				queuePoint(from, walked);
			}
			for (std::int64_t step = 1; step < steps; ++step) {
				queuePoint(
					from + Vec2i64{along.x * step / steps, along.y * step / steps},
					walked + stretch * static_cast<double>(step) / static_cast<double>(steps)
				);
			}
			walked += stretch;
		}
	}

	void ForestRecord::commitSweep(const PendingShape& point) {
		++swept;
		BuiltSight built = sightBuilder(point.centre, point.ranges);
		if (!geometry::simpleAround(built.shape, point.centre)) {
			return;
		}
		if (geometry::nav::sightShapeFrontier(*mesh, built.shape, point.centre, *frontierScratch, pieces) ==
			geometry::nav::SightFrontier::Covered) {
			return;
		}
		PendingShape shape = pendingOf(std::move(built.shape), point.centre, Origin::Sweep);
		shape.sightPolygon = true;
		shape.ranges = built.rimRanges;
		enter(shape);
	}

	void ForestRecord::commitNow(const std::vector<Vec2i64>& shape, const Vec2i64& centre) {
		const PendingShape pending = pendingOf(shape, centre, Origin::Landing);
		if (!covered(pending)) {
			enter(pending);
		}
		while (!entering.empty()) {
			registerPiece();
		}
	}

	void ForestRecord::queueSightPolygon(
		std::vector<Vec2i64> polygon,
		const Vec2i64&		 centre,
		const SightRanges&	 rimRanges,
		std::vector<Vec2i64> walk,
		const SightRanges&	 walkStartRanges
	) {
		PendingShape shape = pendingOf(std::move(polygon), centre, Origin::Polygon);
		shape.queuedTick = ticks;
		shape.sightPolygon = true;
		shape.ranges = rimRanges;
		shape.sweepPath = std::move(walk);
		shape.walkStartRanges = walkStartRanges;
		waiting.push_back(std::move(shape));
	}

	void ForestRecord::queueSightAt(const Vec2i64& point, const SightRanges& ranges, std::vector<Vec2i64> walk, const SightRanges& walkStartRanges) {
		if (!sightBuilder) {
			return;
		}
		BuiltSight built = sightBuilder(point, ranges);
		if (geometry::simpleAround(built.shape, point)) {
			queueSightPolygon(std::move(built.shape), point, built.rimRanges, std::move(walk), walkStartRanges);
		}
	}

	bool ForestRecord::setOpenLeg(
		LegId								 leg,
		const Vec2i64&						 start,
		const Vec2i64&						 position,
		const SightRanges&					 ranges,
		const std::optional<InheritedShape>& inherited
	) {
		auto found = openLegs.find(leg);
		if (found == openLegs.end()) {
			PendingShape shape;
			shape.origin = Origin::Leg;
			shape.legRanges = ranges;
			shape.legShape = rimOf({0, 0}, ranges);
			shape.legPosition = position;
			movedBy(shape.legShape, start, shape.legHeld);
			if (inherited.has_value()) {
				hullOfConvex(shape.legHeld, rimOf(inherited->position, inherited->ranges), shape.legHeld);
			}
			if (position == start) {
				ring = shape.legHeld;
			} else {
				hullOfConvex(shape.legHeld, movedBy(shape.legShape, position, moved), ring);
			}
			if (ring.size() > kSoftLegMostVertices) {
				return false;
			}
			boxOf(ring, shape.low, shape.high);
			shape.outline = ring;
			shape.centre = start;
			openLegs.emplace(leg, std::move(shape));
			return true;
		}
		PendingShape& shape = found->second;
		if (shape.legPosition == position && shape.legRanges == ranges) {
			return true;
		}
		const bool changed = shape.legRanges != ranges;
		const std::vector<Vec2i64>& held = changed ? shape.outline : shape.legHeld;
		const std::vector<Vec2i64>	newShape = changed ? rimOf({0, 0}, ranges) : std::vector<Vec2i64>{};
		hullOfConvex(held, movedBy(changed ? newShape : shape.legShape, position, moved), ring);
		if (ring.size() > kSoftLegMostVertices) {
			return false;
		}
		if (changed) {
			shape.legHeld = shape.outline;
			shape.legShape = newShape;
			shape.legRanges = ranges;
		}
		shape.legPosition = position;
		boxOf(ring, shape.low, shape.high);
		std::swap(shape.outline, ring);
		return true;
	}

	void ForestRecord::closeLeg(LegId leg) {
		auto found = openLegs.find(leg);
		if (found == openLegs.end()) {
			return;
		}
		found->second.queuedTick = ticks;
		waiting.push_back(std::move(found->second));
		openLegs.erase(found);
	}

	void ForestRecord::update() {
		++ticks;
		if (scheduler.testWhilePiecesWait) {
			for (std::size_t tested = 0; tested < vision::kSeenRingsPerTick && !waiting.empty(); ++tested) {
				if (!covered(waiting.front())) {
					uncovered.push_back(std::move(waiting.front()));
				}
				waiting.pop_front();
			}
			for (std::size_t cut = 0; entering.size() < scheduler.piecesPerTick && cut < vision::kSeenRingsPerTick && !uncovered.empty(); ++cut) {
				enter(uncovered.front());
				uncovered.pop_front();
			}
		} else {
			for (std::size_t tested = 0; entering.size() < scheduler.piecesPerTick && tested < vision::kSeenRingsPerTick && !waiting.empty(); ++tested) {
				if (!covered(waiting.front())) {
					enter(waiting.front());
				}
				waiting.pop_front();
			}
		}
		const bool registering = !entering.empty();
		for (std::size_t count = 0; count < scheduler.piecesPerTick && !entering.empty(); ++count) {
			registerPiece();
		}
		if (!sightBuilder) {
			return;
		}
		if (scheduler.sweepsPerTick > 0) {
			for (std::size_t built = 0; built < scheduler.sweepsPerTick && !sweeps.empty(); ++built) {
				const PendingShape point = std::move(sweeps.front());
				sweeps.pop_front();
				commitSweep(point);
			}
		} else if (!registering && !sweeps.empty()) {
			const PendingShape point = std::move(sweeps.front());
			sweeps.pop_front();
			commitSweep(point);
		}
	}

	ForestRecord::Queue ForestRecord::queue() const { return {openLegs.size(), waiting.size() + uncovered.size(), entering.size()}; }

	double ForestRecord::committedSquareMeters() const {
		const auto& triangles = mesh->triangles();
		const auto& vertices = mesh->vertices();
		double		doubled = 0.0;
		for (std::int32_t t = 0; t < static_cast<std::int32_t>(triangles.size()); ++t) {
			if (!mesh->triangleLive(t)) {
				continue;
			}
			const geometry::nav::NavTriangle& triangle = triangles[static_cast<std::size_t>(t)];
			doubled += geometry::cross(vertices[triangle.v[1]] - vertices[triangle.v[0]], vertices[triangle.v[2]] - vertices[triangle.v[0]]).toDouble();
		}
		return doubled / 2e6;
	}

} // namespace sight_bench
