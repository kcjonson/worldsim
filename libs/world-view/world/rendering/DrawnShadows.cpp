#include "DrawnShadows.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <tuple>

namespace worldview {

	namespace {

		// A band and a fin reach this far past the bright shape's farthest point from the apex.
		constexpr double kBandPastBrightMm = 1000.0;
		// What a ring never built is taken to cost, in pieces a body, against the budget a frame.
		constexpr std::size_t kDueBodyLeastPieces = 8;
		// How far a running ease's apex may lie off a face near a new build's corner to be taken up.
		constexpr double kEaseFaceSlackMm = 2.0;

		glm::dvec2 toDouble(const geometry::Vec2i64& point) {
			return {static_cast<double>(point.x), static_cast<double>(point.y)};
		}

		double distanceToSegment(glm::dvec2 point, glm::dvec2 from, glm::dvec2 to) {
			const glm::dvec2 along = to - from;
			const double	 length = glm::dot(along, along);
			const double	 share = length > 0.0 ? std::clamp(glm::dot(point - from, along) / length, 0.0, 1.0) : 0.0;
			return glm::length(point - (from + share * along));
		}

		bool onFace(glm::dvec2 apex, const vision::Face& face) {
			return distanceToSegment(apex, toDouble(face.from), toDouble(face.to)) <= kEaseFaceSlackMm;
		}

		// Whether `first` turns left into `second` at their shared corner: with the solid on the left of
		// the outline, a convex corner, so a straight line from one face to the other runs through the body.
		bool convexBetween(const vision::Face& first, const vision::Face& second) {
			const glm::dvec2 into = toDouble(first.to) - toDouble(first.from);
			const glm::dvec2 onward = toDouble(second.to) - toDouble(second.from);
			return into.x * onward.y - into.y * onward.x > 0.0;
		}

		// Whether `apex` lies on one of the two faces (vision::Face, a whole straight side of the outline)
		// that meet at `corner`, or on the face beyond one of those across a convex corner, as a wall's end
		// cap is: the ease runs along a face or through the body, never across a notch's open ground,
		// whose inner corner is reflex.
		bool onFaceNear(const vision::SightBody& body, std::uint32_t corner, glm::dvec2 apex) {
			const geometry::Vec2i64 point = body.edges[corner].a;
			for (const vision::Face& meeting : body.faces) {
				if (meeting.from != point && meeting.to != point) {
					continue;
				}
				if (onFace(apex, meeting)) {
					return true;
				}
				const bool ending = meeting.to == point;
				for (const vision::Face& beyond : body.faces) {
					const bool joined = ending ? beyond.to == meeting.from : beyond.from == meeting.to;
					if (joined && (ending ? convexBetween(beyond, meeting) : convexBetween(meeting, beyond)) && onFace(apex, beyond)) {
						return true;
					}
				}
			}
			return false;
		}

	} // namespace

	void DrawnShadows::takeUpEases(const ecs::VisionSystem::SightBodies& bodies) {
		const std::vector<CornerEase>& running = eases;
		std::vector<CornerEase>		   next;
		next.reserve(bodies.shadowEdges->size());
		// Every new edge and running ease that may pair: the same outline and lit side, the apex or the
		// corner it eases to on a face within two of the new corner, nearest first. A wall body's id names
		// it only within one build of the walls' geometry, so the outline is what says the body is the same,
		// and the faces say the ease is this edge's and not a far corner's.
		std::vector<std::tuple<double, std::size_t, std::size_t>> pairs;
		for (std::size_t index = 0; index < bodies.shadowEdges->size(); ++index) {
			const vision::ShadowEdge& edge = (*bodies.shadowEdges)[index];
			const vision::SightBody&  body = (*bodies.bodies)[edge.body];
			const glm::dvec2		  corner = toDouble(body.edges[edge.corner].a);
			next.push_back(
				{.source = body.source, .outline = outlineOf(body), .litTurn = edge.litTurn, .buildCorner = body.edges[edge.corner].a}
			);
			for (std::size_t old = 0; old < running.size(); ++old) {
				const CornerEase& ease = running[old];
				if (!ease.started || ease.source != body.source || ease.outline != next.back().outline || ease.litTurn != edge.litTurn ||
					!(onFaceNear(body, edge.corner, ease.apex) || onFaceNear(body, edge.corner, ease.target))) {
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

	std::uint64_t DrawnShadows::outlineOf(const vision::SightBody& body) {
		std::uint64_t hash = 1469598103934665603ULL;
		const auto	  mix = [&hash](std::uint64_t value) {
			   hash ^= value;
			   hash *= 1099511628211ULL;
		};
		for (const geometry::OccluderSegment& edge : body.edges) {
			mix(static_cast<std::uint64_t>(edge.a.x));
			mix(static_cast<std::uint64_t>(edge.a.y));
		}
		return hash;
	}

	double DrawnShadows::distanceOf(const vision::SightBody& body, glm::dvec2 eyeMm) {
		return std::max(0.0, glm::length(toDouble(body.centre) - eyeMm) - static_cast<double>(body.diameterMm) / 2.0);
	}

	double DrawnShadows::nearestOf(std::size_t ring) {
		return std::max(kSoftRingStartMm[ring] * (1.0 - kSoftRingOverlap), kSoftRingLeastDistanceMm);
	}

	std::uint32_t DrawnShadows::ringsAt(double distanceMm) {
		std::uint32_t rings = 0;
		for (std::size_t ring = 0; ring < kSoftRings; ++ring) {
			const double low = kSoftRingStartMm[ring] * (1.0 - kSoftRingOverlap);
			const double high =
				ring + 1 < kSoftRings ? kSoftRingStartMm[ring + 1] * (1.0 + kSoftRingOverlap) : std::numeric_limits<double>::infinity();
			if (distanceMm >= low && distanceMm < high) {
				rings |= 1U << ring;
			}
		}
		return rings;
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

		// Whether a soft body `distanceMm` off can draw anything: within `reachLimit`, and its triangle, at
		// most its diameter wide and diameter x distance / (ray - diameter) long, a pixel or more on screen.
		bool softBodyShows(
			const vision::SightBody&	 body,
			double						 distanceMm,
			double						 reachLimit,
			double						 pixelsPerMm,
			const vision::SightTunables& tunables
		) {
			if (distanceMm > reachLimit) {
				return false;
			}
			const double diameter = static_cast<double>(body.diameterMm);
			const double ray = static_cast<double>(tunables.softShadowRayWidthMm);
			const double length = diameter < ray ? (distanceMm + diameter) * diameter / (ray - diameter) : 0.0;
			return (diameter + length) * pixelsPerMm >= 1.0;
		}

	} // namespace

	std::uint32_t DrawnShadows::ringsChangedBy(const ecs::VisionSystem::SightBodies& bodies) {
		buildOutlines.clear();
		std::uint32_t changed = 0;
		// A body new to his build, or reshaped under the same key, that already lies inside his bright
		// shape less the feather: one walking brings into reach is past the rim and waits for its ring.
		const double showsWithin = frameReachLimit - static_cast<double>(frameTunables.sightFeatherMm);
		for (const vision::SightBody& body : *bodies.bodies) {
			if (body.kind != vision::BodyKind::Soft) {
				continue;
			}
			const std::uint64_t key = keyOf(body);
			const std::uint64_t outline = outlineOf(body);
			buildOutlines.emplace(key, outline);
			const auto known = knownOutlines.find(key);
			if (known == knownOutlines.end() || known->second != outline) {
				const double distance = distanceOf(body, frameEye);
				if (distance <= showsWithin) {
					changed |= ringsAt(distance);
				}
			}
		}
		// A member dropped or reshaped; its arrival changed while he stands.
		std::unordered_map<std::uint64_t, std::int64_t> arrivals;
		for (const vision::SightBody& body : *bodies.bodies) {
			if (body.kind == vision::BodyKind::Soft) {
				arrivals.emplace(keyOf(body), body.arrival);
			}
		}
		for (std::size_t ring = 0; ring < kSoftRings; ++ring) {
			for (const Member& member : softRings[ring].members) {
				const auto now = buildOutlines.find(member.key);
				if (now == buildOutlines.end() || now->second != member.outline ||
					(stillSeconds > 0.0 && arrivals.at(member.key) != member.arrival)) {
					changed |= 1U << ring;
					break;
				}
			}
		}
		std::swap(knownOutlines, buildOutlines);
		return changed;
	}

	void DrawnShadows::update(
		const ecs::VisionSystem::SightBodies& bodies,
		std::uint64_t						  buildRevision,
		glm::dvec2							  eyeMm,
		double								  walkedMm,
		double								  seconds,
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
		// He has stopped once he has stayed within kStillMm of where the window began for kStillSeconds,
		// once each stop; moving past it starts the window again.
		if (glm::length(eyeMm - stillAt) > kStillMm) {
			stillAt = eyeMm;
			stillSeconds = 0.0;
			stopBuilt = false;
		} else {
			stillSeconds += seconds;
		}
		const bool stopped = !stopBuilt && stillSeconds >= kStillSeconds;
		stopBuilt = stopBuilt || stopped;
		const bool	  newBuild = !built || buildRevision != lastBuild;
		std::uint32_t due = 0;
		if (newBuild) {
			takeUpEases(bodies);
			lastBuild = buildRevision;
			due |= ringsChangedBy(bodies);
		}
		built = true;
		frameCounts.bandsRebuilt = buildBands(bodies, frameReachLimit, newBuild);

		const bool retuned = !(tunables == softTunables);
		if (retuned || pixelsPerMm != lookedPixelsPerMm) {
			due = (1U << kSoftRings) - 1U;
		} else if (frameReachLimit != lookedReachLimit) {
			// His reach follows his bright rim, which moves on most frames he walks in forest: only the rings
			// holding a body it takes in or leaves out fall due.
			const double nearer = std::min(frameReachLimit, lookedReachLimit);
			const double farther = std::max(frameReachLimit, lookedReachLimit);
			for (const vision::SightBody& body : *bodies.bodies) {
				if (body.kind == vision::BodyKind::Soft) {
					const double distance = distanceOf(body, eyeMm);
					if (distance > nearer && distance <= farther) {
						due |= ringsAt(distance);
					}
				}
			}
		}
		softTunables = tunables;
		lookedPixelsPerMm = pixelsPerMm;
		lookedReachLimit = frameReachLimit;
		const double fraction = static_cast<double>(tunables.softShadowRebuildPercent) / 100.0;
		for (std::size_t index = 0; index < kSoftRings; ++index) {
			Ring&		 ring = softRings[index];
			const double cadence = fraction * nearestOf(index);
			const bool	 isDue = (due & (1U << index)) != 0U || !ring.built || (stopped && ring.builtFrom != eyeMm) ||
							   (walkedMm > 0.0 && glm::length(eyeMm - ring.builtFrom) > cadence);
			if (isDue && ring.dueSince == 0) {
				ring.dueSince = frame;
			}
		}
	}

	void DrawnShadows::appendDue(std::vector<Due>& out) const {
		for (std::size_t index = 0; index < kSoftRings; ++index) {
			const Ring& ring = softRings[index];
			if (ring.dueSince == 0) {
				continue;
			}
			// A ring never built is taken at its bodies now, kDueBodyLeastPieces a body.
			std::size_t bodies = ring.members.size();
			if (!ring.built) {
				bodies = static_cast<std::size_t>(std::ranges::count_if(*frameBodies.bodies, [&](const vision::SightBody& body) {
					return body.kind == vision::BodyKind::Soft && (ringsAt(distanceOf(body, frameEye)) & (1U << index)) != 0U;
				}));
			}
			const double turn = ring.built ? glm::length(frameEye - ring.builtFrom) / nearestOf(index) : 1.0;
			out.push_back(
				{.since = ring.dueSince, .turn = turn, .ring = index, .pieces = std::max(ring.pieces.size(), bodies * kDueBodyLeastPieces)}
			);
		}
	}

	std::size_t DrawnShadows::build(std::size_t index) {
		Ring& ring = softRings[index];
		ring.pieces.clear();
		ring.drawn.clear();
		ring.members.clear();
		std::swap(ring.memberKeys, heldKeys);
		ring.memberKeys.clear();
		for (const vision::SightBody& body : *frameBodies.bodies) {
			if (body.kind != vision::BodyKind::Soft) {
				continue;
			}
			// One distance decides the ring, the reach, and the pixel: a body that shows is built, and one
			// that does not is a member all the same, so its ring's cadence brings it in when it does.
			// A body that has left this ring's band stays in it until a ring whose band now holds it has
			// been built with it, so a starved queue never leaves a body in no ring.
			const double		distance = distanceOf(body, frameEye);
			const std::uint32_t owners = ringsAt(distance);
			const std::uint64_t key = keyOf(body);
			if ((owners & (1U << index)) == 0U) {
				bool held = heldKeys.contains(key);
				for (std::size_t other = 0; held && other < kSoftRings; ++other) {
					held = (owners & (1U << other)) == 0U || !softRings[other].memberKeys.contains(key);
				}
				if (!held) {
					continue;
				}
			}
			ring.memberKeys.insert(key);
			ring.members.push_back({.key = keyOf(body), .outline = outlineOf(body), .arrival = body.arrival});
			if (!softBodyShows(body, distance, frameReachLimit, framePixelsPerMm, frameTunables)) {
				continue;
			}
			const std::size_t				before = ring.pieces.size();
			const std::optional<SoftShadow> shadow = appendSoftShadow(body, frameEye, frameTunables, frameBright, ring.pieces);
			if (shadow.has_value()) {
				const bool home =
					distance >= kSoftRingStartMm[index] && (index + 1 == kSoftRings || distance < kSoftRingStartMm[index + 1]);
				ring.drawn.push_back(
					{.bodyId = body.id,
					 .diameterMm = body.diameterMm,
					 .shadow = *shadow,
					 .pieces = ring.pieces.size() - before,
					 .home = home}
				);
			}
			++frameCounts.bodiesBuilt;
		}
		ring.builtFrom = frameEye;
		ring.built = true;
		ring.dueSince = 0;
		frameCounts.softChangedRings |= 1U << index;
		++frameCounts.ringsBuilt;
		return ring.pieces.size();
	}

	std::size_t DrawnShadows::softPieceCount() const {
		std::size_t count = 0;
		for (const Ring& ring : softRings) {
			count += ring.pieces.size();
		}
		return count;
	}

	std::size_t DrawnShadows::softBodyCount() const {
		std::size_t count = 0;
		for (const Ring& ring : softRings) {
			count += static_cast<std::size_t>(std::ranges::count_if(ring.drawn, [](const SoftDrawn& drawn) { return drawn.home; }));
		}
		return count;
	}

} // namespace worldview
