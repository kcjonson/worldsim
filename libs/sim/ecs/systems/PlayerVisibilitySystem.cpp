#include "PlayerVisibilitySystem.h"

#include "TimeSystem.h"
#include "VisionSystem.h"

#include "../World.h"
#include "../components/Colonist.h"
#include "../components/Memory.h"
#include "../components/Movement.h"
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
#include <cassert>
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

		constexpr double kSeenJumpMm = vision::kSeenJumpMeters * static_cast<double>(geometry::kMillimetersPerMeter);
		constexpr double kWholeIntervalSlack = 1e-9;

		// A leg's ranges: the build's at the slack radius (sight-softening.md 6.3).
		vision::SightRanges legRangesOf(const geometry::SightSideValues& buildRanges) {
			vision::SightRanges ranges;
			for (std::size_t side = 0; side < geometry::kSightSides; ++side) {
				ranges[side] = std::max(geometry::kForestShapeLeastRangeMm, buildRanges[side] - vision::kSeenSlackMm);
			}
			return ranges;
		}

		// Whether an opaque wall body was in reach of his last build: only then does the polygon path
		// sweep (sight-softening.md 6.6).
		bool opaqueWallInReach(const VisionSystem& vision, EntityID colonist) {
			const std::optional<VisionSystem::SightBodies> bodies = vision.sightBodies(colonist);
			return bodies.has_value() && std::ranges::any_of(*bodies->bodies, [](const vision::SightBody& body) {
					   return body.source == vision::SightBody::Source::Wall && body.kind == vision::BodyKind::Opaque;
				   });
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

	std::int64_t PlayerVisibilitySystem::legToleranceMm(std::int64_t toleranceMm, double intervalWalkMm, double timeScale) {
		if (intervalWalkMm <= 0.0 || timeScale <= 0.0) {
			return toleranceMm;
		}
		// The walk a build needs is more than the step; a hair of slack keeps a walk that divides the
		// step exactly from rounding up to one interval more.
		const auto spacing = [](double walkMm) {
			return walkMm * std::ceil(static_cast<double>(VisionSystem::kSightRebuildStepMm) / walkMm - kWholeIntervalSlack);
		};
		return std::llround(static_cast<double>(toleranceMm) * spacing(intervalWalkMm * timeScale) / spacing(intervalWalkMm));
	}

	void PlayerVisibilitySystem::beginLeg(Leg& leg, const Vec2i64& at, const vision::SightRanges& ranges) {
		leg		   = Leg{};
		leg.open   = true;
		leg.start  = at;
		leg.end	   = at;
		leg.least  = ranges;
		leg.most   = ranges;
		leg.ranges = ranges;
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

	void PlayerVisibilitySystem::followLeg(
		EntityID colonist, Observer& observer, const Vec2i64& at, const vision::SightRanges& ranges, bool newBuild, std::int64_t toleranceMm
	) {
		Leg& leg = observer.leg;
		// The shape drawn this frame, which the next leg holds first after a turn, the spread, or the
		// ring's vertices (sight-softening.md 6.2).
		const std::optional<vision::SeenArea::InheritedShape> drawn =
			observer.ease.valid ? std::optional(vision::SeenArea::InheritedShape{at, rangesOfOffsets(drawnOffsets(observer.ease))}) : std::nullopt;
		if (!leg.open || lengthOf(at - leg.end) > kSeenJumpMm) {
			closeLeg(colonist, leg);
			beginLeg(leg, at, ranges);
			joinLeg(colonist, observer, at, std::nullopt);
			return;
		}
		bool spread = false;
		for (std::size_t side = 0; newBuild && side < geometry::kSightSides; ++side) {
			spread = spread || std::max(leg.most[side], ranges[side]) - std::min(leg.least[side], ranges[side]) > toleranceMm;
		}
		if (spread) {
			closeLeg(colonist, leg);
			beginLeg(leg, at, ranges);
			joinLeg(colonist, observer, at, drawn);
			return;
		}
		if (!holds(leg, at)) {
			const Vec2i64 turnedAt = leg.end;
			closeLeg(colonist, leg);
			beginLeg(leg, turnedAt, ranges);
			hold(leg, at);
			joinLeg(colonist, observer, at, drawn);
			return;
		}
		hold(leg, at);
		for (std::size_t side = 0; side < geometry::kSightSides; ++side) {
			leg.least[side] = std::min(leg.least[side], ranges[side]);
			leg.most[side]	= std::max(leg.most[side], ranges[side]);
		}
		leg.ranges = ranges;
		if (!seen.setOpenLeg(colonist, leg.start, at, leg.ranges, std::nullopt)) {
			closeLeg(colonist, leg);
			beginLeg(leg, at, ranges);
			joinLeg(colonist, observer, at, drawn);
		}
	}

	// A leg just begun holds at most two shapes and the inherited one, far under kSoftLegMostVertices.
	void PlayerVisibilitySystem::joinLeg(EntityID colonist, Observer& observer, const Vec2i64& at, const std::optional<vision::SeenArea::InheritedShape>& inherited) {
		++legsOpened;
		[[maybe_unused]] const bool joined = seen.setOpenLeg(colonist, observer.leg.start, at, observer.leg.ranges, inherited);
		assert(joined);
	}

	void PlayerVisibilitySystem::closeLeg(EntityID colonist, Leg& leg) {
		if (!leg.open) {
			return;
		}
		seen.closeLeg(colonist);
		leg.open = false;
	}

	// The sweep runs along the walk, so it stays within the slack of where he went: a turn is a corner
	// of the walk. A polygon built before a jump says nothing about what the walls hide from here, and
	// no sweep crosses the jump.
	void PlayerVisibilitySystem::followStretch(Observer& observer, const Vec2i64& at) {
		Leg& stretch = observer.stretch;
		if (lengthOf(at - stretch.end) > kSeenJumpMm) {
			dropWallSight(observer);
			return;
		}
		if (!holds(stretch, at)) {
			const Vec2i64 turnedAt = stretch.end;
			observer.walk.push_back(turnedAt);
			beginLeg(stretch, turnedAt, stretch.ranges);
		}
		hold(stretch, at);
	}

	void PlayerVisibilitySystem::dropWallSight(Observer& observer) {
		observer.wallSight = false;
		observer.walk.clear();
	}

	void PlayerVisibilitySystem::enterPolygonAt(Observer& observer, const Vec2i64& at) {
		if (!observer.wallSight || observer.walk.empty() || observer.walk.front() == at) {
			return;
		}
		const vision::SightRanges rim = observer.walkStartRanges;
		seen.queueSightAt(at, observer.sightMm, rim, observer.sweepWanted ? std::move(observer.walk) : std::vector<Vec2i64>{}, rim);
		observer.walk.assign(1, at);
		beginLeg(observer.stretch, at, rim);
	}

	// Vision's polygon cut to its rim, the forest shape of his sight round its build, goes into the
	// record, with his walk from the build before it for the sweep when an opaque wall is in reach:
	// past a placed body's shadow alone the seen edge steps with vision's builds (sight-softening.md 6.6).
	void PlayerVisibilitySystem::registerPolygon(EntityID colonist, Observer& observer, const VisionSystem& vision, const VisionSystem::SightShape& shape) {
		const Vec2i64			  build	  = nav::toMm(shape.center);
		const std::int64_t		  sightMm = std::llround(static_cast<double>(shape.radius) * 1000.0);
		const vision::SightRanges rim	  = *shape.ranges;
		std::vector<Vec2i64>	  inSight = geometry::starClippedToConvex(*shape.polygon, build, vision::rimOf(build, rim));
		observer.sweepWanted			  = opaqueWallInReach(vision, colonist);
		if (geometry::simpleAround(inSight, build)) {
			seen.queueSightPolygon(
				std::move(inSight), build, sightMm, rim, observer.sweepWanted ? std::move(observer.walk) : std::vector<Vec2i64>{}, observer.walkStartRanges
			);
		}
		observer.walk.assign(1, build);
		observer.walkStartRanges = rim;
		beginLeg(observer.stretch, build, rim);
		observer.sightMm		 = sightMm;
		observer.wallSight		 = true;
		observer.polygonRevision = shape.polygonRevision;
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
	// one is. Its centre is the mean of its corners, round which a band or junction winds once.
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

	// The ease's state is integers: a frame's walk rounded up, so `walkedMm` is never less than his
	// straight distance from the start, and a start rounded down and 1 mm less, which pays for the
	// start position's rounding to the millimetre (sight-softening.md 6.3).
	void PlayerVisibilitySystem::followEase(
		Ease& ease, const VisionSystem::SightShape& shape, std::int64_t sightMm, std::int64_t rimEaseMm, std::int64_t walkedMm, const Vec2i64& at
	) {
		ease.walkedMm += walkedMm;
		if (ease.valid && ease.buildRevision == shape.buildRevision) {
			return;
		}
		const std::int64_t	inMm = sightMm - (vision::drawnVisibleRadiusMm(sightMm) - rimEaseMm);
		vision::SightRanges drawnRanges{};
		for (std::size_t side = 0; side < geometry::kSightSides; ++side) {
			drawnRanges[side] = std::max(geometry::kForestShapeLeastRangeMm, (*shape.ranges)[side] - inMm);
		}
		const geometry::Ring forest = geometry::forestShape({0, 0}, drawnRanges);
		SideOffsets			 newest{};
		for (std::size_t side = 0; side < geometry::kSightSides; ++side) {
			std::int64_t support = 0;
			for (std::size_t vertex = 0; vertex < forest.size(); ++vertex) {
				const std::int64_t along = geometry::kSightSideNormals[side][0] * forest[vertex].x + geometry::kSightSideNormals[side][1] * forest[vertex].y;
				support					 = vertex == 0 ? along : std::max(support, along);
			}
			newest[side] = support / geometry::kSightSideNormalScale - (support % geometry::kSightSideNormalScale < 0 ? 1 : 0);
		}
		if (ease.valid) {
			ease.start = drawnOffsets(ease);
			for (std::int64_t& offset : ease.start) {
				offset -= 1;
			}
			ease.walkedMm = 0;
		} else {
			ease.start	  = newest;
			ease.walkedMm = rimEaseMm;
		}
		ease.newest		   = newest;
		ease.startPosition = at;
		ease.rimEaseMm	   = rimEaseMm;
		ease.buildRevision = shape.buildRevision;
		ease.valid		   = true;
	}

	PlayerVisibilitySystem::SideOffsets PlayerVisibilitySystem::drawnOffsets(const Ease& ease) {
		const double blend = std::min(1.0, static_cast<double>(ease.walkedMm) / static_cast<double>(ease.rimEaseMm));
		SideOffsets	 drawn{};
		for (std::size_t side = 0; side < geometry::kSightSides; ++side) {
			drawn[side] = static_cast<std::int64_t>(
				std::floor((1.0 - blend) * static_cast<double>(ease.start[side]) + blend * static_cast<double>(ease.newest[side]))
			);
		}
		return drawn;
	}

	// Side k of a forest shape lies at range x kSightCosHalfSide / 2^8 along a normal of length
	// kSightSideNormalScale (2^22), so at range x cos(pi / kSightSides) millimetres: a range of
	// offset x 2^30 / kSightCosHalfSide, rounded down, puts it at or inside the offset.
	vision::SightRanges PlayerVisibilitySystem::rangesOfOffsets(const SideOffsets& offsets) {
		vision::SightRanges ranges{};
		for (std::size_t side = 0; side < geometry::kSightSides; ++side) {
			ranges[side] = std::clamp(
				offsets[side] * geometry::kSightDirectionScale / geometry::kSightCosHalfSide, geometry::kForestShapeLeastRangeMm, geometry::kForestRadiusLimitMm - 1
			);
		}
		return ranges;
	}

	void PlayerVisibilitySystem::update(float deltaTime) {
		const VisionSystem* vision = world->tryGetSystem<VisionSystem>();
		if (vision != nullptr && sweepVision != vision) {
			sweepVision = vision;
			seen.setSightBuilder([vision](const Vec2i64& centre, std::int64_t sightRadiusMm, const vision::SightRanges& rimRanges) {
				const vision::SightRanges own = vision->forestRangesAt(centre, sightRadiusMm);
				vision::SeenArea::BuiltSight built;
				for (std::size_t side = 0; side < geometry::kSightSides; ++side) {
					built.rimRanges[side] = std::max(geometry::kForestShapeLeastRangeMm, std::min(own[side], rimRanges[side]));
				}
				const std::int64_t most = *std::max_element(built.rimRanges.begin(), built.rimRanges.end());
				// The visibility polygon is cut to its own sight polygon already, which is the rim when
				// every range is equal.
				built.shape = vision->hardPolygon(centre, most);
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
			const std::int64_t tolerance = vision->sightTunables().softLegToleranceMm;
			const std::int64_t rimEaseMm = vision->sightTunables().rimEaseMm;
			for (auto [entity, position, colonist, memory] : world->view<Position, Colonist, Memory>()) {
				Observer& observer = observers[entity];
				observer.present   = true;
				const Vec2i64 at   = nav::toMm(position.value);
				const auto	  walkedMm = observer.placed ? static_cast<std::int64_t>(std::ceil(lengthOf(at - observer.lastPosition))) : 0;
				observer.placed		   = true;
				observer.lastPosition  = at;
				const std::optional<VisionSystem::SightShape> shape = vision->sightShape(entity);
				if (!shape.has_value()) {
					continue;
				}
				const std::int64_t sightMm = std::llround(static_cast<double>(shape->radius) * 1000.0);
				if (sightMm - vision::kSeenSlackMm < vision::kSeenLeastRadiusMm) {
					closeLeg(entity, observer.leg);
					dropWallSight(observer);
					continue;
				}
				const bool newBuild = !observer.ease.valid || observer.ease.buildRevision != shape->buildRevision;
				if (shape->forest != nullptr) {
					followEase(observer.ease, *shape, sightMm, rimEaseMm, walkedMm, at);
				}
				const vision::SightRanges ranges = legRangesOf(*shape->ranges);
				if (shape->polygon == nullptr) {
					dropWallSight(observer);
					const auto*	 movement	  = world->getComponent<MovementTarget>(entity);
					const double speedMmPerS  = movement != nullptr && movement->active ? static_cast<double>(movement->speed) * 1000.0 : 0.0;
					const double intervalWalk = speedMmPerS * static_cast<double>(deltaTime) * static_cast<double>(vision->updateInterval());
					followLeg(entity, observer, at, ranges, newBuild, legToleranceMm(tolerance, intervalWalk, timeScale));
					continue;
				}
				// The polygon path ends his leg, run on to here with its own ranges first.
				if (observer.leg.open) {
					if (holds(observer.leg, at) && seen.setOpenLeg(entity, observer.leg.start, at, observer.leg.ranges, std::nullopt)) {
						hold(observer.leg, at);
					}
					closeLeg(entity, observer.leg);
				}
				const bool newPolygon = shape->polygonRevision != observer.polygonRevision;
				if (observer.wallSight) {
					const bool resting = at == observer.stretch.end;
					followStretch(observer, at);
					if (resting && !newPolygon) {
						enterPolygonAt(observer, at);
					}
				}
				if (newPolygon) {
					registerPolygon(entity, observer, *vision, *shape);
					enterStructures(*shape);
				}
			}
		}
		for (auto it = observers.begin(); it != observers.end();) {
			if (it->second.present) {
				it->second.present = false;
				++it;
			} else {
				enterPolygonAt(it->second, it->second.stretch.end);
				closeLeg(it->first, it->second.leg);
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
	std::optional<std::vector<Vec2i64>> PlayerVisibilitySystem::drawnVisible(EntityID colonist) const {
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
		} else if (observer->second.ease.valid) {
			const geometry::Ring& forest = drawnForestFor(colonist, drawnOffsets(observer->second.ease));
			eased.resize(forest.size());
			std::ranges::transform(forest, eased.begin(), [&](const Vec2i64& vertex) { return vertex + standing; });
		} else {
			return std::nullopt;
		}
		std::vector<Vec2i64> cut;
		if (shape->polygon == nullptr) {
			cut = std::move(eased);
		} else {
			if (!observer->second.wallSight || observer->second.polygonRevision != shape->polygonRevision) {
				return std::nullopt;
			}
			const Vec2i64 build = nav::toMm(shape->center);
			cut					= geometry::starClippedToConvex(*shape->polygon, build, geometry::sightPolygon(standing, drawnMm));
			if (!cut.empty()) {
				cut = geometry::starClippedToConvex(cut, build, eased);
			}
		}
		if (cut.empty()) {
			return std::nullopt;
		}
		return cut;
	}

	// The eased forest shape round the origin, built when the drawn offsets change: every frame of an
	// ease, then not again until the next build. It is built in offsets from its centre, so moving it by
	// where he stands gives what building it there would.
	const geometry::Ring& PlayerVisibilitySystem::drawnForestFor(EntityID colonist, const SideOffsets& offsets) const {
		DrawnForest& drawn = drawnForests[colonist];
		if (drawn.forest.empty() || drawn.offsets != offsets) {
			drawn.offsets = offsets;
			drawn.forest  = geometry::forestShape({0, 0}, rangesOfOffsets(offsets));
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
