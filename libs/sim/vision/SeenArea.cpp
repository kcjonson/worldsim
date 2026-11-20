#include "SeenArea.h"

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
#include <cassert>
#include <cmath>
#include <iterator>
#include <numbers>
#include <utility>

namespace vision {

	using geometry::Vec2i64;

	namespace {

		// How many millimetres lastSeenToward steps back from a stretch's end for a point that rounds
		// onto seen ground.
		constexpr int kStepsBack = 8;

		bool convex(const std::vector<Vec2i64>& ring) {
			for (std::size_t i = 0; i < ring.size(); ++i) {
				const Vec2i64& previous = ring[(i + ring.size() - 1) % ring.size()];
				const Vec2i64& next		= ring[(i + 1) % ring.size()];
				if (geometry::cross(ring[i] - previous, next - ring[i]).sign() < 0) {
					return false;
				}
			}
			return true;
		}

		void boxOf(const std::vector<Vec2i64>& ring, Vec2i64& low, Vec2i64& high) {
			low	 = ring.front();
			high = ring.front();
			for (const Vec2i64& point : ring) {
				low	 = {std::min(low.x, point.x), std::min(low.y, point.y)};
				high = {std::max(high.x, point.x), std::max(high.y, point.y)};
			}
		}

		bool boxesMeet(const Vec2i64& firstLow, const Vec2i64& firstHigh, const Vec2i64& secondLow, const Vec2i64& secondHigh) {
			return firstLow.x <= secondHigh.x && secondLow.x <= firstHigh.x && firstLow.y <= secondHigh.y && secondLow.y <= firstHigh.y;
		}

		std::int64_t floorDivide(std::int64_t value, std::int64_t divisor) {
			const std::int64_t quotient = value / divisor;
			return value % divisor < 0 ? quotient - 1 : quotient;
		}

		// The cell of K's grid of registered boxes.
		constexpr std::int64_t kRingBoxCellMm = 32000;

		// The seen-box cell holding `millimetres` along one axis, rounding toward negative infinity.
		std::int64_t cellOf(std::int64_t millimetres) {
			const std::int64_t cell = millimetres / kSeenBoxCellMm;
			return millimetres % kSeenBoxCellMm < 0 ? cell - 1 : cell;
		}

		bool insideBox(const Vec2i64& point, const Vec2i64& low, const Vec2i64& high) {
			return point.x >= low.x && point.x <= high.x && point.y >= low.y && point.y <= high.y;
		}

		bool allEqual(const SightRanges& ranges) {
			return std::all_of(ranges.begin(), ranges.end(), [&ranges](std::int64_t range) { return range == ranges.front(); });
		}

		std::int64_t mostOf(const SightRanges& ranges) { return *std::max_element(ranges.begin(), ranges.end()); }

		double distanceBetween(const Vec2i64& first, const Vec2i64& second) {
			const Vec2i64 gap = first - second;
			return std::sqrt(geometry::dot(gap, gap).toDouble());
		}

		double crossOf(double firstX, double firstY, double secondX, double secondY) { return firstX * secondY - firstY * secondX; }

		// Where the segment from `from` along `direction` meets the edge (edgeStart, edgeEnd), as
		// parameters along the segment in [0, 1]: one for a crossing, the overlap's ends for an edge
		// in line with it.
		void meetings(const Vec2i64& from, const Vec2i64& direction, const Vec2i64& edgeStart, const Vec2i64& edgeEnd, std::vector<double>& out) {
			const Vec2i64 edge		 = edgeEnd - edgeStart;
			const Vec2i64 offset	 = edgeStart - from;
			const int	  turn		 = geometry::cross(direction, edge).sign();
			const double  directionX = static_cast<double>(direction.x);
			const double  directionY = static_cast<double>(direction.y);
			if (turn == 0) {
				if (geometry::cross(offset, direction).sign() != 0) {
					return;
				}
				const double lengthSquared = directionX * directionX + directionY * directionY;
				for (const Vec2i64& end : {edgeStart, edgeEnd}) {
					const Vec2i64 reach = end - from;
					const double  along = (static_cast<double>(reach.x) * directionX + static_cast<double>(reach.y) * directionY) / lengthSquared;
					if (along >= 0.0 && along <= 1.0) {
						out.push_back(along);
					}
				}
				return;
			}
			const double denominator = crossOf(directionX, directionY, static_cast<double>(edge.x), static_cast<double>(edge.y));
			const double along = crossOf(static_cast<double>(offset.x), static_cast<double>(offset.y), static_cast<double>(edge.x), static_cast<double>(edge.y)) / denominator;
			const double onEdge = crossOf(static_cast<double>(offset.x), static_cast<double>(offset.y), directionX, directionY) / denominator;
			if (along >= 0.0 && along <= 1.0 && onEdge >= 0.0 && onEdge <= 1.0) {
				out.push_back(along);
			}
		}

		// Whether any vertex of `pieces` lies within kSeenSweepRimBandMm of the rim of `ranges` round
		// `centre`, read on each vertex against the range of the side it faces: with every range equal,
		// past the circle that far inside the sight.
		bool addsNearRim(const std::vector<std::vector<Vec2i64>>& pieces, const Vec2i64& centre, const SightRanges& ranges) {
			for (const std::vector<Vec2i64>& piece : pieces) {
				for (const Vec2i64& vertex : piece) {
					const Vec2i64 offset	= vertex - centre;
					std::size_t	  facing	= 0;
					std::int64_t  mostAlong = 0;
					for (std::size_t side = 0; side < geometry::kSightSides; ++side) {
						const std::int64_t along = geometry::kSightSideNormals[side][0] * offset.x + geometry::kSightSideNormals[side][1] * offset.y;
						if (side == 0 || along > mostAlong) {
							mostAlong = along;
							facing	  = side;
						}
					}
					const std::int64_t inner = ranges[facing] - kSeenSweepRimBandMm;
					if (!geometry::withinDistanceOfSegment(vertex, centre, centre, inner)) {
						return true;
					}
				}
			}
			return false;
		}

		Vec2i64 pointAlong(const Vec2i64& from, const Vec2i64& direction, double along) {
			return {from.x + std::llround(static_cast<double>(direction.x) * along), from.y + std::llround(static_cast<double>(direction.y) * along)};
		}

		// A convex ring's two chains between its lexicographically least and greatest vertices, each in
		// increasing order.
		void appendChains(const std::vector<Vec2i64>& ring, std::vector<Vec2i64>& lower, std::vector<Vec2i64>& upper) {
			const std::size_t count = ring.size();
			const auto		  least = static_cast<std::size_t>(std::min_element(ring.begin(), ring.end()) - ring.begin());
			const auto		  most	= static_cast<std::size_t>(std::max_element(ring.begin(), ring.end()) - ring.begin());
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

		// One monotone chain of the hull, turning left only, so points in line are dropped. A leg's
		// points lie well under 2^31 mm apart, so the cross products of their differences fit an int64.
		void hullChain(const std::vector<Vec2i64>& sorted, bool forward, std::vector<Vec2i64>& out) {
			const std::size_t first = out.size();
			const std::size_t count = sorted.size();
			for (std::size_t k = 0; k < count; ++k) {
				const Vec2i64& point = sorted[forward ? k : count - 1 - k];
				while (out.size() >= first + 2) {
					const Vec2i64 last	 = out.back() - out[out.size() - 2];
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

	} // namespace

	struct ConvexHullScratch {
		std::vector<Vec2i64> firstLower, firstUpper, secondLower, secondUpper, lower, upper;
	};

	namespace {

		// The convex hull of two convex counter-clockwise rings: each one's lower chains merged in order
		// and swept once, and the upper the same. `out` may be `first`.
		void hullOfConvexInto(const std::vector<Vec2i64>& first, const std::vector<Vec2i64>& second, ConvexHullScratch& hull, std::vector<Vec2i64>& out) {
			hull.firstLower.clear();
			hull.firstUpper.clear();
			hull.secondLower.clear();
			hull.secondUpper.clear();
			hull.lower.clear();
			hull.upper.clear();
			appendChains(first, hull.firstLower, hull.firstUpper);
			appendChains(second, hull.secondLower, hull.secondUpper);
			std::merge(hull.firstLower.begin(), hull.firstLower.end(), hull.secondLower.begin(), hull.secondLower.end(), std::back_inserter(hull.lower));
			std::merge(hull.firstUpper.begin(), hull.firstUpper.end(), hull.secondUpper.begin(), hull.secondUpper.end(), std::back_inserter(hull.upper));
			out.clear();
			hullChain(hull.lower, true, out);
			hullChain(hull.upper, false, out);
		}

		std::vector<Vec2i64>& movedBy(const std::vector<Vec2i64>& ring, const Vec2i64& offset, std::vector<Vec2i64>& out) {
			out.clear();
			for (const Vec2i64& point : ring) {
				out.push_back(point + offset);
			}
			return out;
		}

	} // namespace

	std::int64_t drawnVisibleRadiusMm(std::int64_t sightMm) {
		std::int64_t diagonal = 0;
		while (diagonal * diagonal < 2 * kSeenSlackMm * kSeenSlackMm) {
			++diagonal;
		}
		return geometry::sightInradius(sightMm - kSeenSlackMm) - diagonal;
	}

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

	SeenArea::SeenArea()
		: mesh(std::make_unique<geometry::nav::PatchableMesh>()),
		  scratch(std::make_unique<geometry::nav::SightCoverScratch>()),
		  frontierScratch(std::make_unique<geometry::nav::SightFrontierScratch>()),
		  hullScratch(std::make_unique<ConvexHullScratch>()) {}

	SeenArea::~SeenArea() = default;

	SeenArea::PendingShape SeenArea::pendingOf(std::vector<Vec2i64> outline, const Vec2i64& centre, Origin origin) {
		PendingShape shape;
		boxOf(outline, shape.low, shape.high);
		shape.convex  = convex(outline);
		shape.outline = std::move(outline);
		shape.centre  = centre;
		shape.origin  = origin;
		return shape;
	}

	void SeenArea::hullOfConvex(const std::vector<Vec2i64>& first, const std::vector<Vec2i64>& second, std::vector<Vec2i64>& out) {
		hullOfConvexInto(first, second, *hullScratch, out);
		++work.legJoins;
		work.legVerticesJoined += first.size() + second.size();
	}

	// Test tight, register loose (player-visibility.md 3.3): the covered test reads the true shape.
	bool SeenArea::covered(const PendingShape& shape) {
		mostTestWait = std::max(mostTestWait, ticks - shape.queuedTick);
		++work.shapesTested;
		if (geometry::nav::sightShapeCovered(*mesh, shape.outline, shape.centre, *scratch)) {
			++dropped;
			return true;
		}
		return false;
	}

	// A sight shape that adds ground enters grown by the skirt along every side but its rim, and what
	// it adds is cut into pieces that wait, pending, to be registered. A pending shape is entered
	// whatever its skirt does, so nothing shown as seen is lost.
	void SeenArea::enter(const PendingShape& shape) {
		std::vector<Vec2i64> grown;
		if (shape.sightRadius > 0) {
			grown = geometry::skirtedSightShape(shape.outline, shape.centre, rimOf(shape.centre, shape.rimRanges), geometry::kSeenSkirtMm);
			skipped += grown.empty() ? 1 : 0;
		}
		const std::vector<Vec2i64>& cut = grown.empty() ? shape.outline : grown;
		++work.shapesCut;
		work.verticesCut += cut.size();
		if (geometry::nav::sightShapeFrontier(*mesh, cut, shape.centre, *frontierScratch, pieces) == geometry::nav::SightFrontier::Covered) {
			++dropped;
			return;
		}
		if (!shape.sweepPath.empty() && sightBuilder && addsNearRim(pieces, shape.centre, shape.rimRanges)) {
			queueSweep(shape);
		}
		std::deque<PendingShape>& into = shape.origin == Origin::Sweep ? sweepEntering : entering;
		for (std::vector<Vec2i64>& piece : pieces) {
			into.push_back(pendingOf(std::move(piece), shape.centre, shape.origin));
		}
	}

	void SeenArea::registerPiece(std::deque<PendingShape>& from) {
		markPhase(TickPhase::Patch);
		PendingShape& piece = from.front();
		holdCommitted(piece.low, piece.high);
		if (changedBoxes.size() == kSeenChangedBoxesMost) {
			Box whole = changedBoxes.front();
			for (const Box& box : changedBoxes) {
				whole.low  = {std::min(whole.low.x, box.low.x), std::min(whole.low.y, box.low.y)};
				whole.high = {std::max(whole.high.x, box.high.x), std::max(whole.high.y, box.high.y)};
			}
			changedBoxes.assign(1, whole);
		}
		changedBoxes.push_back({piece.low, piece.high});
		++registeredFrom[static_cast<std::size_t>(piece.origin)];
		++work.piecesRegistered;
		work.verticesRegistered += piece.outline.size();
		geometry::nav::PatchEdits edits;
		edits.adds.push_back(geometry::nav::NavInputPolygon{.ring = std::move(piece.outline)});
		from.pop_front();
		const geometry::nav::PatchReport report = mesh->applyPatch(edits);
		for (const std::int32_t added : report.added) {
			if (added == geometry::nav::kNoRing) {
				continue;
			}
			registered.push_back(added);
			if (!ringsMetCounted) {
				continue;
			}
			const std::vector<Vec2i64>& ring = mesh->ring(added).polygon.ring;
			Box							box{ring.front(), ring.front()};
			for (const Vec2i64& point : ring) {
				box.low	 = {std::min(box.low.x, point.x), std::min(box.low.y, point.y)};
				box.high = {std::max(box.high.x, point.x), std::max(box.high.y, point.y)};
			}
			// The boxes met are read from a grid of kRingBoxCellMm cells, so the work is the boxes near this
			// one, not every box registered this session; a box in several of its cells is counted once.
			const auto		   index = static_cast<std::uint32_t>(registeredBoxes.size());
			const std::int64_t lowX	 = floorDivide(box.low.x, kRingBoxCellMm);
			const std::int64_t lowY	 = floorDivide(box.low.y, kRingBoxCellMm);
			const std::int64_t highX = floorDivide(box.high.x, kRingBoxCellMm);
			const std::int64_t highY = floorDivide(box.high.y, kRingBoxCellMm);
			std::size_t		   met	 = 0;
			for (std::int64_t cellY = lowY; cellY <= highY; ++cellY) {
				for (std::int64_t cellX = lowX; cellX <= highX; ++cellX) {
					std::vector<std::uint32_t>& inCell = ringBoxCells[{cellX, cellY}];
					for (const std::uint32_t other : inCell) {
						if (ringBoxCounted[other] != index && boxesMeet(box.low, box.high, registeredBoxes[other].low, registeredBoxes[other].high)) {
							ringBoxCounted[other] = index;
							++met;
						}
					}
					inCell.push_back(index);
				}
			}
			if (index > 0) {
				ringsMetTotal += met;
				ringsMetGreatest = std::max(ringsMetGreatest, met);
			}
			registeredBoxes.push_back(box);
			ringBoxCounted.push_back(index);
		}
		markPhase(TickPhase::Widths);
		mesh->refreshWidths();
	}

	// A full queue thins each run, a walk's points between two builds' shapes, on its own: its first and
	// last points stay and every other point between them goes, so a run keeps its ends whatever the
	// load. When no run has a point between its ends to give, the oldest run goes whole, its stretch
	// left to vision's builds.
	void SeenArea::thinSweeps() {
		std::deque<PendingShape> kept;
		for (std::size_t first = 0; first < sweeps.size();) {
			std::size_t end = first;
			while (end < sweeps.size() && sweeps[end].sweepRun == sweeps[first].sweepRun) {
				++end;
			}
			for (std::size_t index = first; index < end; ++index) {
				if (index == first || index + 1 == end || (index - first) % 2 == 0) {
					kept.push_back(std::move(sweeps[index]));
				}
			}
			first = end;
		}
		if (kept.size() == sweeps.size()) {
			const std::uint64_t oldest = kept.front().sweepRun;
			while (!kept.empty() && kept.front().sweepRun == oldest) {
				kept.pop_front();
			}
			++sweepRunsDropped;
		}
		const std::size_t dropping = sweeps.size() - kept.size();
		sweepsDropped += dropping;
		work.sweepPointsDropped += dropping;
		sweeps = std::move(kept);
	}

	// Points no farther apart than kSeenSweepSpacingMm along the walk from the build before to this
	// one, its turns among them; the two builds had shapes of their own. Each point's rim is the two
	// builds' rims interpolated by where it lies along the walk (sight-softening.md 6.4), held in by
	// the inset of the nearer build: their rims share the 64 side normals, so a point `apart` from a
	// build sees past that build's rim by at most apart x pi / kSightSides plus apart squared over twice
	// the sight, `apart` taking the most its rim's vertices stray from that build's as well as its
	// distance, which would otherwise register a sliver beside every vertex.
	void SeenArea::queueSweep(const PendingShape& shape) {
		const std::vector<Vec2i64>& walk		  = shape.sweepPath;
		const Vec2i64&				to			  = shape.centre;
		const double				halfSideAngle = std::numbers::pi / static_cast<double>(geometry::kSightSides);
		double						walkLength	  = 0.0;
		for (std::size_t corner = 0; corner < walk.size(); ++corner) {
			walkLength += distanceBetween(walk[corner], corner + 1 < walk.size() ? walk[corner + 1] : to);
		}
		double			   walked = 0.0;
		const std::uint64_t run	   = ++sweepRuns;
		auto			   queuePoint = [&](const Vec2i64& point, double along) {
			  const double		 fraction  = walkLength > 0.0 ? std::clamp(along / walkLength, 0.0, 1.0) : 0.0;
			  const bool		 nearStart = distanceBetween(point, walk.front()) <= distanceBetween(point, to);
			  const SightRanges& nearer	   = nearStart ? shape.walkStartRanges : shape.rimRanges;
			  SightRanges		 interpolated;
			  std::int64_t		 strays = 0;
			  for (std::size_t side = 0; side < geometry::kSightSides; ++side) {
				  const double start = static_cast<double>(shape.walkStartRanges[side]);
				  interpolated[side] = static_cast<std::int64_t>(std::floor(start + (static_cast<double>(shape.rimRanges[side]) - start) * fraction));
				  strays			 = std::max(strays, std::abs(interpolated[side] - nearer[side]));
			  }
			  const double apart   = std::min(distanceBetween(point, walk.front()), distanceBetween(point, to)) + static_cast<double>(strays);
			  const double inset   = halfSideAngle * apart + apart * apart / (2.0 * static_cast<double>(mostOf(interpolated)));
			  const auto   insetMm = static_cast<std::int64_t>(std::ceil(inset)) + 1;
			  PendingShape sweep;
			  sweep.centre		= point;
			  sweep.sightRadius = shape.sightRadius;
			  for (std::size_t side = 0; side < geometry::kSightSides; ++side) {
				  sweep.rimRanges[side] = std::max(geometry::kForestShapeLeastRangeMm, interpolated[side] - insetMm);
			  }
			  sweep.sweepRun = run;
			  if (sweeps.size() == kSeenSweepPointsMost) {
				  thinSweeps();
			  }
			  sweeps.push_back(std::move(sweep));
		};
		for (std::size_t corner = 0; corner < walk.size(); ++corner) {
			const Vec2i64&	   from	   = walk[corner];
			const Vec2i64&	   until   = corner + 1 < walk.size() ? walk[corner + 1] : to;
			const Vec2i64	   along   = until - from;
			const double	   stretch = distanceBetween(until, from);
			const std::int64_t steps   = static_cast<std::int64_t>(std::ceil(stretch / static_cast<double>(kSeenSweepSpacingMm)));
			if (corner > 0) {
				queuePoint(from, walked);
			}
			for (std::int64_t step = 1; step < steps; ++step) {
				queuePoint(from + Vec2i64{along.x * step / steps, along.y * step / steps}, walked + stretch * static_cast<double>(step) / static_cast<double>(steps));
			}
			walked += stretch;
		}
	}

	// A sweep shape was never pending, so the frontier of the true shape is its test, in place of the
	// covered test: that test's exact fallback reads every ring near a shadow edge, which is slow where
	// the record is dense, and what it would settle lies within kSeenCoverToleranceMm of a ray. Test
	// tight, register loose: a shape that adds nothing as it is would add the skirt's sliver once grown,
	// so the test is not the cut enter() makes of the skirted shape.
	void SeenArea::commitSweep(const PendingShape& point) {
		++swept;
		++work.sweepPointsBuilt;
		BuiltSight built = sightBuilder(point.centre, point.sightRadius, point.rimRanges);
		if (!geometry::simpleAround(built.shape, point.centre)) {
			++skipped;
			return;
		}
		++work.sweepFrontierTests;
		work.verticesCut += built.shape.size();
		if (geometry::nav::sightShapeFrontier(*mesh, built.shape, point.centre, *frontierScratch, pieces) == geometry::nav::SightFrontier::Covered) {
			++dropped;
			return;
		}
		PendingShape shape = pendingOf(std::move(built.shape), point.centre, Origin::Sweep);
		shape.sightRadius  = point.sightRadius;
		shape.rimRanges	   = built.rimRanges;
		enter(shape);
	}

	void SeenArea::commitNow(const std::vector<Vec2i64>& shape, const Vec2i64& centre) {
		const PendingShape pending = pendingOf(shape, centre, Origin::Other);
		if (!covered(pending)) {
			enter(pending);
		}
		while (!entering.empty()) {
			registerPiece(entering);
		}
	}

	void SeenArea::queue(std::vector<Vec2i64> shape, const Vec2i64& centre) {
		PendingShape pending = pendingOf(std::move(shape), centre, Origin::Other);
		pending.queuedTick	 = ticks;
		waiting.push_back(std::move(pending));
	}

	void SeenArea::queueSightPolygon(
		std::vector<Vec2i64> polygon,
		const Vec2i64&		 centre,
		std::int64_t		 sightRadiusMm,
		const SightRanges&	 rimRanges,
		std::vector<Vec2i64> walk,
		const SightRanges&	 walkStartRanges
	) {
		PendingShape shape	  = pendingOf(std::move(polygon), centre, Origin::Polygon);
		shape.queuedTick	  = ticks;
		shape.sightRadius	  = sightRadiusMm;
		shape.rimRanges		  = rimRanges;
		shape.sweepPath		  = std::move(walk);
		shape.walkStartRanges = walkStartRanges;
		waiting.push_back(std::move(shape));
	}

	void SeenArea::queueSightAt(
		const Vec2i64&		 point,
		std::int64_t		 sightRadiusMm,
		const SightRanges&	 rimRanges,
		std::vector<Vec2i64> walk,
		const SightRanges&	 walkStartRanges
	) {
		if (!sightBuilder) {
			return;
		}
		BuiltSight built = sightBuilder(point, sightRadiusMm, rimRanges);
		if (geometry::simpleAround(built.shape, point)) {
			queueSightPolygon(std::move(built.shape), point, sightRadiusMm, built.rimRanges, std::move(walk), walkStartRanges);
		}
	}

	bool SeenArea::setOpenLeg(LegId leg, const Vec2i64& start, const Vec2i64& position, const SightRanges& ranges, const std::optional<InheritedShape>& inherited) {
		assert(std::all_of(ranges.begin(), ranges.end(), [](std::int64_t range) { return range >= geometry::kForestShapeLeastRangeMm; }));
		auto found = openLegs.find(leg);
		if (found == openLegs.end()) {
			PendingShape shape;
			shape.origin	  = Origin::Leg;
			shape.legRanges	  = ranges;
			shape.legShape	  = rimOf({0, 0}, ranges);
			shape.legPosition = position;
			movedBy(shape.legShape, start, shape.legHeld);
			if (inherited.has_value()) {
				hullOfConvex(shape.legHeld, geometry::forestShape(inherited->position, inherited->ranges), shape.legHeld);
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
			shape.convex  = true;
			shape.outline = ring;
			shape.centre  = start;
			openLegs.emplace(leg, std::move(shape));
			return true;
		}
		PendingShape& shape = found->second;
		if (shape.legPosition == position && shape.legRanges == ranges) {
			return true;
		}
		const bool changed = shape.legRanges != ranges;
		// The present ranges run on to where he stands, then any new ranges' shape joins there: the
		// bright shape still eases toward the old ranges' shape at his position on the frame they change.
		hullOfConvex(changed ? shape.outline : shape.legHeld, movedBy(shape.legShape, position, moved), ring);
		std::vector<Vec2i64> newShape;
		if (changed) {
			newShape = rimOf({0, 0}, ranges);
			hullOfConvex(ring, movedBy(newShape, position, moved), ring);
		}
		if (ring.size() > kSoftLegMostVertices) {
			return false;
		}
		if (changed) {
			shape.legHeld	= ring;
			shape.legShape	= newShape;
			shape.legRanges = ranges;
		}
		shape.legPosition = position;
		boxOf(ring, shape.low, shape.high);
		std::swap(shape.outline, ring);
		return true;
	}

	void SeenArea::closeLeg(LegId leg) {
		auto found = openLegs.find(leg);
		if (found == openLegs.end()) {
			return;
		}
		found->second.queuedTick = ticks;
		waiting.push_back(std::move(found->second));
		openLegs.erase(found);
	}

	void SeenArea::markPhase(TickPhase phase) const {
		if (phaseListener != nullptr) {
			phaseListener->phaseBegins(phase);
		}
	}

	void SeenArea::update() {
		markPhase(TickPhase::Tests);
		for (std::size_t tested = 0; tested < kSeenRingsPerTick && !waiting.empty(); ++tested) {
			if (!covered(waiting.front())) {
				uncovered.push_back(std::move(waiting.front()));
			}
			waiting.pop_front();
		}
		markPhase(TickPhase::Cuts);
		for (std::size_t cut = 0; entering.size() < kSeenPiecesRegisteredPerTick && cut < kSeenRingsPerTick && !uncovered.empty(); ++cut) {
			mostCutWait = std::max(mostCutWait, ticks - uncovered.front().queuedTick);
			const PendingShape shape = std::move(uncovered.front());
			uncovered.pop_front();
			enter(shape);
		}
		// Shapes' pieces first, the sweep's with what is left: the sweep's pieces never hold the shapes'
		// cut back, since it reads only the shapes' pieces waiting.
		std::size_t count = 0;
		for (; count < kSeenPiecesRegisteredPerTick && !entering.empty(); ++count) {
			registerPiece(entering);
		}
		for (; count < kSeenPiecesRegisteredPerTick && !sweepEntering.empty(); ++count) {
			registerPiece(sweepEntering);
		}
		// The sweep's own budget, whatever the shapes hold, while its own pieces drain.
		markPhase(TickPhase::Sweeps);
		for (std::size_t built = 0; sightBuilder && built < kSeenSweepPointsPerTick && sweepEntering.size() < kSeenPiecesRegisteredPerTick && !sweeps.empty(); ++built) {
			const PendingShape point = std::move(sweeps.front());
			sweeps.pop_front();
			commitSweep(point);
		}
		markPhase(TickPhase::End);
		++ticks;
		lastWork = work;
		work	 = TickWork{};
	}

	SeenArea::Pending SeenArea::pending() const {
		return {openLegs.size(), waiting.size() + uncovered.size(), entering.size() + sweepEntering.size()};
	}

	template <typename Visit> void SeenArea::forEachPending(Visit visit) const {
		for (const auto& [leg, shape] : openLegs) {
			visit(shape);
		}
		for (const PendingShape& shape : waiting) {
			visit(shape);
		}
		for (const PendingShape& shape : uncovered) {
			visit(shape);
		}
		for (const PendingShape& piece : entering) {
			visit(piece);
		}
		for (const PendingShape& piece : sweepEntering) {
			visit(piece);
		}
	}

	bool SeenArea::isSeen(const Vec2i64& point) const {
		if (inRecord(point)) {
			return true;
		}
		bool seen = false;
		forEachPending([&](const PendingShape& shape) {
			seen = seen || (insideBox(point, shape.low, shape.high) && geometry::pointInPolygon(point, shape.outline) != geometry::PointInPolygon::Outside);
		});
		return seen;
	}

	bool SeenArea::covers(const std::vector<Vec2i64>& outline) const {
		if (outline.size() < 3) {
			return std::all_of(outline.begin(), outline.end(), [this](const Vec2i64& point) { return isSeen(point); });
		}
		if (mesh->groundCovers(outline)) {
			return true;
		}
		Vec2i64 low;
		Vec2i64 high;
		boxOf(outline, low, high);
		bool held	 = false;
		bool nearAny = false;
		forEachPending([&](const PendingShape& shape) {
			if (held || !boxesMeet(low, high, shape.low, shape.high)) {
				return;
			}
			nearAny = true;
			held	= shape.convex && std::all_of(outline.begin(), outline.end(), [&shape](const Vec2i64& point) {
						  return geometry::pointInPolygon(point, shape.outline) != geometry::PointInPolygon::Outside;
					  });
		});
		if (held || !nearAny) {
			return held;
		}

		// Ground from the record and the pending shapes together near the outline: the record's
		// triangles there and the pending shapes that reach it, each a ground ring.
		geometry::nav::PatchEdits edits;
		forEachPending([&](const PendingShape& shape) {
			if (boxesMeet(low, high, shape.low, shape.high)) {
				edits.adds.push_back(geometry::nav::NavInputPolygon{.ring = shape.outline});
			}
		});
		std::vector<Triangle> triangles;
		appendTriangles(triangles);
		for (const Triangle& triangle : triangles) {
			const std::vector<Vec2i64> corners(triangle.corners.begin(), triangle.corners.end());
			Vec2i64					   triangleLow;
			Vec2i64					   triangleHigh;
			boxOf(corners, triangleLow, triangleHigh);
			if (boxesMeet(low, high, triangleLow, triangleHigh)) {
				edits.adds.push_back(geometry::nav::NavInputPolygon{.ring = corners});
			}
		}
		geometry::nav::PatchableMesh together;
		together.applyPatch(edits);
		return together.groundCovers(outline);
	}

	std::optional<Vec2i64> SeenArea::lastSeenToward(const Vec2i64& from, const Vec2i64& to) const {
		if (isSeen(to)) {
			return to;
		}
		const Vec2i64 direction = to - from;
		Vec2i64		  low{std::min(from.x, to.x), std::min(from.y, to.y)};
		Vec2i64		  high{std::max(from.x, to.x), std::max(from.y, to.y)};

		std::vector<double>						 along{0.0, 1.0};
		std::vector<std::pair<Vec2i64, Vec2i64>> edges;
		appendEdges(low, high, edges);
		for (const auto& [edgeStart, edgeEnd] : edges) {
			meetings(from, direction, edgeStart, edgeEnd, along);
		}
		forEachPending([&](const PendingShape& shape) {
			if (!boxesMeet(low, high, shape.low, shape.high)) {
				return;
			}
			for (std::size_t k = 0; k < shape.outline.size(); ++k) {
				meetings(from, direction, shape.outline[k], shape.outline[(k + 1) % shape.outline.size()], along);
			}
		});
		std::sort(along.begin(), along.end());
		along.erase(std::unique(along.begin(), along.end()), along.end());

		// Between two neighbouring meetings the segment is wholly seen or wholly not, so the last
		// seen stretch ends at the meeting after the last stretch whose middle is seen. Rounding to
		// the millimetre can put that end a hair outside, so it steps back toward the middle.
		const double lengthMm = std::sqrt(static_cast<double>(direction.x) * static_cast<double>(direction.x) +
										  static_cast<double>(direction.y) * static_cast<double>(direction.y));
		for (std::size_t i = along.size() - 1; i > 0; --i) {
			const double  middle	  = (along[i - 1] + along[i]) / 2.0;
			const Vec2i64 middlePoint = pointAlong(from, direction, middle);
			if (!isSeen(middlePoint)) {
				continue;
			}
			for (int millimetres = 0; millimetres <= kStepsBack && along[i] - millimetres / lengthMm > middle; ++millimetres) {
				const Vec2i64 candidate = pointAlong(from, direction, along[i] - millimetres / lengthMm);
				if (isSeen(candidate)) {
					return candidate;
				}
			}
			return middlePoint;
		}
		return isSeen(from) ? std::optional<Vec2i64>(from) : std::nullopt;
	}

	std::uint64_t SeenArea::epoch() const { return mesh->epoch(); }

	bool SeenArea::inRecord(const Vec2i64& point) const { return mesh->locate(point) >= 0; }

	void SeenArea::appendEdges(const Vec2i64& low, const Vec2i64& high, std::vector<std::pair<Vec2i64, Vec2i64>>& out) const {
		std::vector<std::int32_t>				 edgeIds;
		std::vector<std::pair<Vec2i64, Vec2i64>> edges;
		mesh->meshEdgesNear(low, high, edgeIds, edges);
		out.insert(out.end(), edges.begin(), edges.end());
	}

	void SeenArea::takeChangedBoxes(std::vector<Box>& out) {
		out.insert(out.end(), changedBoxes.begin(), changedBoxes.end());
		changedBoxes.clear();
	}

	void SeenArea::appendTriangles(std::vector<Triangle>& out) const {
		const auto& triangles = mesh->triangles();
		const auto& vertices  = mesh->vertices();
		for (std::int32_t t = 0; t < static_cast<std::int32_t>(triangles.size()); ++t) {
			if (!mesh->triangleLive(t)) {
				continue;
			}
			const geometry::nav::NavTriangle& triangle = triangles[static_cast<std::size_t>(t)];
			out.push_back({{vertices[triangle.v[0]], vertices[triangle.v[1]], vertices[triangle.v[2]]}});
		}
	}

	void SeenArea::appendPendingShapes(std::vector<const std::vector<Vec2i64>*>& out) const {
		forEachPending([&out](const PendingShape& shape) { out.push_back(&shape.outline); });
	}

	void SeenArea::holdCommitted(const Vec2i64& low, const Vec2i64& high) {
		for (std::int64_t cellY = cellOf(low.y); cellY <= cellOf(high.y); ++cellY) {
			for (std::int64_t cellX = cellOf(low.x); cellX <= cellOf(high.x); ++cellX) {
				const Vec2i64 cellLow{cellX * kSeenBoxCellMm, cellY * kSeenBoxCellMm};
				const Vec2i64 cellHigh{cellLow.x + kSeenBoxCellMm, cellLow.y + kSeenBoxCellMm};
				const Box	  part{{std::max(low.x, cellLow.x), std::max(low.y, cellLow.y)}, {std::min(high.x, cellHigh.x), std::min(high.y, cellHigh.y)}};
				const auto [held, fresh] = committedBoxes.try_emplace({cellX, cellY}, part);
				if (!fresh) {
					held->second.low  = {std::min(held->second.low.x, part.low.x), std::min(held->second.low.y, part.low.y)};
					held->second.high = {std::max(held->second.high.x, part.high.x), std::max(held->second.high.y, part.high.y)};
				}
			}
		}
	}

	template <typename Take> void SeenArea::forEachSeenBox(const Vec2i64& low, const Vec2i64& high, Take take) const {
		auto takeMeeting = [&](const Box& box) {
			if (boxesMeet(low, high, box.low, box.high)) {
				take(box);
			}
		};
		const std::int64_t columns = cellOf(high.x) - cellOf(low.x) + 1;
		const std::int64_t rows	   = cellOf(high.y) - cellOf(low.y) + 1;
		if (static_cast<double>(columns) * static_cast<double>(rows) > static_cast<double>(committedBoxes.size())) {
			for (const auto& [cell, box] : committedBoxes) {
				takeMeeting(box);
			}
		} else {
			for (std::int64_t cellY = cellOf(low.y); cellY <= cellOf(high.y); ++cellY) {
				for (std::int64_t cellX = cellOf(low.x); cellX <= cellOf(high.x); ++cellX) {
					const auto found = committedBoxes.find({cellX, cellY});
					if (found != committedBoxes.end()) {
						takeMeeting(found->second);
					}
				}
			}
		}
		forEachPending([&](const PendingShape& shape) { takeMeeting({shape.low, shape.high}); });
	}

	void SeenArea::appendSeenBoxes(const Vec2i64& low, const Vec2i64& high, std::vector<Box>& out) const {
		forEachSeenBox(low, high, [&out](const Box& box) { out.push_back(box); });
	}

	bool SeenArea::anySeenBox(const Vec2i64& low, const Vec2i64& high) const {
		bool any = false;
		forEachSeenBox(low, high, [&any](const Box&) { any = true; });
		return any;
	}

	double SeenArea::committedSquareMeters() const {
		std::vector<Triangle> triangles;
		appendTriangles(triangles);
		double doubled = 0.0;
		for (const Triangle& triangle : triangles) {
			doubled += geometry::cross(triangle.corners[1] - triangle.corners[0], triangle.corners[2] - triangle.corners[0]).toDouble();
		}
		return doubled / 2e6;
	}

	const std::vector<Vec2i64>& SeenArea::registeredRing(std::size_t index) const { return mesh->ring(registered[index]).polygon.ring; }

	double SeenArea::ringsMetMean() const {
		return registeredBoxes.size() > 1 ? static_cast<double>(ringsMetTotal) / static_cast<double>(registeredBoxes.size() - 1) : 0.0;
	}

} // namespace vision
