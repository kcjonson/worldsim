#include "SelectionSystem.h"

#include <assets/AssetRegistry.h>
#include <assets/placement/PlacementExecutor.h>
#include <construction/ConstructionWorld.h>
#include <core/Vec2i64.h>
#include <ecs/components/Appearance.h>
#include <ecs/components/Colonist.h>
#include <ecs/components/Inventory.h>
#include <ecs/components/Packaged.h>
#include <ecs/components/Transform.h>
#include <ecs/components/WorkQueue.h>
#include <primitives/Primitives.h>
#include <utils/Log.h>
#include <world/chunk/ChunkCoordinate.h>

#include <cmath>

namespace world_sim {

SelectionSystem::SelectionSystem(const Args& args)
	: ecsWorld(args.world)
	, camera(args.camera)
	, placementExecutor(args.placementExecutor)
	, constructionWorld(args.constructionWorld)
	, callbacks(args.callbacks) {}

void SelectionSystem::handleClick(float screenX, float screenY, int viewportW, int viewportH) {
	if (ecsWorld == nullptr || camera == nullptr) {
		return;
	}

	// Convert screen position to world position
	auto worldPos = camera->screenToWorld(screenX, screenY, viewportW, viewportH, kPixelsPerMeter);

	LOG_DEBUG(Game, "Click at screen (%.1f, %.1f) -> world (%.2f, %.2f)", screenX, screenY, worldPos.x, worldPos.y);

	// Priority 1: Check ECS colonists first (dynamic, moving entities)
	float		  closestColonistDist = kSelectionRadius;
	ecs::EntityID closestColonist = 0;

	for (auto [entity, pos, colonist] : ecsWorld->view<ecs::Position, ecs::Colonist>()) {
		float dx = pos.value.x - worldPos.x;
		float dy = pos.value.y - worldPos.y;
		float dist = std::sqrt(dx * dx + dy * dy);

		if (dist < closestColonistDist) {
			closestColonistDist = dist;
			closestColonist = entity;
		}
	}

	if (closestColonist != 0) {
		selection = ColonistSelection{closestColonist};
		if (auto* colonist = ecsWorld->getComponent<ecs::Colonist>(closestColonist)) {
			LOG_INFO(Game, "Selected colonist: %s", colonist->name.c_str());
		}
		if (callbacks.onSelectionChanged) {
			callbacks.onSelectionChanged(selection);
		}
		return;
	}

	// Priority 1.5: Check ECS stations (entities with WorkQueue)
	float		  closestStationDist = kSelectionRadius;
	ecs::EntityID closestStation = 0;

	for (auto [entity, pos, appearance, workQueue] : ecsWorld->view<ecs::Position, ecs::Appearance, ecs::WorkQueue>()) {
		float dx = pos.value.x - worldPos.x;
		float dy = pos.value.y - worldPos.y;
		float dist = std::sqrt(dx * dx + dy * dy);

		if (dist < closestStationDist) {
			closestStationDist = dist;
			closestStation = entity;
		}
	}

	if (closestStation != 0) {
		auto* pos = ecsWorld->getComponent<ecs::Position>(closestStation);
		auto* appearance = ecsWorld->getComponent<ecs::Appearance>(closestStation);
		if (pos != nullptr && appearance != nullptr) {
			selection = CraftingStationSelection{
				closestStation, appearance->defName, Foundation::Vec2{pos->value.x, pos->value.y}
			};
			LOG_INFO(Game, "Selected station: %s at (%.1f, %.1f)", appearance->defName.c_str(), pos->value.x, pos->value.y);
		}
		if (callbacks.onSelectionChanged) {
			callbacks.onSelectionChanged(selection);
		}
		return;
	}

	// Priority 1.6: Check ECS storage containers (entities with Inventory but no WorkQueue)
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

		if (dist < closestStorageDist) {
			closestStorageDist = dist;
			closestStorage = entity;
		}
	}

	if (closestStorage != 0) {
		auto* pos = ecsWorld->getComponent<ecs::Position>(closestStorage);
		auto* appearance = ecsWorld->getComponent<ecs::Appearance>(closestStorage);
		if (pos != nullptr && appearance != nullptr) {
			bool isPackaged = ecsWorld->getComponent<ecs::Packaged>(closestStorage) != nullptr;
			selection = FurnitureSelection{
				closestStorage, appearance->defName, Foundation::Vec2{pos->value.x, pos->value.y}, isPackaged
			};
			LOG_INFO(
				Game,
				"Selected storage: %s at (%.1f, %.1f)%s",
				appearance->defName.c_str(),
				pos->value.x,
				pos->value.y,
				isPackaged ? " (packaged)" : ""
			);
		}
		if (callbacks.onSelectionChanged) {
			callbacks.onSelectionChanged(selection);
		}
		return;
	}

	// Priority 2: Check world entities (static placed assets)
	if (placementExecutor != nullptr) {
		auto&						   assetRegistry = engine::assets::AssetRegistry::Get();
		engine::world::ChunkCoordinate chunkCoord = engine::world::worldToChunk(engine::world::WorldPosition{worldPos.x, worldPos.y});
		const auto*					   spatialIndex = placementExecutor->getChunkIndex(chunkCoord);
		if (spatialIndex != nullptr) {
			auto nearbyEntities = spatialIndex->queryRadius({worldPos.x, worldPos.y}, kSelectionRadius);

			float								closestEntityDist = kSelectionRadius;
			const engine::assets::PlacedEntity* closestWorldEntity = nullptr;

			for (const auto* placedEntity : nearbyEntities) {
				// Only select entities with capabilities (not grass/decorative)
				const auto* def = assetRegistry.getDefinition(placedEntity->defName);
				if (def == nullptr || !def->capabilities.hasAny()) {
					continue;
				}

				float dx = placedEntity->position.x - worldPos.x;
				float dy = placedEntity->position.y - worldPos.y;
				float dist = std::sqrt(dx * dx + dy * dy);

				if (dist < closestEntityDist) {
					closestEntityDist = dist;
					closestWorldEntity = placedEntity;
				}
			}

			if (closestWorldEntity != nullptr) {
				selection = WorldEntitySelection{closestWorldEntity->defName, closestWorldEntity->position};
				LOG_INFO(
					Game,
					"Selected world entity: %s at (%.1f, %.1f)",
					closestWorldEntity->defName.c_str(),
					closestWorldEntity->position.x,
					closestWorldEntity->position.y
				);
				if (callbacks.onSelectionChanged) {
					callbacks.onSelectionChanged(selection);
				}
				return;
			}
		}
	}

	// Priority 3: Foundations (lowest). Quantize the click to integer mm and ask
	// the ConstructionWorld for the topmost foundation containing it. Reaching
	// here means no higher-priority entity was hit, so a colonist or item sitting
	// on the foundation still wins the click.
	if (constructionWorld != nullptr) {
		auto foundationId = constructionWorld->foundationAt(geometry::quantize(Foundation::Vec2{worldPos.x, worldPos.y}));
		if (foundationId != engine::construction::kInvalidFoundation) {
			selection = FoundationSelection{foundationId};
			LOG_INFO(Game, "Selected foundation #%llu", static_cast<unsigned long long>(foundationId));
			if (callbacks.onSelectionChanged) {
				callbacks.onSelectionChanged(selection);
			}
			return;
		}
	}

	// Nothing found - deselect
	selection = NoSelection{};
	LOG_DEBUG(Game, "No selectable entity found, deselecting");
	if (callbacks.onSelectionChanged) {
		callbacks.onSelectionChanged(selection);
	}
}

void SelectionSystem::clearSelection() {
	selection = NoSelection{};
	if (callbacks.onSelectionChanged) {
		callbacks.onSelectionChanged(selection);
	}
}

void SelectionSystem::selectColonist(ecs::EntityID entityId) {
	selection = ColonistSelection{entityId};
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

	// Get world position from selection (colonists, stations, and furniture have ECS positions)
	glm::vec2 worldPos{0.0F, 0.0F};
	bool	  hasPosition = false;

	if (auto* colonistSel = std::get_if<ColonistSelection>(&selection)) {
		if (auto* pos = ecsWorld->getComponent<ecs::Position>(colonistSel->entityId)) {
			worldPos = pos->value;
			hasPosition = true;
		}
	} else if (auto* stationSel = std::get_if<CraftingStationSelection>(&selection)) {
		if (auto* pos = ecsWorld->getComponent<ecs::Position>(stationSel->entityId)) {
			worldPos = pos->value;
			hasPosition = true;
		}
	} else if (auto* furnitureSel = std::get_if<FurnitureSelection>(&selection)) {
		if (auto* pos = ecsWorld->getComponent<ecs::Position>(furnitureSel->entityId)) {
			worldPos = pos->value;
			hasPosition = true;
		}
	}

	if (!hasPosition) {
		return;
	}

	// Convert world position to screen position
	auto screenPos = camera->worldToScreen(worldPos.x, worldPos.y, viewportW, viewportH, kPixelsPerMeter);

	// Convert selection radius from world units to screen pixels
	float screenRadius = camera->worldDistanceToScreen(kIndicatorRadius, kPixelsPerMeter);

	// Draw selection circle with border-only style (transparent fill)
	Renderer::Primitives::drawCircle(
		Renderer::Primitives::CircleArgs{
			.center = Foundation::Vec2{screenPos.x, screenPos.y},
			.radius = screenRadius,
			.style =
				Foundation::CircleStyle{
					.fill = Foundation::Color(0.0F, 0.0F, 0.0F, 0.0F), // Transparent fill
					.border =
						Foundation::BorderStyle{
							.color = Foundation::Color(1.0F, 0.85F, 0.0F, 0.8F), // Gold color
							.width = 2.0F,
						},
				},
			.id = "selection-indicator",
			.zIndex = 100, // Above entities
		}
	);
}

} // namespace world_sim
