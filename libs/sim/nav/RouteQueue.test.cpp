#include "RouteQueue.h"

#include <core/Vec2i64.h>
#include <nav/NavMesh.h>
#include <nav/PatchableMesh.h>
#include <nav/PathQuery.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <unordered_set>
#include <vector>

using geometry::Vec2i64;
using geometry::nav::BeliefFilter;
using geometry::nav::NavInputPolygon;
using geometry::nav::NavView;
using geometry::nav::PatchableMesh;
using geometry::nav::PatchEdits;
using geometry::nav::PatchReport;
using geometry::nav::PathResult;
using geometry::nav::SearchArgs;
using geometry::nav::SearchProgress;
using geometry::nav::SearchState;
using namespace nav;

namespace {

	constexpr std::int64_t kObstacleTag = -2;
	constexpr std::int64_t kWallSegment = 7;
	constexpr std::int64_t kRadius		= 150;

	NavInputPolygon solidBox(const Vec2i64& lo, const Vec2i64& hi, std::int64_t tag) {
		return {{lo, {hi.x, lo.y}, hi, {lo.x, hi.y}}, true, tag};
	}

	NavInputPolygon ground(const Vec2i64& lo, const Vec2i64& hi) {
		return {{lo, {hi.x, lo.y}, hi, {lo.x, hi.y}}, false, 1};
	}

	// A 160 m square of posts on a 1.6 m pitch, with a bar across the middle (y 80 to
	// 80.5 m) from x = 10 m to the east edge: a way between points just either side of the
	// bar runs round its west end. Gap points (gapPoint) sit at least 450 mm from any post.
	constexpr std::int64_t kSide	= 160000;
	constexpr std::int64_t kPitch	= 1600;
	constexpr std::int64_t kBarLoY	= 80000;
	constexpr std::int64_t kBarHiY	= 80500;
	constexpr std::int64_t kBarLoX	= 10000;

	Vec2i64 gapPoint(std::int64_t i, std::int64_t j) {
		return {2150 + kPitch * i, 2150 + kPitch * j};
	}

	PatchableMesh buildBarField() {
		PatchableMesh mesh;
		PatchEdits	  edits;
		edits.adds.push_back(ground({0, 0}, {kSide, kSide}));
		edits.adds.push_back(solidBox({kBarLoX, kBarLoY}, {kSide, kBarHiY}, kObstacleTag));
		for (std::int64_t x = 1000; x < kSide - 1000; x += kPitch) {
			for (std::int64_t y = 1000; y < kSide - 1000; y += kPitch) {
				const std::int64_t s = 300 + (x * 7 + y * 3) % 400;
				if (y + s > kBarLoY - 300 && y < kBarHiY + 300 && x + s > kBarLoX - 300) {
					continue;
				}
				edits.adds.push_back(solidBox({x, y}, {x + s, y + s}, kObstacleTag));
			}
		}
		mesh.applyPatch(edits);
		mesh.refreshWidths();
		return mesh;
	}

	const PatchableMesh& barField() {
		static const PatchableMesh mesh = buildBarField();
		return mesh;
	}

	// Points either side of the bar's middle: 3.2 m apart in a line, about 140 m on foot.
	const Vec2i64 kBelowBar = gapPoint(49, 48);
	const Vec2i64 kAboveBar = gapPoint(49, 50);

	RouteRequestArgs routeArgs(const Vec2i64& start, std::vector<Vec2i64> goals, const BeliefFilter& belief = {}) {
		return {.start = start, .goals = std::move(goals), .agentRadiusMm = kRadius, .belief = belief};
	}

	// What the queue runs for a request, run alone to its end.
	SearchState searchAlone(const NavView& view, const RouteRequestArgs& args) {
		SearchState search(SearchArgs{.start = args.start, .goals = args.goals, .agentRadiusMm = args.agentRadiusMm, .belief = geometry::nav::copyBelief(args.belief)});
		search.step(view, geometry::nav::kRunToEnd);
		return search;
	}

	// Admits and runs slices until every ticket is answered.
	std::map<RouteTicket, RouteAnswer> runUntilAnswered(RouteQueue& queue, const NavView& view, const std::vector<RouteTicket>& tickets) {
		std::map<RouteTicket, RouteAnswer> answers;
		for (int guard = 0; guard < 2000; ++guard) {
			queue.admit();
			for (const RouteTicket t : tickets) {
				if (std::optional<RouteAnswer> a = queue.takeAnswer(t)) {
					answers.emplace(t, std::move(*a));
				}
			}
			if (answers.size() == tickets.size()) {
				return answers;
			}
			queue.runSlice(view, {});
		}
		ADD_FAILURE() << "requests never all answered";
		return answers;
	}

	double walkLength(const PathResult& path) {
		double length = 0.0;
		for (std::size_t i = 1; i < path.points.size(); ++i) {
			const double dx = static_cast<double>(path.points[i].x - path.points[i - 1].x);
			const double dy = static_cast<double>(path.points[i].y - path.points[i - 1].y);
			length += std::sqrt(dx * dx + dy * dy);
		}
		return length;
	}

	// The turns of one slice against the sharing rule: each turn goes to the next pending
	// request by ticket, wrapping, starting at `first`; grants min(Q, left) - 1; spends at
	// most one more; and the slice stops only with fewer than 2 left or nothing pending.
	void expectSharingRule(std::span<const RouteTurn> turns, std::vector<RouteTicket> pending, RouteTicket first) {
		std::uint32_t left = kRouteWorkPerSlice;
		auto		  at   = static_cast<std::size_t>(std::lower_bound(pending.begin(), pending.end(), first) - pending.begin());
		for (const RouteTurn& turn : turns) {
			ASSERT_FALSE(pending.empty());
			at %= pending.size();
			EXPECT_EQ(turn.ticket, pending[at]);
			EXPECT_EQ(turn.granted, std::min(kRouteQuantum, left) - 1);
			EXPECT_LE(turn.spent, turn.granted + 1);
			EXPECT_LE(turn.spent, kRouteQuantum);
			ASSERT_LE(turn.spent, left);
			left -= turn.spent;
			if (turn.ended) {
				pending.erase(pending.begin() + static_cast<std::ptrdiff_t>(at));
			} else {
				++at;
			}
		}
		EXPECT_TRUE(left < 2 || pending.empty()) << "the slice stopped with work left and requests pending";
	}

	std::uint32_t spentIn(std::span<const RouteTurn> turns) {
		std::uint32_t total = 0;
		for (const RouteTurn& t : turns) {
			total += t.spent;
		}
		return total;
	}

	PatchReport addPost(PatchableMesh& mesh, const Vec2i64& center, std::int64_t half, RouteQueue& queue) {
		PatchEdits edits;
		edits.adds.push_back(solidBox({center.x - half, center.y - half}, {center.x + half, center.y + half}, kObstacleTag));
		PatchReport report = mesh.applyPatch(edits);
		queue.notePatches(report);
		queue.notePatches(mesh.refreshWidths());
		return report;
	}

	// The bar field with two goals in walled pens: a nearest-of search for them from outside
	// floods everything the start can reach before it answers.
	const Vec2i64 kPens[] = {gapPoint(30, 20), gapPoint(70, 90)};

	PatchableMesh buildPennedField() {
		PatchableMesh mesh = barField();
		PatchEdits	  pens;
		for (const Vec2i64& c : kPens) {
			pens.adds.push_back(solidBox({c.x - 1100, c.y - 1100}, {c.x + 1100, c.y - 900}, kObstacleTag));
			pens.adds.push_back(solidBox({c.x - 1100, c.y + 900}, {c.x + 1100, c.y + 1100}, kObstacleTag));
			pens.adds.push_back(solidBox({c.x - 1100, c.y - 900}, {c.x - 900, c.y + 900}, kObstacleTag));
			pens.adds.push_back(solidBox({c.x + 900, c.y - 900}, {c.x + 1100, c.y + 900}, kObstacleTag));
		}
		mesh.applyPatch(pens);
		mesh.refreshWidths();
		return mesh;
	}

	const PatchableMesh& pennedField() {
		static const PatchableMesh mesh = buildPennedField();
		return mesh;
	}

	RouteRequestArgs floodArgs(const Vec2i64& start) {
		return routeArgs(start, {kPens[0], kPens[1]});
	}

} // namespace

// An answer is pathThrough's for the same start, goal, disc, and belief, on open ground and
// round the bar: the same walk length, and the very walk the request's search gives run
// alone. Between equally short walks the goal-side phase may pick another than pathThrough.
TEST(RouteQueue, AnswerEqualsPathThrough) {
	const PatchableMesh& mesh = barField();
	const NavView		 view = mesh.view();
	RouteQueue			 queue;
	const std::vector<std::pair<Vec2i64, Vec2i64>> pairs = {
		{gapPoint(3, 4), gapPoint(40, 30)}, {gapPoint(90, 10), gapPoint(20, 45)}, {kBelowBar, kAboveBar}, {gapPoint(5, 95), gapPoint(80, 52)}};
	std::vector<RouteTicket> tickets;
	for (const auto& [from, to] : pairs) {
		tickets.push_back(queue.request(routeArgs(from, {to})));
	}
	const std::map<RouteTicket, RouteAnswer> answers = runUntilAnswered(queue, view, tickets);
	for (std::size_t i = 0; i < pairs.size(); ++i) {
		const RouteAnswer& answer = answers.at(tickets[i]);
		const PathResult   once	  = geometry::nav::pathThrough(view, pairs[i].first, pairs[i].second, kRadius);
		ASSERT_TRUE(once.reachable) << i;
		EXPECT_EQ(answer.outcome, RouteOutcome::Routed) << i;
		EXPECT_EQ(answer.goalIndex, 0) << i;
		EXPECT_EQ(answer.path.points.front(), once.points.front()) << i;
		EXPECT_EQ(answer.path.points.back(), once.points.back()) << i;
		EXPECT_NEAR(walkLength(answer.path), walkLength(once), 2.0) << i;
		EXPECT_EQ(answer.path.points, searchAlone(view, routeArgs(pairs[i].first, {pairs[i].second})).result().points) << i;
		EXPECT_EQ(answer.restarts, 0U) << i;
	}
}

// Each slice spends at most W, a turn at most Q, turns go round the pending requests in
// ticket order, and the next slice picks up after the last turn.
TEST(RouteQueue, WorkIsSharedAQuantumAtATimeInTicketOrder) {
	const PatchableMesh& mesh = barField();
	const NavView		 view = mesh.view();
	RouteQueue			 queue;
	std::vector<RouteTicket> tickets;
	for (std::int64_t i = 0; i < 5; ++i) {
		tickets.push_back(queue.request(routeArgs(gapPoint(40 + 4 * i, 48), {gapPoint(40 + 4 * i, 50)})));
	}
	queue.admit();
	queue.runSlice(view, {});
	const std::vector<RouteTurn> first(queue.lastSlice().begin(), queue.lastSlice().end());
	expectSharingRule(first, tickets, tickets[0]);
	EXPECT_LE(spentIn(first), kRouteWorkPerSlice);
	ASSERT_GE(first.size(), 6U);
	for (std::size_t i = 0; i < 5; ++i) {
		EXPECT_EQ(first[i].ticket, tickets[i]);
		EXPECT_EQ(first[i].granted, kRouteQuantum - 1);
		EXPECT_FALSE(first[i].ended) << "every search here runs past one turn";
	}
	EXPECT_EQ(first[5].ticket, tickets[0]) << "a second round starts at the oldest";

	std::vector<RouteTicket> pending;
	for (const RouteTicket t : tickets) {
		pending.push_back(t);
	}
	int slices = 1;
	while (queue.answeredTickets().size() < tickets.size() && slices < 200) {
		const RouteTurn last = queue.lastSlice().back();
		std::erase_if(pending, [&](RouteTicket t) {
			return std::any_of(queue.lastSlice().begin(), queue.lastSlice().end(), [&](const RouteTurn& turn) { return turn.ticket == t && turn.ended; });
		});
		// The next slice starts at the first pending ticket after the last turn's, wrapping.
		const auto	after = std::upper_bound(pending.begin(), pending.end(), last.ticket);
		RouteTicket start = pending.empty() ? kNoRouteTicket : (after == pending.end() ? pending.front() : *after);
		queue.admit();
		queue.runSlice(view, {});
		++slices;
		if (!pending.empty()) {
			expectSharingRule(queue.lastSlice(), pending, start);
			EXPECT_LE(spentIn(queue.lastSlice()), kRouteWorkPerSlice);
		}
	}
	queue.admit();
	EXPECT_EQ(queue.answeredTickets(), tickets);
	EXPECT_GT(slices, 3);
}

// The slice an answer lands on is the same run after run, and a short request answers in
// its first slice whatever else is pending, until the requests ahead of it fill the slice.
TEST(RouteQueue, AnswerSlicesRepeatAndFollowTheSharingRule) {
	const PatchableMesh& mesh = barField();
	const NavView		 view = mesh.view();
	const RouteRequestArgs shortRoute = routeArgs(gapPoint(10, 10), {gapPoint(16, 14)});

	struct Run {
		std::map<RouteTicket, RouteAnswer>	 answers;
		RouteTicket							 shortTicket = kNoRouteTicket;
		std::map<RouteTicket, std::uint64_t> firstTurn; // the slice of each request's first turn
	};
	auto scenario = [&](int longAhead, int longBehind, bool untilShort) {
		Run						 run;
		RouteQueue				 queue;
		std::vector<RouteTicket> tickets;
		for (int i = 0; i < longAhead; ++i) {
			tickets.push_back(queue.request(routeArgs(gapPoint(20 + 3 * (i % 25), 48), {gapPoint(20 + 3 * (i % 25), 50)})));
		}
		run.shortTicket = queue.request(shortRoute);
		tickets.push_back(run.shortTicket);
		for (int i = 0; i < longBehind; ++i) {
			tickets.push_back(queue.request(routeArgs(gapPoint(22 + 3 * (i % 25), 48), {gapPoint(22 + 3 * (i % 25), 50)})));
		}
		for (int guard = 0; guard < 500 && run.answers.size() < tickets.size(); ++guard) {
			queue.admit();
			for (const RouteTicket t : queue.answeredTickets()) {
				run.answers.emplace(t, *queue.takeAnswer(t));
			}
			if (untilShort && run.answers.count(run.shortTicket) != 0) {
				return run;
			}
			queue.runSlice(view, {});
			for (const RouteTurn& turn : queue.lastSlice()) {
				run.firstTurn.emplace(turn.ticket, queue.slices());
			}
		}
		EXPECT_EQ(run.answers.size(), tickets.size());
		return run;
	};

	const Run once	= scenario(12, 4, false);
	const Run again = scenario(12, 4, false);
	ASSERT_EQ(once.answers.size(), again.answers.size());
	for (const auto& [ticket, answer] : once.answers) {
		EXPECT_EQ(answer.slice, again.answers.at(ticket).slice) << ticket;
		EXPECT_EQ(answer.path.points, again.answers.at(ticket).path.points) << ticket;
		EXPECT_EQ(answer.path.nodesExpanded, again.answers.at(ticket).path.nodesExpanded) << ticket;
	}
	EXPECT_EQ(once.firstTurn, again.firstTurn);

	const RouteAnswer alone = scenario(0, 0, true).answers.begin()->second;
	ASSERT_EQ(alone.outcome, RouteOutcome::Routed);
	ASSERT_LT(alone.path.nodesExpanded, static_cast<std::int64_t>(kRouteQuantum) - 1) << "the short route must fit one turn";
	EXPECT_EQ(alone.slice, 1U);
	for (const int ahead : {0, 3, 8}) {
		for (const int behind : {0, 5, 30}) {
			const Run run = scenario(ahead, behind, true);
			EXPECT_EQ(run.answers.at(run.shortTicket).slice, 1U) << ahead << " ahead, " << behind << " behind";
			EXPECT_EQ(run.answers.at(run.shortTicket).path.points, alone.path.points);
		}
	}
	// Twelve long requests admitted with it and ahead of it fill the first slice: it keeps
	// its place at the head and answers in the second.
	EXPECT_EQ(once.firstTurn.at(once.shortTicket), 2U);
	EXPECT_EQ(once.answers.at(once.shortTicket).slice, once.firstTurn.at(once.shortTicket));
}

// A search longer than a slice carries on across slices to the same answer it gives run
// alone; so does one that explores all it can reach and finds no way.
TEST(RouteQueue, LongSearchesSpanSlicesAndFinish) {
	const PatchableMesh& mesh = barField();
	const NavView		 view = mesh.view();

	RouteQueue			   queue;
	const RouteRequestArgs around = routeArgs(kBelowBar, {kAboveBar});
	const RouteTicket	   ticket = queue.request(around);
	const RouteAnswer	   answer = runUntilAnswered(queue, view, {ticket}).at(ticket);
	const SearchState	   alone  = searchAlone(view, around);
	ASSERT_EQ(answer.outcome, RouteOutcome::Routed);
	EXPECT_GT(answer.path.nodesExpanded, static_cast<std::int64_t>(kRouteWorkPerSlice));
	EXPECT_GE(answer.slice, 2U);
	EXPECT_EQ(answer.path.points, alone.result().points);
	EXPECT_EQ(answer.path.nodesExpanded, alone.expansions());
	EXPECT_GT(walkLength(answer.path), 100000.0) << "the way runs round the bar";

	RouteQueue			   floodQueue;
	const NavView		   sealedView = pennedField().view();
	const RouteRequestArgs penned	  = floodArgs(gapPoint(50, 20));
	const RouteTicket	   flood	  = floodQueue.request(penned);
	const RouteAnswer	   none		  = runUntilAnswered(floodQueue, sealedView, {flood}).at(flood);
	EXPECT_EQ(none.outcome, RouteOutcome::NoKnownWay);
	EXPECT_EQ(none.goalIndex, -1);
	EXPECT_TRUE(none.path.points.empty());
	EXPECT_GT(none.slice, 3U);
	EXPECT_EQ(none.path.nodesExpanded, searchAlone(sealedView, penned).expansions());
}

// Every patch that changes a triangle a running search has read restarts it, however many
// times, and its answer is the one for the mesh as last patched.
TEST(RouteQueue, EachPatchMeetingARunningSearchRestartsIt) {
	PatchableMesh		   mesh = barField();
	RouteQueue			   queue;
	const RouteRequestArgs around = routeArgs(kBelowBar, {kAboveBar});
	const RouteTicket	   ticket = queue.request(around);
	for (const std::int64_t side : {-700, 700}) {
		queue.admit();
		queue.runSlice(mesh.view(), {});
		ASSERT_TRUE(queue.answeredTickets().empty());
		queue.admit();
		ASSERT_TRUE(queue.waiting(ticket));
		addPost(mesh, {kBelowBar.x + side, kBelowBar.y}, 150, queue);
	}
	const RouteAnswer answer = runUntilAnswered(queue, mesh.view(), {ticket}).at(ticket);
	const SearchState alone	 = searchAlone(mesh.view(), around);
	EXPECT_EQ(answer.restarts, 2U);
	ASSERT_EQ(answer.outcome, RouteOutcome::Routed);
	EXPECT_EQ(answer.path.points, alone.result().points);
	EXPECT_EQ(answer.path.nodesExpanded, alone.expansions());
	EXPECT_EQ(answer.path.points, geometry::nav::pathThrough(mesh.view(), kBelowBar, kAboveBar, kRadius).points);
}

// A patch that changes nothing the search reads leaves it running as if nothing happened.
TEST(RouteQueue, PatchAwayFromASearchDoesNotDisturbIt) {
	PatchableMesh		   mesh = barField();
	const RouteRequestArgs around = routeArgs(kBelowBar, {kAboveBar});
	const SearchState	   alone  = searchAlone(mesh.view(), around);
	const std::unordered_set<std::int32_t> read(alone.touchedTriangles().begin(), alone.touchedTriangles().end());

	RouteQueue		  queue;
	const RouteTicket ticket = queue.request(around);
	queue.admit();
	queue.runSlice(mesh.view(), {});
	queue.admit();
	ASSERT_TRUE(queue.waiting(ticket));
	const Vec2i64	  far = gapPoint(95, 95);
	const PatchReport report = addPost(mesh, far, 150, queue);
	ASSERT_FALSE(report.changes.empty());
	for (const geometry::nav::ChangedTriangle& c : report.changes) {
		ASSERT_EQ(read.count(c.id), 0U) << "the patch must lie outside everything the search reads";
	}
	const RouteAnswer answer = runUntilAnswered(queue, mesh.view(), {ticket}).at(ticket);
	EXPECT_EQ(answer.restarts, 0U);
	EXPECT_EQ(answer.path.points, alone.result().points);
	EXPECT_EQ(answer.path.nodesExpanded, alone.expansions());
}

// A cancelled request does no more work and never answers, wherever it was: waiting to
// join, running, or finished and not yet taken.
TEST(RouteQueue, CancelRemovesARequestAndItsWork) {
	const PatchableMesh& mesh = barField();
	const NavView		 view = mesh.view();
	RouteQueue			 queue;
	const RouteTicket	 a = queue.request(routeArgs(gapPoint(40, 48), {gapPoint(40, 50)}));
	const RouteTicket	 b = queue.request(routeArgs(gapPoint(60, 48), {gapPoint(60, 50)}));
	const RouteTicket	 unjoined = queue.request(routeArgs(gapPoint(70, 48), {gapPoint(70, 50)}));
	queue.cancel(unjoined);
	EXPECT_FALSE(queue.waiting(unjoined));
	queue.admit();
	queue.runSlice(view, {});
	queue.cancel(a);
	EXPECT_FALSE(queue.waiting(a));
	EXPECT_TRUE(queue.waiting(b));
	for (int i = 0; i < 100 && queue.waiting(b); ++i) {
		queue.admit();
		queue.runSlice(view, {});
		for (const RouteTurn& turn : queue.lastSlice()) {
			EXPECT_EQ(turn.ticket, b);
		}
	}
	queue.admit();
	EXPECT_FALSE(queue.takeAnswer(a).has_value());
	EXPECT_FALSE(queue.takeAnswer(unjoined).has_value());
	EXPECT_TRUE(queue.takeAnswer(b).has_value());

	// Finished in a slice, cancelled before the admit that would deliver it.
	const RouteTicket quick = queue.request(routeArgs(gapPoint(10, 10), {gapPoint(12, 12)}));
	queue.admit();
	queue.runSlice(view, {});
	ASSERT_TRUE(queue.lastSlice().back().ended);
	queue.cancel(quick);
	queue.admit();
	EXPECT_FALSE(queue.takeAnswer(quick).has_value());
	EXPECT_TRUE(queue.answeredTickets().empty());
	// Delivered, then cancelled before it was taken.
	const RouteTicket taken = queue.request(routeArgs(gapPoint(10, 10), {gapPoint(12, 12)}));
	queue.admit();
	queue.runSlice(view, {});
	queue.admit();
	ASSERT_EQ(queue.answeredTickets(), std::vector<RouteTicket>{taken});
	queue.cancel(taken);
	EXPECT_FALSE(queue.takeAnswer(taken).has_value());
}

// Several goals: the answer is the one nearest on foot, here not the one nearest in a line.
TEST(RouteQueue, NearestOfReturnsTheNearestByWalk) {
	const PatchableMesh&	   mesh	 = barField();
	const NavView			   view	 = mesh.view();
	const std::vector<Vec2i64> goals = {kAboveBar, gapPoint(49, 25), gapPoint(80, 20)};
	RouteQueue				   queue;
	const RouteTicket		   ticket = queue.request(routeArgs(kBelowBar, goals));
	const RouteAnswer		   answer = runUntilAnswered(queue, view, {ticket}).at(ticket);
	ASSERT_EQ(answer.outcome, RouteOutcome::Routed);
	std::vector<double> walks;
	for (const Vec2i64& g : goals) {
		walks.push_back(walkLength(geometry::nav::pathThrough(view, kBelowBar, g, kRadius)));
	}
	const auto nearest = static_cast<std::int32_t>(std::min_element(walks.begin(), walks.end()) - walks.begin());
	EXPECT_EQ(nearest, 1) << "the goal over the bar is nearest in a line, not on foot";
	EXPECT_EQ(answer.goalIndex, nearest);
	EXPECT_EQ(answer.path.points.back(), goals[static_cast<std::size_t>(nearest)]);
	EXPECT_NEAR(walkLength(answer.path), walks[static_cast<std::size_t>(nearest)], 2.0);
}

namespace {

	// A 20 x 12 m floor split by wall segment kWallSegment at x = 10 m, open for 1 m at the
	// south end and 2 m at the north, so from mid-room the way round is the north end; and a pen of solid
	// obstacle round (16, 6) m.
	PatchableMesh wallRoom() {
		PatchableMesh mesh;
		PatchEdits	  edits;
		edits.adds.push_back(ground({0, 0}, {20000, 12000}));
		edits.adds.push_back(solidBox({10000, 1000}, {10300, 10000}, kWallSegment));
		edits.adds.push_back(solidBox({15000, 5000}, {17000, 5200}, kObstacleTag));
		edits.adds.push_back(solidBox({15000, 6800}, {17000, 7000}, kObstacleTag));
		edits.adds.push_back(solidBox({15000, 5200}, {15200, 6800}, kObstacleTag));
		edits.adds.push_back(solidBox({16800, 5200}, {17000, 6800}, kObstacleTag));
		mesh.applyPatch(edits);
		mesh.refreshWidths();
		return mesh;
	}

} // namespace

// The request keeps the belief it was made with: the caller's sets changing after it
// changes nothing about the answer.
TEST(RouteQueue, BeliefIsCopiedWhenTheRequestIsMade) {
	const PatchableMesh						mesh = wallRoom();
	const NavView							view = mesh.view();
	std::unordered_set<std::uint64_t>		segments{static_cast<std::uint64_t>(kWallSegment)};
	std::unordered_set<std::uint64_t>		openings;
	const BeliefFilter						knowsWall{&segments, &openings};
	const Vec2i64							start{5000, 6000};
	const Vec2i64							goal{13000, 6000};
	const PathResult						around = geometry::nav::pathThrough(view, start, goal, kRadius, knowsWall);

	RouteQueue		  queue;
	const RouteTicket ticket = queue.request(routeArgs(start, {goal}, knowsWall));
	segments.clear();
	const PathResult through = geometry::nav::pathThrough(view, start, goal, kRadius, knowsWall);
	ASSERT_EQ(through.points.size(), 2U) << "with the wall forgotten the walk is straight";
	ASSERT_GT(around.points.size(), 2U);

	const RouteAnswer answer = runUntilAnswered(queue, view, {ticket}).at(ticket);
	EXPECT_EQ(answer.path.points, around.points);
}

// A search that runs out of ground to explore answers no known way, once: the queue
// does not run it again.
TEST(RouteQueue, AnExhaustedSearchAnswersNoKnownWayAndIsNotRetried) {
	const PatchableMesh mesh = wallRoom();
	const NavView		view = mesh.view();
	RouteQueue			queue;
	const RouteTicket	ticket = queue.request(routeArgs({5000, 6000}, {{16000, 6000}}));
	const RouteAnswer	answer = runUntilAnswered(queue, view, {ticket}).at(ticket);
	EXPECT_EQ(answer.outcome, RouteOutcome::NoKnownWay);
	EXPECT_EQ(answer.goalIndex, -1);
	EXPECT_FALSE(queue.waiting(ticket));
	for (int i = 0; i < 3; ++i) {
		queue.admit();
		queue.runSlice(view, {});
		EXPECT_TRUE(queue.lastSlice().empty());
	}
	queue.admit();
	EXPECT_FALSE(queue.takeAnswer(ticket).has_value());
	EXPECT_TRUE(queue.answeredTickets().empty());
}

// A search that ended in a slice is checked against the patches that land before the admit
// that would deliver it: one that changes what it read sends it back to run on the patched
// mesh, and what is delivered is that mesh's answer.
TEST(RouteQueue, PatchBeforeDeliveryRestartsAFinishedSearch) {
	PatchableMesh		   mesh	 = barField();
	const Vec2i64		   from	 = gapPoint(10, 10);
	const Vec2i64		   to	 = gapPoint(10, 14);
	const RouteRequestArgs args	 = routeArgs(from, {to});
	const PathResult	   clear = searchAlone(mesh.view(), args).result();
	ASSERT_EQ(clear.points.size(), 2U) << "the way runs straight up a clear lane";

	RouteQueue		  queue;
	const RouteTicket ticket = queue.request(args);
	queue.admit();
	queue.runSlice(mesh.view(), {});
	ASSERT_TRUE(queue.lastSlice().back().ended);
	addPost(mesh, gapPoint(10, 12), 100, queue);
	queue.admit();
	EXPECT_FALSE(queue.takeAnswer(ticket).has_value()) << "the finished answer was read before the post";
	EXPECT_TRUE(queue.waiting(ticket));

	const RouteAnswer answer = runUntilAnswered(queue, mesh.view(), {ticket}).at(ticket);
	EXPECT_EQ(answer.restarts, 1U);
	ASSERT_EQ(answer.outcome, RouteOutcome::Routed);
	EXPECT_NE(answer.path.points, clear.points);
	EXPECT_EQ(answer.path.points, searchAlone(mesh.view(), args).result().points);
	EXPECT_EQ(answer.path.points, geometry::nav::pathThrough(mesh.view(), from, to, kRadius).points);
}

// searchSpansTicks is true only while a request that has had a turn is still running.
TEST(RouteQueue, SearchSpansTicksOnlyPastAFirstSlice) {
	const PatchableMesh& mesh = barField();
	const NavView		 view = mesh.view();
	RouteQueue			 queue;
	EXPECT_FALSE(queue.searchSpansTicks());
	queue.request(routeArgs(gapPoint(10, 10), {gapPoint(16, 14)}));
	queue.request(routeArgs(gapPoint(30, 30), {gapPoint(33, 35)}));
	queue.admit();
	EXPECT_FALSE(queue.searchSpansTicks()) << "admitted, not yet run";
	queue.runSlice(view, {});
	ASSERT_EQ(queue.lastSlice().size(), 2U);
	ASSERT_TRUE(queue.lastSlice()[0].ended && queue.lastSlice()[1].ended);
	EXPECT_FALSE(queue.searchSpansTicks()) << "both finished in their first slice";
	queue.admit();
	EXPECT_FALSE(queue.searchSpansTicks());

	const RouteTicket around = queue.request(routeArgs(kBelowBar, {kAboveBar}));
	queue.admit();
	EXPECT_FALSE(queue.searchSpansTicks());
	queue.runSlice(view, {});
	EXPECT_TRUE(queue.searchSpansTicks());
	queue.admit();
	EXPECT_TRUE(queue.searchSpansTicks());
	runUntilAnswered(queue, view, {around});
	EXPECT_FALSE(queue.searchSpansTicks());
}

// A request admitted while many long searches run has its first turn at the head of the
// next slice, so a short one answers from that slice.
TEST(RouteQueue, ANewRequestGoesFirstInTheNextSlice) {
	const PatchableMesh& mesh = pennedField();
	const NavView		 view = mesh.view();
	RouteQueue			 queue;
	std::vector<RouteTicket> floods;
	for (std::int64_t i = 0; i < 40; ++i) {
		floods.push_back(queue.request(floodArgs(gapPoint(40 + i % 20, 10 + 2 * (i / 20)))));
	}
	// Nine first turns fit a slice: run until every search has had one.
	std::unordered_set<RouteTicket> turned;
	for (int s = 0; s < 20 && turned.size() < floods.size(); ++s) {
		queue.admit();
		queue.runSlice(view, {});
		for (const RouteTurn& turn : queue.lastSlice()) {
			turned.insert(turn.ticket);
		}
	}
	ASSERT_EQ(turned.size(), floods.size());
	queue.admit();
	ASSERT_TRUE(queue.answeredTickets().empty());
	ASSERT_TRUE(queue.searchSpansTicks());

	const RouteTicket shortTicket = queue.request(routeArgs(gapPoint(10, 10), {gapPoint(16, 14)}));
	queue.admit();
	queue.runSlice(view, {});
	ASSERT_FALSE(queue.lastSlice().empty());
	EXPECT_EQ(queue.lastSlice().front().ticket, shortTicket);
	EXPECT_TRUE(queue.lastSlice().front().ended);
	const std::uint64_t slice = queue.slices();
	queue.admit();
	const std::optional<RouteAnswer> answer = queue.takeAnswer(shortTicket);
	ASSERT_TRUE(answer.has_value());
	EXPECT_EQ(answer->outcome, RouteOutcome::Routed);
	EXPECT_EQ(answer->slice, slice);
	for (const RouteTicket t : floods) {
		EXPECT_TRUE(queue.waiting(t));
	}
}

// A start on no meshed ground is its own answer, never no known way; goals on no meshed
// ground from a start on it are no known way.
TEST(RouteQueue, AStartOffTheMeshIsNotNoKnownWay) {
	const PatchableMesh mesh = wallRoom();
	const NavView		view = mesh.view();
	RouteQueue			queue;
	const RouteTicket	off		 = queue.request(routeArgs({50000, 50000}, {{5000, 6000}}));
	const RouteTicket	bothOff	 = queue.request(routeArgs({50000, 50000}, {{60000, 50000}}));
	const RouteTicket	goalsOff = queue.request(routeArgs({5000, 6000}, {{60000, 50000}}));
	const std::map<RouteTicket, RouteAnswer> answers = runUntilAnswered(queue, view, {off, bothOff, goalsOff});
	EXPECT_EQ(answers.at(off).outcome, RouteOutcome::StartOffMesh);
	EXPECT_EQ(answers.at(bothOff).outcome, RouteOutcome::StartOffMesh);
	EXPECT_EQ(answers.at(goalsOff).outcome, RouteOutcome::NoKnownWay);
	EXPECT_EQ(answers.at(off).goalIndex, -1);
	EXPECT_TRUE(answers.at(off).path.points.empty());
}

// An answer carries the epochs passed to the slice it finished in.
TEST(RouteQueue, AnAnswerCarriesTheEpochsOfItsSlice) {
	const PatchableMesh& mesh = barField();
	const NavView		 view = mesh.view();
	RouteQueue			 queue;
	const RouteTicket	 quick	= queue.request(routeArgs(gapPoint(10, 10), {gapPoint(16, 14)}));
	const RouteTicket	 around = queue.request(routeArgs(kBelowBar, {kAboveBar}));
	std::map<RouteTicket, RouteAnswer> answers;
	for (std::uint64_t s = 1; s < 50 && answers.size() < 2; ++s) {
		queue.admit();
		for (const RouteTicket t : queue.answeredTickets()) {
			answers.emplace(t, *queue.takeAnswer(t));
		}
		queue.runSlice(view, {.labels = 100 + s, .patches = 200 + s});
	}
	ASSERT_EQ(answers.size(), 2U);
	for (const RouteTicket t : {quick, around}) {
		const RouteAnswer& a = answers.at(t);
		EXPECT_EQ(a.epochs.labels, 100 + a.slice) << t;
		EXPECT_EQ(a.epochs.patches, 200 + a.slice) << t;
	}
	EXPECT_EQ(answers.at(quick).slice, 1U);
	EXPECT_GT(answers.at(around).slice, 1U);
}
