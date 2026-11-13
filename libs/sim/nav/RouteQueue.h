#pragma once

// RouteQueue: route searches under a per-tick work budget. A request is a ticket; each
// slice does a fixed number of search expansions shared among the pending requests, and
// a search carries on across slices until it ends, Found or Exhausted. Nothing caps a
// search. The work is a count, never a clock, so the slice an answer is ready on depends
// only on the requests, cancels, patch reports, and the fixed counts, never on the machine
// or its load.
//
// Geometry only: it reads a NavView and PatchReports and knows nothing of who asks.
//
// Concurrency. A tick's slice (runSlice) may run on one worker thread while the main
// thread goes on with its frame:
//   - request, cancel, takeAnswer, answeredTickets, and waiting touch only main-thread
//     state and may be called while a slice runs;
//   - notePatches, admit, searchSpansTicks, lastSlice, and slices read or write what the
//     slice does and must not be called while one runs;
//   - nothing may write the mesh (patch, refreshWidths, door edits) while a
//     slice runs or between an admit and the slice after it, and every report from a write
//     must reach notePatches before the next admit;
//   - one slice runs at a time, and the queue is driven from one main thread.
//
// Per tick, between slices: hand notePatches every PatchReport since the last slice (the
// patches' and the width refresh's), then admit, then start the slice.
//
// When an answer is takeable. A request made during tick t joins at the admit before
// slice t + 1. A search that ends in slice s has its answer held, with what it read, until
// the admit after slice s. Every report handed to notePatches before that admit is checked
// against what it read; one that changed a triangle it read sends it back to running, its
// work discarded, a restart counted. Otherwise that admit delivers it, and takeAnswer
// returns it from then on. So an answer is never delivered over a patch it did not see.
//
// Restarts. A search restarts whenever a reported change meets a triangle it has read,
// as many times as that happens; nothing limits them. A caller that wants a long search
// to finish holds back additions to the mesh while searchSpansTicks() is true.

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

	struct RouteRequestArgs {
		geometry::Vec2i64				 start;
		std::vector<geometry::Vec2i64> goals; // the answer is the one nearest by walk
		std::int64_t		 agentRadiusMm = 0;
		// Copied when the request is made: the caller may change its sets at once.
		geometry::nav::BeliefFilter belief;
	};

	enum class RouteOutcome {
		Routed,		  // a goal is reached
		NoKnownWay,	  // explored everything this mover can reach on meshed ground, or no goal can be stood on
		// The start, or a goal, is off the mesh inside a visibility limit still on its way in: ask
		// again once it lands. Only NavigationSystem, which holds the limits, answers it.
		AwaitingMesh,
	};

	// The mesh as a slice read it: the caller passes the mesh's epochs to runSlice.
	struct RouteMeshEpochs {
		std::uint64_t labels  = 0; // PatchableMesh::components().epoch()
		std::uint64_t patches = 0; // PatchableMesh::epoch()
	};

	struct RouteAnswer {
		RouteOutcome			 outcome   = RouteOutcome::NoKnownWay;
		std::int32_t			 goalIndex = -1; // into RouteRequestArgs::goals; -1 unless Routed
		geometry::nav::PathResult path;			 // the walk and the work it took, as SearchState gives it
		std::uint32_t			 restarts = 0;	 // patches that changed what it had read, each a restart
		std::uint64_t			 slice	  = 0;	 // the slice it finished in (slices() numbering)
		RouteMeshEpochs			 epochs;		 // as passed to that slice
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

	// Sharing. A slice starts with W = kRouteWorkPerSlice expansions left. First, each request
	// admitted since the last slice gets one turn, in ticket order. Then the rotation: the
	// running requests in ticket order, cyclically, starting at the first after the one that
	// had the last rotation turn of the previous slice (by ticket, wrapping). Every turn grants
	// min(kRouteQuantum, left) - 1, so with the goal it may accept a turn spends at most
	// min(kRouteQuantum, left), and left drops by what it spent. A request that ends leaves
	// the cycle. The slice stops when fewer than 2 expansions are left or nothing is running.
	class RouteQueue {
	  public:
		// Main thread; may overlap a slice.
		RouteTicket request(const RouteRequestArgs& args);
		// Drops the request, its work, and its answer; takeAnswer never returns it.
		void cancel(RouteTicket ticket);
		// The answer once delivered by an admit, removed by the take; nothing before then.
		std::optional<RouteAnswer> takeAnswer(RouteTicket ticket);
		// Tickets whose answers wait for takeAnswer, ascending.
		std::vector<RouteTicket> answeredTickets() const;
		// Requested, not cancelled, and not yet delivered.
		bool waiting(RouteTicket ticket) const;

		// Main thread; never while a slice runs.
		// Restarts every running search, and every finished one not yet delivered, that has
		// read a triangle the report changed.
		void notePatches(const geometry::nav::PatchReport& report);
		// Answers finished since the last admit are delivered, cancels take effect, and
		// requests made since then join, to have their first turn at the head of the next slice.
		void admit();
		// A request has had a turn in some slice and is still running: a search spanning ticks.
		bool						searchSpansTicks() const;
		std::span<const RouteTurn>	lastSlice() const { return turns; }
		std::uint64_t				slices() const { return sliceCount; }

		// The slice: up to kRouteWorkPerSlice expansions against the mesh, whose corridor
		// widths must be current. Reads the mesh and the queue's slice state, nothing else.
		void runSlice(const geometry::nav::NavView& mesh, const RouteMeshEpochs& epochs);

	  private:
		struct Route {
			RouteTicket				   ticket = kNoRouteTicket;
			geometry::nav::SearchState search;
			std::uint32_t			   restarts = 0;
			bool					   hadTurn	= false;
			// The box of touchedTriangles()[0, boxed): a report whose box misses it changed
			// nothing the search read.
			std::size_t		  boxed	 = 0;
			bool			  hasBox = false;
			geometry::Vec2i64 lo;
			geometry::Vec2i64 hi;
		};
		struct Finished {
			Route		route;
			RouteAnswer answer;
		};

		static bool hits(Route& route, const geometry::nav::PatchReport& report);
		static void restart(Route& route);
		static void growBox(Route& route, const geometry::nav::NavView& mesh);
		bool		turn(std::size_t at, std::uint32_t& left, const geometry::nav::NavView& mesh, const RouteMeshEpochs& epochs);
		void		finish(std::size_t at, const RouteMeshEpochs& epochs);

		// Main-thread state.
		RouteTicket						  nextTicket = 1;
		std::vector<Route>				  inbox;
		std::vector<RouteTicket>		  cancels;
		std::unordered_set<RouteTicket>	  outstanding;
		std::map<RouteTicket, RouteAnswer> delivered;

		// Slice state.
		std::vector<Route>		 running;  // ascending tickets
		std::vector<RouteTicket> newcomers; // admitted, first turn not yet had; ascending
		std::vector<Finished>	 finished;
		std::vector<RouteTurn>	 turns;
		RouteTicket				 nextTurn	= kNoRouteTicket;
		std::uint64_t			 sliceCount = 0;
	};

} // namespace nav
