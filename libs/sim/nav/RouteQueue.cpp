#include "RouteQueue.h"

#include <algorithm>
#include <cassert>

namespace nav {

	using geometry::nav::NavView;
	using geometry::nav::PatchReport;
	using geometry::nav::SearchArgs;
	using geometry::nav::SearchProgress;
	using geometry::nav::SearchState;

	namespace {

		bool boxesMeet(const geometry::Vec2i64& aLo, const geometry::Vec2i64& aHi, const geometry::Vec2i64& bLo, const geometry::Vec2i64& bHi) {
			return aLo.x <= bHi.x && bLo.x <= aHi.x && aLo.y <= bHi.y && bLo.y <= aHi.y;
		}

	} // namespace

	RouteQueue::RouteQueue(const RouteQueueArgs& args) : deferralMarginMm(args.deferralMarginMm) {}

	RouteTicket RouteQueue::request(const RouteRequestArgs& args) {
		const RouteTicket ticket = nextTicket++;
		SearchArgs		  search{.start = args.start, .goals = args.goals, .agentRadiusMm = args.agentRadiusMm, .belief = geometry::nav::copyBelief(args.belief)};
		inbox.push_back({.ticket = ticket, .search = SearchState(std::move(search))});
		outstanding.insert(ticket);
		return ticket;
	}

	void RouteQueue::cancel(RouteTicket ticket) {
		delivered.erase(ticket);
		if (outstanding.erase(ticket) == 0) {
			return;
		}
		const auto queued = std::find_if(inbox.begin(), inbox.end(), [&](const Route& r) { return r.ticket == ticket; });
		if (queued != inbox.end()) {
			inbox.erase(queued);
			return;
		}
		cancels.push_back(ticket);
	}

	std::optional<RouteAnswer> RouteQueue::takeAnswer(RouteTicket ticket) {
		const auto it = delivered.find(ticket);
		if (it == delivered.end()) {
			return std::nullopt;
		}
		RouteAnswer answer = std::move(it->second);
		delivered.erase(it);
		return answer;
	}

	std::vector<RouteTicket> RouteQueue::answeredTickets() const {
		std::vector<RouteTicket> out;
		out.reserve(delivered.size());
		for (const auto& [ticket, answer] : delivered) {
			out.push_back(ticket);
		}
		return out;
	}

	bool RouteQueue::waiting(RouteTicket ticket) const {
		return outstanding.count(ticket) != 0;
	}

	void RouteQueue::notePatches(const PatchReport& report) {
		if (report.changes.empty()) {
			return;
		}
		// A changed triangle the search read lies in its touched box, or borders one that
		// does, so a touched box clear of the report's box holds none of the changes.
		for (Route& route : running) {
			if (!route.hasBox || !boxesMeet(route.lo, route.hi, report.lo, report.hi)) {
				continue;
			}
			const bool hit = std::any_of(report.changes.begin(), report.changes.end(),
										 [&](const geometry::nav::ChangedTriangle& c) { return route.search.touches(c.id); });
			if (!hit) {
				continue;
			}
			assert(route.restarts == 0 && "RouteQueue: a patch landed inside a deferral box");
			route.search.restart();
			++route.restarts;
			route.boxed	 = 0;
			route.hasBox = false;
		}
	}

	void RouteQueue::admit() {
		for (auto& [ticket, answer] : finished) {
			if (outstanding.erase(ticket) != 0) {
				delivered.emplace(ticket, std::move(answer));
			}
		}
		finished.clear();
		if (!cancels.empty()) {
			std::sort(cancels.begin(), cancels.end());
			std::erase_if(running, [&](const Route& r) { return std::binary_search(cancels.begin(), cancels.end(), r.ticket); });
			cancels.clear();
		}
		for (Route& route : inbox) {
			running.push_back(std::move(route));
		}
		inbox.clear();
	}

	std::vector<RouteDeferral> RouteQueue::deferrals() const {
		std::vector<RouteDeferral> out;
		for (const Route& route : running) {
			if (route.restarts == 0 || !route.hasBox) {
				continue;
			}
			out.push_back({.ticket = route.ticket,
						   .lo	   = {route.lo.x - deferralMarginMm, route.lo.y - deferralMarginMm},
						   .hi	   = {route.hi.x + deferralMarginMm, route.hi.y + deferralMarginMm}});
		}
		return out;
	}

	void RouteQueue::growBox(Route& route, const NavView& mesh) {
		const std::span<const std::int32_t> touched = route.search.touchedTriangles();
		for (; route.boxed < touched.size(); ++route.boxed) {
			const geometry::nav::NavTriangle& tri = mesh.triangle(touched[route.boxed]);
			for (const std::uint32_t v : tri.v) {
				const geometry::Vec2i64& p = mesh.vertex(v);
				if (!route.hasBox) {
					route.lo	 = p;
					route.hi	 = p;
					route.hasBox = true;
					continue;
				}
				route.lo = {std::min(route.lo.x, p.x), std::min(route.lo.y, p.y)};
				route.hi = {std::max(route.hi.x, p.x), std::max(route.hi.y, p.y)};
			}
		}
	}

	void RouteQueue::finish(Route& route) {
		RouteAnswer answer;
		answer.path		 = route.search.result();
		answer.outcome	 = route.search.progress() == SearchProgress::Found ? RouteOutcome::Routed : RouteOutcome::NoKnownWay;
		answer.goalIndex = route.search.goalIndex();
		answer.restarts	 = route.restarts;
		answer.slice	 = sliceCount;
		finished.emplace_back(route.ticket, std::move(answer));
	}

	void RouteQueue::runSlice(const NavView& meshIn) {
		++sliceCount;
		turns.clear();
		const NavView mesh = meshIn.resolved();
		assert(mesh.widthsCurrent() && "RouteQueue: corridor widths lag behind a patch");
		if (running.empty() || !mesh.widthsCurrent()) {
			return;
		}
		std::uint32_t left = kRouteWorkPerSlice;
		std::size_t	  at   = static_cast<std::size_t>(
			  std::lower_bound(running.begin(), running.end(), nextTurn, [](const Route& r, RouteTicket t) { return r.ticket < t; }) - running.begin());
		while (left >= 2 && !running.empty()) {
			if (at >= running.size()) {
				at = 0;
			}
			Route&				route	= running[at];
			const std::uint32_t granted = std::min(kRouteQuantum, left) - 1;
			const std::int64_t	before	= route.search.expansions();
			const SearchProgress how	= route.search.step(mesh, granted);
			const auto			spent	= static_cast<std::uint32_t>(route.search.expansions() - before);
			assert(spent <= granted + 1 && "RouteQueue: a turn closed more than its grant and the goal");
			assert((spent > 0 || how != SearchProgress::Running) && "RouteQueue: a turn did no work");
			left -= spent;
			const bool ended = how != SearchProgress::Running;
			turns.push_back({.ticket = route.ticket, .granted = granted, .spent = spent, .ended = ended});
			if (ended) {
				finish(route);
				running.erase(running.begin() + static_cast<std::ptrdiff_t>(at));
				nextTurn = at < running.size() ? running[at].ticket : kNoRouteTicket;
				continue;
			}
			growBox(route, mesh);
			++at;
			nextTurn = at < running.size() ? running[at].ticket : kNoRouteTicket;
		}
	}

} // namespace nav
