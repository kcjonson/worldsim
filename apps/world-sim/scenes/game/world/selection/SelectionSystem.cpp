#include "SelectionSystem.h"

#include <assets/AssetRegistry.h>
#include <assets/ConstructionRegistry.h>
#include <assets/placement/PlacementExecutor.h>
#include <boolean/RingBoolean.h>
#include <construction/ConstructionWorld.h>
#include <construction/OpeningGeometry.h>
#include <core/Vec2i64.h>
#include <ecs/components/Appearance.h>
#include <ecs/components/Colonist.h>
#include <ecs/components/FacingDirection.h>
#include <ecs/components/Inventory.h>
#include <ecs/components/Packaged.h>
#include <ecs/components/ResourceStack.h>
#include <ecs/components/Transform.h>
#include <ecs/components/WorkQueue.h>
#include <offset/WallOffset.h>
#include <predicates/Predicates.h>
#include <primitives/Primitives.h>
#include <utils/Log.h>
#include <world/chunk/ChunkCoordinate.h>
#include <world/rendering/PackagedLayout.h> // shared crate+item placement (no drift)
#include <world/rendering/PileLayout.h>	   // shared loose-pile placement (no drift)
#include <world/rendering/TemplateBounds.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace world_sim {

namespace {
	// A segment's half-thickness in mm from its material/thickness preset, or 0 if
	// the material has no wall config. The topology store is config-agnostic (it
	// holds the preset NAME), so the renderer and this hit-test both resolve the
	// number through ConstructionRegistry; same lookup as DrawingSystem's band render.
	std::int64_t segmentHalfThicknessMm(const construction::WallSegment& seg) {
		const auto* preset = assets::ConstructionRegistry::Get().getThicknessPreset(seg.material, seg.thicknessPreset);
		return (preset != nullptr) ? preset->halfThicknessMm : 0;
	}

	// Widest wall half-thickness any segment could have, from the config presets (a
	// handful), NOT by scanning committed segments (which grows with wall count). Used
	// only to size the single-radius segmentAt query; the hit is then confirmed against
	// the picked segment's own radius.
	std::int64_t maxWallHalfThicknessMm() {
		std::int64_t maxHalf = 0;
		for (const auto& [name, material] : assets::ConstructionRegistry::Get().getAllMaterials()) {
			for (const auto& preset : material.wallThicknesses) {
				maxHalf = std::max(maxHalf, preset.halfThicknessMm);
			}
		}
		return maxHalf;
	}

	// The defName the dynamic render path resolves for an entity: its base Appearance
	// defName plus the facing suffix when it carries a FacingDirection. Uses the shared
	// ecs::directionSuffix so the hit-test/outline pick the exact template the entity draws.
	std::string dynamicRenderDefName(ecs::World* world, ecs::EntityID entity, const std::string& baseDefName) {
		if (const auto* facing = world->getComponent<ecs::FacingDirection>(entity)) {
			return baseDefName + ecs::directionSuffix(facing->direction);
		}
		return baseDefName;
	}

	// True if `worldPos` (meters) lies inside any ring of `sil` under transform `t`. The
	// click is mapped into the template-local frame and quantized to mm to match the
	// silhouette rings (which are template-local integer millimeters).
	bool silhouetteRingsContain(
		const assets::AssetSilhouette& sil, const worldview::AssetInstanceTransform& t, glm::vec2 worldPos
	) {
		const glm::vec2			local = t.toLocal(worldPos);
		const geometry::Vec2i64 localMm = geometry::quantize(Foundation::Vec2{local.x, local.y});
		// Hit-test against the closed hitRegion (whole-clump: gaps between disjoint
		// blobs are clickable), not the crisp outline rings.
		for (const auto& ring : sil.hitRegion) {
			if (geometry::pointInPolygon(localMm, ring) == geometry::PointInPolygon::Inside) {
				return true;
			}
		}
		return false;
	}

	// True if the click is under a placed (static/baked) entity's rendered silhouette,
	// using the SAME position-as-origin transform BatchedEntityRenderer draws it with.
	// Falls back to a centroid radius test when the def has no cached silhouette so a
	// silhouette-less asset stays selectable. Reused for world entities and for each
	// packaged crate/item sub-entity (both are drawn as position-as-origin instances).
	bool placedUnderClick(const assets::PlacedEntity& pe, glm::vec2 worldPos, float fallbackRadius) {
		const auto* sil = assets::AssetRegistry::Get().getSilhouette(pe.defName);
		if (sil == nullptr || !sil->valid) {
			const float dx = pe.position.x - worldPos.x;
			const float dy = pe.position.y - worldPos.y;
			return (dx * dx + dy * dy) < (fallbackRadius * fallbackRadius);
		}
		return silhouetteRingsContain(*sil, worldview::staticInstanceTransform(pe.position, pe.rotation, pe.scale), worldPos);
	}

	// True if the click is under a dynamic ECS entity's rendered silhouette. Resolves the
	// render defName (Appearance defName + facing suffix) and the dynamic (bbox-centered)
	// transform exactly as DynamicEntityRenderSystem. Returns true (defer to the caller's
	// centroid pre-cull) when the def has no cached silhouette so silhouette-less types
	// stay selectable.
	bool dynamicUnderClick(
		ecs::World* world, ecs::EntityID entity, const std::string& baseDefName, float scale, glm::vec2 entityPos, glm::vec2 worldPos
	) {
		const std::string defName = dynamicRenderDefName(world, entity, baseDefName);
		const auto*		  sil = assets::AssetRegistry::Get().getSilhouette(defName);
		if (sil == nullptr || !sil->valid) {
			return true;
		}
		return silhouetteRingsContain(*sil, worldview::dynamicInstanceTransform(entityPos, scale, sil->boundsCenterMeters), worldPos);
	}

	// The sprites packaged furniture draws (back crate, item, front crate), laid out exactly as
	// DynamicEntityRenderSystem lays them (shared packagedLayout).
	worldview::PackagedLayout packagedLayoutFor(const ecs::Appearance& appearance, glm::vec2 entityPos) {
		auto& registry = assets::AssetRegistry::Get();
		return worldview::packagedLayout(
			entityPos,
			appearance.defName,
			appearance.scale,
			appearance.colorTint,
			worldview::templateBounds(registry.getTemplate(appearance.defName))
		);
	}

	// True if the click is under any sprite of a packaged item: the crate layers or the item.
	bool packagedUnderClick(const ecs::Appearance& appearance, glm::vec2 entityPos, glm::vec2 worldPos, float fallbackRadius) {
		const worldview::PackagedLayout pl = packagedLayoutFor(appearance, entityPos);
		return placedUnderClick(pl.crateBack, worldPos, fallbackRadius) || placedUnderClick(pl.item, worldPos, fallbackRadius)
			   || placedUnderClick(pl.crateFront, worldPos, fallbackRadius);
	}

	// The sprites a loose pile draws, laid out exactly as DynamicEntityRenderSystem lays them
	// (shared pileLayout).
	worldview::PileLayout pileLayoutFor(const ecs::Appearance& appearance, std::uint32_t quantity, glm::vec2 entityPos) {
		auto& registry = assets::AssetRegistry::Get();
		return worldview::pileLayout(
			entityPos,
			appearance.defName,
			appearance.scale,
			appearance.colorTint,
			worldview::templateBounds(registry.getTemplate(appearance.defName)),
			worldview::pileItem(registry.getDefinition(appearance.defName)),
			quantity
		);
	}

	// Stable identity for a candidate: (variant type index, structure/entity id). Lets
	// handleClick tell "the same stack is under the cursor" from "a different stack",
	// so a same-spot click only cycles when the set is unchanged. NoSelection never
	// appears in a candidate list, so its id (0) is inert.
	std::pair<int, std::uint64_t> candidateKey(const Selection& sel) {
		// Type discriminant taken from the outer variant so the visitor never copies
		// the alternative (string-bearing alternatives would otherwise allocate).
		const int type = static_cast<int>(sel.index());
		return std::visit(
			[type](const auto& s) -> std::pair<int, std::uint64_t> {
				using T = std::decay_t<decltype(s)>;
				if constexpr (std::is_same_v<T, ColonistSelection>) {
					return {type, static_cast<std::uint64_t>(s.entityId)};
				} else if constexpr (std::is_same_v<T, CraftingStationSelection>) {
					return {type, static_cast<std::uint64_t>(s.entityId)};
				} else if constexpr (std::is_same_v<T, FurnitureSelection>) {
					return {type, static_cast<std::uint64_t>(s.entityId)};
				} else if constexpr (std::is_same_v<T, ResourcePileSelection>) {
					return {type, static_cast<std::uint64_t>(s.entityId)};
				} else if constexpr (std::is_same_v<T, OpeningSelection>) {
					return {type, static_cast<std::uint64_t>(s.id)};
				} else if constexpr (std::is_same_v<T, WallSegmentSelection>) {
					return {type, static_cast<std::uint64_t>(s.id)};
				} else if constexpr (std::is_same_v<T, FoundationSelection>) {
					return {type, static_cast<std::uint64_t>(s.id)};
				} else if constexpr (std::is_same_v<T, WorldEntitySelection>) {
					// Positional, no integer id: fold the quantized position (low 32 bits
					// of each mm axis) with a hash of the defName so two different-defName
					// entities stacked at the same spot get distinct, frame-stable keys
					// (same entity -> same key, so click-cycling holds).
					const auto			mm = geometry::quantize(s.position);
					const std::uint64_t x = static_cast<std::uint64_t>(mm.x) & 0xFFFFFFFFULL;
					const std::uint64_t y = static_cast<std::uint64_t>(mm.y) & 0xFFFFFFFFULL;
					const std::uint64_t nameHash = std::hash<std::string>{}(s.defName);
					return {type, ((x << 32) | y) ^ nameHash};
				} else {
					// NoSelection never appears in a candidate list.
					return {type, 0};
				}
			},
			sel
		);
	}

	// Map each template-local silhouette ring through `t` into world meters and append
	// it as a closed loop, tracking the max world-Y over every appended vertex (the
	// silhouette's ground-contact, used as the outline's depth key). Degenerate rings
	// (<2 verts) are skipped so worldRings stays free of undrawable entries.
	void appendWorldRings(
		const std::vector<geometry::Ring>&			 rings,
		const worldview::AssetInstanceTransform& t,
		std::vector<std::vector<Foundation::Vec2>>&	 out,
		float&										 maxY
	) {
		for (const auto& ring : rings) {
			if (ring.size() < 2) {
				continue;
			}
			std::vector<Foundation::Vec2> worldRing;
			worldRing.reserve(ring.size());
			for (const geometry::Vec2i64& mm : ring) {
				const Foundation::Vec2 dq = geometry::dequantize(mm);
				const glm::vec2		   w = t.toWorld({dq.x, dq.y});
				worldRing.push_back(Foundation::Vec2{w.x, w.y});
				maxY = std::max(maxY, w.y);
			}
			out.push_back(std::move(worldRing));
		}
	}

	// The same mapping into world millimeters, for a composite outline's union.
	void appendWorldRingsMm(
		const std::vector<geometry::Ring>& rings, const worldview::AssetInstanceTransform& t, std::vector<geometry::Ring>& out
	) {
		for (const auto& ring : rings) {
			if (ring.size() < 3) {
				continue;
			}
			geometry::Ring worldRing;
			worldRing.reserve(ring.size());
			for (const geometry::Vec2i64& mm : ring) {
				const Foundation::Vec2 dq = geometry::dequantize(mm);
				const glm::vec2		   w = t.toWorld({dq.x, dq.y});
				worldRing.push_back(geometry::quantize(Foundation::Vec2{w.x, w.y}));
			}
			out.push_back(std::move(worldRing));
		}
	}
} // namespace

const std::vector<geometry::Ring>& SelectionSystem::unionCompositeRings(std::vector<geometry::Ring> worldRingsMm) const {
	if (worldRingsMm == compositeOutlineInput) {
		return compositeOutlineRings;
	}
	compositeOutlineInput = worldRingsMm;

	bool merged = true;
	while (merged) {
		merged = false;
		for (std::size_t i = 0; i < worldRingsMm.size() && !merged; ++i) {
			for (std::size_t j = i + 1; j < worldRingsMm.size(); ++j) {
				geometry::BooleanResult u = geometry::unionRings(worldRingsMm[i], worldRingsMm[j]);
				if (u.ok()) {
					worldRingsMm[i] = std::move(u.ring);
					worldRingsMm.erase(worldRingsMm.begin() + static_cast<std::ptrdiff_t>(j));
					merged = true;
					break;
				}
			}
		}
	}
	compositeOutlineRings = std::move(worldRingsMm);
	return compositeOutlineRings;
}

SelectionSystem::SelectionSystem(const Args& args)
	: ecsWorld(args.world)
	, camera(args.camera)
	, placementExecutor(args.placementExecutor)
	, constructionWorld(args.constructionWorld)
	, callbacks(args.callbacks) {}

std::vector<Selection> SelectionSystem::gatherCandidates(glm::vec2 worldPos) {
	std::vector<Selection> candidates;

	// Priority 1: Check ECS colonists first (dynamic, moving entities). Centroid pre-cull
	// to kSelectionRadius, then a precise silhouette hit; nearest containing colonist wins
	// (single-select). The pre-cull bound only shrinks on an accepted hit, so a closer
	// silhouette-miss never blocks a farther silhouette-hit.
	float		  closestColonistDist = kSelectionRadius;
	ecs::EntityID closestColonist = 0;

	for (auto [entity, pos, colonist] : ecsWorld->view<ecs::Position, ecs::Colonist>()) {
		float dx = pos.value.x - worldPos.x;
		float dy = pos.value.y - worldPos.y;
		float dist = std::sqrt(dx * dx + dy * dy);

		if (dist >= closestColonistDist) {
			continue;
		}
		// A colonist without an Appearance has no template to test; accept on the pre-cull.
		const auto* appearance = ecsWorld->getComponent<ecs::Appearance>(entity);
		const bool	under = (appearance == nullptr)
							 ? true
							 : dynamicUnderClick(ecsWorld, entity, appearance->defName, appearance->scale, pos.value, worldPos);
		if (!under) {
			continue;
		}
		closestColonistDist = dist;
		closestColonist = entity;
	}

	if (closestColonist != 0) {
		candidates.emplace_back(ColonistSelection{closestColonist});
	}

	// Priority 1.5: Check ECS stations (entities with WorkQueue). Same centroid pre-cull
	// then silhouette test as colonists.
	float		  closestStationDist = kSelectionRadius;
	ecs::EntityID closestStation = 0;

	for (auto [entity, pos, appearance, workQueue] : ecsWorld->view<ecs::Position, ecs::Appearance, ecs::WorkQueue>()) {
		float dx = pos.value.x - worldPos.x;
		float dy = pos.value.y - worldPos.y;
		float dist = std::sqrt(dx * dx + dy * dy);

		if (dist >= closestStationDist) {
			continue;
		}
		if (!dynamicUnderClick(ecsWorld, entity, appearance.defName, appearance.scale, pos.value, worldPos)) {
			continue;
		}
		closestStationDist = dist;
		closestStation = entity;
	}

	if (closestStation != 0) {
		auto* pos = ecsWorld->getComponent<ecs::Position>(closestStation);
		auto* appearance = ecsWorld->getComponent<ecs::Appearance>(closestStation);
		if (pos != nullptr && appearance != nullptr) {
			candidates.emplace_back(
				CraftingStationSelection{closestStation, appearance->defName, Foundation::Vec2{pos->value.x, pos->value.y}}
			);
		}
	}

	// Priority 1.6: Check ECS storage containers (entities with Inventory but no
	// WorkQueue). Packaged ones test the crate layers + item; placed ones test their own
	// silhouette. Centroid pre-cull first, same as the other dynamic types.
	float		  closestStorageDist = kSelectionRadius;
	ecs::EntityID closestStorage = 0;

	for (auto [entity, pos, appearance, inventory] : ecsWorld->view<ecs::Position, ecs::Appearance, ecs::Inventory>()) {
		// Skip entities that also have WorkQueue (those are crafting stations)
		if (ecsWorld->getComponent<ecs::WorkQueue>(entity) != nullptr) {
			continue;
		}
		// Skip colonists (they have Inventory for carrying items)
		if (ecsWorld->getComponent<ecs::Colonist>(entity) != nullptr) {
			continue;
		}

		float dx = pos.value.x - worldPos.x;
		float dy = pos.value.y - worldPos.y;
		float dist = std::sqrt(dx * dx + dy * dy);

		if (dist >= closestStorageDist) {
			continue;
		}
		const bool packaged = ecsWorld->getComponent<ecs::Packaged>(entity) != nullptr;
		const bool under = packaged
							 ? packagedUnderClick(appearance, pos.value, worldPos, kSelectionRadius)
							 : dynamicUnderClick(ecsWorld, entity, appearance.defName, appearance.scale, pos.value, worldPos);
		if (!under) {
			continue;
		}
		closestStorageDist = dist;
		closestStorage = entity;
	}

	if (closestStorage != 0) {
		auto* pos = ecsWorld->getComponent<ecs::Position>(closestStorage);
		auto* appearance = ecsWorld->getComponent<ecs::Appearance>(closestStorage);
		if (pos != nullptr && appearance != nullptr) {
			bool isPackaged = ecsWorld->getComponent<ecs::Packaged>(closestStorage) != nullptr;
			candidates.emplace_back(
				FurnitureSelection{closestStorage, appearance->defName, Foundation::Vec2{pos->value.x, pos->value.y}, isPackaged}
			);
		}
	}

	// Priority 1.8: Loose ground piles (ResourceStack). Same centroid pre-cull, then the
	// silhouette of every sprite the pile draws. A single log or tool is a small target, so a
	// click within kPilePickSlop of the pile center also hits (like kWallPickSlopMm for thin
	// walls), but only when no pile's sprites contain the click, so slop around a small pile
	// never steals a click on a big neighbour's sprite. Nearest pile wins within each kind.
	float		  closestHitDist = kSelectionRadius;
	ecs::EntityID closestHit = 0;
	float		  closestSlopDist = kPilePickSlop;
	ecs::EntityID closestSlop = 0;

	for (auto [entity, pos, appearance, stack] : ecsWorld->view<ecs::Position, ecs::Appearance, ecs::ResourceStack>()) {
		const float dx = pos.value.x - worldPos.x;
		const float dy = pos.value.y - worldPos.y;
		const float dist = std::sqrt(dx * dx + dy * dy);
		if (dist < closestSlopDist) {
			closestSlopDist = dist;
			closestSlop = entity;
		}
		if (dist >= closestHitDist) {
			continue;
		}
		const worldview::PileLayout pile = pileLayoutFor(appearance, stack.quantity, pos.value);
		bool						under = false;
		for (std::size_t i = 0; i < pile.count && !under; ++i) {
			under = placedUnderClick(pile.sprites[i], worldPos, kPilePickSlop);
		}
		if (under) {
			closestHitDist = dist;
			closestHit = entity;
		}
	}

	if (const ecs::EntityID closestPile = closestHit != 0 ? closestHit : closestSlop; closestPile != 0) {
		auto* pos = ecsWorld->getComponent<ecs::Position>(closestPile);
		auto* appearance = ecsWorld->getComponent<ecs::Appearance>(closestPile);
		if (pos != nullptr && appearance != nullptr) {
			candidates.emplace_back(
				ResourcePileSelection{closestPile, appearance->defName, Foundation::Vec2{pos->value.x, pos->value.y}}
			);
		}
	}

	// Priority 2: Check world entities (static placed assets). Broad-phase the click
	// chunk plus its 8 neighbors (a large canopy can sit far from its trunk anchor, or
	// straddle a chunk edge), each queried at the widened kWorldEntitySelectRadius; the
	// precise silhouette test then filters the false positives. Every containing entity
	// is pushed as its own candidate, ordered topmost-first (largest anchorY draws
	// frontmost) so a same-spot click cycles front to back through the stack.
	if (placementExecutor != nullptr) {
		auto&						   assetRegistry = assets::AssetRegistry::Get();
		const world::ChunkCoordinate cc =
			world::worldToChunk(world::WorldPosition{worldPos.x, worldPos.y});

		std::vector<const assets::PlacedEntity*> hits;
		for (int dyC = -1; dyC <= 1; ++dyC) {
			for (int dxC = -1; dxC <= 1; ++dxC) {
				const auto* spatialIndex = placementExecutor->getChunkIndex(world::ChunkCoordinate{cc.x + dxC, cc.y + dyC});
				if (spatialIndex == nullptr) {
					continue;
				}
				for (const auto* placedEntity : spatialIndex->queryRadius({worldPos.x, worldPos.y}, kWorldEntitySelectRadius)) {
					// Only select entities with capabilities (not grass/decorative)
					const auto* def = assetRegistry.getDefinition(placedEntity->defName);
					if (def == nullptr || !def->capabilities.hasAny()) {
						continue;
					}
					if (!placedUnderClick(*placedEntity, worldPos, kSelectionRadius)) {
						continue;
					}
					hits.push_back(placedEntity);
				}
			}
		}

		// stable_sort so entities with equal anchorY keep a deterministic order (matches
		// WorldDepthSort's stability): the candidate-key vector stays identical across
		// same-spot clicks, so click-cycling advances instead of resetting.
		std::stable_sort(hits.begin(), hits.end(), [](const assets::PlacedEntity* a, const assets::PlacedEntity* b) {
			return a->anchorY > b->anchorY; // largest anchorY = frontmost = topmost candidate
		});
		for (const auto* placedEntity : hits) {
			candidates.emplace_back(WorldEntitySelection{placedEntity->defName, placedEntity->position});
		}
	}

	// Priority 2.3: Openings (doors/windows). An opening sits IN the wall it cuts,
	// so a click on its footprint must beat the wall band underneath it (and the
	// foundation below that), while still losing to anything resting on top
	// (entities/items/colonists, handled above). Test the click against each
	// opening's oriented footprint (the same rectangle the render and indicator use)
	// in integer mm via exact point-in-polygon. Iterate openings() in its stable
	// insertion order; a contained hit wins, tie-break the highest id (deterministic,
	// matching foundationAt / segmentAt). Openings don't overlap, so "contained"
	// resolves uniquely except on a shared boundary, which the id tie-break settles.
	if (constructionWorld != nullptr) {
		const auto						clickMm = geometry::quantize(Foundation::Vec2{worldPos.x, worldPos.y});
		construction::OpeningId hitOpening = construction::kInvalidOpening;
		for (const auto& opening : constructionWorld->openings()) {
			const geometry::Ring footprint = construction::openingFootprint(*constructionWorld, opening);
			if (footprint.size() < 3) {
				continue;
			}
			if (geometry::pointInPolygon(clickMm, footprint) == geometry::PointInPolygon::Outside) {
				continue;
			}
			if (opening.id >= hitOpening) { // highest-id tie-break; ids are monotonic
				hitOpening = opening.id;
			}
		}
		if (hitOpening != construction::kInvalidOpening) {
			candidates.emplace_back(OpeningSelection{hitOpening});
		}
	}

	// Priority 2.5: Wall segments. A wall stands ON a foundation, so it must beat
	// foundation selection (a click on the wall band selects the wall, not the floor
	// under it) while still losing to anything that sits on the wall (entities, items,
	// colonists handled above). The per-segment pick radius is max(half-thickness,
	// UI slop): a thin wall gets a forgiving target, a thick wall keeps its own face.
	// segmentAt applies one radius to every segment (the topology store is
	// config-agnostic), so query with the widest needed radius, then confirm the hit
	// lies within its OWN segment's radius.
	if (constructionWorld != nullptr) {
		const auto clickMm = geometry::quantize(Foundation::Vec2{worldPos.x, worldPos.y});

		const std::int64_t queryRadiusMm = std::max(kWallPickSlopMm, maxWallHalfThicknessMm());

		const auto segmentId = constructionWorld->segmentAt(clickMm, queryRadiusMm);
		if (segmentId != construction::kInvalidSegment) {
			const auto* seg = constructionWorld->getSegment(segmentId);
			const auto* v0 = (seg != nullptr) ? constructionWorld->getVertex(seg->v0) : nullptr;
			const auto* v1 = (seg != nullptr) ? constructionWorld->getVertex(seg->v1) : nullptr;
			if (seg != nullptr && v0 != nullptr && v1 != nullptr) {
				const std::int64_t segRadiusMm = std::max(kWallPickSlopMm, segmentHalfThicknessMm(*seg));
				if (geometry::withinDistanceOfSegment(clickMm, v0->pos, v1->pos, segRadiusMm)) {
					candidates.emplace_back(WallSegmentSelection{segmentId});
				}
			}
		}
	}

	// Priority 3: Foundations (lowest). Quantize the click to integer mm and ask
	// the ConstructionWorld for the topmost foundation containing it. A colonist or
	// item sitting on the foundation outranks it via its earlier candidate slot.
	if (constructionWorld != nullptr) {
		auto foundationId = constructionWorld->foundationAt(geometry::quantize(Foundation::Vec2{worldPos.x, worldPos.y}));
		if (foundationId != construction::kInvalidFoundation) {
			candidates.emplace_back(FoundationSelection{foundationId});
		}
	}

	return candidates;
}

void SelectionSystem::handleClick(float screenX, float screenY, int viewportW, int viewportH) {
	if (ecsWorld == nullptr || camera == nullptr) {
		return;
	}

	// Convert screen position to world position
	auto worldPos = camera->screenToWorld(screenX, screenY, viewportW, viewportH, kPixelsPerMeter);

	LOG_DEBUG(Game, "Click at screen (%.1f, %.1f) -> world (%.2f, %.2f)", screenX, screenY, worldPos.x, worldPos.y);

	// Gather every entity under the cursor, ordered highest-priority-first, then build
	// the matching identity keys so we can tell a repeat same-spot click from a new one.
	const std::vector<Selection> candidates = gatherCandidates(glm::vec2{worldPos.x, worldPos.y});
	std::vector<CandidateKey>	 keys;
	keys.reserve(candidates.size());
	for (const auto& candidate : candidates) {
		keys.emplace_back(candidateKey(candidate));
	}

	const glm::vec2 clickScreen{screenX, screenY};
	const float		dx = clickScreen.x - lastClickScreen.x;
	const float		dy = clickScreen.y - lastClickScreen.y;
	// 2D (Euclidean) distance, not per-axis: a diagonal click just outside the
	// radius must not count as the same spot and wrongly advance the cycle.
	const bool sameSpot = hasLastClick && (dx * dx + dy * dy) <= (kClickCycleTolerancePx * kClickCycleTolerancePx);

	if (sameSpot && !keys.empty() && keys == lastCandidateKeys) {
		// Same stack, same spot: advance to the next thing underneath.
		cycleIndex = (cycleIndex + 1) % candidates.size();
	} else {
		// New spot, or the stack changed: start fresh at the top-priority hit.
		cycleIndex = 0;
		lastCandidateKeys = keys;
	}

	hasLastClick = true;
	lastClickScreen = clickScreen;

	if (candidates.empty()) {
		selection = NoSelection{};
		LOG_DEBUG(Game, "No selectable entity found, deselecting");
	} else {
		selection = candidates[cycleIndex];
		LOG_DEBUG(
			Game,
			"Selected candidate %zu/%zu (type index %d)",
			cycleIndex + 1,
			candidates.size(),
			static_cast<int>(selection.index())
		);
	}

	if (callbacks.onSelectionChanged) {
		callbacks.onSelectionChanged(selection);
	}
}

void SelectionSystem::resetCycleState() {
	hasLastClick = false;
	lastCandidateKeys.clear();
	cycleIndex = 0;
}

void SelectionSystem::clearSelection() {
	resetCycleState();
	selection = NoSelection{};
	if (callbacks.onSelectionChanged) {
		callbacks.onSelectionChanged(selection);
	}
}

void SelectionSystem::selectColonist(ecs::EntityID entityId) {
	resetCycleState();
	selection = ColonistSelection{entityId};
	if (callbacks.onSelectionChanged) {
		callbacks.onSelectionChanged(selection);
	}
}

void SelectionSystem::setSelection(const Selection& newSelection) {
	resetCycleState();
	selection = newSelection;
	if (callbacks.onSelectionChanged) {
		callbacks.onSelectionChanged(selection);
	}
}

void SelectionSystem::renderIndicator(int viewportW, int viewportH) {
	if (ecsWorld == nullptr || camera == nullptr) {
		return;
	}

	// Foundations: outline the selected ring (no single point), reusing the ring
	// stored in the ConstructionWorld. Drawn above the interim foundation render.
	if (auto* foundationSel = std::get_if<FoundationSelection>(&selection)) {
		const construction::Foundation* foundation =
			(constructionWorld != nullptr) ? constructionWorld->get(foundationSel->id) : nullptr;
		if (foundation != nullptr && foundation->ring.size() >= 3) {
			const std::size_t n = foundation->ring.size();
			for (std::size_t i = 0; i < n; ++i) {
				auto a = geometry::dequantize(foundation->ring[i]);
				auto b = geometry::dequantize(foundation->ring[(i + 1) % n]);
				auto sa = camera->worldToScreen(a.x, a.y, viewportW, viewportH, kPixelsPerMeter);
				auto sb = camera->worldToScreen(b.x, b.y, viewportW, viewportH, kPixelsPerMeter);
				Renderer::Primitives::drawLine(
					Renderer::Primitives::LineArgs{
						.start = Foundation::Vec2{sa.x, sa.y},
						.end = Foundation::Vec2{sb.x, sb.y},
						.style =
							Foundation::LineStyle{
								.color = Foundation::Color(1.0F, 0.85F, 0.0F, 0.9F), // Gold, matches entity indicator
								.width = 3.0F,
							},
						.id = "foundation-selection-indicator",
						.key = 100,
					}
				);
			}
		}
		return;
	}

	// Openings: outline the selected opening's footprint (the same oriented rectangle
	// the render and hit-test use), mirroring the foundation/wall gold-ring. Drawn
	// above the committed opening fill (z 63-64) at z 100.
	if (auto* openingSel = std::get_if<OpeningSelection>(&selection)) {
		const construction::Opening* opening =
			(constructionWorld != nullptr) ? constructionWorld->getOpening(openingSel->id) : nullptr;
		if (opening != nullptr) {
			const geometry::Ring footprint = construction::openingFootprint(*constructionWorld, *opening);
			const std::size_t	 n = footprint.size();
			for (std::size_t i = 0; i < n; ++i) {
				auto a = geometry::dequantize(footprint[i]);
				auto b = geometry::dequantize(footprint[(i + 1) % n]);
				auto sa = camera->worldToScreen(a.x, a.y, viewportW, viewportH, kPixelsPerMeter);
				auto sb = camera->worldToScreen(b.x, b.y, viewportW, viewportH, kPixelsPerMeter);
				Renderer::Primitives::drawLine(
					Renderer::Primitives::LineArgs{
						.start = Foundation::Vec2{sa.x, sa.y},
						.end = Foundation::Vec2{sb.x, sb.y},
						.style =
							Foundation::LineStyle{
								.color = Foundation::Color(1.0F, 0.85F, 0.0F, 0.9F), // Gold, matches wall/foundation indicator
								.width = 3.0F,
							},
						.id = "opening-selection-indicator",
						.key = 100,
					}
				);
			}
		}
		return;
	}

	// Wall segments: outline the selected segment's BAND (centerline offset by its
	// half-thickness), mirroring the foundation gold-ring. Using geometry::band for
	// the single segment keeps the indicator independent of the whole-graph junction
	// trim the interim wall render runs; the outline reads cleanly even where two
	// bands meet. Drawn above the wall band render (z 60-62) at z 100.
	if (auto* wallSel = std::get_if<WallSegmentSelection>(&selection)) {
		const construction::WallSegment* seg =
			(constructionWorld != nullptr) ? constructionWorld->getSegment(wallSel->id) : nullptr;
		const construction::Vertex* v0 = (seg != nullptr) ? constructionWorld->getVertex(seg->v0) : nullptr;
		const construction::Vertex* v1 = (seg != nullptr) ? constructionWorld->getVertex(seg->v1) : nullptr;
		if (seg != nullptr && v0 != nullptr && v1 != nullptr) {
			const std::int64_t halfThicknessMm = std::max<std::int64_t>(1, segmentHalfThicknessMm(*seg));
			const geometry::Ring band = geometry::band(v0->pos, v1->pos, halfThicknessMm);
			const std::size_t  n = band.size();
			for (std::size_t i = 0; i < n; ++i) {
				auto a = geometry::dequantize(band[i]);
				auto b = geometry::dequantize(band[(i + 1) % n]);
				auto sa = camera->worldToScreen(a.x, a.y, viewportW, viewportH, kPixelsPerMeter);
				auto sb = camera->worldToScreen(b.x, b.y, viewportW, viewportH, kPixelsPerMeter);
				Renderer::Primitives::drawLine(
					Renderer::Primitives::LineArgs{
						.start = Foundation::Vec2{sa.x, sa.y},
						.end = Foundation::Vec2{sb.x, sb.y},
						.style =
							Foundation::LineStyle{
								.color = Foundation::Color(1.0F, 0.85F, 0.0F, 0.9F), // Gold, matches foundation indicator
								.width = 3.0F,
							},
						.id = "wall-segment-selection-indicator",
						.key = 100,
					}
				);
			}
		}
		return;
	}

	// Entity-backed selections (world entities, colonists, stations, furniture,
	// packaged items, loose piles) no longer draw here: their outlines are built by
	// buildEntityOutline and injected into the entity depth-sort render pass so a
	// nearer entity correctly occludes a farther selection's outline.
}

worldview::SelectionOutline SelectionSystem::buildEntityOutline() const {
	worldview::SelectionOutline outline;
	if (ecsWorld == nullptr) {
		return outline;
	}

	auto&		assetRegistry = assets::AssetRegistry::Get();
	const float kNoY = -std::numeric_limits<float>::max();
	float		maxY = kNoY;
	// Prefer the entity's canonical depth key (PlacedEntity::anchorY) when available so the
	// outline injects at the exact depth the entity draws; the silhouette-vertex maxY is only
	// a fallback (raster maxY can differ slightly from the template's).
	float canonicalAnchorY = kNoY;
	// Composite selections (packaged furniture, piles) collect world-mm rings here to union.
	std::vector<geometry::Ring> compositeMm;

	// World entities: re-resolve the live PlacedEntity by defName + position and
	// build its silhouette at the static/baked transform. A felled or unloaded
	// entity resolves to nullptr -> invalid outline (draws nothing).
	if (const auto* worldSel = std::get_if<WorldEntitySelection>(&selection)) {
		const assets::PlacedEntity* pe = resolveWorldEntity(*worldSel);
		if (pe == nullptr) {
			return outline;
		}
		canonicalAnchorY = pe->anchorY; // exact placement depth key, not the raster maxY
		const auto* sil = assetRegistry.getSilhouette(pe->defName);
		if (sil == nullptr || !sil->valid) {
			return outline;
		}
		appendWorldRings(
			sil->rings, worldview::staticInstanceTransform(pe->position, pe->rotation, pe->scale), outline.worldRings, maxY
		);
	} else if (const auto* pileSel = std::get_if<ResourcePileSelection>(&selection)) {
		// Loose pile: every sprite of the shared pile layout at its position-as-origin
		// transform, unioned into one outline around the heap as drawn. A drained (destroyed)
		// pile -> invalid outline.
		if (!ecsWorld->isAlive(pileSel->entityId)) {
			return outline;
		}
		const auto* pos = ecsWorld->getComponent<ecs::Position>(pileSel->entityId);
		const auto* appearance = ecsWorld->getComponent<ecs::Appearance>(pileSel->entityId);
		const auto* stack = ecsWorld->getComponent<ecs::ResourceStack>(pileSel->entityId);
		if (pos == nullptr || appearance == nullptr || stack == nullptr) {
			return outline;
		}
		const auto* sil = assetRegistry.getSilhouette(appearance->defName);
		if (sil == nullptr || !sil->valid) {
			return outline;
		}
		const worldview::PileLayout pile = pileLayoutFor(*appearance, stack->quantity, pos->value);
		for (std::size_t i = 0; i < pile.count; ++i) {
			const assets::PlacedEntity& sprite = pile.sprites[i];
			appendWorldRingsMm(sil->rings, worldview::staticInstanceTransform(sprite.position, sprite.rotation, sprite.scale), compositeMm);
		}
		canonicalAnchorY = pile.sprites[pile.count - 1].anchorY; // the front-most sprite's key
	} else {
		// Colonists, crafting stations, and placed furniture all resolve from the ECS.
		ecs::EntityID entityId = 0;
		bool		  isFurniture = false;
		if (const auto* colonistSel = std::get_if<ColonistSelection>(&selection)) {
			entityId = colonistSel->entityId;
		} else if (const auto* stationSel = std::get_if<CraftingStationSelection>(&selection)) {
			entityId = stationSel->entityId;
		} else if (const auto* furnitureSel = std::get_if<FurnitureSelection>(&selection)) {
			entityId = furnitureSel->entityId;
			isFurniture = true;
		}
		if (entityId == 0) {
			return outline; // construction / room / no selection: not entity-backed
		}

		const auto* pos = ecsWorld->getComponent<ecs::Position>(entityId);
		const auto* appearance = ecsWorld->getComponent<ecs::Appearance>(entityId);
		if (pos == nullptr || appearance == nullptr) {
			return outline;
		}

		// Packaged furniture: crate layers + item at their layout transforms (drawn as
		// position-as-origin instances, matching the render path; sub-entities are NOT
		// re-centered), unioned into one outline. With the item inside the crate that is the
		// crate's silhouette.
		if (isFurniture && ecsWorld->getComponent<ecs::Packaged>(entityId) != nullptr) {
			const worldview::PackagedLayout pl = packagedLayoutFor(*appearance, pos->value);
			for (const assets::PlacedEntity* sub : {&pl.crateBack, &pl.item, &pl.crateFront}) {
				const auto* sil = assetRegistry.getSilhouette(sub->defName);
				if (sil != nullptr && sil->valid) {
					appendWorldRingsMm(sil->rings, worldview::staticInstanceTransform(sub->position, sub->rotation, sub->scale), compositeMm);
				}
			}
			canonicalAnchorY = pl.crateFront.anchorY;
		} else {
			// Non-packaged dynamic entity: the facing sprite's silhouette at the dynamic
			// (bbox-centered) transform.
			const std::string defName = dynamicRenderDefName(ecsWorld, entityId, appearance->defName);
			const auto*		  sil = assetRegistry.getSilhouette(defName);
			if (sil == nullptr || !sil->valid) {
				return outline;
			}
			appendWorldRings(
				sil->rings, worldview::dynamicInstanceTransform(pos->value, appearance->scale, sil->boundsCenterMeters), outline.worldRings, maxY
			);
		}
	}

	if (!compositeMm.empty()) {
		for (const geometry::Ring& ring : unionCompositeRings(std::move(compositeMm))) {
			std::vector<Foundation::Vec2> worldRing;
			worldRing.reserve(ring.size());
			for (const geometry::Vec2i64& mm : ring) {
				const Foundation::Vec2 w = geometry::dequantize(mm);
				worldRing.push_back(w);
				maxY = std::max(maxY, w.y);
			}
			outline.worldRings.push_back(std::move(worldRing));
		}
	}

	outline.valid = !outline.worldRings.empty();
	if (outline.valid) {
		// Canonical sprite anchorY when we have it (world entities, piles, packaged crates; a
		// unioned ring's maxY can land under the sprite's key), else the silhouette ground-contact
		// (dynamic entities, whose sprite bottom == the silhouette bottom).
		outline.anchorY = (canonicalAnchorY != kNoY) ? canonicalAnchorY : maxY;
		outline.rgba	= {1.0F, 0.85F, 0.0F, 0.9F};
		outline.widthPx = kOutlineWidthPx;
	}
	return outline;
}

const assets::PlacedEntity* SelectionSystem::resolveWorldEntity(const WorldEntitySelection& sel) const {
	if (placementExecutor == nullptr) {
		return nullptr;
	}
	const world::ChunkCoordinate cc =
		world::worldToChunk(world::WorldPosition{sel.position.x, sel.position.y});
	// The stored position is an exact copy of the placed entity's; a millimeter of slop
	// absorbs any float round-trip. Match on defName too so a same-spot stack of
	// different assets never cross-resolves.
	constexpr float kMatchTolMeters = 0.001F;
	for (int dyC = -1; dyC <= 1; ++dyC) {
		for (int dxC = -1; dxC <= 1; ++dxC) {
			const auto* spatialIndex = placementExecutor->getChunkIndex(world::ChunkCoordinate{cc.x + dxC, cc.y + dyC});
			if (spatialIndex == nullptr) {
				continue;
			}
			for (const auto* pe : spatialIndex->queryRadius({sel.position.x, sel.position.y}, kWorldEntitySelectRadius)) {
				if (pe->defName != sel.defName) {
					continue;
				}
				const float ex = pe->position.x - sel.position.x;
				const float ey = pe->position.y - sel.position.y;
				if (ex * ex + ey * ey <= kMatchTolMeters * kMatchTolMeters) {
					return pe;
				}
			}
		}
	}
	return nullptr;
}

} // namespace world_sim
