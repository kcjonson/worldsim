#pragma once

// RouteQueue: route searches under a per-tick work budget. A request is a ticket; each
// slice does a fixed number of search expansions shared among the pending requests, and
// a search carries on across slices until it ends, Found or Exhausted. Nothing caps a
// search. The work is a count, never a clock, so the slice an answer is ready on depends
// only on the requests, the mesh, and the patches, never on the machine or its load.
//
// Geometry only: it reads a NavView and PatchReports and knows nothing of who asks.
//
// Concurrency. A tick's slice (runSlice) may run on one worker thread while the main
// thread goes on with its frame:
//   - request, cancel, takeAnswer, answeredTickets, and waiting touch only main-thread
//     state and may be called while a slice runs;
//   - admit, notePatches, deferrals, lastSlice, and slices read or write what the slice
//     does and must not be called while one runs;
//   - nothing may write the mesh (patch, refreshWidths, rebuildForests, door edits) while a
//     slice runs, and every report from a write must reach notePatches before the next one;
//   - one slice runs at a time, and the queue is driven from one main thread.
//
// Per tick, between slices: hand notePatches every PatchReport since the last slice (the
// patches' and the width refresh's), then admit, then start the slice. A request made
// during tick t joins at the admit before slice t + 1; an answer finished in slice t + 1
// reaches takeAnswer at the admit before slice t + 2.

#include <core/Vec2i64.h>
#include <nav/PatchableMesh.h>
#include <nav/PathQuery.h>

#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <unordered_set>
#include <vector>

namespace nav {

	// Expansions a slice may do: 8 ms of route worker a tick (half a 60 Hz frame, leaving
	// the rest for the patch job and slow expansions) at an estimated mean of 0.45 us an
	// expansion on the real mesh. A count, so answers land on the same slice on any machine.
	constexpr std::uint32_t kRouteWorkPerSlice = 18000;
	// The most a request spends in one turn, so short requests are not held behind a long one.
	constexpr std::uint32_t kRouteQuantum = 2000;

	using RouteTicket						= std::uint64_t;
	constexpr RouteTicket kNoRouteTicket	= 0;

	struct RouteQueueArgs {
		// A deferral box is the touched box grown by this: how far a patch's width refresh
		// reaches (PatchableMeshArgs::maxClearanceMm of the mesh the queue reads).
		std::int64_t deferralMarginMm = geometry::nav::kDefaultMaxClearanceMm;
	};

	struct RouteRequestArgs {
		geometry::Vec2i64				 start;
		std::vector<geometry::Vec2i64> goals; // the answer is the one nearest by walk
		std::int64_t		 agentRadiusMm = 0;
		// Copied when the request is made: the caller may change its sets at once.
		geometry::nav::BeliefFilter belief;
	};

	enum class RouteOutcome {
		Routed,		// a goal is reached
		NoKnownWay, // the search explored everything this mover can reach on meshed ground
	};

	struct RouteAnswer {
		RouteOutcome			 outcome   = RouteOutcome::NoKnownWay;
		std::int32_t			 goalIndex = -1; // into RouteRequestArgs::goals; -1 unless Routed
		geometry::nav::PathResult path;			 // the walk and the work it took, as SearchState gives it
		std::uint32_t			 restarts = 0;	 // restarts by patches that met what it had read
		std::uint64_t			 slice	  = 0;	 // the slice it finished in (slices() numbering)
	};

	// One turn of a slice: a request given up to `granted` expansions, and what it spent.
	// A search may close one label past what it is granted, only to accept the goal that
	// ends it, so a turn spends at most granted + 1.
	struct RouteTurn {
		RouteTicket	  ticket  = kNoRouteTicket;
		std::uint32_t granted = 0;
		std::uint32_t spent	  = 0;
		bool		  ended	  = false;
	};

	// Where patches must wait: a request restarted once, and the box of every triangle it has
	// read since, grown by the deferral margin. A patch that meets the box would restart it
	// again, so the caller holds such patches until the request ends (it leaves deferrals()).
	struct RouteDeferral {
		RouteTicket ticket = kNoRouteTicket;
		geometry::Vec2i64		lo;
		geometry::Vec2i64		hi;
	};

	// Sharing. Requests are served in ticket order, cyclically, a turn at a time. A slice
	// starts with W = kRouteWorkPerSlice expansions left; each turn goes to the next request
	// in the cycle and grants min(kRouteQuantum, left) - 1, so with the goal it may accept a
	// turn spends at most min(kRouteQuantum, left), and left drops by what it spent. A request
	// that ends leaves the cycle. The slice stops when fewer than 2 expansions are left or no
	// request is pending. The next slice's first turn goes to the request after the one that
	// had the last turn (by ticket, wrapping).
	class RouteQueue {
	  public:
		explicit RouteQueue(const RouteQueueArgs& args = {});

		// Main thread; may overlap a slice.
		RouteTicket request(const RouteRequestArgs& args);
		// Drops the request, its work, and its answer; takeAnswer never returns it.
		void cancel(RouteTicket ticket);
		// The answer once admitted after its slice, removed by the take; nothing before then.
		std::optional<RouteAnswer> takeAnswer(RouteTicket ticket);
		// Tickets whose answers wait for takeAnswer, ascending.
		std::vector<RouteTicket> answeredTickets() const;
		// Requested, not cancelled, and not yet answered.
		bool waiting(RouteTicket ticket) const;

		// Main thread; never while a slice runs.
		// Restarts every running search that has read a triangle the report changed.
		void notePatches(const geometry::nav::PatchReport& report);
		// Answers finished since the last admit become takeable, cancels take effect, and
		// requests made since then join the cycle.
		void admit();
		std::vector<RouteDeferral>	   deferrals() const;
		std::span<const RouteTurn>	   lastSlice() const { return turns; }
		std::uint64_t				   slices() const { return sliceCount; }

		// The slice: up to kRouteWorkPerSlice expansions against the mesh, whose corridor
		// widths must be current. Reads the mesh and the queue's slice state, nothing else.
		void runSlice(const geometry::nav::NavView& mesh);

	  private:
		struct Route {
			RouteTicket				   ticket = kNoRouteTicket;
			geometry::nav::SearchState search;
			std::uint32_t			   restarts = 0;
			// The box of touchedTriangles()[0, boxed).
			std::size_t boxed  = 0;
			bool		hasBox = false;
			geometry::Vec2i64		lo;
			geometry::Vec2i64		hi;
		};

		void finish(Route& route);
		void growBox(Route& route, const geometry::nav::NavView& mesh);

		std::int64_t deferralMarginMm;

		// Main-thread state.
		RouteTicket						  nextTicket = 1;
		std::vector<Route>				  inbox;
		std::vector<RouteTicket>		  cancels;
		std::unordered_set<RouteTicket>	  outstanding;
		std::map<RouteTicket, RouteAnswer> delivered;

		// Slice state.
		std::vector<Route>										running; // ascending tickets
		std::vector<std::pair<RouteTicket, RouteAnswer>>		finished;
		std::vector<RouteTurn>									turns;
		RouteTicket												nextTurn   = kNoRouteTicket;
		std::uint64_t											sliceCount = 0;
	};

} // namespace nav
