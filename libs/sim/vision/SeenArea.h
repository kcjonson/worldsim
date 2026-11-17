#pragma once

// The seen record of player visibility (player-visibility.md 3.3): an exact, grow-only union of the
// sight shapes colonists have swept, in integer millimetres. Committed shapes live in a nav mesh of
// their own that holds them as ground rings and nothing else; open legs and shapes waiting for the
// budget are pending, and every query and the drawing read the record and the pending shapes
// together. Nothing is ever removed.

#include <core/Vec2i64.h>
#include <visibility/ForestDepth.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace geometry::nav {
	class PatchableMesh;
	struct SightCoverScratch;
	struct SightFrontierScratch;
} // namespace geometry::nav

namespace vision {

	// How far a path may wobble from a leg's straight line, or step back from its farthest reach,
	// before the leg ends. A committed leg's radius is sight less this.
	constexpr std::int64_t kSeenSlackMm = 50;
	// A step longer than this in one frame ends the leg without bridging it.
	constexpr double kSeenJumpMeters = 2.0;
	// Shapes tested a tick, first in, first out: above the 10 a tick that 50 colonists indoors at 10x
	// make, at about 15 us a test (player-visibility.md 6.6). Testing stops at the first shape that
	// adds anything.
	constexpr std::size_t kSeenRingsPerTick = 16;
	// Pieces registered a tick: each is a patch, whose cost grows with the rings registered where it
	// lands, so a shape that adds several pieces spreads them over as many ticks
	// (player-visibility.md 6.7).
	constexpr std::size_t kSeenPiecesRegisteredPerTick = 1;
	// No leg is held below this sight radius: a sight polygon smaller than this can be non-convex
	// once its vertices are pulled inside the circle, and a leg's capsule needs it convex.
	constexpr std::int64_t kSeenLeastRadiusMm = 1000;
	// A sight polygon that adds ground near its rim is followed by the sweep: polygons from points
	// this far apart on the walk back to the build before it, so the seen edge where a shadow keeps
	// old ground uncovered steps by at most this much instead of by a whole rebuild step
	// (player-visibility.md 3.3).
	constexpr std::int64_t kSeenSweepSpacingMm = 100;
	// How near its rim a sight polygon must add ground to be swept: the steps are where one build's
	// rim and the next's, a rebuild step apart, leave a notch. A rebuild step, the skirt, and margin.
	constexpr std::int64_t kSeenSweepRimBandMm = 600;
	// The side of a cell of the seen boxes, the cache that says where the record holds ground
	// (appendSeenBoxes): a chunk's, so a chunk asks its own cell and the neighbours its reach crosses into.
	constexpr std::int64_t kSeenBoxCellMm = 512000;

	// The most vertices a leg's ring holds (sight-softening.md 6.2).
	constexpr std::size_t kSoftLegMostVertices = 256;

	// The radius of the disc drawn as visible round a colonist of sight `sightMm`. Every held
	// position lies within kSeenSlackMm of his open leg's line and no more than kSeenSlackMm past its
	// end, so within kSeenSlackMm x sqrt(2) of the segment; the leg's ring reaches at least
	// sightInradius(sight - kSeenSlackMm) from it, so a disc this much smaller lies in the ring.
	std::int64_t drawnVisibleRadiusMm(std::int64_t sightMm);

	// A shape's range on each side of the sight polygon, in millimetres from its point
	// (sight-softening.md 6.3): absolute ranges, never depths.
	using SightRanges = geometry::SightSideValues;

	// Every side at `radiusMm`: sight with no forest.
	SightRanges equalRanges(std::int64_t radiusMm);

	// The convex ring of `ranges` round `centre`, counter-clockwise: the sight polygon when every range
	// is equal, the forest shape otherwise (sight-softening.md 4.2). Each range lies in
	// [geometry::kForestShapeLeastRangeMm, geometry::kForestRadiusLimitMm).
	std::vector<geometry::Vec2i64> rimOf(const geometry::Vec2i64& centre, const SightRanges& ranges);

	class SeenArea {
	  public:
		using LegId = std::uint64_t;
		// A sight shape round `centre` with the bodies standing now, cut to its rim, and the rim's ranges:
		// on each side the lesser of `ranges` and what the bodies round `centre` leave
		// (sight-softening.md 6.4).
		struct BuiltSight {
			std::vector<geometry::Vec2i64> shape;
			SightRanges					   rimRanges{};
		};
		using SightBuilder = std::function<BuiltSight(const geometry::Vec2i64& centre, const SightRanges& ranges)>;

		// The shape a new leg holds first: the shape drawn round `position` on the frame it opens
		// (sight-softening.md 6.2, 6.3).
		struct InheritedShape {
			geometry::Vec2i64 position;
			SightRanges		  ranges{};
		};

		// What the record has done, for the state view and the benches (sight-softening.md 6.6, 8).
		// K is the registered rings whose boxes meet a piece's box, taken as each piece registers.
		struct Counts {
			std::size_t legsTested = 0;
			std::size_t legsCovered = 0;
			std::size_t legPieces = 0; // pieces the frontier cut from legs that added ground
			std::size_t registrations = 0;
			std::size_t ringsMetTotal = 0; // K summed over the registrations
			std::size_t ringsMetMost = 0;
		};

		struct Triangle {
			std::array<geometry::Vec2i64, 3> corners; // counter-clockwise
		};

		struct Box {
			geometry::Vec2i64 low;
			geometry::Vec2i64 high;
		};

		SeenArea();
		~SeenArea();
		SeenArea(const SeenArea&) = delete;
		SeenArea& operator=(const SeenArea&) = delete;

		// Tests `shape` and commits it now, outside the budget: the landing, before the crew exists.
		void commitNow(const std::vector<geometry::Vec2i64>& shape, const geometry::Vec2i64& centre);

		// A shape waiting its turn. It must be simple and wind once round `centre`
		// (geometry::simpleAround), which the covered test reads from.
		void queue(std::vector<geometry::Vec2i64> shape, const geometry::Vec2i64& centre);
		// A sight polygon on the polygon path round `centre`, cut to its rim, rimOf(centre, rimRanges),
		// waiting its turn; `walk` is the path from the build before it on the same walk, with no jump
		// between, to just short of it: that build, then every turn since, and `walkStartRanges` that
		// build's rim ranges. Empty when there was none. It is pending and tested as it is; when not
		// covered it enters grown by geometry::kSeenSkirtMm everywhere but along its rim
		// (geometry::skirtedSightShape), or as it is when the grown ring is not simple. When it adds
		// ground within kSeenSweepRimBandMm of its rim, the walk is swept (kSeenSweepSpacingMm), each
		// point's rim ranges interpolated between the two builds' and held in by the sweep's inset.
		// Same requirements as queue.
		void queueSightPolygon(
			std::vector<geometry::Vec2i64> polygon,
			const geometry::Vec2i64&	   centre,
			const SightRanges&			   rimRanges,
			std::vector<geometry::Vec2i64> walk,
			const SightRanges&			   walkStartRanges
		);
		// A sight polygon built now round `point` with the sight builder, no wider than `ranges`, and
		// queued as queueSightPolygon queues one: where a walk on the polygon path comes to rest, or
		// ends, away from vision's builds.
		void queueSightAt(
			const geometry::Vec2i64&	   point,
			const SightRanges&			   ranges,
			std::vector<geometry::Vec2i64> walk,
			const SightRanges&			   walkStartRanges
		);

		// Joins rimOf(position, ranges) to the open leg `leg`'s ring, the convex hull of every shape it
		// has taken, pending with `start` as its centre (sight-softening.md 6.2, 6.3). The call that
		// opens the leg also joins the shape at `start` and `inherited`, when there is one. False,
		// leaving the leg as it was, when the joined ring would pass kSoftLegMostVertices.
		[[nodiscard]] bool setOpenLeg(
			LegId								 leg,
			const geometry::Vec2i64&			 start,
			const geometry::Vec2i64&			 position,
			const SightRanges&					 ranges,
			const std::optional<InheritedShape>& inherited
		);
		// Ends the open leg and queues its capsule.
		void closeLeg(LegId leg);
		// What builds the sweep's polygons. Without one nothing is swept.
		void setSightBuilder(SightBuilder builder) { sightBuilder = std::move(builder); }

		// While no pieces wait, tests up to kSeenRingsPerTick queued shapes, first in, first out, and
		// stops at the first that adds anything. A shape the record already covers
		// (geometry::nav::sightShapeCovered) is dropped; one it does not is cut to what it adds
		// (geometry::nav::sightShapeFrontier), and those pieces wait, pending, to be registered
		// kSeenPiecesRegisteredPerTick a tick, one ring each. A sight polygon that adds ground near its
		// rim queues the points of its sweep apart from the shapes; on a tick with nothing to register
		// one of them builds its polygon, which the frontier then cuts as any sight polygon.
		void update();

		// The point is in the record or a pending shape.
		[[nodiscard]] bool isSeen(const geometry::Vec2i64& point) const;
		// The whole closed area of a simple outline is in the record or the pending shapes.
		[[nodiscard]] bool covers(const std::vector<geometry::Vec2i64>& outline) const;
		// The seen point on the segment from `from` to `to` nearest `to`: `to` itself when it is
		// seen, else the far end of the last seen stretch, to the millimetre. None when no point of
		// the segment is seen.
		[[nodiscard]] std::optional<geometry::Vec2i64> lastSeenToward(const geometry::Vec2i64& from, const geometry::Vec2i64& to) const;
		// Moves whenever the committed record does.
		[[nodiscard]] std::uint64_t epoch() const;

		// The committed record as triangles, for drawing.
		void appendTriangles(std::vector<Triangle>& out) const;
		// Every pending shape: the open legs by id, then the queue in order, then the pieces waiting.
		void appendPendingShapes(std::vector<const std::vector<geometry::Vec2i64>*>& out) const;
		// Boxes that together hold every seen point in the box from `low` to `high`: for each cell of
		// kSeenBoxCellMm the query meets, the box of what the record holds in it, and the box of each
		// pending shape that meets the query. A cache over the record, kept as pieces register: it
		// never shrinks, and a box can hold ground that is not seen, never the other way.
		void appendSeenBoxes(const geometry::Vec2i64& low, const geometry::Vec2i64& high, std::vector<Box>& out) const;
		// Whether appendSeenBoxes would give any box for the same query: false means nothing there is seen.
		[[nodiscard]] bool anySeenBox(const geometry::Vec2i64& low, const geometry::Vec2i64& high) const;

		[[nodiscard]] double		   committedSquareMeters() const;
		[[nodiscard]] std::size_t	   ringsRegistered() const { return registered.size(); }
		[[nodiscard]] const std::vector<geometry::Vec2i64>& registeredRing(std::size_t index) const;
		// Open legs, shapes waiting, and pieces waiting.
		[[nodiscard]] std::size_t	   shapesPending() const { return openLegs.size() + waiting.size() + entering.size(); }
		[[nodiscard]] std::size_t	   shapesDropped() const { return dropped; }
		// Sweep polygons that were not simple, so never entered, and sight polygons whose grown ring was
		// not simple, so entered as they are.
		[[nodiscard]] std::size_t shapesSkipped() const { return skipped; }
		// Sweep points whose polygons were built, and those waiting. A waiting point is not pending
		// ground: it has no shape until its polygon is built.
		[[nodiscard]] std::size_t	sweepsBuilt() const { return swept; }
		[[nodiscard]] std::size_t	sweepsWaiting() const { return sweeps.size(); }
		[[nodiscard]] const Counts& counts() const { return counted; }

	  private:
		struct PendingShape {
			std::vector<geometry::Vec2i64> outline;
			geometry::Vec2i64			   centre;
			geometry::Vec2i64			   low;
			geometry::Vec2i64			   high;
			bool						   convex = false;
			bool						   leg = false;
			geometry::Vec2i64			   legPosition; // an open leg's last held position and the ranges it took there
			SightRanges					   legRanges{};
			std::vector<geometry::Vec2i64> legShape; // rimOf(origin, legRanges), placed at each held position by moving it
			bool						   sightPolygon = false;
			SightRanges					   ranges{};  // a sight polygon's rim ranges, or a sweep point's before its build
			std::vector<geometry::Vec2i64> sweepPath; // a sight polygon's walk from the build before it
			SightRanges					   walkStartRanges{};
		};

		// Buffers a leg's hull merge reuses each frame.
		struct HullScratch {
			std::vector<geometry::Vec2i64> firstLower;
			std::vector<geometry::Vec2i64> firstUpper;
			std::vector<geometry::Vec2i64> secondLower;
			std::vector<geometry::Vec2i64> secondUpper;
			std::vector<geometry::Vec2i64> lower;
			std::vector<geometry::Vec2i64> upper;
			std::vector<geometry::Vec2i64> moved;
			std::vector<geometry::Vec2i64> ring;
		};

		static PendingShape pendingOf(std::vector<geometry::Vec2i64> outline, const geometry::Vec2i64& centre);
		void				hullOfConvex(
						   const std::vector<geometry::Vec2i64>& first,
						   const std::vector<geometry::Vec2i64>& second,
						   std::vector<geometry::Vec2i64>&		 out
					   );
		// Drops the shape when the record covers it, else enters it.
		void				test(const PendingShape& shape);
		// Cuts the shape, skirted when it is a sight polygon, to the pieces it adds, which wait to be
		// registered, and queues its sweep.
		void				enter(const PendingShape& shape);
		void				registerPiece();
		// Grows the committed boxes of the cells the box from `low` to `high` meets by its part in each.
		void				holdCommitted(const geometry::Vec2i64& low, const geometry::Vec2i64& high);
		// Builds a sweep point's polygon and enters it when it adds anything.
		void commitSweep(const PendingShape& point);
		// Queues the sweep's points along the shape's walk and on to its centre, short of the two ends,
		// whose own polygons went in.
		void queueSweep(const PendingShape& shape);
		template <typename Visit>
		void forEachPending(Visit visit) const;
		// Calls `take` with each box appendSeenBoxes gives for the query.
		template <typename Take> void forEachSeenBox(const geometry::Vec2i64& low, const geometry::Vec2i64& high, Take take) const;

		std::unique_ptr<geometry::nav::PatchableMesh>		 mesh;
		std::unique_ptr<geometry::nav::SightCoverScratch>	 scratch;
		std::unique_ptr<geometry::nav::SightFrontierScratch> frontierScratch;
		std::unique_ptr<HullScratch>						 hullScratch;
		std::vector<std::vector<geometry::Vec2i64>>			 pieces;
		std::size_t											 skipped = 0;
		std::size_t											 swept	 = 0;
		SightBuilder										 sightBuilder;
		std::map<LegId, PendingShape>						 openLegs;
		std::deque<PendingShape>							 waiting;
		std::deque<PendingShape>							 entering;	 // pieces of shapes that add ground, waiting to be registered
		std::deque<PendingShape>							 sweeps;	 // points (centre) with their rim ranges, polygons not yet built
		std::vector<std::int32_t>							 registered; // ring ids, in the order they went in
		std::vector<Box>									 registeredBoxes; // their boxes, for K
		Counts												 counted;
		std::size_t											 dropped = 0;
		std::map<std::pair<std::int64_t, std::int64_t>, Box> committedBoxes; // by cell of kSeenBoxCellMm, each cut to its cell
	};

} // namespace vision
