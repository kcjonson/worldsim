#include "PlacementSystem.h"

#include <assets/AssetRegistry.h>
#include <assets/RecipeRegistry.h>
#include <ecs/components/Appearance.h>
#include <ecs/components/Inventory.h>
#include <ecs/components/Packaged.h>
#include <ecs/components/StorageConfiguration.h>
#include <ecs/components/Transform.h>
#include <ecs/components/WorkQueue.h>
#include <primitives/Primitives.h>
#include <utils/Log.h>

namespace world_sim {

	PlacementSystem::PlacementSystem(const Args& args)
		: ecsWorld(args.world),
		  camera(args.camera),
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
				// Spawning new entity
				spawnEntity(defName, worldPos);
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
				auto& recipeRegistry = engine::assets::RecipeRegistry::Get();
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
		placementMode.updateGhostPosition({worldPos.x, worldPos.y});
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

	ecs::EntityID PlacementSystem::spawnEntity(const std::string& defName, Foundation::Vec2 worldPos) {
		if (ecsWorld == nullptr) {
			LOG_ERROR(Game, "PlacementSystem: no world set");
			return ecs::EntityID{0};
		}

		// Create ECS entity with components needed for rendering
		auto entity = ecsWorld->createEntity();

		ecsWorld->addComponent<ecs::Position>(entity, ecs::Position{{worldPos.x, worldPos.y}});
		ecsWorld->addComponent<ecs::Rotation>(entity, ecs::Rotation{0.0F});
		ecsWorld->addComponent<ecs::Appearance>(entity, ecs::Appearance{defName, 1.0F, {1.0F, 1.0F, 1.0F, 1.0F}});

		// Check if this is a crafting station or storage container
		auto&		assetRegistry = engine::assets::AssetRegistry::Get();
		const auto* assetDef = assetRegistry.getDefinition(defName);

		if (assetDef != nullptr && assetDef->capabilities.craftable.has_value()) {
			// Crafting station - add WorkQueue
			ecsWorld->addComponent<ecs::WorkQueue>(entity, ecs::WorkQueue{});
			LOG_INFO(Game, "Spawned station '%s' at (%.1f, %.1f) with WorkQueue", defName.c_str(), worldPos.x, worldPos.y);
		} else if (assetDef != nullptr && assetDef->capabilities.storage.has_value()) {
			// Storage container - add Inventory configured from StorageCapability
			const auto&	   storageCap = assetDef->capabilities.storage.value();
			ecs::Inventory inventory{};
			inventory.maxCapacity = storageCap.maxCapacity;
			inventory.maxStackSize = storageCap.maxStackSize;
			ecsWorld->addComponent<ecs::Inventory>(entity, inventory);

			// Add StorageConfiguration - default to accepting all categories the container supports
			// If acceptedCategories is empty (accepts all), use createAcceptEverything()
			// Otherwise, create config from the specific categories
			ecs::StorageConfiguration config;
			if (storageCap.acceptedCategories.empty()) {
				config = ecs::StorageConfiguration::createAcceptEverything();
			} else {
				config = ecs::StorageConfiguration::createAcceptAll(storageCap.acceptedCategories);
			}
			ecsWorld->addComponent<ecs::StorageConfiguration>(entity, config);

			LOG_INFO(
				Game,
				"Spawned storage '%s' at (%.1f, %.1f) with Inventory (capacity=%u) and %zu storage rules",
				defName.c_str(),
				worldPos.x,
				worldPos.y,
				storageCap.maxCapacity,
				config.getRuleCount()
			);
		} else {
			LOG_INFO(Game, "Spawned '%s' at (%.1f, %.1f)", defName.c_str(), worldPos.x, worldPos.y);
		}

		return entity;
	}

} // namespace world_sim
