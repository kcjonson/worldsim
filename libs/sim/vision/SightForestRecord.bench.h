#pragma once

// The seen record as sight-softening.md 6 has it, for the step 1a bench only: SeenArea's record
// (player-visibility.md 3.3) with forest legs, rims through the skirt and the sweep, the K counter,
// and the scheduler options of 12.1, so the bench measures the design without touching the shipped
// SeenArea. With every range equal, no scheduler option on, and no sight builder it is SeenArea.

#include "SeenArea.h"

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
#include <vector>

namespace geometry::nav {
	class PatchableMesh;
	struct SightCoverScratch;
	struct SightFrontierScratch;
} // namespace geometry::nav

namespace sight_bench {

	// A shape's range on each side of the sight polygon, in millimetres from its point (sight-softening.md 6.3).
	using SightRanges = geometry::SightSideValues;

	// The most vertices a leg's ring holds (sight-softening.md 6.2).
	constexpr std::size_t kSoftLegMostVertices = 256;

	SightRanges equalRanges(std::int64_t radiusMm);

	// The convex ring of `ranges` round `centre`, counter-clockwise: the sight polygon when every range
	// is equal, the forest shape otherwise (sight-softening.md 4.2).
	std::vector<geometry::Vec2i64> rimOf(const geometry::Vec2i64& centre, const SightRanges& ranges);

	// Where a registered piece came from.
	enum class Origin : std::uint8_t { Landing, Leg, Polygon, Sweep, Count };

	// How the record spends a tick. The defaults are SeenArea's: shapes are tested only while no piece
	// waits, one piece registers a tick, and a sweep point is built only on a tick with nothing to
	// register. Shapes are tested and cut while fewer pieces wait than a tick registers.
	struct Scheduler {
		// Covered tests go on while pieces wait, up to kSeenRingsPerTick a tick; a shape that adds
		// ground waits, tested, for the frontier until fewer pieces wait than a tick registers.
		bool		testWhilePiecesWait = false;
		// Sweep points built every tick, whatever waits; 0 keeps SeenArea's rule.
		std::size_t sweepsPerTick = 0;
		std::size_t piecesPerTick = vision::kSeenPiecesRegisteredPerTick;
	};

	class ForestRecord {
	  public:
		using LegId = std::uint64_t;

		// A sight shape round `centre` with the bodies standing now, cut to its rim, and the rim's ranges.
		struct BuiltSight {
			std::vector<geometry::Vec2i64> shape;
			SightRanges					   rimRanges{};
		};
		using SightBuilder = std::function<BuiltSight(const geometry::Vec2i64& centre, const SightRanges& ranges)>;

		// The shape a new leg holds first: the shape drawn round `position` on the frame it opens.
		struct InheritedShape {
			geometry::Vec2i64 position;
			SightRanges		  ranges{};
		};

		// The pending queue's three parts: open legs, shapes waiting to be tested or, tested and adding
		// ground, to be cut, and pieces waiting to register.
		struct Queue {
			std::size_t openLegs = 0;
			std::size_t shapesWaiting = 0;
			std::size_t piecesWaiting = 0;
			[[nodiscard]] std::size_t total() const { return openLegs + shapesWaiting + piecesWaiting; }
		};

		struct Counts {
			std::array<std::size_t, static_cast<std::size_t>(Origin::Count)> registrations{};
			std::size_t legsTested = 0;
			std::size_t legsCovered = 0;
			// K: the registered rings whose boxes meet a piece's box, taken as each piece registers.
			std::size_t ringsMetTotal = 0;
			std::size_t ringsMetMost = 0;
			double		ringsMetMs = 0.0; // the counter's own time, which is not the record's
			// The most ticks a queued shape waited before its covered test.
			std::size_t mostTicksToTest = 0;
		};

		explicit ForestRecord(const Scheduler& scheduler);
		~ForestRecord();
		ForestRecord(const ForestRecord&) = delete;
		ForestRecord& operator=(const ForestRecord&) = delete;

		void commitNow(const std::vector<geometry::Vec2i64>& shape, const geometry::Vec2i64& centre);
		// A shape on the polygon path cut to rimOf(centre, rimRanges), with its walk from the build before
		// it and that build's rim ranges (sight-softening.md 6.4).
		void queueSightPolygon(
			std::vector<geometry::Vec2i64> polygon,
			const geometry::Vec2i64&	   centre,
			const SightRanges&			   rimRanges,
			std::vector<geometry::Vec2i64> walk,
			const SightRanges&			   walkStartRanges
		);
		void queueSightAt(
			const geometry::Vec2i64&	   point,
			const SightRanges&			   ranges,
			std::vector<geometry::Vec2i64> walk,
			const SightRanges&			   walkStartRanges
		);
		// The open leg's ring: SeenArea's capsule while the ranges hold, the hull of rimOf(start, ranges)
		// and the same shape at `position`. When the ranges change, the ring so far is kept and the new
		// shape joins it at `position`, so a leg's ring is the hull of a capsule a stretch of equal
		// ranges. The call that opens the leg also joins `inherited`. False, leaving the leg as it was,
		// when the ring would pass kSoftLegMostVertices.
		[[nodiscard]] bool setOpenLeg(
			LegId								 leg,
			const geometry::Vec2i64&			 start,
			const geometry::Vec2i64&			 position,
			const SightRanges&					 ranges,
			const std::optional<InheritedShape>& inherited
		);
		void closeLeg(LegId leg);
		void setSightBuilder(SightBuilder builder) { sightBuilder = std::move(builder); }
		void update();

		[[nodiscard]] Queue			queue() const;
		[[nodiscard]] std::size_t	sweepsWaiting() const { return sweeps.size(); }
		[[nodiscard]] std::size_t	sweepsBuilt() const { return swept; }
		[[nodiscard]] std::size_t	ringsRegistered() const { return registeredBoxes.size(); }
		[[nodiscard]] const Counts& counts() const { return counted; }
		[[nodiscard]] double		committedSquareMeters() const;

	  private:
		struct PendingShape {
			std::vector<geometry::Vec2i64> outline;
			geometry::Vec2i64			   centre;
			geometry::Vec2i64			   low;
			geometry::Vec2i64			   high;
			Origin						   origin = Origin::Landing;
			std::size_t					   queuedTick = 0;
			// An open leg's ring before the stretch of its present ranges, its ranges' shape at the
			// origin, and its last held position.
			std::vector<geometry::Vec2i64> legHeld;
			std::vector<geometry::Vec2i64> legShape;
			SightRanges					   legRanges{};
			geometry::Vec2i64			   legPosition;
			bool						   sightPolygon = false;
			SightRanges					   ranges{}; // a sight polygon's rim ranges, or a sweep point's before its build
			std::vector<geometry::Vec2i64> sweepPath;
			SightRanges					   walkStartRanges{};
		};

		static PendingShape pendingOf(std::vector<geometry::Vec2i64> outline, const geometry::Vec2i64& centre, Origin origin);
		void				hullOfConvex(
						   const std::vector<geometry::Vec2i64>& first,
						   const std::vector<geometry::Vec2i64>& second,
						   std::vector<geometry::Vec2i64>&		 out
					   );
		// Whether the record covers the shape, and the wait counted.
		bool covered(const PendingShape& shape);
		void enter(const PendingShape& shape);
		void registerPiece();
		void commitSweep(const PendingShape& point);
		void queueSweep(const PendingShape& shape);

		Scheduler											 scheduler;
		std::unique_ptr<geometry::nav::PatchableMesh>		 mesh;
		std::unique_ptr<geometry::nav::SightCoverScratch>	 scratch;
		std::unique_ptr<geometry::nav::SightFrontierScratch> frontierScratch;
		std::vector<std::vector<geometry::Vec2i64>>			 pieces;
		std::vector<geometry::Vec2i64>						 firstLower, firstUpper, secondLower, secondUpper, lower, upper, moved, ring;
		SightBuilder										 sightBuilder;
		std::map<LegId, PendingShape>						 openLegs;
		std::deque<PendingShape>							 waiting;
		std::deque<PendingShape>							 uncovered; // tested and adding ground, waiting for the frontier
		std::deque<PendingShape>							 entering;
		std::deque<PendingShape>							 sweeps;
		std::vector<vision::SeenArea::Box>					 registeredBoxes;
		std::size_t											 swept = 0;
		std::size_t											 ticks = 0;
		Counts												 counted;
	};

} // namespace sight_bench
