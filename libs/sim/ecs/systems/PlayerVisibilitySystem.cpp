#include "PlayerVisibilitySystem.h"

#include "VisionSystem.h"

#include "../World.h"
#include "../components/Colonist.h"
#include "../components/Memory.h"
#include "../components/Transform.h"

#include "nav/NavCoords.h"

#include <predicates/Predicates.h>
#include <visibility/SightPolygon.h>
#include <visibility/Visibility.h>

#include <algorithm>
#include <cmath>

namespace ecs {

	using geometry::Vec2i64;

	namespace {

		double lengthOf(const Vec2i64& offset) {
			const double x = static_cast<double>(offset.x);
			const double y = static_cast<double>(offset.y);
			return std::sqrt(x * x + y * y);
		}

		double crossOf(const std::array<double, 2>& first, double secondX, double secondY) { return first[0] * secondY - first[1] * secondX; }

		constexpr double kSeenJumpMm =vision::kSeenJumpMeters * static_cast<double>(geometry::kMillimetersPerMeter);

	} // namespace

	void PlayerVisibilitySystem::revealLanding(glm::vec2 drop, float radiusMeters) {
		const Vec2i64 centre = nav::toMm(drop);
		seen.commitNow(geometry::sightPolygon(centre, std::llround(static_cast<double>(radiusMeters) * 1000.0)), centre);
	}

	void PlayerVisibilitySystem::beginLeg(Leg& leg, const Vec2i64& at, std::int64_t radiusMm) {
		leg			   = Leg{};
		leg.open	   = true;
		leg.start	   = at;
		leg.end		   = at;
		leg.radiusMm   = radiusMm;
	}

	// The leg may end at `at` when every position held since it began stays within kSeenSlackMm of
	// the line from its start to `at` (its direction is in the wedge) and none lay more than
	// kSeenSlackMm farther from the start than `at` does.
	bool PlayerVisibilitySystem::holds(const Leg& leg, const Vec2i64& at) {
		const Vec2i64 offset = at - leg.start;
		const double  length = lengthOf(offset);
		if (leg.farthestMm > length + static_cast<double>(vision::kSeenSlackMm)) {
			return false;
		}
		if (!leg.hasWedge || length == 0.0) {
			return true;
		}
		const double offsetX = static_cast<double>(offset.x);
		const double offsetY = static_cast<double>(offset.y);
		return crossOf(leg.wedgeLow, offsetX, offsetY) >= 0.0 && crossOf(leg.wedgeHigh, offsetX, offsetY) <= 0.0;
	}

	// Holds `at`: it is the leg's new end, and, once it lies farther from the start than the slack,
	// the wedge narrows to the directions whose line passes within the slack of it.
	void PlayerVisibilitySystem::hold(Leg& leg, const Vec2i64& at) {
		leg.end				 = at;
		const Vec2i64 offset = at - leg.start;
		const double  length = lengthOf(offset);
		leg.farthestMm		 = std::max(leg.farthestMm, length);
		const double slack	 = static_cast<double>(vision::kSeenSlackMm);
		if (length <= slack) {
			return;
		}
		const double alongX = static_cast<double>(offset.x) / length;
		const double alongY = static_cast<double>(offset.y) / length;
		const double sine	= slack / length;
		const double cosine = std::sqrt(length * length - slack * slack) / length;
		const std::array<double, 2> low{alongX * cosine + alongY * sine, alongY * cosine - alongX * sine};
		const std::array<double, 2> high{alongX * cosine - alongY * sine, alongY * cosine + alongX * sine};
		if (!leg.hasWedge) {
			leg.hasWedge  = true;
			leg.wedgeLow  = low;
			leg.wedgeHigh = high;
			return;
		}
		if (crossOf(leg.wedgeLow, low[0], low[1]) > 0.0) {
			leg.wedgeLow = low;
		}
		if (crossOf(leg.wedgeHigh, high[0], high[1]) < 0.0) {
			leg.wedgeHigh = high;
		}
	}

	void PlayerVisibilitySystem::followLeg(EntityID colonist, Leg& leg, const Vec2i64& at, std::int64_t radiusMm) {
		if (!leg.open || leg.radiusMm != radiusMm) {
			closeLeg(colonist, leg);
			beginLeg(leg, at, radiusMm);
		} else if (lengthOf(at - leg.end) > kSeenJumpMm) {
			closeLeg(colonist, leg);
			beginLeg(leg, at, radiusMm);
		} else if (!holds(leg, at)) {
			const Vec2i64 turnedAt = leg.end;
			closeLeg(colonist, leg);
			beginLeg(leg, turnedAt, radiusMm);
			hold(leg, at);
		} else {
			hold(leg, at);
		}
		seen.setOpenLeg(colonist, leg.start, leg.end, leg.radiusMm);
	}

	void PlayerVisibilitySystem::closeLeg(EntityID colonist, Leg& leg) {
		if (!leg.open) {
			return;
		}
		seen.setOpenLeg(colonist, leg.start, leg.end, leg.radiusMm);
		seen.closeLeg(colonist);
		leg.open = false;
	}

	// Each visibility polygon is queued whole, with the observer as its centre, unless it is not a
	// simple ring round him (the next one, 0.5 m on, picks the ground up).
	void PlayerVisibilitySystem::registerPolygon(const std::vector<Vec2i64>& polygon, const Vec2i64& observer) {
		if (!geometry::simpleAround(polygon, observer)) {
			return;
		}
		seen.queue(polygon, observer);
	}

	void PlayerVisibilitySystem::update(float /*deltaTime*/) {
		const VisionSystem* vision = world->tryGetSystem<VisionSystem>();
		if (vision != nullptr) {
			for (auto [entity, position, colonist, memory] : world->view<Position, Colonist, Memory>()) {
				Observer& observer = observers[entity];
				observer.present   = true;
				const std::optional<VisionSystem::SightShape> shape = vision->sightShape(entity);
				if (!shape.has_value()) {
					continue;
				}
				if (shape->polygon != nullptr) {
					closeLeg(entity, observer.leg);
					if (shape->polygonRevision != observer.polygonRevision) {
						observer.polygonRevision = shape->polygonRevision;
						registerPolygon(*shape->polygon, nav::toMm(shape->center));
					}
					continue;
				}
				const std::int64_t radiusMm = std::llround(static_cast<double>(shape->radius) * 1000.0) - vision::kSeenSlackMm;
				if (radiusMm < vision::kSeenLeastRadiusMm) {
					closeLeg(entity, observer.leg);
					continue;
				}
				followLeg(entity, observer.leg, nav::toMm(position.value), radiusMm);
			}
		}
		for (auto it = observers.begin(); it != observers.end();) {
			if (it->second.present) {
				it->second.present = false;
				++it;
			} else {
				closeLeg(it->first, it->second.leg);
				it = observers.erase(it);
			}
		}
		seen.update();
	}

	bool PlayerVisibilitySystem::isSeen(glm::vec2 meters) const { return seen.isSeen(nav::toMm(meters)); }

	bool PlayerVisibilitySystem::isVisible(glm::vec2 meters) const {
		const VisionSystem* vision = world->tryGetSystem<VisionSystem>();
		if (vision == nullptr) {
			return false;
		}
		for (auto [entity, colonist, memory] : world->view<Colonist, Memory>()) {
			const std::optional<VisionSystem::SightShape> shape = vision->sightShape(entity);
			if (!shape.has_value()) {
				continue;
			}
			if (shape->polygon != nullptr) {
				if (geometry::pointInPolygon(nav::toMm(meters), *shape->polygon) != geometry::PointInPolygon::Outside) {
					return true;
				}
				continue;
			}
			const glm::vec2 offset = meters - shape->center;
			if (offset.x * offset.x + offset.y * offset.y <= shape->radius * shape->radius) {
				return true;
			}
		}
		return false;
	}

	bool PlayerVisibilitySystem::covers(const std::vector<glm::vec2>& outlineMeters) const {
		std::vector<Vec2i64> outline;
		outline.reserve(outlineMeters.size());
		for (const glm::vec2 point : outlineMeters) {
			outline.push_back(nav::toMm(point));
		}
		return seen.covers(outline);
	}

	std::optional<glm::vec2> PlayerVisibilitySystem::lastSeenToward(glm::vec2 from, glm::vec2 to) const {
		const std::optional<Vec2i64> point = seen.lastSeenToward(nav::toMm(from), nav::toMm(to));
		if (!point.has_value()) {
			return std::nullopt;
		}
		return nav::toMeters(*point);
	}

} // namespace ecs
