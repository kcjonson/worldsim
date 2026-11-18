#include "VisionSystem.h"

#include "NavigationSystem.h"
#include "TimeSystem.h"

#include "../World.h"
#include "../components/AgentRadius.h"
#include "../components/Appearance.h"
#include "../components/Knowledge.h"
#include "../components/Memory.h"
#include "../components/Movement.h"
#include "../components/Transform.h"

#include "assets/AssetDefinition.h"
#include "assets/AssetRegistry.h"
#include "assets/RecipeRegistry.h"
#include "assets/placement/PlacementExecutor.h"
#include "world/chunk/Chunk.h"
#include "world/chunk/ChunkManager.h"

#include "nav/NavCoords.h"

#include <predicates/Predicates.h>
#include <visibility/SightClip.h>
#include <visibility/SightPolygon.h>

#include <utils/Log.h>

#include <algorithm>
#include <cmath>

namespace ecs {

	namespace {
		// Synthetic definition name for shore points, the land beside water where
		// colonists stand to drink
		constexpr const char* kShoreDefName = "Terrain_Shore";

		// The longest sight forest depth is taken for: its reach, sight plus the longest lead, stays
		// under the bound its integers hold to (sight-softening.md 4.4). A colonist's is 30 m; a longer
		// sight takes no forest depth and its polygon path no forest clip.
		constexpr std::int64_t kForestSightMostMm = geometry::kForestRadiusLimitMm - vision::kSightLeadMostMm - 1;

		// Each side's range from `radiusMm` less its forest depth, never under the least the forest
		// shape takes (a floor that low comes only from a sight of a few metres at the lowest floor).
		geometry::SightSideValues rangesFrom(std::int64_t radiusMm, const geometry::SightSideValues& depthMm) {
			geometry::SightSideValues rangeMm{};
			for (std::size_t side = 0; side < geometry::kSightSides; ++side) {
				rangeMm[side] = std::max(geometry::kForestShapeLeastRangeMm, radiusMm - depthMm[side]);
			}
			return rangeMm;
		}

		bool forestTakenFor(std::int64_t radiusMm) { return radiusMm > 0 && radiusMm <= kForestSightMostMm; }

		// The slope-limited forest depth round `eyeMm` of a sight of `radiusMm` from `bodies`.
		geometry::SightSideValues forestDepthOf(
			const geometry::Vec2i64& eyeMm, std::int64_t radiusMm, std::span<const geometry::ForestBody> bodies, const geometry::ForestTunables& forest
		) {
			const geometry::ForestTunables sightForest = geometry::clampedToRanges(forest, radiusMm);
			return geometry::forestLimitedDepth(geometry::forestRawDepth(eyeMm, radiusMm, bodies, sightForest), radiusMm, sightForest);
		}

		/// The recipe learning `newlyLearnedId` unlocks: one that is not innate, has it as an input, and whose
		/// inputs the colonist now all knows. None when there is none.
		const assets::RecipeDef* recipeUnlockedBy(
			const Knowledge& knowledge,
			uint32_t newlyLearnedId,
			const assets::AssetRegistry& registry,
			const assets::RecipeRegistry& recipeRegistry
		) {
			for (const auto& [defName, recipe] : recipeRegistry.allRecipes()) {
				if (recipe.innate) {
					continue;
				}
				bool hasNewInput = false;
				std::vector<uint32_t> inputIds;
				for (const auto& input : recipe.inputs) {
					uint32_t inputId = registry.getDefNameId(input.defName);
					inputIds.push_back(inputId);
					if (inputId == newlyLearnedId) {
						hasNewInput = true;
					}
				}
				if (hasNewInput && knowledge.knowsAll(inputIds)) {
					return &recipe;
				}
			}
			return nullptr;
		}
	} // namespace

	void VisionSystem::ensureTerrainDefinitionsRegistered() {
		if (m_terrainDefsRegistered) {
			return;
		}

		auto& registry = assets::AssetRegistry::Get();

		// Colonists drink and wash AT the shore
		m_shoreCapabilityMask = static_cast<uint16_t>(
			(1U << static_cast<size_t>(assets::CapabilityType::Drinkable)) | (1U << static_cast<size_t>(assets::CapabilityType::Washable))
		);

		// Register synthetic shore definition
		m_shoreDefNameId = registry.registerSyntheticDefinition(kShoreDefName, m_shoreCapabilityMask);

		m_terrainDefsRegistered = true;
	}

	void VisionSystem::setSightTunables(const vision::SightTunables& next) {
		const vision::SightTunables clamped = vision::clampedToRanges(next);
		// Only a tunable the sim's shape reads rebuilds shapes; the cover's are read where drawn.
		if (!vision::sameShapeTunables(clamped, tunables)) {
			++tunablesVersion;
		}
		tunables = clamped;
	}

	const vision::PlacedOccluderIndex* VisionSystem::placedIndex() const {
		const NavigationSystem* nav = world->tryGetSystem<NavigationSystem>();
		return nav != nullptr ? &nav->placedOccluders() : nullptr;
	}

	bool VisionSystem::opaqueOccluders(
		const geometry::Vec2i64&						  eye,
		std::int64_t									  reachMm,
		const std::vector<const vision::PlacedOccluder*>& placed,
		std::vector<geometry::OccluderSegment>&			  out
	) const {
		const bool backFacesHidden = m_geometry.queryFacingOccluders(eye, reachMm, out);
		// A placed ring goes in on both faces: the eye is never inside one.
		for (const vision::PlacedOccluder* ring : placed) {
			if (ring->kind != vision::BodyKind::Opaque) {
				continue;
			}
			for (std::size_t index = 0; index < ring->outline.size(); ++index) {
				out.push_back({ring->outline[index], ring->outline[(index + 1) % ring->outline.size()]});
			}
		}
		return backFacesHidden;
	}

	geometry::Ring VisionSystem::hardPolygon(const geometry::Vec2i64& centre, std::int64_t reachMm) const {
		std::vector<const vision::PlacedOccluder*> placed;
		if (const vision::PlacedOccluderIndex* index = placedIndex()) {
			index->within(centre, reachMm, placed);
		}
		std::vector<geometry::OccluderSegment> occluders;
		opaqueOccluders(centre, reachMm, placed, occluders);
		return geometry::computeVisibilityPolygon(centre, reachMm, occluders);
	}

	geometry::SightSideValues VisionSystem::forestRangesAt(const geometry::Vec2i64& centre, std::int64_t sightMm) const {
		if (!forestTakenFor(sightMm)) {
			geometry::SightSideValues every{};
			every.fill(sightMm);
			return every;
		}
		const TimeSystem*			   time	   = world->tryGetSystem<TimeSystem>();
		const std::int64_t			   clockMs = time != nullptr ? std::llround(time->simulationSeconds() * 1000.0) : 0;
		const geometry::ForestTunables forest  = tunables.forest();
		std::vector<const vision::PlacedOccluder*> placed;
		if (const vision::PlacedOccluderIndex* index = placedIndex()) {
			index->within(centre, sightMm, placed);
		}
		std::vector<geometry::ForestBody> bodies;
		bodies.reserve(placed.size());
		for (const vision::PlacedOccluder* ring : placed) {
			bodies.push_back({
				.centre		 = ring->centre,
				.widthFactor = geometry::forestWidthFactor(ring->perimeterMm, forest),
				.arrival	 = time != nullptr ? geometry::forestArrivalFraction(clockMs, ring->arrivedMs, forest) : geometry::kFractionOne,
			});
		}
		return rangesFrom(sightMm, forestDepthOf(centre, sightMm, bodies, forest));
	}

	bool VisionSystem::SightShape::holds(glm::vec2 standing, glm::vec2 point) const {
		const glm::vec2 offset = point - standing;
		if (offset.x * offset.x + offset.y * offset.y > radius * radius) {
			return false;
		}
		if (polygon != nullptr && geometry::pointInPolygon(nav::toMm(point), *polygon) == geometry::PointInPolygon::Outside) {
			return false;
		}
		return forest == nullptr || geometry::insideForestHalfPlanes(nav::toMm(standing), *forest, nav::toMm(point));
	}

	VisionSystem::SightShape VisionSystem::shapeOf(const VisibilityCache& entry) {
		return SightShape{
			.center			 = entry.builtPos,
			.radius			 = entry.radius,
			.path			 = entry.path,
			.polygon		 = entry.hasPolygon ? &entry.polygon : nullptr,
			.polygonRevision = entry.polygonRevision,
			.buildRevision	 = entry.buildRevision,
			.ranges			 = &entry.ranges,
			.forest			 = entry.hasForest ? &entry.forest : nullptr,
			.segmentsSeen	 = &entry.segmentsSeen,
			.openingsSeen	 = &entry.openingsSeen,
		};
	}

	std::optional<VisionSystem::SightShape> VisionSystem::sightShape(EntityID observer) const {
		const auto found = m_visibilityCache.find(observer);
		if (found == m_visibilityCache.end()) {
			return std::nullopt;
		}
		return shapeOf(found->second);
	}

	std::optional<VisionSystem::SightBodies> VisionSystem::sightBodies(EntityID observer) const {
		const auto found = m_visibilityCache.find(observer);
		if (found == m_visibilityCache.end() || !found->second.built) {
			return std::nullopt;
		}
		const VisibilityCache& entry = found->second;
		return SightBodies{
			.bodies		 = &entry.bodies,
			.shadowEdges = &entry.shadowEdges,
			.leadMm		 = entry.leadMm,
			.reachMm	 = entry.reachMm,
			.eyeCentre	 = entry.builtMm,
		};
	}

	void VisionSystem::update(float deltaTime) {
		// Throttle: only run every N frames to reduce CPU overhead
		// Colonists walk 2 m/s, so a pass every 5 frames (12 a second at 60 fps, 24 at 120) is plenty
		m_frameCounter++;
		if (m_frameCounter < m_updateInterval) {
			return;
		}
		m_frameCounter = 0;
		m_sightingTime.reset();

		// The kind thresholds go to both indices first: the wall index rebuilds when its threshold
		// moved, the placed index re-decides its kinds.
		NavigationSystem* nav = world->tryGetSystem<NavigationSystem>();
		if (nav != nullptr) {
			nav->placedOccluders().setOpaqueLeastDiameterMm(tunables.placedOpaqueLeastDiameterMm);
		}
		m_geometry.setOpaqueLeastDiameterMm(tunables.wallOpaqueLeastDiameterMm);
		// Refresh the wall bodies before any observer reads them (version-gated, so this is a no-op
		// when the construction graph hasn't changed). With no construction world wired the index
		// stays empty.
		m_geometry.rebuildIfStale();
		const vision::PlacedOccluderIndex* placed = nav != nullptr ? &nav->placedOccluders() : nullptr;

		// Register terrain definitions on first update
		ensureTerrainDefinitionsRegistered();

		// The clock arrivals are read against, and how long until the next pass in sim time: the
		// measured frame interval times the interval's frames times the game's speed.
		// A ring's arrival advances only with the clock: with no clock every ring has arrived, and while
		// the game is paused an arriving ring keeps no build due, since the next build would be the same.
		const TimeSystem*		 time			 = world->tryGetSystem<TimeSystem>();
		const std::int64_t		 clockMs		 = time != nullptr ? std::llround(time->simulationSeconds() * 1000.0) : 0;
		const double			 timeScale		 = time != nullptr ? static_cast<double>(time->effectiveTimeScale()) : 1.0;
		const bool						clockRuns		= time != nullptr && timeScale > 0.0;
		const double					intervalSeconds = static_cast<double>(deltaTime) * static_cast<double>(m_updateInterval) * timeScale;
		const geometry::ForestTunables forest			= tunables.forest();
		const auto						arrivalOf		= [&](const vision::PlacedOccluder& ring) {
			   return time != nullptr ? geometry::forestArrivalFraction(clockMs, ring.arrivedMs, forest) : geometry::kFractionOne;
		};

		// Scratch reused across observers to avoid per-observer allocation.
		std::vector<geometry::OccluderSegment>	   occluderScratch;
		std::vector<const vision::PlacedOccluder*> placedScratch;
		std::vector<std::uint64_t>				   serialScratch;
		std::vector<std::size_t>				   wallScratch;
		std::vector<geometry::ForestBody>		   forestScratch;

		// Iterate all entities with Position and Memory components
		for (auto [entity, pos, memory] : world->view<Position, Memory>()) {
			// Get optional Knowledge component for permanent discovery tracking
			auto* knowledge = world->getComponent<Knowledge>(entity);

			// The meters->mm boundary is crossed here (and only here) via NavCoords. `lead` is how far
			// he can be from his build eye before the next pass: the rebuild step plus the farthest he
			// walks until then.
			const std::int64_t		radiusMm   = std::llround(static_cast<double>(memory.sightRadius) * 1000.0);
			const geometry::Vec2i64 observerMm = nav::toMm(pos.value);
			// A still colonist walks nothing. A walk is taken up to whole kLeadWalkStepMm, and a build
			// stores this pass's lead with one kLeadWalkStepMm more, so a frame time jittering across one
			// such boundary at a steady walk never outgrows it, while a real speed-up does. Nothing older
			// than the pass that builds goes into the lead it stores.
			const auto*		   movement	  = world->getComponent<MovementTarget>(entity);
			const double	   speed	  = movement != nullptr && movement->active ? static_cast<double>(movement->speed) : 0.0;
			const auto		   walkMm	  = static_cast<std::int64_t>(std::ceil(speed * intervalSeconds * 1000.0 / kLeadWalkStepMm)) * kLeadWalkStepMm;
			const std::int64_t passLeadMm = std::min(vision::kSightLeadMostMm, kSightRebuildStepMm + walkMm);
			const std::int64_t leadMm	  = std::min(vision::kSightLeadMostMm, passLeadMm + kLeadWalkStepMm);
			const std::int64_t reachMm	  = radiusMm + leadMm;
			VisibilityCache&   entry	  = m_visibilityCache[entity];
			entry.seenThisTick				= true;

			// Rebuild conditions (sight-softening.md 2.4), on the plain disc and the polygon path alike: no
			// build yet; he moved more than the rebuild step from the build; the walls, the shape's
			// tunables, or his sight changed; this pass's lead is longer than the build's, so its reach
			// falls short; the placed rings within the build's reach of its eye are not the ones it was
			// built with; or one recorded at the build was still arriving. On the plain disc, also the
			// first pass a body that puts him on the polygon path (a wall of any kind, an opaque placed
			// ring) lies within his reach of where he stands, so where he takes the polygon path
			// follows where he is, not where his last plain build fell. A stationary colonist hits none
			// of these and keeps his build, and so does one walking over ground with nothing on it.
			const geometry::Vec2i64 moved = observerMm - entry.builtMm;
			bool due = !entry.built || geometry::dot(moved, moved) > geometry::Int128::product(kSightRebuildStepMm, kSightRebuildStepMm) ||
					   entry.builtVersion != m_geometry.generation() || entry.tunablesVersion != tunablesVersion ||
					   entry.radius != memory.sightRadius || passLeadMm > entry.leadMm;
			if (!due && !entry.hasPolygon) {
				due = m_geometry.anyBodyWithin(observerMm, reachMm);
			}
			// A ring in sight of the build, so one that adds forest depth, recorded while still arriving
			// rebuilds each pass the clock runs, until the build that records it whole. One between sight
			// and reach weighs nothing and waits for a step.
			const geometry::Int128 sightSquared = geometry::Int128::product(radiusMm, radiusMm);
			due = due || (clockRuns && std::ranges::any_of(entry.bodies, [&](const vision::SightBody& body) {
							  const geometry::Vec2i64 offset = body.centre - entry.builtMm;
							  return body.arrival < geometry::kFractionOne && geometry::dot(offset, offset) < sightSquared;
						  }));
			if (!due && placed != nullptr) {
				// He is within the rebuild step of the build, so a step more holds every ring in his reach.
				const std::int64_t queryMm = entry.hasPolygon ? entry.reachMm : std::max(entry.reachMm, reachMm) + kSightRebuildStepMm;
				placed->within(entry.builtMm, queryMm, placedScratch);
				serialScratch.clear();
				for (const vision::PlacedOccluder* ring : placedScratch) {
					if (!entry.hasPolygon && ring->kind == vision::BodyKind::Opaque && ring->within(observerMm, reachMm)) {
						due = true;
					}
					if (!ring->within(entry.builtMm, entry.reachMm)) {
						continue;
					}
					serialScratch.push_back(ring->serial);
				}
				due = due || serialScratch != entry.placedSerials;
			}
			if (!due && !entry.hasPolygon) {
				// On the plain disc his sight is the circle round where he stands.
				entry.builtPos = pos.value;
			}

			if (due) {
				++builds;
				placedScratch.clear();
				if (placed != nullptr) {
					placed->within(observerMm, reachMm, placedScratch);
				}
				m_geometry.bodiesWithin(observerMm, reachMm, wallScratch);
				entry.bodies.clear();
				entry.placedSerials.clear();
				forestScratch.clear();
				// Any wall in reach takes the polygon path, a stub too, so what a colonist sees of it joins
				// memory; so does an opaque placed ring.
				bool polygonPath  = !wallScratch.empty();
				bool ringsInSight = false;
				for (const std::size_t index : wallScratch) {
					const GeometryIndex::WallBody& wall = m_geometry.bodies()[index];
					entry.bodies.push_back({
						.source		  = vision::SightBody::Source::Wall,
						.kind		  = wall.kind,
						.id			  = index,
						.edges		  = wall.edges,
						.previousEdge = wall.previousEdge,
						.diameterMm	  = wall.diameterMm,
						.centre		  = wall.centre,
					});
				}
				for (const vision::PlacedOccluder* ring : placedScratch) {
					vision::SightBody& body = entry.bodies.emplace_back(vision::SightBody{
						.source		= vision::SightBody::Source::Placed,
						.kind		= ring->kind,
						.id			= ring->serial,
						.diameterMm = ring->diameterMm,
						.centre		= ring->centre,
						.defName	= ring->defName,
						.arrival	= arrivalOf(*ring),
					});
					const auto corners = static_cast<std::uint32_t>(ring->outline.size());
					for (std::uint32_t corner = 0; corner < corners; ++corner) {
						body.edges.push_back({ring->outline[corner], ring->outline[(corner + 1) % corners]});
						body.previousEdge.push_back((corner + corners - 1) % corners);
					}
					entry.placedSerials.push_back(ring->serial);
					polygonPath = polygonPath || ring->kind == vision::BodyKind::Opaque;
					const geometry::Vec2i64 offset = ring->centre - observerMm;
					ringsInSight = ringsInSight || geometry::dot(offset, offset) < geometry::Int128::product(radiusMm, radiusMm);
					forestScratch.push_back({
						.centre		 = ring->centre,
						.widthFactor = geometry::forestWidthFactor(ring->perimeterMm, forest),
						.arrival	 = body.arrival,
					});
				}

				// Forest depth (sight-softening.md 4) from every placed ring in sight, opaque or soft.
				const bool						forestTaken = forestTakenFor(radiusMm);
				const geometry::SightSideValues depthMm		= forestTaken ? forestDepthOf(observerMm, radiusMm, forestScratch, forest)
																		  : geometry::SightSideValues{};
				entry.ranges	= rangesFrom(radiusMm, depthMm);
				entry.path		= polygonPath ? SightPath::Polygon : (ringsInSight ? SightPath::Leg : SightPath::Open);
				entry.hasForest = forestTaken;
				if (entry.hasForest) {
					entry.forest = geometry::forestHalfPlanes(entry.ranges);
				}

				if (!polygonPath) {
					if (entry.hasPolygon) {
						entry.polygon.clear();
						entry.segmentsSeen.clear();
						entry.openingsSeen.clear();
						entry.hasPolygon = false;
						++entry.polygonRevision;
					}
					entry.shadowEdges.clear();
				} else {
					// The hard polygon the opaque bodies leave, its shadow edges tagged before the forest
					// shape of his reach cuts it (3.1, 5.1).
					const bool backFacesHidden = opaqueOccluders(observerMm, reachMm, placedScratch, occluderScratch);
					geometry::Ring hard = geometry::computeVisibilityPolygon(observerMm, reachMm, occluderScratch);
					std::vector<vision::ShadowEdge> tagged = vision::tagShadowEdges(hard, observerMm, entry.bodies);
					if (forestTaken) {
						vision::ForestCut cut = vision::cutByForest(
							hard, observerMm, geometry::forestShape(observerMm, rangesFrom(reachMm, depthMm)), std::move(tagged), entry.bodies
						);
						if (!cut.cut && !clipFailureLogged) {
							clipFailureLogged = true;
							LOG_WARNING(
								Engine,
								"Vision: the forest clip could not cut the hard polygon at (%lld, %lld) mm; it serves uncut",
								static_cast<long long>(observerMm.x),
								static_cast<long long>(observerMm.y)
							);
						}
						entry.polygon	  = std::move(cut.polygon);
						entry.shadowEdges = std::move(cut.shadowEdges);
					} else {
						entry.polygon	  = std::move(hard);
						entry.shadowEdges = std::move(tagged);
					}
					entry.hasPolygon = true;
					++entry.polygonRevision;
					++m_polygonBuildCount;

					// Only when the polygon was (re)built this pass: a reused polygon sees the
					// same structures, already in memory, so re-scanning every pass is wasted.
					// New structures appear only via a geometry change, which bumps the
					// generation and forces a rebuild here.
					rememberStructures(
						observerMm, radiusMm, entry.polygon, backFacesHidden, memory, entry.segmentsSeen, entry.openingsSeen
					);
				}
				entry.builtMm		  = observerMm;
				entry.builtVersion	  = m_geometry.generation();
				entry.tunablesVersion = tunablesVersion;
				entry.radius		  = memory.sightRadius;
				entry.leadMm		  = leadMm;
				entry.reachMm		  = reachMm;
				entry.built			  = true;
				++entry.buildRevision;
				entry.builtPos		  = pos.value;
			}

			const SightShape shape = shapeOf(entry);
			lookOver(entity, pos.value, memory.sightRadius, &shape, memory, knowledge);
		}

		for (const Discovery& discovery : m_learnedScratch) {
			for (const DiscoveryObserver& listener : m_discoveryObservers) {
				listener(discovery);
			}
		}
		m_learnedScratch.clear();

		// Prune cache entries for observers not seen this tick (dead/despawned).
		// Mark-and-sweep keeps the map bounded.
		for (auto it = m_visibilityCache.begin(); it != m_visibilityCache.end();) {
			if (it->second.seenThisTick) {
				it->second.seenThisTick = false; // reset for next tick
				++it;
			} else {
				it = m_visibilityCache.erase(it);
			}
		}
	}

	void VisionSystem::rememberArea(EntityID observer, glm::vec2 center, float radiusMeters) {
		auto* memory = world->getComponent<Memory>(observer);
		if (memory == nullptr) {
			return;
		}
		ensureTerrainDefinitionsRegistered();
		m_sightingTime.reset();
		lookOver(observer, center, radiusMeters, nullptr, *memory, world->getComponent<Knowledge>(observer));
		m_learnedScratch.clear();
	}

	void VisionSystem::rememberStructures(
		geometry::Vec2i64					  centerMm,
		std::int64_t						  radiusMm,
		const geometry::Ring&				  builtPolygon,
		bool								  backFacesHidden,
		Memory&								  memory,
		std::vector<construction::SegmentId>& segments,
		std::vector<construction::OpeningId>& openings
	) {
		segments.clear();
		openings.clear();
		// The polygon may reach a rebuild step past sight; what is seen is the part within it.
		const geometry::Ring polygon = geometry::starClippedToConvex(builtPolygon, centerMm, geometry::sightPolygon(centerMm, radiusMm));
		// Pass 0: structure-as-observable (vision-architecture D4). A wall or
		// opening that bounds or intersects the visibility polygon is seen, and
		// its stable construction id enters memory. Runs only for indoor
		// observers (a polygon exists here), over built structures radius-culled
		// to dozens; the per-edge crossing test is O(structures * ring edges) but
		// both are small. Deterministic: set inserts are order-independent.
		//
		// A wall is seen when one of its outline edges (the occluders) is, an
		// opening when the stretch of either wall face across it is: a transparent
		// opening's gap mouth, which a steep eye crosses though every ray then ends
		// on the far jamb, or an opaque opening's face. A span [p0,p1] is seen if either
		// endpoint lies in/on the polygon, or the span crosses a ring edge. The
		// crossing fallback matters because a wall's near face IS the polygon
		// boundary: a long wall facing the observer can have both corners out of
		// sight radius while its middle is the boundary seen along, which an
		// endpoint-only test would miss.
		const auto pointSeen = [&](geometry::Vec2i64 point) {
			return geometry::pointInPolygon(point, polygon) != geometry::PointInPolygon::Outside;
		};
		const auto structureSeen = [&](geometry::Vec2i64 p0, geometry::Vec2i64 p1) -> bool {
			if (pointSeen(p0) || pointSeen(p1)) {
				return true;
			}
			for (std::size_t i = 0; i < polygon.size(); ++i) {
				const geometry::Vec2i64& e0 = polygon[i];
				const geometry::Vec2i64& e1 = polygon[(i + 1) % polygon.size()];
				if (geometry::intersectSegments(p0, p1, e0, e1).relation != geometry::SegmentRelation::Disjoint) {
					return true;
				}
			}
			return false;
		};

		// From outside the solid a back face is hidden but for its ends (GeometryIndex.h),
		// which can touch the polygon where another wall's face meets it.
		// A wall's edges are listed together, so one already seen is the last one added.
		for (const GeometryIndex::OccluderRecord& occluder : m_geometry.occluders()) {
			if ((!segments.empty() && segments.back() == occluder.source) ||
				!occluder.within(centerMm, radiusMm)) {
				continue;
			}
			const bool seen = backFacesHidden && GeometryIndex::facesAway(occluder.seg, centerMm)
								  ? pointSeen(occluder.seg.a) || pointSeen(occluder.seg.b)
								  : structureSeen(occluder.seg.a, occluder.seg.b);
			if (seen) {
				memory.rememberSegment(occluder.source);
				segments.push_back(occluder.source);
			}
		}

		for (const auto& op : m_geometry.builtOpenings()) {
			if (!op.within(centerMm, radiusMm)) {
				continue;
			}
			if (structureSeen(op.leftFace.start, op.leftFace.end) || structureSeen(op.rightFace.start, op.rightFace.end)) {
				memory.rememberOpening(op.openingId);
				openings.push_back(op.openingId);
			}
		}
	}

	void VisionSystem::learn(EntityID observer, Knowledge& knowledge, uint32_t defNameId) {
		if (!knowledge.learn(defNameId)) {
			return;
		}
		auto& registry = assets::AssetRegistry::Get();
		m_learnedScratch.push_back({.colonist = observer, .kind = DiscoveryKind::NewKind, .definition = defNameId});
		const assets::RecipeDef* recipe = recipeUnlockedBy(knowledge, defNameId, registry, assets::RecipeRegistry::Get());
		if (recipe == nullptr) {
			return;
		}
		m_learnedScratch.push_back({
			.colonist = observer,
			.kind = DiscoveryKind::Recipe,
			.definition = recipe->outputs.empty() ? 0U : registry.getDefNameId(recipe->outputs.front().defName),
			.recipeLabel = recipe->label,
		});
	}

	void VisionSystem::lookOver(
		EntityID			  observer,
		glm::vec2			  center,
		float				  radiusMeters,
		const SightShape*	  shape,
		Memory&				  memory,
		Knowledge*			  knowledge
	) {
		// Chunk size in world units (meters, since kTileSize = 1.0F)
		constexpr float kChunkWorldSize = static_cast<float>(world::kChunkSize);

		auto& registry = assets::AssetRegistry::Get();

		// Calculate bounding box of vision in world coordinates
		float minX = center.x - radiusMeters;
		float maxX = center.x + radiusMeters;
		float minY = center.y - radiusMeters;
		float maxY = center.y + radiusMeters;

		// Convert to chunk coordinate range
		int32_t chunkMinX = static_cast<int32_t>(std::floor(minX / kChunkWorldSize));
		int32_t chunkMaxX = static_cast<int32_t>(std::floor(maxX / kChunkWorldSize));
		int32_t chunkMinY = static_cast<int32_t>(std::floor(minY / kChunkWorldSize));
		int32_t chunkMaxY = static_cast<int32_t>(std::floor(maxY / kChunkWorldSize));

		float sightRadiusSq = radiusMeters * radiusMeters;

		// A candidate already inside the sight radius is visible iff his sight shape holds it from where he
		// stands (on its boundary counts as seen). The plain disc holds everything in radius.
		auto visible = [&](const glm::vec2& candidatePos) -> bool { return shape == nullptr || shape->holds(center, candidatePos); };

		// Reconciliation (vision-architecture D4): a remembered entity whose position
		// the observer can see RIGHT NOW, but which is neither in the placement index
		// nor among the ECS entities pass 2 sees this tick, is stale -- forget it so
		// tasks fail gracefully and belief replans trigger. This is the continuous, look-and-correct corrector for
		// targets the colonist is away from; ActionSystem keeps its at-target
		// forgets (the synchronous backstop that closes the 0-5 frame throttle
		// window where AIDecision could re-select a just-failed phantom).
		//
		// Gate cheaply, in order: radius (squared distance, no sqrt) -> visible()
		// (the polygon test, indoors only) -> the index query (the costly step).
		// Only what's both in range AND not occluded gets verified: you can't
		// notice a bush is gone through a wall. The index check distinguishes
		// destructive harvest/pickup (removeEntity drops it from the index ->
		// forget) from a regrowth cooldown (entity stays in the index, only a
		// separate cooldown map flips -> keep). Shore entries are synthetic
		// terrain, not placement entities; they never disappear, so skip them.
		//
		// Entries absent from the index are only candidates: piles, boxes, stations and
		// spawned flora live in the ECS alone, so pass 2 strikes off each one it sees,
		// and what is left after it is forgotten. An unchanged pile in view then costs
		// no memory write. Forgetting a SET of keys is order-independent (no LRU/
		// capability state read here depends on order), so this stays deterministic
		// regardless of unordered_map traversal order.
		//
		// Null placement data -> can't verify -> forget nothing (same guard as
		// pass 1). Cost: one pass over knownWorldEntities per observer per tick
		// (throttled to every m_updateInterval frames); a colonist's remembered
		// set is realistically dozens-to-hundreds, and most entries fail the cheap
		// radius/visible gate before ever touching the index.
		std::vector<StaleCandidate>& stale = m_staleScratch;
		stale.clear();
		if (m_placementExecutor != nullptr && m_processedChunks != nullptr) {
			for (const auto& [key, known] : memory.knownWorldEntities) {
				// Shore points are terrain, not placement entities -- never reconcile.
				if (known.defNameId == m_shoreDefNameId) {
					continue;
				}

				const float ddx = known.position.x - center.x;
				const float ddy = known.position.y - center.y;
				if (ddx * ddx + ddy * ddy > sightRadiusSq) {
					continue; // out of sight radius
				}
				if (!visible(known.position)) {
					continue; // occluded -- can't see whether it's gone
				}

				// Existence check: is a PlacedEntity of the same defName still in the
				// index near the remembered position? Query a tiny radius (the position
				// is the entity's own, quantized only by memory's 0.1m hash grid), then
				// match by defName.
				const std::string& defName = registry.getDefName(known.defNameId);
				if (defName.empty()) {
					continue; // unknown id -> can't verify -> keep
				}

				const world::ChunkCoordinate coord = world::worldToChunk({known.position.x, known.position.y});
				if (m_processedChunks->find(coord) == m_processedChunks->end()) {
					continue; // chunk not placement-processed -> can't verify -> keep
				}
				const auto* chunkIndex = m_placementExecutor->getChunkIndex(coord);
				if (chunkIndex == nullptr) {
					continue; // no index -> can't verify -> keep
				}

				constexpr float kPresenceEpsilon = 0.2F; // tiles; covers the 0.1m memory quantization
				// hasNearby is the allocation-free presence check (queryRadius would
				// build a vector just to test emptiness).
				if (!chunkIndex->hasNearby(known.position, kPresenceEpsilon, defName)) {
					stale.push_back({key, known.position, known.defNameId, false});
				}
			}
			std::ranges::sort(stale, {}, &StaleCandidate::key);
		}

		// Pass 1: placed entities from world generation (needs placement data wired).
		if (m_placementExecutor != nullptr && m_processedChunks != nullptr) {
			// Query each potentially visible chunk for placed entities
			for (int32_t cy = chunkMinY; cy <= chunkMaxY; ++cy) {
				for (int32_t cx = chunkMinX; cx <= chunkMaxX; ++cx) {
					world::ChunkCoordinate coord{cx, cy};

					// Only query processed chunks for placed entities
					if (m_processedChunks->find(coord) == m_processedChunks->end()) {
						continue;
					}

					const auto* chunkIndex = m_placementExecutor->getChunkIndex(coord);
					if (chunkIndex == nullptr) {
						continue;
					}

					// Query entities within sight radius from this chunk
					auto nearbyEntities = chunkIndex->queryRadius(center, radiusMeters);

					// Remember each discovered entity
					for (const auto* placedEntity : nearbyEntities) {
						// Occlusion gate: skip entities a wall hides.
						if (!visible(placedEntity->position)) {
							continue;
						}

						// Get defNameId and capability mask for registry notification
						uint32_t defNameId = registry.getDefNameId(placedEntity->defName);
						if (defNameId == 0) {
							continue;
						}
						uint16_t capabilityMask = registry.getCapabilityMask(defNameId);

						// Remember in colonist's memory (returns true only for NEW discoveries)
						bool isNewDiscovery = memory.rememberWorldEntity(placedEntity->position, defNameId, capabilityMask);

						if (isNewDiscovery && knowledge != nullptr) {
							learn(observer, *knowledge, defNameId);
						}
					}
				}
			}
		}

		// Pass 2: ECS entities with Appearance (e.g., bio piles created by ActionSystem).
		// These are runtime-spawned entities, as opposed to those placed during world generation, movers
		// (other colonists) among them.
		for (auto [otherEntity, otherPos, appearance] : world->view<Position, Appearance>()) {
			// Don't "see" yourself
			if (otherEntity == observer) {
				continue;
			}

			// Check if within sight radius
			float dx = otherPos.value.x - center.x;
			float dy = otherPos.value.y - center.y;
			if (dx * dx + dy * dy <= sightRadiusSq) {
				// Occlusion gate: skip entities a wall hides.
				if (!visible(otherPos.value)) {
					continue;
				}

				// Get defNameId and capability mask from registry
				uint32_t defNameId = registry.getDefNameId(appearance.defName);
				if (defNameId != 0 && world->hasComponent<AgentRadius>(otherEntity)) {
					// A mover (navigation's Position + AgentRadius): remembered where and when it was seen,
					// its kind the def it appears as. Never a world entity, which is keyed by where it stands.
					if (!m_sightingTime.has_value()) {
						m_sightingTime = simulationSeconds(*world);
					}
					memory.rememberDynamicEntity(otherEntity, defNameId, otherPos.value, *m_sightingTime);
					continue;
				}
				if (defNameId != 0) {
					if (!stale.empty()) {
						const uint64_t key = hashWorldEntity(otherPos.value, defNameId);
						const auto	   it = std::ranges::lower_bound(stale, key, {}, &StaleCandidate::key);
						if (it != stale.end() && it->key == key) {
							it->seen = true; // here in the ECS, so not gone
						}
					}

					uint16_t capabilityMask = registry.getCapabilityMask(defNameId);

					// Remember in colonist's memory (returns true only for NEW discoveries)
					bool isNewDiscovery = memory.rememberWorldEntity(otherPos.value, defNameId, capabilityMask);

					if (isNewDiscovery && knowledge != nullptr) {
						learn(observer, *knowledge, defNameId);
					}
				}
			}
		}

		for (const StaleCandidate& candidate : stale) {
			if (!candidate.seen) {
				memory.forgetWorldEntity(candidate.position, candidate.defNameId);
			}
		}

		// Pass 3: shore points, sampled along each chunk's water rings when the
		// chunk was generated (terrain polygons D11).
		if (m_chunkManager != nullptr && m_shoreDefNameId != 0) {
			for (int32_t cy = chunkMinY; cy <= chunkMaxY; ++cy) {
				for (int32_t cx = chunkMinX; cx <= chunkMaxX; ++cx) {
					world::ChunkCoordinate coord{cx, cy};

					const auto* chunk = m_chunkManager->getChunk(coord);
					if (chunk == nullptr || !chunk->isReady()) {
						continue;
					}

					for (const geometry::Vec2i64& shorePoint : chunk->terrainPolygons().shorePoints) {
						const Foundation::Vec2 meters = geometry::dequantize(shorePoint);
						const glm::vec2		   shoreWorldPos{meters.x, meters.y};

						// Check if within sight radius
						float dx = shoreWorldPos.x - center.x;
						float dy = shoreWorldPos.y - center.y;
						if (dx * dx + dy * dy <= sightRadiusSq) {
							// Occlusion gate: skip shore a wall hides.
							if (!visible(shoreWorldPos)) {
								continue;
							}

							memory.rememberWorldEntity(shoreWorldPos, m_shoreDefNameId, m_shoreCapabilityMask);

							if (knowledge != nullptr) {
								learn(observer, *knowledge, m_shoreDefNameId);
							}
						}
					}
				}
			}
		}
	}

} // namespace ecs
