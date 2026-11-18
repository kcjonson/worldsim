#include "PlayerVisibilitySystem.h"

#include "TimeSystem.h"
#include "VisionSystem.h"

#include "../World.h"
#include "../components/Colonist.h"
#include "../components/Memory.h"
#include "../components/Transform.h"

#include "nav/NavCoords.h"
#include "nav/NavInputBuilder.h"

#include <assets/ConstructionRegistry.h>
#include <predicates/Predicates.h>
#include <visibility/ForestShape.h>
#include <visibility/SightClip.h>
#include <visibility/SightPolygon.h>
#include <visibility/Visibility.h>
#include <world/chunk/ChunkCoordinate.h>

#include <algorithm>
#include <cmath>

namespace ecs {

	using geometry::Vec2i64;

	namespace {

		// Whether an opaque wall body was in reach of his last build: only then does the polygon path
		// sweep (sight-softening.md 6.6).
		bool opaqueWallInReach(const VisionSystem& vision, EntityID colonist) {
			const std::optional<VisionSystem::SightBodies> bodies = vision.sightBodies(colonist);
			return bodies.has_value() && std::ranges::any_of(*bodies->bodies, [](const vision::SightBody& body) {
					   return body.source == vision::SightBody::Source::Wall && body.kind == vision::BodyKind::Opaque;
				   });
		}

		// A side within this angle of the ray from the build eye through its middle runs along that ray: a
		// shadow side. The clip steps a shadow side's far end up to 8 mm off its ray (starClippedToConvex),
		// which turns a side metres long by a fraction of a degree; a rim side lies across its ray.
		constexpr double kAlongRayCosine = 0.995;
		// How far a body face's ends may lie from the outline edge it is on.
		constexpr std::int64_t kOnFaceMm = 2;

		bool alongRayFrom(const Vec2i64& eye, const Vec2i64& from, const Vec2i64& to) {
			const glm::dvec2 along{static_cast<double>(to.x - from.x), static_cast<double>(to.y - from.y)};
			const glm::dvec2 outward{
				(static_cast<double>(from.x) + static_cast<double>(to.x)) / 2.0 - static_cast<double>(eye.x),
				(static_cast<double>(from.y) + static_cast<double>(to.y)) / 2.0 - static_cast<double>(eye.y)
			};
			const double lengths = std::sqrt((along.x * along.x + along.y * along.y) * (outward.x * outward.x + outward.y * outward.y));
			return lengths > 0.0 && std::abs(along.x * outward.x + along.y * outward.y) >= kAlongRayCosine * lengths;
		}

		bool onOpaqueFace(const std::vector<vision::SightBody>& bodies, const Vec2i64& from, const Vec2i64& to) {
			for (const vision::SightBody& body : bodies) {
				if (body.kind != vision::BodyKind::Opaque) {
					continue;
				}
				for (const geometry::OccluderSegment& edge : body.edges) {
					if (geometry::withinDistanceOfSegment(from, edge.a, edge.b, kOnFaceMm) && geometry::withinDistanceOfSegment(to, edge.a, edge.b, kOnFaceMm)) {
						return true;
					}
				}
			}
			return false;
		}

		static_assert(
			vision::kSeenBoxCellMm == static_cast<std::int64_t>(world::kChunkWorldSize) * geometry::kMillimetersPerMeter,
			"a seen-box cell is a chunk"
		);

		// A query box in millimetres rounded outward, so it holds every millimetre of the box in meters.
		Vec2i64 millimetresBelow(glm::vec2 meters) {
			constexpr double kPerMeter = static_cast<double>(geometry::kMillimetersPerMeter);
			return {static_cast<std::int64_t>(std::floor(static_cast<double>(meters.x) * kPerMeter)), static_cast<std::int64_t>(std::floor(static_cast<double>(meters.y) * kPerMeter))};
		}

		Vec2i64 millimetresAbove(glm::vec2 meters) {
			constexpr double kPerMeter = static_cast<double>(geometry::kMillimetersPerMeter);
			return {static_cast<std::int64_t>(std::ceil(static_cast<double>(meters.x) * kPerMeter)), static_cast<std::int64_t>(std::ceil(static_cast<double>(meters.y) * kPerMeter))};
		}

	} // namespace

	void PlayerVisibilitySystem::revealLanding(glm::vec2 drop, float radiusMeters) {
		const Vec2i64 centre = nav::toMm(drop);
		seen.commitNow(geometry::sightPolygon(centre, std::llround(static_cast<double>(radiusMeters) * 1000.0)), centre);
	}

	void PlayerVisibilitySystem::refreshFootprints() {
		if (constructionWorld == nullptr || constructionWorld->version() == footprintVersion) {
			return;
		}
		footprintVersion = constructionWorld->version();
		footprints.clear();
		footprintsBySegment.clear();
		footprintsByOpening.clear();
		std::vector<geometry::nav::NavInputPolygon> rings;
		std::vector<geometry::nav::DoorPortal>		portals;
		std::vector<nav::WallJunction>				junctions;
		nav::extractWalls(*constructionWorld, assets::ConstructionRegistry::Get(), rings, portals, &junctions);
		// A junction joins the record once any wall meeting at it is seen, not only the one it is tagged with.
		std::unordered_map<std::size_t, const std::vector<construction::SegmentId>*> junctionWalls;
		for (const nav::WallJunction& junction : junctions) {
			junctionWalls[junction.polygon] = &junction.walls;
		}
		for (geometry::nav::NavInputPolygon& ring : rings) {
			const std::size_t index	   = footprints.size();
			const auto		  junction = junctionWalls.find(index);
			if (junction != junctionWalls.end()) {
				for (const construction::SegmentId wall : *junction->second) {
					footprintsBySegment[wall].push_back(index);
				}
			} else if (ring.provenanceId > 0) {
				footprintsBySegment[static_cast<construction::SegmentId>(ring.provenanceId)].push_back(index);
			}
			if (ring.openingId >= 0) {
				footprintsByOpening[static_cast<construction::OpeningId>(ring.openingId)].push_back(index);
			}
			footprints.push_back(std::move(ring.ring));
		}
	}

	// A footprint goes in once, as it is now: a wall rebuilt the same is not queued again, a changed
	// one is. Its centre is the mean of its corners, round which a band, a junction, or a placed body's
	// convex ring winds once.
	void PlayerVisibilitySystem::enterFootprint(const std::vector<Vec2i64>& ring) {
		if (ring.size() < 3 || !enteredFootprints.insert(ring).second) {
			return;
		}
		Vec2i64 sum{0, 0};
		for (const Vec2i64& corner : ring) {
			sum = sum + corner;
		}
		const auto	  count = static_cast<std::int64_t>(ring.size());
		const Vec2i64 centre{sum.x / count, sum.y / count};
		if (geometry::simpleAround(ring, centre)) {
			seen.queue(ring, centre);
		}
	}

	void PlayerVisibilitySystem::enterStructures(const VisionSystem::SightShape& shape) {
		if (shape.placedSeen != nullptr) {
			for (const geometry::Ring& outline : *shape.placedSeen) {
				enterFootprint(outline);
			}
		}
		if (shape.segmentsSeen == nullptr || (shape.segmentsSeen->empty() && shape.openingsSeen->empty())) {
			return;
		}
		refreshFootprints();
		for (const construction::SegmentId segment : *shape.segmentsSeen) {
			const auto found = footprintsBySegment.find(segment);
			if (found != footprintsBySegment.end()) {
				for (const std::size_t index : found->second) {
					enterFootprint(footprints[index]);
				}
			}
		}
		for (const construction::OpeningId opening : *shape.openingsSeen) {
			const auto found = footprintsByOpening.find(opening);
			if (found != footprintsByOpening.end()) {
				for (const std::size_t index : found->second) {
					enterFootprint(footprints[index]);
				}
			}
		}
	}

	void PlayerVisibilitySystem::update(float /*deltaTime*/) {
		const VisionSystem* vision = world->tryGetSystem<VisionSystem>();
		if (vision != nullptr && sweepVision != vision) {
			sweepVision = vision;
			seen.setSightBuilder([vision](const Vec2i64& centre, std::int64_t sightRadiusMm, const vision::SightRanges& rimRanges) {
				const vision::SightRanges	 own = vision->forestRangesAt(centre, sightRadiusMm);
				vision::SeenArea::BuiltSight built;
				for (std::size_t side = 0; side < geometry::kSightSides; ++side) {
					built.rimRanges[side] = std::max(geometry::kForestShapeLeastRangeMm, std::min(own[side], rimRanges[side]));
				}
				const std::int64_t most = *std::max_element(built.rimRanges.begin(), built.rimRanges.end());
				// The visibility polygon is cut to its own sight polygon already, which is the rim when
				// every range is equal.
				built.shape			= vision->hardPolygon(centre, most);
				const bool plainRim = std::ranges::all_of(built.rimRanges, [most](std::int64_t range) { return range == most; });
				if (!plainRim) {
					built.shape = geometry::starClippedToConvex(built.shape, centre, vision::rimOf(centre, built.rimRanges));
				}
				return built;
			});
		}
		if (vision != nullptr) {
			const TimeSystem*  time		 = world->tryGetSystem<TimeSystem>();
			const double	   timeScale = time != nullptr ? static_cast<double>(time->effectiveTimeScale()) : 1.0;
			const std::int64_t tolerance = vision::SightFeed::legToleranceMm(vision->sightTunables().softLegToleranceMm, timeScale);
			const std::int64_t rimEaseMm = vision->sightTunables().rimEaseMm;
			for (auto [entity, position, colonist, memory] : world->view<Position, Colonist, Memory>()) {
				Observer& observer = observers[entity];
				observer.present   = true;
				const std::optional<VisionSystem::SightShape> shape = vision->sightShape(entity);
				if (!shape.has_value()) {
					continue;
				}
				const std::int64_t sightMm = std::llround(static_cast<double>(shape->radius) * 1000.0);
				if (sightMm - vision::kSeenSlackMm < vision::kSeenLeastRadiusMm) {
					observer.feed.stop(seen, entity);
					continue;
				}
				const vision::SightBuild build{
					.buildRevision	   = shape->buildRevision,
					.polygonRevision   = shape->polygonRevision,
					.eye			   = nav::toMm(shape->center),
					.sightMm		   = sightMm,
					.ranges			   = shape->ranges,
					.forest			   = shape->forest != nullptr,
					.polygon		   = shape->polygon,
					.opaqueWallInReach = shape->polygon != nullptr && opaqueWallInReach(*vision, entity),
				};
				if (observer.feed.follow(seen, entity, nav::toMm(position.value), build, tolerance, rimEaseMm)) {
					enterStructures(*shape);
				}
			}
		}
		for (auto it = observers.begin(); it != observers.end();) {
			if (it->second.present) {
				it->second.present = false;
				++it;
			} else {
				it->second.feed.leave(seen, it->first);
				drawnForests.erase(it->first);
				it = observers.erase(it);
			}
		}
		seen.update();
	}

	const PlayerVisibilitySystem* PlayerVisibilitySystem::gate(World& world) {
		const PlayerVisibilitySystem* visibility = world.tryGetSystem<PlayerVisibilitySystem>();
		return visibility != nullptr && !visibility->revealed() ? visibility : nullptr;
	}

	const vision::SightFeed* PlayerVisibilitySystem::feedOf(EntityID colonist) const {
		const auto found = observers.find(colonist);
		return found != observers.end() ? &found->second.feed : nullptr;
	}

	bool PlayerVisibilitySystem::isSeen(glm::vec2 meters) const { return seen.isSeen(nav::toMm(meters)); }

	void PlayerVisibilitySystem::appendSeenBoxes(glm::vec2 low, glm::vec2 high, std::vector<SeenBox>& out) const {
		std::vector<vision::SeenArea::Box> boxes;
		seen.appendSeenBoxes(millimetresBelow(low), millimetresAbove(high), boxes);
		for (const vision::SeenArea::Box& box : boxes) {
			out.push_back({nav::toMeters(box.low), nav::toMeters(box.high)});
		}
	}

	bool PlayerVisibilitySystem::anySeenBox(glm::vec2 low, glm::vec2 high) const {
		return seen.anySeenBox(millimetresBelow(low), millimetresAbove(high));
	}

	bool PlayerVisibilitySystem::isVisible(glm::vec2 meters) const {
		const VisionSystem* vision = world->tryGetSystem<VisionSystem>();
		if (vision == nullptr) {
			return false;
		}
		for (auto [entity, position, colonist, memory] : world->view<Position, Colonist, Memory>()) {
			const std::optional<VisionSystem::SightShape> shape = vision->sightShape(entity);
			if (shape.has_value() && shape->holds(position.value, meters)) {
				return true;
			}
		}
		return false;
	}

	// Off the polygon path the eased shape lies in his leg's ring (sight-softening.md 6.2); on it, it
	// cuts vision's polygon, round the polygon's build.
	std::optional<PlayerVisibilitySystem::DrawnShape> PlayerVisibilitySystem::drawnVisible(EntityID colonist) const {
		const VisionSystem* vision	 = world->tryGetSystem<VisionSystem>();
		const Position*		position = world->getComponent<Position>(colonist);
		if (vision == nullptr || position == nullptr) {
			return std::nullopt;
		}
		const std::optional<VisionSystem::SightShape> shape = vision->sightShape(colonist);
		if (!shape.has_value()) {
			return std::nullopt;
		}
		const std::int64_t sightMm = std::llround(static_cast<double>(shape->radius) * 1000.0);
		if (sightMm - vision::kSeenSlackMm < vision::kSeenLeastRadiusMm) {
			return std::nullopt;
		}
		const auto observer = observers.find(colonist);
		if (observer == observers.end()) {
			return std::nullopt;
		}
		const Vec2i64	   standing = nav::toMm(position->value);
		const std::int64_t drawnMm	= vision::drawnVisibleRadiusMm(sightMm);
		// A sight too long for forest depth has no ranges to ease: its rim is the sight polygon drawn in
		// by the ease's inset.
		std::vector<Vec2i64> eased;
		if (shape->forest == nullptr) {
			eased = geometry::sightPolygon(standing, drawnMm - vision->sightTunables().rimEaseMm);
		} else if (const std::optional<vision::SightFeed::SideOffsets> offsets = observer->second.feed.drawnOffsets()) {
			const geometry::Ring& forest = drawnForestFor(colonist, *offsets);
			eased.resize(forest.size());
			std::ranges::transform(forest, eased.begin(), [&](const Vec2i64& vertex) { return vertex + standing; });
		} else {
			return std::nullopt;
		}
		if (shape->polygon == nullptr) {
			DrawnShape drawn{.outline = std::move(eased)};
			drawn.rim.assign(drawn.outline.size(), true);
			return drawn;
		}
		if (!observer->second.feed.holdsPolygon(shape->polygonRevision)) {
			return std::nullopt;
		}
		const Vec2i64		 build = nav::toMm(shape->center);
		const std::vector<Vec2i64> disc	 = geometry::sightPolygon(standing, drawnMm);
		std::vector<Vec2i64> cut   = geometry::starClippedToConvex(*shape->polygon, build, disc);
		if (!cut.empty()) {
			cut = geometry::starClippedToConvex(cut, build, eased);
		}
		if (cut.empty()) {
			return std::nullopt;
		}
		// Every side is rim but a shadow side, which runs out along a ray from the build eye, and a body
		// face, which lies on an opaque body's outline: whatever ring cut the rest (the disc, the eased
		// shape, or vision's own forest rim where the ease reaches past it), it bounds his sight.
		const std::optional<VisionSystem::SightBodies> bodies = vision->sightBodies(colonist);
		DrawnShape									   drawn{.outline = std::move(cut)};
		drawn.rim.resize(drawn.outline.size());
		for (std::size_t side = 0; side < drawn.outline.size(); ++side) {
			const Vec2i64& from = drawn.outline[side];
			const Vec2i64& to	= drawn.outline[(side + 1) % drawn.outline.size()];
			drawn.rim[side]		= !alongRayFrom(build, from, to) && !(bodies.has_value() && onOpaqueFace(*bodies->bodies, from, to));
		}
		return drawn;
	}

	// The eased forest shape round the origin, built when the drawn offsets change: every frame of an
	// ease, then not again until the next build. It is built in offsets from its centre, so moving it by
	// where he stands gives what building it there would.
	const geometry::Ring& PlayerVisibilitySystem::drawnForestFor(EntityID colonist, const vision::SightFeed::SideOffsets& offsets) const {
		DrawnForest& drawn = drawnForests[colonist];
		if (drawn.forest.empty() || drawn.offsets != offsets) {
			drawn.offsets = offsets;
			drawn.forest  = geometry::forestShape({0, 0}, vision::SightFeed::rangesOfOffsets(offsets));
			++drawnForestBuilds;
		}
		return drawn.forest;
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
