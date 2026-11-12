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

	bool RouteQueue::hits(Route& route, const PatchReport& report) {
		// A changed triangle the search read lies in its touched box, or borders one that
		// does, so a touched box clear of the report's box holds none of the changes.
		if (!route.hasBox || !boxesMeet(route.lo, route.hi, report.lo, report.hi)) {
			return false;
		}
		return std::any_of(report.changes.begin(), report.changes.end(), [&](const geometry::nav::ChangedTriangle& c) { return route.search.touches(c.id); });
	}

	void RouteQueue::restart(Route& route) {
		route.search.restart();
		++route.restarts;
		route.boxed	 = 0;
		route.hasBox = false;
	}

	void RouteQueue::notePatches(const PatchReport& report) {
		if (report.changes.empty()) {
			return;
		}
		for (Route& route : running) {
			if (hits(route, report)) {
				restart(route);
			}
		}
		for (auto it = finished.begin(); it != finished.end();) {
			if (!hits(it->route, report)) {
				++it;
				continue;
			}
			Route& route = it->route;
			restart(route);
			const auto at = std::lower_bound(running.begin(), running.end(), route.ticket, [](const Route& r, RouteTicket t) { return r.ticket < t; });
			running.insert(at, std::move(route));
			it = finished.erase(it);
		}
	}

	void RouteQueue::admit() {
		for (Finished& done : finished) {
			if (outstanding.erase(done.route.ticket) != 0) {
				delivered.emplace(done.route.ticket, std::move(done.answer));
			}
		}
		finished.clear();
		if (!cancels.empty()) {
			std::sort(cancels.begin(), cancels.end());
			const auto cancelled = [&](RouteTicket t) { return std::binary_search(cancels.begin(), cancels.end(), t); };
			std::erase_if(running, [&](const Route& r) { return cancelled(r.ticket); });
			std::erase_if(newcomers, cancelled);
			cancels.clear();
		}
		for (Route& route : inbox) {
			newcomers.push_back(route.ticket);
			running.push_back(std::move(route));
		}
		inbox.clear();
	}

	bool RouteQueue::searchSpansTicks() const {
		return std::any_of(running.begin(), running.end(), [](const Route& r) { return r.hadTurn; });
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

	void RouteQueue::finish(std::size_t at, const NavView& mesh, const RouteMeshEpochs& epochs) {
		Route&		route = running[at];
		RouteAnswer answer;
		answer.path = route.search.result();
		if (route.search.progress() == SearchProgress::Found) {
			answer.outcome = RouteOutcome::Routed;
		} else if (mesh.locate(route.search.args().start) < 0) {
			answer.outcome = RouteOutcome::StartOffMesh;
		} else {
			answer.outcome = RouteOutcome::NoKnownWay;
		}
		answer.goalIndex = route.search.goalIndex();
		answer.restarts	 = route.restarts;
		answer.slice	 = sliceCount;
		answer.epochs	 = epochs;
		finished.push_back({.route = std::move(route), .answer = std::move(answer)});
		running.erase(running.begin() + static_cast<std::ptrdiff_t>(at));
	}

	// One turn for running[at]; true when it ended the request (and removed it from running).
	bool RouteQueue::turn(std::size_t at, std::uint32_t& left, const NavView& mesh, const RouteMeshEpochs& epochs) {
		Route&				 route	 = running[at];
		const std::uint32_t	 granted = std::min(kRouteQuantum, left) - 1;
		const std::int64_t	 before	 = route.search.expansions();
		const SearchProgress how	 = route.search.step(mesh, granted);
		const auto			 spent	 = static_cast<std::uint32_t>(route.search.expansions() - before);
		assert(spent <= granted + 1 && "RouteQueue: a turn closed more than its grant and the goal");
		assert((spent > 0 || how != SearchProgress::Running) && "RouteQueue: a turn did no work");
		left -= spent;
		route.hadTurn	 = true;
		const bool ended = how != SearchProgress::Running;
		turns.push_back({.ticket = route.ticket, .granted = granted, .spent = spent, .ended = ended});
		growBox(route, mesh);
		if (ended) {
			finish(at, mesh, epochs);
		}
		return ended;
	}

	void RouteQueue::runSlice(const NavView& meshIn, const RouteMeshEpochs& epochs) {
		++sliceCount;
		turns.clear();
		const NavView mesh = meshIn.resolved();
		assert(mesh.widthsCurrent() && "RouteQueue: corridor widths lag behind a patch");
		if (running.empty() || !mesh.widthsCurrent()) {
			return;
		}
		const auto indexOf = [&](RouteTicket t) {
			return static_cast<std::size_t>(std::lower_bound(running.begin(), running.end(), t, [](const Route& r, RouteTicket u) { return r.ticket < u; }) -
											running.begin());
		};
		std::uint32_t left	 = kRouteWorkPerSlice;
		std::size_t	  served = 0;
		for (; served < newcomers.size() && left >= 2; ++served) {
			turn(indexOf(newcomers[served]), left, mesh, epochs);
		}
		newcomers.erase(newcomers.begin(), newcomers.begin() + static_cast<std::ptrdiff_t>(served));

		std::size_t at = indexOf(nextTurn);
		while (left >= 2 && !running.empty()) {
			if (at >= running.size()) {
				at = 0;
			}
			if (!turn(at, left, mesh, epochs)) {
				++at;
			}
			nextTurn = at < running.size() ? running[at].ticket : kNoRouteTicket;
		}
	}

} // namespace nav
