#include "DrawnShadows.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <tuple>

namespace worldview {

	namespace {

		// How far an eased apex may lie from a face and still be on it: the apex is a float blend of two
		// corners of that face.
		constexpr double kOnFaceMm = 1.0;
		// A band and a fin reach this far past the bright shape's farthest point from the apex.
		constexpr double kBandPastBrightMm = 1000.0;

		glm::dvec2 toDouble(const geometry::Vec2i64& point) { return {static_cast<double>(point.x), static_cast<double>(point.y)}; }

		double distanceToSegment(glm::dvec2 point, glm::dvec2 from, glm::dvec2 to) {
			const glm::dvec2 along	= to - from;
			const double	 length = glm::dot(along, along);
			const double	 t		= length > 0.0 ? std::clamp(glm::dot(point - from, along) / length, 0.0, 1.0) : 0.0;
			return glm::length(point - (from + t * along));
		}

	} // namespace

	void DrawnShadows::takeUpEases(const ecs::VisionSystem::SightBodies& bodies) {
		const std::vector<CornerEase>& running = eases;
		std::vector<CornerEase>		   next;
		next.reserve(bodies.shadowEdges->size());
		// Every new edge and running ease that may pair: same body and lit side, the ease's apex on a face
		// at the new edge's corner, nearest first.
		std::vector<std::tuple<double, std::size_t, std::size_t>> pairs;
		for (std::size_t index = 0; index < bodies.shadowEdges->size(); ++index) {
			const vision::ShadowEdge& edge = (*bodies.shadowEdges)[index];
			const vision::SightBody&  body = (*bodies.bodies)[edge.body];
			const glm::dvec2		  corner = toDouble(body.edges[edge.corner].a);
			const glm::dvec2		  after	 = toDouble(body.edges[edge.corner].b);
			const glm::dvec2		  before = toDouble(body.edges[body.previousEdge[edge.corner]].a);
			next.push_back({.source = body.source, .bodyId = body.id, .litTurn = edge.litTurn, .buildCorner = body.edges[edge.corner].a});
			for (std::size_t old = 0; old < running.size(); ++old) {
				const CornerEase& ease = running[old];
				if (!ease.started || ease.source != body.source || ease.bodyId != body.id || ease.litTurn != edge.litTurn) {
					continue;
				}
				if (distanceToSegment(ease.apex, before, corner) <= kOnFaceMm || distanceToSegment(ease.apex, corner, after) <= kOnFaceMm) {
					pairs.emplace_back(glm::length(ease.apex - corner), index, old);
				}
			}
		}
		std::ranges::sort(pairs);
		std::vector<bool> taken(running.size(), false);
		std::vector<bool> given(next.size(), false);
		for (const auto& [distance, index, old] : pairs) {
			if (taken[old] || given[index]) {
				continue;
			}
			taken[old]				  = true;
			given[index]			  = true;
			const geometry::Vec2i64 corner = next[index].buildCorner;
			next[index]				  = running[old];
			next[index].buildCorner	  = corner;
		}
		eases = std::move(next);
	}

	bool DrawnShadows::softDue(glm::dvec2 eyeMm, const vision::SightTunables& tunables) const {
		return !softBuilt || !(tunables == softTunables) || glm::length(eyeMm - softEye) >= static_cast<double>(tunables.drawnShadowRebuildMm);
	}

	void DrawnShadows::buildSoft(const ecs::VisionSystem::SightBodies& bodies, glm::dvec2 eyeMm, double reachLimit, const vision::SightTunables& tunables) {
		softTriangles.clear();
		softDrawn.clear();
		for (const vision::SightBody& body : *bodies.bodies) {
			if (body.kind != vision::BodyKind::Soft) {
				continue;
			}
			// A cheap bound first: no corner nearer than the reach less the body's own diameter.
			const glm::dvec2 offset = toDouble(body.centre) - eyeMm;
			if (glm::length(offset) - static_cast<double>(body.diameterMm) > reachLimit) {
				continue;
			}
			const std::size_t				 before = softTriangles.size();
			const std::optional<SoftShadow> shadow = appendSoftShadow(body, eyeMm, tunables, softTriangles);
			if (shadow.has_value() && shadow->distance <= reachLimit) {
				softDrawn.push_back({.bodyId = body.id, .diameterMm = body.diameterMm, .shadow = *shadow});
			} else {
				softTriangles.resize(before);
			}
		}
		softBuilt	 = true;
		softEye		 = eyeMm;
		softTunables = tunables;
	}

	bool DrawnShadows::buildBands(
		const ecs::VisionSystem::SightBodies& bodies,
		glm::dvec2							  eyeMm,
		double								  reachLimit,
		double								  brightReachMm,
		const vision::SightTunables&		  tunables,
		bool								  force,
		double								  walkedMm
	) {
		const double easeMm	  = static_cast<double>(tunables.shadowCornerEaseMm);
		// Standing still nothing a band draws moves: his walk is what eases an apex.
		const bool	 changing = force || walkedMm > 0.0 || !bandsBuilt || eyeMm != bandEye || !(tunables == bandTunables);
		if (!changing || (bodies.shadowEdges->empty() && bandTriangles.empty())) {
			return false;
		}
		bandTriangles.clear();
		edgesDrawn.clear();
		const double widening = std::max(static_cast<double>(bodies.leadMm), glm::length(eyeMm - toDouble(bodies.eyeCentre)));
		for (std::size_t index = 0; index < bodies.shadowEdges->size(); ++index) {
			const vision::ShadowEdge& edge	   = (*bodies.shadowEdges)[index];
			const vision::SightBody&  body	   = (*bodies.bodies)[edge.body];
			CornerEase&				  ease	   = eases[index];
			const std::uint32_t		  followed = followedCorner(body, edge.corner, edge.litTurn, eyeMm);
			const glm::dvec2		  target   = toDouble(body.edges[followed].a);
			if (!ease.started) {
				ease.started  = true;
				ease.apex	  = target;
				ease.from	  = target;
				ease.target	  = target;
				ease.walkedMm = easeMm;
			} else if (target != ease.target) {
				ease.from	  = ease.apex;
				ease.target	  = target;
				ease.walkedMm = 0.0;
			}
			const double eased = std::min(1.0, ease.walkedMm / easeMm);
			ease.apex		   = ease.from + eased * (ease.target - ease.from);
			if (glm::length(ease.apex - eyeMm) > reachLimit) {
				continue;
			}
			const ShadowBand band = shadowBandAt(eyeMm, ease.apex, edge.litTurn, widening, tunables);
			appendShadowBand(band, band.cornerDistance + brightReachMm + kBandPastBrightMm, bandTriangles);
			edgesDrawn.push_back(
				{.body = edge.body, .bodyId = body.id, .diameterMm = body.diameterMm, .buildCorner = edge.corner, .followedCorner = followed, .band = band, .ease = eased}
			);
		}
		bandsBuilt	 = true;
		bandEye		 = eyeMm;
		bandTunables = tunables;
		return true;
	}

	void DrawnShadows::update(
		const ecs::VisionSystem::SightBodies& bodies,
		std::uint64_t						  buildRevision,
		glm::dvec2							  eyeMm,
		double								  walkedMm,
		double								  brightReachMm,
		const vision::SightTunables&		  tunables,
		bool								  rebuildSoft
	) {
		for (CornerEase& ease : eases) {
			ease.walkedMm += walkedMm;
		}
		const bool newBuild = !built || buildRevision != lastBuild;
		if (newBuild) {
			takeUpEases(bodies);
			lastBuild = buildRevision;
		}
		built					 = true;
		const double reachLimit	 = std::min(static_cast<double>(std::min(tunables.drawnShadowReachMm, bodies.reachMm)), brightReachMm);
		frameCounts.softRebuilt	 = rebuildSoft;
		if (rebuildSoft) {
			buildSoft(bodies, eyeMm, reachLimit, tunables);
		}
		frameCounts.bandsRebuilt = buildBands(bodies, eyeMm, reachLimit, brightReachMm, tunables, newBuild, walkedMm);
	}

} // namespace worldview
