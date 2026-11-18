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

	// A leg's ring (sight-softening.md 6.3): the convex hull of two convex counter-clockwise rings,
	// counter-clockwise, no three vertices in line. With the sight polygons of one radius round a leg's
	// start and end it is the open leg's capsule.
	std::vector<geometry::Vec2i64> legCapsule(const std::vector<geometry::Vec2i64>& held, const std::vector<geometry::Vec2i64>& joined);

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
			std::size_t legVerticesJoined	= 0; // vertices fed to the legs' hull merges
			std::size_t shapesTested		= 0;
			std::size_t shapesCut			= 0;
			std::size_t verticesCut			= 0; // vertices of the shapes cut, skirted where they are
			std::size_t piecesRegistered	= 0;
			std::size_t verticesRegistered	= 0;
			std::size_t sweepPointsBuilt	= 0;
		};

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
		// when they change, the ring so far is kept and the new shape joins it at `position`. The call
		// that opens the leg also joins `inherited`, read only then. False, leaving the leg as it was,
		// when the ring would pass kSoftLegMostVertices.
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
		// tested, to be cut. While fewer than kSeenPiecesRegisteredPerTick pieces wait, tested shapes are
		// cut to what they add (geometry::nav::sightShapeFrontier), up to kSeenRingsPerTick; then up to
		// kSeenPiecesRegisteredPerTick pieces register, one ring each, and up to kSeenSweepPointsPerTick
		// sweep points build their shapes, which the frontier then cuts as any sight shape.
		void update();

		// The point is in the record or a pending shape.
		[[nodiscard]] bool isSeen(const geometry::Vec2i64& point) const;
		// The point is in the committed record alone, by its mesh's locate.
		[[nodiscard]] bool inRecord(const geometry::Vec2i64& point) const;
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
		// The committed record's edge meeting the box from `low` to `high`, and some near it: every live
		// triangle edge with nothing across it, each once, as (start, end) with seen ground on the left.
		void appendEdges(const geometry::Vec2i64& low, const geometry::Vec2i64& high, std::vector<std::pair<geometry::Vec2i64, geometry::Vec2i64>>& out) const;
		// The boxes of the rings registered since the last call, appended to `out`.
		void takeChangedBoxes(std::vector<Box>& out);
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
		};

		static PendingShape pendingOf(std::vector<geometry::Vec2i64> outline, const geometry::Vec2i64& centre, Origin origin);
		// The hull of two convex rings into `out`, which may be `first`.
		void hullOfConvex(const std::vector<geometry::Vec2i64>& first, const std::vector<geometry::Vec2i64>& second, std::vector<geometry::Vec2i64>& out);
		// Whether the record covers the shape, its wait counted.
		bool covered(const PendingShape& shape);
		// Cuts the shape, skirted when it is a sight shape, to the pieces it adds, which wait to be
		// registered, and queues its sweep.
		void enter(const PendingShape& shape);
		void registerPiece();
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
		std::map<LegId, PendingShape>						 openLegs;
		std::deque<PendingShape>							 waiting;
		std::deque<PendingShape>							 uncovered; // tested and adding ground, waiting to be cut
		std::deque<PendingShape>							 entering;	// pieces of shapes that add ground, waiting to be registered
		std::deque<PendingShape>							 sweeps;	// points with their sight radius and rim, shapes not yet built
		std::vector<std::int32_t>							 registered; // ring ids, in the order they went in
		std::vector<Box>									 changedBoxes;
		std::size_t											 dropped = 0;
		std::array<std::size_t, static_cast<std::size_t>(Origin::Count)> registeredFrom{};
		std::size_t											 ticks		  = 0;
		std::size_t											 mostTestWait = 0;
		std::size_t											 mostCutWait  = 0;
		TickWork											 work;
		TickWork											 lastWork;
		std::map<std::pair<std::int64_t, std::int64_t>, Box> committedBoxes; // by cell of kSeenBoxCellMm, each cut to its cell
	};

} // namespace vision
