#include "SelectionSystem.h"

#include <assets/AssetRegistry.h>
#include <assets/ConstructionRegistry.h>
#include <assets/placement/PlacementExecutor.h>
#include <construction/ConstructionWorld.h>
#include <construction/OpeningGeometry.h>
#include <core/Vec2i64.h>
#include <ecs/components/Appearance.h>
#include <ecs/components/Colonist.h>
#include <ecs/components/FacingDirection.h>
#include <ecs/components/Inventory.h>
#include <ecs/components/Packaged.h>
#include <ecs/components/Transform.h>
#include <ecs/components/WorkQueue.h>
#include <offset/WallOffset.h>
#include <predicates/Predicates.h>
#include <primitives/Primitives.h>
#include <utils/Log.h>
#include <world/chunk/ChunkCoordinate.h>
#include <world/rendering/PackagedLayout.h> // shared crate+item placement (no drift)

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
	std::int64_t segmentHalfThicknessMm(const engine::construction::WallSegment& seg) {
		const auto* preset = engine::assets::ConstructionRegistry::Get().getThicknessPreset(seg.material, seg.thicknessPreset);
		return (preset != nullptr) ? preset->halfThicknessMm : 0;
	}

	// Widest wall half-thickness any segment could have, from the config presets (a
	// handful), NOT by scanning committed segments (which grows with wall count). Used
	// only to size the single-radius segmentAt query; the hit is then confirmed against
	// the picked segment's own radius.
	std::int64_t maxWallHalfThicknessMm() {
		std::int64_t maxHalf = 0;
		for (const auto& [name, material] : engine::assets::ConstructionRegistry::Get().getAllMaterials()) {
			for (const auto& preset : material.wallThicknesses) {
				maxHalf = std::max(maxHalf, preset.halfThicknessMm);
			}
		}
		return maxHalf;
	}

	// Direction suffix the dynamic render path appends for a directional sprite. Mirrors
	// DynamicEntityRenderSystem::getDirectionSuffix so the hit-test and outline pick the
	// exact directional template the entity draws.
	const char* directionSuffix(ecs::CardinalDirection dir) {
		switch (dir) {
			case ecs::CardinalDirection::Up:
				return "_up";
			case ecs::CardinalDirection::Down:
				return "_down";
			case ecs::CardinalDirection::Left:
				return "_left";
			case ecs::CardinalDirection::Right:
				return "_right";
		}
		return "_down";
	}

	// The defName the dynamic render path resolves for an entity: its base Appearance
	// defName plus the facing suffix when it carries a FacingDirection.
	std::string dynamicRenderDefName(ecs::World* world, ecs::EntityID entity, const std::string& baseDefName) {
		if (const auto* facing = world->getComponent<ecs::FacingDirection>(entity)) {
			return baseDefName + directionSuffix(facing->direction);
		}
		return baseDefName;
	}

	// True if `worldPos` (meters) lies inside any ring of `sil` under transform `t`. The
	// click is mapped into the template-local frame and quantized to mm to match the
	// silhouette rings (which are template-local integer millimeters).
	bool silhouetteRingsContain(
		const engine::assets::AssetSilhouette& sil, const engine::world::AssetInstanceTransform& t, glm::vec2 worldPos
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
	bool placedUnderClick(const engine::assets::PlacedEntity& pe, glm::vec2 worldPos, float fallbackRadius) {
		const auto* sil = engine::assets::AssetRegistry::Get().getSilhouette(pe.defName);
		if (sil == nullptr || !sil->valid) {
			const float dx = pe.position.x - worldPos.x;
			const float dy = pe.position.y - worldPos.y;
			return (dx * dx + dy * dy) < (fallbackRadius * fallbackRadius);
		}
		return silhouetteRingsContain(*sil, engine::world::staticInstanceTransform(pe.position, pe.rotation, pe.scale), worldPos);
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
		const auto*		  sil = engine::assets::AssetRegistry::Get().getSilhouette(defName);
		if (sil == nullptr || !sil->valid) {
			return true;
		}
		return silhouetteRingsContain(*sil, engine::world::dynamicInstanceTransform(entityPos, scale, sil->boundsCenterMeters), worldPos);
	}

	// True if the click is under a packaged item's drawn sprite: the crate behind or the
	// shrunk item in front, laid out and transformed exactly as the render path via
	// packagedLayout + the per-sub-entity position-as-origin transform.
	bool packagedUnderClick(const ecs::Appearance& appearance, glm::vec2 entityPos, glm::vec2 worldPos, float fallbackRadius) {
		float itemWorldHeight = 0.6F; // matches DynamicEntityRenderSystem's fallback
		if (const auto* itemDef = engine::assets::AssetRegistry::Get().getDefinition(appearance.defName)) {
			itemWorldHeight = itemDef->worldHeight;
		}
		const engine::world::PackagedLayout pl = engine::world::packagedLayout(
			entityPos, appearance.defName, appearance.scale, appearance.colorTint, itemWorldHeight
		);
		return placedUnderClick(pl.crate, worldPos, fallbackRadius) || placedUnderClick(pl.item, worldPos, fallbackRadius);
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
		const engine::world::AssetInstanceTransform& t,
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
} // namespace

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
	// WorkQueue). Packaged ones test the crate + item sprite; placed ones test their own
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

	// Priority 2: Check world entities (static placed assets). Broad-phase the click
	// chunk plus its 8 neighbors (a large canopy can sit far from its trunk anchor, or
	// straddle a chunk edge), each queried at the widened kWorldEntitySelectRadius; the
	// precise silhouette test then filters the false positives. Every containing entity
	// is pushed as its own candidate, ordered topmost-first (largest anchorY draws
	// frontmost) so a same-spot click cycles front to back through the stack.
	if (placementExecutor != nullptr) {
		auto&						   assetRegistry = engine::assets::AssetRegistry::Get();
		const engine::world::ChunkCoordinate cc =
			engine::world::worldToChunk(engine::world::WorldPosition{worldPos.x, worldPos.y});

		std::vector<const engine::assets::PlacedEntity*> hits;
		for (int dyC = -1; dyC <= 1; ++dyC) {
			for (int dxC = -1; dxC <= 1; ++dxC) {
				const auto* spatialIndex = placementExecutor->getChunkIndex(engine::world::ChunkCoordinate{cc.x + dxC, cc.y + dyC});
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

		std::sort(hits.begin(), hits.end(), [](const engine::assets::PlacedEntity* a, const engine::assets::PlacedEntity* b) {
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
		engine::construction::OpeningId hitOpening = engine::construction::kInvalidOpening;
		for (const auto& opening : constructionWorld->openings()) {
			const geometry::Ring footprint = engine::construction::openingFootprint(*constructionWorld, opening);
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
		if (hitOpening != engine::construction::kInvalidOpening) {
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
		if (segmentId != engine::construction::kInvalidSegment) {
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
		if (foundationId != engine::construction::kInvalidFoundation) {
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
		const engine::construction::Foundation* foundation =
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
						.zIndex = 100,
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
		const engine::construction::Opening* opening =
			(constructionWorld != nullptr) ? constructionWorld->getOpening(openingSel->id) : nullptr;
		if (opening != nullptr) {
			const geometry::Ring footprint = engine::construction::openingFootprint(*constructionWorld, *opening);
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
						.zIndex = 100,
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
		const engine::construction::WallSegment* seg =
			(constructionWorld != nullptr) ? constructionWorld->getSegment(wallSel->id) : nullptr;
		const engine::construction::Vertex* v0 = (seg != nullptr) ? constructionWorld->getVertex(seg->v0) : nullptr;
		const engine::construction::Vertex* v1 = (seg != nullptr) ? constructionWorld->getVertex(seg->v1) : nullptr;
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
						.zIndex = 100,
					}
				);
			}
		}
		return;
	}

	// Entity-backed selections (world entities, colonists, stations, furniture,
	// packaged items) no longer draw here: their outlines are built by
	// buildEntityOutline and injected into the entity depth-sort render pass so a
	// nearer entity correctly occludes a farther selection's outline.
}

engine::world::SelectionOutline SelectionSystem::buildEntityOutline() const {
	engine::world::SelectionOutline outline;
	if (ecsWorld == nullptr) {
		return outline;
	}

	auto&		assetRegistry = engine::assets::AssetRegistry::Get();
	const float kNoY = -std::numeric_limits<float>::max();
	float		maxY = kNoY;

	// World entities: re-resolve the live PlacedEntity by defName + position and
	// build its silhouette at the static/baked transform. A felled or unloaded
	// entity resolves to nullptr -> invalid outline (draws nothing).
	if (const auto* worldSel = std::get_if<WorldEntitySelection>(&selection)) {
		const engine::assets::PlacedEntity* pe = resolveWorldEntity(*worldSel);
		if (pe == nullptr) {
			return outline;
		}
		const auto* sil = assetRegistry.getSilhouette(pe->defName);
		if (sil == nullptr || !sil->valid) {
			return outline;
		}
		appendWorldRings(
			sil->rings, engine::world::staticInstanceTransform(pe->position, pe->rotation, pe->scale), outline.worldRings, maxY
		);
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

		// Packaged furniture: crate + item at their layout transforms (both drawn as
		// position-as-origin instances, matching the render path; sub-entities are NOT
		// re-centered).
		if (isFurniture && ecsWorld->getComponent<ecs::Packaged>(entityId) != nullptr) {
			float itemWorldHeight = 0.6F; // matches DynamicEntityRenderSystem's fallback
			if (const auto* itemDef = assetRegistry.getDefinition(appearance->defName)) {
				itemWorldHeight = itemDef->worldHeight;
			}
			const engine::world::PackagedLayout pl = engine::world::packagedLayout(
				pos->value, appearance->defName, appearance->scale, appearance->colorTint, itemWorldHeight
			);
			for (const engine::assets::PlacedEntity* sub : {&pl.crate, &pl.item}) {
				const auto* sil = assetRegistry.getSilhouette(sub->defName);
				if (sil != nullptr && sil->valid) {
					appendWorldRings(
						sil->rings, engine::world::staticInstanceTransform(sub->position, sub->rotation, sub->scale), outline.worldRings, maxY
					);
				}
			}
		} else {
			// Non-packaged dynamic entity: the facing sprite's silhouette at the dynamic
			// (bbox-centered) transform.
			const std::string defName = dynamicRenderDefName(ecsWorld, entityId, appearance->defName);
			const auto*		  sil = assetRegistry.getSilhouette(defName);
			if (sil == nullptr || !sil->valid) {
				return outline;
			}
			appendWorldRings(
				sil->rings, engine::world::dynamicInstanceTransform(pos->value, appearance->scale, sil->boundsCenterMeters), outline.worldRings, maxY
			);
		}
	}

	outline.valid = !outline.worldRings.empty();
	if (outline.valid) {
		outline.anchorY = maxY;			 // max world-Y over all verts = ground-contact depth key
		outline.rgba = {1.0F, 0.85F, 0.0F, 0.9F};
		outline.widthPx = kOutlineWidthPx;
	}
	return outline;
}

const engine::assets::PlacedEntity* SelectionSystem::resolveWorldEntity(const WorldEntitySelection& sel) const {
	if (placementExecutor == nullptr) {
		return nullptr;
	}
	const engine::world::ChunkCoordinate cc =
		engine::world::worldToChunk(engine::world::WorldPosition{sel.position.x, sel.position.y});
	// The stored position is an exact copy of the placed entity's; a millimeter of slop
	// absorbs any float round-trip. Match on defName too so a same-spot stack of
	// different assets never cross-resolves.
	constexpr float kMatchTolMeters = 0.001F;
	for (int dyC = -1; dyC <= 1; ++dyC) {
		for (int dxC = -1; dxC <= 1; ++dxC) {
			const auto* spatialIndex = placementExecutor->getChunkIndex(engine::world::ChunkCoordinate{cc.x + dxC, cc.y + dyC});
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
