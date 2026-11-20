#pragma once

// The seen record of player visibility (player-visibility.md 3.3, sight-softening.md 6): an exact,
// grow-only union of the sight shapes colonists have swept, in integer millimetres. Committed shapes
// live in a nav mesh of their own that holds them as ground rings and nothing else; open legs and
// shapes waiting for the budget are pending, and every query and the drawing read the record and the
// pending shapes together. Nothing is ever removed.

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

	struct ConvexHullScratch;

	// A shape's range on each side of the sight polygon, in millimetres from its point
	// (sight-softening.md 6.3).
	using SightRanges = geometry::SightSideValues;

	// How far a path may wobble from a leg's straight line, or step back from its farthest reach,
	// before the leg ends. A leg's ranges are sight less this.
	constexpr std::int64_t kSeenSlackMm = 50;
	// A step longer than this in one frame ends the leg without bridging it.
	constexpr double kSeenJumpMeters = 2.0;
	// Shapes tested a tick, first in, first out, whatever waits: above the 10 a tick that 50 colonists
	// indoors at 10x make, at about 15 us a test (player-visibility.md 6.6).
	constexpr std::size_t kSeenRingsPerTick = 16;
	// Pieces registered a tick: each is a patch, whose cost grows with the rings registered where it
	// lands. Shapes that add ground are cut while fewer than this wait (sight-softening.md 6.6).
	constexpr std::size_t kSeenPiecesRegisteredPerTick = 4;
	// Sweep points whose shapes are built a tick, whatever else waits (sight-softening.md 6.6).
	constexpr std::size_t kSeenSweepPointsPerTick = 4;
	// The most sweep points that wait: a second of building at kSeenSweepPointsPerTick at 60 ticks a
	// second. A shape queues a point every kSeenSweepSpacingMm of the walk before it, so colonists
	// walking beside walls at 10x queue more than the budget builds. Past this each run, a walk's points
	// between two builds' shapes, is thinned on its own, keeping its ends and every other point between;
	// when no run has a point between its ends left, the oldest run goes whole. Ground seen only in
	// passing between two kept points may stay unrecorded (sight-softening.md 6.6).
	constexpr std::size_t kSeenSweepPointsMost = 240;
	// The most vertices a leg's ring holds (sight-softening.md 6.2).
	constexpr std::size_t kSoftLegMostVertices = 256;
	// No leg is held below this sight radius: a sight polygon smaller than this can be non-convex
	// once its vertices are pulled inside the circle, and a leg's ring needs it convex.
	constexpr std::int64_t kSeenLeastRadiusMm = 1000;
	// A sight shape that adds ground near its rim is followed by the sweep: shapes from points this
	// far apart on the walk back to the build before it, so the seen edge where a shadow keeps old
	// ground uncovered steps by at most this much instead of by a whole rebuild step
	// (player-visibility.md 3.3).
	constexpr std::int64_t kSeenSweepSpacingMm = 100;
	// How near its rim a sight shape must add ground to be swept: the steps are where one build's rim
	// and the next's, a rebuild step apart, leave a notch. A rebuild step, the skirt, and margin.
	constexpr std::int64_t kSeenSweepRimBandMm = 600;
	// The side of a cell of the seen boxes, the cache that says where the record holds ground
	// (appendSeenBoxes): a chunk's, so a chunk asks its own cell and the neighbours its reach crosses into.
	constexpr std::int64_t kSeenBoxCellMm = 512000;
	// The most changed boxes the record keeps for its reader (takeChangedBoxes): a second of
	// kSeenPiecesRegisteredPerTick at 60 ticks a second.
	constexpr std::size_t kSeenChangedBoxesMost = 240;

	// The radius of the disc drawn as visible round a colonist of sight `sightMm` in the open, before
	// the eased rim's inset (sight-softening.md 6.3). Every held position lies within kSeenSlackMm of
	// his open leg's line and no more than kSeenSlackMm past its end, so within kSeenSlackMm x sqrt(2)
	// of the segment; the leg's ring reaches at least sightInradius(sight - kSeenSlackMm) from it, so a
	// disc this much smaller lies in it.
	std::int64_t drawnVisibleRadiusMm(std::int64_t sightMm);

	SightRanges equalRanges(std::int64_t radiusMm);

	// The convex ring of `ranges` round `centre`, counter-clockwise: the sight polygon when every range
	// is equal, the forest shape otherwise (sight-softening.md 4.2).
	std::vector<geometry::Vec2i64> rimOf(const geometry::Vec2i64& centre, const SightRanges& ranges);

	class SeenArea {
	  public:
		using LegId = std::uint64_t;

		// A sight shape round a point with the bodies standing now, cut to its rim, and the rim's ranges.
		struct BuiltSight {
			std::vector<geometry::Vec2i64> shape;
			SightRanges					   rimRanges{};
		};
		// The shape at a sweep point or a resting point (sight-softening.md 6.4): its rim on each side the
		// lesser of `rimRanges` and the point's own forest depth's at `sightRadiusMm`, with the opaque
		// bodies and walls standing, cut to that rim.
		using SightBuilder =
			std::function<BuiltSight(const geometry::Vec2i64& centre, std::int64_t sightRadiusMm, const SightRanges& rimRanges)>;

		// The shape a new leg holds first: the forest shape drawn round `position` on the frame it opens
		// (sight-softening.md 6.2).
		struct InheritedShape {
			geometry::Vec2i64 position;
			SightRanges		  ranges{};
		};

		struct Triangle {
			std::array<geometry::Vec2i64, 3> corners; // counter-clockwise
		};

		struct Box {
			geometry::Vec2i64 low;
			geometry::Vec2i64 high;
		};

		// The pending queue's three parts: open legs, shapes waiting to be tested or, tested and adding
		// ground, to be cut, and pieces waiting to register.
		struct Pending {
			std::size_t				  openLegs		= 0;
			std::size_t				  shapesWaiting = 0;
			std::size_t				  piecesWaiting = 0;
			[[nodiscard]] std::size_t total() const { return openLegs + shapesWaiting + piecesWaiting; }
		};

		// Where a registered piece came from.
		enum class Origin : std::uint8_t { Other, Leg, Polygon, Sweep, Count };

		// The work done since the update before the last one ended, up to the last update's end: the
		// legs' rings joined between them and what that update did.
		struct TickWork {
			std::size_t legJoins		   = 0; // the legs' hull merges
			std::size_t legVerticesJoined  = 0; // vertices fed to them
			std::size_t shapesTested	   = 0; // covered tests
			std::size_t shapesCut		   = 0; // frontier cuts of tested shapes and sweep shapes, skirted
			std::size_t sweepFrontierTests = 0; // frontier cuts of sweep shapes as they are, their test
			std::size_t verticesCut		   = 0; // vertices of every frontier cut
			std::size_t piecesRegistered   = 0;
			std::size_t verticesRegistered = 0;
			std::size_t sweepPointsBuilt   = 0;
			std::size_t sweepPointsDropped = 0;
		};

		// The steps of update(), marked as each begins for a caller that times them (the forest bench):
		// the covered tests, the cuts, each piece's patch into the record and the mesh's widths after it
		// (a registration's own bookkeeping falls in its patch), the sweep, and the tick's end.
		enum class TickPhase : std::uint8_t { Tests, Cuts, Patch, Widths, Sweeps, End };
		// What hears each step begin; the record holds no clock. Null by default: the game sets none.
		struct TickPhaseListener {
			virtual ~TickPhaseListener()			   = default;
			virtual void phaseBegins(TickPhase phase) = 0;
		};
		void setPhaseListener(TickPhaseListener* listener) { phaseListener = listener; }

		SeenArea();
		~SeenArea();
		SeenArea(const SeenArea&)			 = delete;
		SeenArea& operator=(const SeenArea&) = delete;

		// Tests `shape` and commits it now, outside the budget: the landing, before the crew exists.
		void commitNow(const std::vector<geometry::Vec2i64>& shape, const geometry::Vec2i64& centre);

		// A shape waiting its turn. It must be simple and wind once round `centre`
		// (geometry::simpleAround), which the covered test reads from.
		void queue(std::vector<geometry::Vec2i64> shape, const geometry::Vec2i64& centre);
		// A shape on the polygon path round `centre` (sight-softening.md 6.4), already cut to its rim,
		// rimOf(centre, rimRanges), the forest shape of `sightRadiusMm` round its build. `walk` is the
		// path from the build before it on the same walk, with no jump between, to just short of it: that
		// build, then every turn since; `walkStartRanges` is that build's rim. Empty for no sweep. It is
		// pending and tested as it is; when not covered it enters grown by geometry::kSeenSkirtMm
		// everywhere but along its rim (geometry::skirtedSightShape), or as it is when the grown ring is
		// not simple. When it adds ground within kSeenSweepRimBandMm of its rim, the walk is swept
		// (kSeenSweepSpacingMm). Same requirements as queue.
		void queueSightPolygon(
			std::vector<geometry::Vec2i64> polygon,
			const geometry::Vec2i64&	   centre,
			std::int64_t				   sightRadiusMm,
			const SightRanges&			   rimRanges,
			std::vector<geometry::Vec2i64> walk,
			const SightRanges&			   walkStartRanges
		);
		// A shape built now round `point` with the sight builder and queued as queueSightPolygon queues
		// one: where a walk on the polygon path comes to rest, or ends, away from vision's builds.
		void queueSightAt(
			const geometry::Vec2i64&	   point,
			std::int64_t				   sightRadiusMm,
			const SightRanges&			   rimRanges,
			std::vector<geometry::Vec2i64> walk,
			const SightRanges&			   walkStartRanges
		);

		// The open leg `leg`, begun at `start`, with him at `position` and his build's `ranges`
		// (sight-softening.md 6.3): its ring is pending, with its start as the centre. While the ranges
		// hold, the ring is the hull of the ring as it stood when they began and rimOf(position, ranges);
		// when they change, the ring so far is run on to `position` with the old ranges and kept, and the
		// new shape joins it there. The call that opens the leg also joins `inherited`, read only then.
		// False, leaving the leg as it was, when the ring would pass kSoftLegMostVertices.
		[[nodiscard]] bool setOpenLeg(
			LegId								 leg,
			const geometry::Vec2i64&			 start,
			const geometry::Vec2i64&			 position,
			const SightRanges&					 ranges,
			const std::optional<InheritedShape>& inherited
		);
		// Ends the open leg and queues its ring.
		void closeLeg(LegId leg);
		// What builds the sweep's shapes. Without one nothing is swept.
		void setSightBuilder(SightBuilder builder) { sightBuilder = std::move(builder); }

		// Tests up to kSeenRingsPerTick queued shapes, first in, first out, whatever waits: a shape the
		// record already covers (geometry::nav::sightShapeCovered) is dropped, and one it does not waits,
		// tested, to be cut. While fewer than kSeenPiecesRegisteredPerTick shapes' pieces wait, tested
		// shapes are cut to what they add (geometry::nav::sightShapeFrontier), up to kSeenRingsPerTick;
		// then up to kSeenPiecesRegisteredPerTick pieces register, one ring each, the shapes' first and the
		// sweep's with what is left; and while fewer than kSeenPiecesRegisteredPerTick of the sweep's
		// pieces wait, up to kSeenSweepPointsPerTick sweep points build their shapes, which the frontier
		// then cuts as any sight shape.
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

		// The committed record alone holds the point, edges included: the pending shapes are not read
		// (sight-softening.md 6.3, the feather's cut).
		[[nodiscard]] bool inRecord(const geometry::Vec2i64& point) const;
		// The committed record's edge near the box from `low` to `high`, as (start, end) with seen ground
		// on the left: every live triangle edge with nothing across it that meets the box, and some near
		// it, each once (sight-softening.md 7.2).
		void appendEdges(const geometry::Vec2i64& low, const geometry::Vec2i64& high, std::vector<std::pair<geometry::Vec2i64, geometry::Vec2i64>>& out) const;
		// The boxes of the rings registered since the last call, appended to `out`: the committed record's
		// edge changed nowhere else. Its one reader drains it every frame; past kSeenChangedBoxesMost
		// undrained boxes they are merged into the one box round them all, so it stays bounded whoever reads.
		void takeChangedBoxes(std::vector<Box>& out);

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

		[[nodiscard]] double							   committedSquareMeters() const;
		[[nodiscard]] std::size_t						   ringsRegistered() const { return registered.size(); }
		// K where registered (player-visibility.md 6.1): for each ring after the first, how many rings
		// registered before it have boxes meeting its own. The mean and the most.
		[[nodiscard]] double	  ringsMetMean() const;
		[[nodiscard]] std::size_t ringsMetMost() const { return ringsMetGreatest; }
		[[nodiscard]] const std::vector<geometry::Vec2i64>& registeredRing(std::size_t index) const;
		[[nodiscard]] Pending							   pending() const;
		[[nodiscard]] std::size_t						   shapesPending() const { return pending().total(); }
		[[nodiscard]] std::size_t						   shapesDropped() const { return dropped; }
		// Pieces registered from each origin.
		[[nodiscard]] std::size_t registrations(Origin origin) const { return registeredFrom[static_cast<std::size_t>(origin)]; }
		// The most ticks a queued shape waited before its covered test, and before its frontier cut.
		[[nodiscard]] std::size_t mostTicksToTest() const { return mostTestWait; }
		[[nodiscard]] std::size_t mostTicksToCut() const { return mostCutWait; }
		[[nodiscard]] const TickWork& lastTickWork() const { return lastWork; }
		// Sweep shapes that were not simple, so never entered, and sight shapes whose grown ring was
		// not simple, so entered as they are.
		[[nodiscard]] std::size_t shapesSkipped() const { return skipped; }
		// Sweep points whose shapes were built, and those waiting. A waiting point is not pending
		// ground: it has no shape until its turn.
		[[nodiscard]] std::size_t sweepsBuilt() const { return swept; }
		[[nodiscard]] std::size_t sweepsWaiting() const { return sweeps.size(); }
		// Sweep points dropped unbuilt past kSeenSweepPointsMost, and the runs among them dropped whole.
		[[nodiscard]] std::size_t sweepPointsDropped() const { return sweepsDropped; }
		[[nodiscard]] std::size_t sweepRunsDroppedWhole() const { return sweepRunsDropped; }

	  private:
		struct PendingShape {
			std::vector<geometry::Vec2i64> outline;
			geometry::Vec2i64			   centre;
			geometry::Vec2i64			   low;
			geometry::Vec2i64			   high;
			bool						   convex	  = false;
			Origin						   origin	  = Origin::Other;
			std::size_t					   queuedTick = 0;
			// An open leg's ring before the stretch of its present ranges, those ranges' shape round the
			// origin, the ranges, and its last position.
			std::vector<geometry::Vec2i64> legHeld;
			std::vector<geometry::Vec2i64> legShape;
			SightRanges					   legRanges{};
			geometry::Vec2i64			   legPosition;
			// A sight shape's sight radius (0 for any other shape) and rim, its walk, and the walk's start rim.
			std::int64_t				   sightRadius = 0;
			SightRanges					   rimRanges{};
			std::vector<geometry::Vec2i64> sweepPath;
			SightRanges					   walkStartRanges{};
			std::uint64_t				   sweepRun = 0; // a sweep point's run: its walk between two builds' shapes
		};

		static PendingShape pendingOf(std::vector<geometry::Vec2i64> outline, const geometry::Vec2i64& centre, Origin origin);
		// The hull of two convex rings into `out`, which may be `first`.
		void hullOfConvex(const std::vector<geometry::Vec2i64>& first, const std::vector<geometry::Vec2i64>& second, std::vector<geometry::Vec2i64>& out);
		// Whether the record covers the shape, its wait counted.
		bool covered(const PendingShape& shape);
		// Cuts the shape, skirted when it is a sight shape, to the pieces it adds, which wait to be
		// registered, and queues its sweep.
		void enter(const PendingShape& shape);
		void registerPiece(std::deque<PendingShape>& from);
		void markPhase(TickPhase phase) const;
		// Thins a full sweep queue run by run.
		void thinSweeps();
		// Grows the committed boxes of the cells the box from `low` to `high` meets by its part in each.
		void holdCommitted(const geometry::Vec2i64& low, const geometry::Vec2i64& high);
		// Builds a sweep point's shape and enters it when it adds anything.
		void commitSweep(const PendingShape& point);
		// Queues the sweep's points along the shape's walk and on to its centre, short of the walk's two
		// ends, whose own shapes went in.
		void queueSweep(const PendingShape& shape);
		template <typename Visit> void forEachPending(Visit visit) const;
		// Calls `take` with each box appendSeenBoxes gives for the query.
		template <typename Take> void forEachSeenBox(const geometry::Vec2i64& low, const geometry::Vec2i64& high, Take take) const;

		std::unique_ptr<geometry::nav::PatchableMesh>		 mesh;
		std::unique_ptr<geometry::nav::SightCoverScratch>	 scratch;
		std::unique_ptr<geometry::nav::SightFrontierScratch> frontierScratch;
		std::vector<std::vector<geometry::Vec2i64>>			 pieces;
		std::unique_ptr<ConvexHullScratch>					 hullScratch;
		std::vector<geometry::Vec2i64>						 moved;
		std::vector<geometry::Vec2i64>						 ring;
		std::size_t											 skipped = 0;
		std::size_t											 swept	 = 0;
		SightBuilder										 sightBuilder;
		TickPhaseListener*									 phaseListener = nullptr;
		std::map<LegId, PendingShape>						 openLegs;
		std::deque<PendingShape>							 waiting;
		std::deque<PendingShape>							 uncovered; // tested and adding ground, waiting to be cut
		std::deque<PendingShape>							 entering;		// pieces of shapes that add ground, waiting to be registered
		std::deque<PendingShape>							 sweepEntering; // pieces of sweep shapes, registered with what is left
		std::deque<PendingShape>							 sweeps;		// points with their sight radius and rim, shapes not yet built
		std::size_t											 sweepsDropped = 0;
		std::size_t											 sweepRunsDropped = 0;
		std::uint64_t										 sweepRuns = 0; // runs queued, the last one's id
		std::vector<std::int32_t>							 registered; // ring ids, in the order they went in
		std::vector<Box>									 registeredBoxes; // their boxes, in the same order
		// The registered boxes by cell of K's grid, and the last registration that counted each.
		std::map<std::pair<std::int64_t, std::int64_t>, std::vector<std::uint32_t>> ringBoxCells;
		std::vector<std::uint32_t>													ringBoxCounted;
		std::size_t											 ringsMetTotal	  = 0;
		std::size_t											 ringsMetGreatest = 0;
		std::size_t											 dropped = 0;
		std::array<std::size_t, static_cast<std::size_t>(Origin::Count)> registeredFrom{};
		std::size_t											 ticks		  = 0;
		std::size_t											 mostTestWait = 0;
		std::size_t											 mostCutWait  = 0;
		TickWork											 work;
		TickWork											 lastWork;
		std::map<std::pair<std::int64_t, std::int64_t>, Box> committedBoxes; // by cell of kSeenBoxCellMm, each cut to its cell
		std::vector<Box>									 changedBoxes;	 // registered since the reader last took them
	};

} // namespace vision
