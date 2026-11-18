#include "DrawnShadows.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <tuple>

namespace worldview {

	namespace {

		// A band and a fin reach this far past the bright shape's farthest point from the apex.
		constexpr double kBandPastBrightMm = 1000.0;
		// What a soft body never built is taken to cost, in pieces, against the budget a frame.
		constexpr std::size_t kDueBodyLeastPieces = 8;
		// The width of each ring of soft bodies by distance; the last ring holds the rest.
		constexpr double kSoftRingWidthMm = 6000.0;

		glm::dvec2 toDouble(const geometry::Vec2i64& point) {
			return {static_cast<double>(point.x), static_cast<double>(point.y)};
		}

	} // namespace

	void DrawnShadows::takeUpEases(const ecs::VisionSystem::SightBodies& bodies) {
		const std::vector<CornerEase>& running = eases;
		std::vector<CornerEase>		   next;
		next.reserve(bodies.shadowEdges->size());
		// Every new edge and running ease that may pair: same body and lit side, nearest first. The apex can
		// lie on any face of the body: on a ring of many short faces a walk between two builds can carry the
		// followed corner several corners on, and an ease dropped there would snap the band to its corner.
		std::vector<std::tuple<double, std::size_t, std::size_t>> pairs;
		for (std::size_t index = 0; index < bodies.shadowEdges->size(); ++index) {
			const vision::ShadowEdge& edge = (*bodies.shadowEdges)[index];
			const vision::SightBody&  body = (*bodies.bodies)[edge.body];
			const glm::dvec2		  corner = toDouble(body.edges[edge.corner].a);
			next.push_back({.source = body.source, .bodyId = body.id, .litTurn = edge.litTurn, .buildCorner = body.edges[edge.corner].a});
			for (std::size_t old = 0; old < running.size(); ++old) {
				const CornerEase& ease = running[old];
				if (!ease.started || ease.source != body.source || ease.bodyId != body.id || ease.litTurn != edge.litTurn) {
					continue;
				}
				pairs.emplace_back(glm::length(ease.apex - corner), index, old);
			}
		}
		std::ranges::sort(pairs);
		std::vector<bool> taken(running.size(), false);
		std::vector<bool> given(next.size(), false);
		for (const auto& [distance, index, old] : pairs) {
			if (taken[old] || given[index]) {
				continue;
			}
			taken[old] = true;
			given[index] = true;
			const geometry::Vec2i64 corner = next[index].buildCorner;
			next[index] = running[old];
			next[index].buildCorner = corner;
		}
		eases = std::move(next);
	}

	std::uint64_t DrawnShadows::keyOf(const vision::SightBody& body) {
		return (body.source == vision::SightBody::Source::Wall ? std::uint64_t{1} << 63U : 0U) | body.id;
	}

	bool DrawnShadows::buildBands(const ecs::VisionSystem::SightBodies& bodies, double reachLimit, bool force) {
		const double easeMm = static_cast<double>(frameTunables.shadowCornerEaseMm);
		// Standing still nothing a band draws moves: his walk is what eases an apex.
		const bool changing = force || frameWalkedMm > 0.0 || !bandsBuilt || frameEye != bandEye || !(frameTunables == bandTunables);
		if (!changing || (bodies.shadowEdges->empty() && bandTriangles.empty())) {
			return false;
		}
		bandTriangles.clear();
		edgesDrawn.clear();
		const double widening = std::max(static_cast<double>(bodies.leadMm), glm::length(frameEye - toDouble(bodies.eyeCentre)));
		double		 brightReachMm = 0.0;
		for (const geometry::Vec2i64& point : frameBright) {
			brightReachMm = std::max(brightReachMm, glm::length(toDouble(point) - frameEye));
		}
		for (std::size_t index = 0; index < bodies.shadowEdges->size(); ++index) {
			const vision::ShadowEdge& edge = (*bodies.shadowEdges)[index];
			const vision::SightBody&  body = (*bodies.bodies)[edge.body];
			CornerEase&				  ease = eases[index];
			const std::uint32_t		  followed = followedCorner(body, edge.corner, edge.litTurn, frameEye);
			const glm::dvec2		  target = toDouble(body.edges[followed].a);
			if (!ease.started) {
				ease.started = true;
				ease.apex = target;
				ease.from = target;
				ease.target = target;
				ease.walkedMm = easeMm;
			} else if (target != ease.target) {
				ease.from = ease.apex;
				ease.target = target;
				ease.walkedMm = 0.0;
			}
			const double eased = std::min(1.0, ease.walkedMm / easeMm);
			ease.apex = ease.from + eased * (ease.target - ease.from);
			if (glm::length(ease.apex - frameEye) > reachLimit) {
				continue;
			}
			const ShadowBand band = shadowBandAt(frameEye, ease.apex, edge.litTurn, widening, frameTunables);
			appendShadowBand(band, band.cornerDistance + brightReachMm + kBandPastBrightMm, bandTriangles);
			edgesDrawn.push_back(
				{.body = edge.body,
				 .bodyId = body.id,
				 .diameterMm = body.diameterMm,
				 .buildCorner = edge.corner,
				 .followedCorner = followed,
				 .band = band,
				 .ease = eased}
			);
		}
		bandsBuilt = true;
		bandEye = frameEye;
		bandTunables = frameTunables;
		return true;
	}

	namespace {

		// Whether a soft body can draw anything from `eyeMm`: some of it within `reachLimit`, and its
		// triangle, at most its diameter wide and diameter x distance / (ray - diameter) long, a pixel
		// or more on screen.
		bool softBodyShows(
			const vision::SightBody&	 body,
			glm::dvec2					 eyeMm,
			double						 reachLimit,
			double						 pixelsPerMm,
			const vision::SightTunables& tunables
		) {
			const double diameter = static_cast<double>(body.diameterMm);
			const double distance = std::max(0.0, glm::length(toDouble(body.centre) - eyeMm) - diameter);
			if (distance > reachLimit) {
				return false;
			}
			const double ray = static_cast<double>(tunables.softShadowRayWidthMm);
			const double length = diameter < ray ? (distance + diameter) * diameter / (ray - diameter) : 0.0;
			return (diameter + length) * pixelsPerMm >= 1.0;
		}

	} // namespace

	void DrawnShadows::update(
		const ecs::VisionSystem::SightBodies& bodies,
		std::uint64_t						  buildRevision,
		glm::dvec2							  eyeMm,
		double								  walkedMm,
		std::span<const geometry::Vec2i64>	  bright,
		double								  brightReachMm,
		double								  pixelsPerMm,
		const vision::SightTunables&		  tunables,
		std::uint64_t						  frame
	) {
		frameCounts = Counts{};
		frameBodies = bodies;
		frameEye = eyeMm;
		frameBright = bright;
		framePixelsPerMm = pixelsPerMm;
		frameTunables = tunables;
		frameWalkedMm = walkedMm;
		frameReachLimit = std::min(static_cast<double>(std::min(tunables.drawnShadowReachMm, bodies.reachMm)), brightReachMm);
		for (CornerEase& ease : eases) {
			ease.walkedMm += walkedMm;
		}
		const bool newBuild = !built || buildRevision != lastBuild;
		if (newBuild) {
			takeUpEases(bodies);
			lastBuild = buildRevision;
		}
		built = true;

		// The soft bodies: dropped when the build no longer holds them, due when new, moved past, changed,
		// crossing a pixel or the reach, or when he stops. A frame that changes none of what decides that
		// looks at no body.
		const bool retuned = !(tunables == softTunables);
		const bool stopped = walking && walkedMm == 0.0;
		const bool looked =
			newBuild || retuned || stopped || walkedMm > 0.0 || pixelsPerMm != lookedPixelsPerMm || frameReachLimit != lookedReachLimit;
		walking = walkedMm > 0.0;
		frameCounts.bandsRebuilt = buildBands(bodies, frameReachLimit, newBuild);
		if (!looked) {
			return;
		}
		lookedPixelsPerMm = pixelsPerMm;
		lookedReachLimit = frameReachLimit;
		if (newBuild) {
			bodyIndex.clear();
			for (std::size_t index = 0; index < bodies.bodies->size(); ++index) {
				if ((*bodies.bodies)[index].kind == vision::BodyKind::Soft) {
					bodyIndex.emplace(keyOf((*bodies.bodies)[index]), index);
				}
			}
		}
		for (auto it = soft.begin(); it != soft.end();) {
			if (!bodyIndex.contains(it->first)) {
				frameCounts.softChangedRings |= 1U << it->second.ring;
				it = soft.erase(it);
			} else {
				++it;
			}
		}
		const double fraction = static_cast<double>(tunables.softShadowRebuildPercent) / 100.0;
		softTunables = tunables;
		for (const auto& [key, index] : bodyIndex) {
			const vision::SightBody& body = (*bodies.bodies)[index];
			auto [entry, fresh] = soft.try_emplace(key);
			SoftDrawn& drawn = entry->second;
			bool	   due = fresh || retuned || drawn.builtArrival != body.arrival || (stopped && drawn.builtFrom != eyeMm);
			if (!due) {
				const bool shows = softBodyShows(body, eyeMm, frameReachLimit, pixelsPerMm, tunables);
				due = shows != drawn.shows ||
					  (drawn.drawn && glm::length(eyeMm - drawn.builtFrom) > fraction * std::max(drawn.shadow.distance, 1.0));
			}
			if (due && drawn.dueSince == 0) {
				drawn.dueSince = frame;
			}
		}
	}

	void DrawnShadows::appendDue(std::vector<Due>& out) const {
		for (const auto& [key, drawn] : soft) {
			if (drawn.dueSince != 0) {
				out.push_back(
					{.since = drawn.dueSince, .key = key, .pieces = std::max<std::size_t>(drawn.pieces.size(), kDueBodyLeastPieces)}
				);
			}
		}
	}

	std::size_t DrawnShadows::build(std::uint64_t key) {
		const auto found = soft.find(key);
		const auto index = bodyIndex.find(key);
		if (found == soft.end() || index == bodyIndex.end()) {
			return 0;
		}
		const vision::SightBody& body = (*frameBodies.bodies)[index->second];
		SoftDrawn&				 drawn = found->second;
		drawn.pieces.clear();
		drawn.drawn = false;
		frameCounts.softChangedRings |= 1U << drawn.ring;
		drawn.shows = softBodyShows(body, frameEye, frameReachLimit, framePixelsPerMm, frameTunables);
		if (drawn.shows) {
			const std::optional<SoftShadow> shadow = appendSoftShadow(body, frameEye, frameTunables, frameBright, drawn.pieces);
			if (shadow.has_value() && shadow->distance <= frameReachLimit) {
				drawn.shadow = *shadow;
				drawn.drawn = true;
				drawn.ring = std::min<std::size_t>(kSoftRings - 1, static_cast<std::size_t>(shadow->distance / kSoftRingWidthMm));
				frameCounts.softChangedRings |= 1U << drawn.ring;
			} else {
				drawn.pieces.clear();
			}
		}
		drawn.bodyId = body.id;
		drawn.diameterMm = body.diameterMm;
		drawn.builtFrom = frameEye;
		drawn.builtArrival = body.arrival;
		drawn.dueSince = 0;
		++frameCounts.softBuilt;
		return drawn.pieces.size();
	}

	void DrawnShadows::appendSoftPieces(std::size_t ring, std::vector<ValueTriangle>& out) const {
		for (const auto& [key, drawn] : soft) {
			if (drawn.ring != ring) {
				continue;
			}
			out.insert(out.end(), drawn.pieces.begin(), drawn.pieces.end());
		}
	}

	std::size_t DrawnShadows::softPieceCount() const {
		std::size_t count = 0;
		for (const auto& [key, drawn] : soft) {
			count += drawn.pieces.size();
		}
		return count;
	}

} // namespace worldview
