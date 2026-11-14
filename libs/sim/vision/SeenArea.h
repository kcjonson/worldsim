#pragma once

// The seen record of player visibility (player-visibility.md 3.3): an exact, grow-only union of the
// sight shapes colonists have swept, in integer millimetres. Committed shapes live in a nav mesh of
// their own that holds them as ground rings and nothing else; open legs and shapes waiting for the
// budget are pending, and every query and the drawing read the record and the pending shapes
// together. Nothing is ever removed.

#include <core/Vec2i64.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
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

	// The radius of the disc drawn as visible round a colonist of sight `sightMm`. Every held
	// position lies within kSeenSlackMm of his open leg's line and no more than kSeenSlackMm past its
	// end, so within kSeenSlackMm x sqrt(2) of the segment; the leg's capsule reaches at least
	// sightInradius(sight - kSeenSlackMm) from it, so a disc this much smaller lies in the capsule.
	std::int64_t drawnVisibleRadiusMm(std::int64_t sightMm);

	// A leg's capsule: the hull of the sight polygons of `radiusMm` round `start` and `end`,
	// counter-clockwise, no three vertices in line.
	std::vector<geometry::Vec2i64> legCapsule(const geometry::Vec2i64& start, const geometry::Vec2i64& end, std::int64_t radiusMm);

	class SeenArea {
	  public:
		using LegId = std::uint64_t;
		// The visibility polygon round `centre` out to `reachMm`, with the walls standing now.
		using SightBuilder = std::function<std::vector<geometry::Vec2i64>(const geometry::Vec2i64& centre, std::int64_t reachMm)>;

		struct Triangle {
			std::array<geometry::Vec2i64, 3> corners; // counter-clockwise
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
		// A visibility polygon with walls in sight, of sight `sightRadiusMm` round `centre`, waiting its
		// turn; `walk` is the path from the build before it on the same walk, with no jump between, to
		// just short of it: that build, then every turn since. Empty when there was none. It is pending
		// and tested as it is; when not covered it enters grown by geometry::kSeenSkirtMm everywhere but
		// along its rim (geometry::skirtedSightShape), or as it is when the grown ring is not simple.
		// When it adds ground near its rim (kSeenSweepRimBandMm), the walk is swept
		// (kSeenSweepSpacingMm). Same requirements as queue.
		void queueSightPolygon(std::vector<geometry::Vec2i64> polygon, const geometry::Vec2i64& centre, std::int64_t sightRadiusMm, std::vector<geometry::Vec2i64> walk);
		// A sight polygon built now round `point` with the sight builder and queued as queueSightPolygon
		// queues one: where a walk with walls in sight comes to rest, or ends, away from vision's builds.
		void queueSightAt(const geometry::Vec2i64& point, std::int64_t sightRadiusMm, std::vector<geometry::Vec2i64> walk);

		// The open leg `leg` now runs from `start` to `end` with capsule radius `radiusMm`: its capsule
		// is pending, with its start as the centre.
		void setOpenLeg(LegId leg, const geometry::Vec2i64& start, const geometry::Vec2i64& end, std::int64_t radiusMm);
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
		[[nodiscard]] std::size_t sweepsBuilt() const { return swept; }
		[[nodiscard]] std::size_t sweepsWaiting() const { return sweeps.size(); }

	  private:
		struct PendingShape {
			std::vector<geometry::Vec2i64> outline;
			geometry::Vec2i64			   centre;
			geometry::Vec2i64			   low;
			geometry::Vec2i64			   high;
			bool						   convex = false;
			geometry::Vec2i64			   legStart; // an open leg's ends and capsule radius
			geometry::Vec2i64			   legEnd;
			std::int64_t				   radius		= 0;
			std::int64_t				   sightRadius	= 0; // a sight polygon's, for its skirt and sweep; 0 for any other shape
			std::vector<geometry::Vec2i64> sweepPath;		 // a sight polygon's walk from the build before it
		};

		static PendingShape pendingOf(std::vector<geometry::Vec2i64> outline, const geometry::Vec2i64& centre);
		// Drops the shape when the record covers it, else enters it.
		void				test(const PendingShape& shape);
		// Cuts the shape, skirted when it is a sight polygon, to the pieces it adds, which wait to be
		// registered, and queues its sweep.
		void				enter(const PendingShape& shape);
		void				registerPiece();
		// Builds a sweep point's polygon and enters it when it adds anything.
		void				commitSweep(const PendingShape& point);
		// Queues the sweep's points along `walk` and on to `to`, short of its two ends, whose own
		// polygons went in.
		void				queueSweep(const std::vector<geometry::Vec2i64>& walk, const geometry::Vec2i64& to, std::int64_t sightRadiusMm);
		template <typename Visit> void forEachPending(Visit visit) const;

		std::unique_ptr<geometry::nav::PatchableMesh>		 mesh;
		std::unique_ptr<geometry::nav::SightCoverScratch>	 scratch;
		std::unique_ptr<geometry::nav::SightFrontierScratch> frontierScratch;
		std::vector<std::vector<geometry::Vec2i64>>			 pieces;
		std::size_t											 skipped = 0;
		std::size_t											 swept	 = 0;
		SightBuilder										 sightBuilder;
		std::map<LegId, PendingShape>					  openLegs;
		std::deque<PendingShape>						  waiting;
		std::deque<PendingShape>						  entering; // pieces of shapes that add ground, waiting to be registered
		std::deque<PendingShape>						  sweeps; // points (centre) with their sight radius, polygons not yet built
		std::vector<std::int32_t>						  registered; // ring ids, in the order they went in
		std::size_t										  dropped = 0;
	};

} // namespace vision
