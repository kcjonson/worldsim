#include "PlacementSystem.h"

#include <assets/RecipeRegistry.h>
#include <ecs/components/Appearance.h>
#include <ecs/components/Packaged.h>
#include <ecs/systems/NavigationSystem.h>
#include <session/EntityFactory.h>
#include <primitives/Primitives.h>
#include <utils/Log.h>

namespace world_sim {

	PlacementSystem::PlacementSystem(const Args& args)
		: ecsWorld(args.world),
		  camera(args.camera),
		  factory(args.factory),
		  navigation(args.navigation),
		  callbacks(args.callbacks) {
		// Initialize placement mode with callback to handle placement
		placementMode = PlacementMode{PlacementMode::Args{.onPlace = [this](const std::string& defName, Foundation::Vec2 worldPos) {
			if (relocatingEntityId != ecs::EntityID{0}) {
				// Set target position on packaged item - colonist will carry it there
				auto* packaged = ecsWorld->getComponent<ecs::Packaged>(relocatingEntityId);
				if (packaged != nullptr) {
					packaged->targetPosition = glm::vec2{worldPos.x, worldPos.y};
					LOG_INFO(
						Game,
						"Set placement target (%.1f, %.1f) for entity %u - awaiting colonist delivery",
						worldPos.x,
						worldPos.y,
						static_cast<uint32_t>(relocatingEntityId)
					);
					// Clear selection after successfully setting target
					if (callbacks.onSelectionCleared) {
						callbacks.onSelectionCleared();
					}
					relocatingEntityId = ecs::EntityID{0};
				} else {
					// Entity no longer has Packaged component - placement failed
					// Don't clear selection so user can see the issue and retry
					LOG_WARNING(
						Game,
						"Entity %u no longer has Packaged component - placement failed",
						static_cast<uint32_t>(relocatingEntityId)
					);
					relocatingEntityId = ecs::EntityID{0};
				}
			} else {
				factory->createWorldEntity(defName, {worldPos.x, worldPos.y});
			}
		}}};
	}

	void PlacementSystem::toggleBuildMenu() {
		switch (placementMode.state()) {
			case PlacementState::None: {
				// Open build menu
				placementMode.enterMenu();
				if (callbacks.onBuildMenuVisibility) {
					callbacks.onBuildMenuVisibility(true);
				}

				// Get innate recipes for the build menu
				auto& recipeRegistry = assets::RecipeRegistry::Get();
				auto  innateRecipes = recipeRegistry.getInnateRecipes();

				std::vector<BuildMenuItem> items;
				for (const auto* recipe : innateRecipes) {
					if (!recipe->outputs.empty()) {
						items.push_back({recipe->outputs[0].defName, recipe->label});
					}
				}

				if (callbacks.onShowBuildMenu) {
					callbacks.onShowBuildMenu(items);
				}
				break;
			}

			case PlacementState::MenuOpen:
			case PlacementState::Placing:
				// Cancel placement
				cancel();
				break;
		}
	}

	void PlacementSystem::selectBuildItem(const std::string& defName) {
		placementMode.selectItem(defName);
		if (callbacks.onHideBuildMenu) {
			callbacks.onHideBuildMenu();
		}
		LOG_INFO(Game, "Selected '%s' for placement", defName.c_str());
	}

	void PlacementSystem::beginRelocation(ecs::EntityID entityId, const std::string& defName) {
		// Store the entity ID we're relocating
		relocatingEntityId = entityId;

		// Enter placement mode with the entity's def name
		placementMode.selectItem(defName);
		LOG_INFO(Game, "Placing entity '%s' (entity %u)", defName.c_str(), static_cast<uint32_t>(entityId));
	}

	void PlacementSystem::handleMouseMove(float screenX, float screenY, int viewportW, int viewportH) {
		if (placementMode.state() != PlacementState::Placing || camera == nullptr) {
			return;
		}

		auto worldPos = camera->screenToWorld(screenX, screenY, viewportW, viewportH, kPixelsPerMeter);
		// Valid IFF the spot is on an active walkable nav face. The nav mesh owns runtime
		// walkability; no terrain/world-source read here. No nav system => nothing placeable.
		const bool valid = navigation != nullptr && navigation->isValidPosition({worldPos.x, worldPos.y});
		placementMode.updateGhostPosition({worldPos.x, worldPos.y}, valid);
	}

	bool PlacementSystem::handleClick() {
		if (placementMode.state() != PlacementState::Placing) {
			return false;
		}

		if (placementMode.tryPlace()) {
			// Successfully placed - update UI state
			if (callbacks.onBuildMenuVisibility) {
				callbacks.onBuildMenuVisibility(false);
			}
			if (callbacks.onHideBuildMenu) {
				callbacks.onHideBuildMenu();
			}
			return true;
		}
		return false;
	}

	void PlacementSystem::cancel() {
		if (!placementMode.isActive()) {
			return;
		}

		placementMode.cancel();
		relocatingEntityId = ecs::EntityID{0};

		if (callbacks.onBuildMenuVisibility) {
			callbacks.onBuildMenuVisibility(false);
		}
		if (callbacks.onHideBuildMenu) {
			callbacks.onHideBuildMenu();
		}
	}

	void PlacementSystem::render(int viewportW, int viewportH) {
		if (camera == nullptr) {
			return;
		}

		// Render active placement ghost (during placement mode)
		if (placementMode.state() == PlacementState::Placing) {
			ghostRenderer.render(
				placementMode.selectedDefName(), placementMode.ghostPosition(), *camera, viewportW, viewportH, placementMode.isValidPlacement()
			);
		}

		// Render ghosts for all packaged items awaiting colonist delivery
		// (Skip items already being carried - they'll be placed shortly)
		if (ecsWorld != nullptr) {
			for (auto [entity, packaged, appearance] : ecsWorld->view<ecs::Packaged, ecs::Appearance>()) {
				if (packaged.targetPosition.has_value() && !packaged.beingCarried) {
					const auto& target = packaged.targetPosition.value();
					ghostRenderer.render(
						appearance.defName,
						{target.x, target.y},
						*camera,
						viewportW,
						viewportH,
						true // Valid placement (already confirmed)
					);
				}
			}
		}
	}

} // namespace world_sim
