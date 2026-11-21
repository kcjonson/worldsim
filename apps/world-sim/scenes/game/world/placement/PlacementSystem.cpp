#include "PlacementSystem.h"

#include <assets/AssetRegistry.h>
#include <assets/ConstructionRegistry.h>
#include <assets/PartsAsset.h>
#include <assets/RecipeRegistry.h>
#include <assets/TreeTraits.h>
#include <assets/TurnQuanta.h>
#include <assets/placement/InstanceTransform.h>
#include <assets/placement/PlacementExecutor.h>
#include <construction/ConstructionWorld.h>
#include <construction/ItemSnap.h>
#include <ecs/components/Appearance.h>
#include <ecs/components/Inventory.h>
#include <ecs/components/Packaged.h>
#include <ecs/components/StorageConfiguration.h>
#include <ecs/components/Transform.h>
#include <ecs/components/WorkQueue.h>
#include <ecs/systems/NavigationSystem.h>
#include <ecs/systems/PlayerVisibilitySystem.h>
#include <primitives/Primitives.h>
#include <utils/Log.h>

#include <glm/geometric.hpp>

#include <algorithm>
#include <bit>
#include <random>
#include <utility>

namespace world_sim {

	namespace {
		// The farthest point of `outlines` from their origin.
		float reachOf(const std::vector<std::vector<glm::vec2>>& outlines) {
			float reach = 0.0F;
			for (const std::vector<glm::vec2>& ring : outlines) {
				for (const glm::vec2 point : ring) {
					reach = std::max(reach, glm::length(point));
				}
			}
			return reach;
		}

		std::vector<std::vector<glm::vec2>> placedRings(const std::vector<std::vector<glm::vec2>>& outlines, const assets::InstanceTransform& placed) {
			std::vector<std::vector<glm::vec2>> rings;
			rings.reserve(outlines.size());
			for (const std::vector<glm::vec2>& outline : outlines) {
				std::vector<glm::vec2>& ring = rings.emplace_back();
				ring.reserve(outline.size());
				for (const glm::vec2 point : outline) {
					ring.push_back(placed.toWorld(point));
				}
			}
			return rings;
		}

		// `defName`'s footprint at `worldPos` turned `turnQuanta`, one ring each: a parts def's collider rings, another
		// def's collision outline (it never turns), or just `worldPos` when it has neither.
		std::vector<std::vector<glm::vec2>> footprintAt(const std::string& defName, Foundation::Vec2 worldPos, uint32_t turnQuanta) {
			std::vector<std::vector<glm::vec2>> rings;
			if (const auto* def = assets::AssetRegistry::Get().getDefinition(defName); def != nullptr) {
				if (def->parts != nullptr) {
					std::vector<std::vector<glm::vec2>> colliders;
					for (const assets::CollisionShape& collider : def->partsColliders) {
						colliders.push_back(collider.pointsMeters);
					}
					rings = placedRings(colliders, {{worldPos.x, worldPos.y}, assets::turnRadiansFromQuanta(turnQuanta), 1.0F});
				} else {
					const assets::InstanceTransform placed = assets::uprightInstance({worldPos.x, worldPos.y});
					const assets::CollisionShape&	footprint = def->collisionFor(0);
					if (footprint.type == assets::CollisionShapeType::Rect) {
						std::vector<glm::vec2>& ring = rings.emplace_back();
						for (const glm::vec2 corner : footprint.rectCornersLocal()) {
							ring.push_back(placed.toWorld(corner));
						}
					} else if (footprint.type == assets::CollisionShapeType::Polygon) {
						std::vector<glm::vec2>& ring = rings.emplace_back();
						for (const glm::vec2 point : footprint.pointsMeters) {
							ring.push_back(placed.toWorld(point));
						}
					}
				}
			}
			if (rings.empty()) {
				rings.push_back({{worldPos.x, worldPos.y}});
			}
			return rings;
		}
	} // namespace

	PlacementSystem::PlacementSystem(const Args& args)
		: ecsWorld(args.world),
		  camera(args.camera),
		  placementExecutor(args.placementExecutor),
		  navigation(args.navigation),
		  worldSeed(args.worldSeed),
		  callbacks(args.callbacks),
		  unbuiltSite(args.insideUnbuiltSite) {
		placementMode = PlacementMode{PlacementMode::Args{.onPlace = [this](const std::string& defName, Foundation::Vec2 worldPos, uint32_t turnQuanta) {
			lastTurns[defName] = turnQuanta;
			if (relocatingEntityId != ecs::EntityID{0}) {
				// Set target position on packaged item - colonist will carry it there
				auto* packaged = ecsWorld->getComponent<ecs::Packaged>(relocatingEntityId);
				if (packaged != nullptr) {
					packaged->targetPosition = glm::vec2{worldPos.x, worldPos.y};
					packaged->targetRotation = assets::turnRadiansFromQuanta(turnQuanta);
					LOG_INFO(
						Game,
						"Set placement target (%.1f, %.1f) turned %u quanta for entity %u - awaiting colonist delivery",
						worldPos.x,
						worldPos.y,
						turnQuanta,
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
				spawnEntity(defName, worldPos, turnQuanta);
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
		const auto last = lastTurns.find(defName);
		placementMode.selectItem(defName, turns(defName) && last != lastTurns.end() ? last->second : 0);
		dropPress();
		if (callbacks.onHideBuildMenu) {
			callbacks.onHideBuildMenu();
		}
		LOG_INFO(Game, "Selected '%s' for placement", defName.c_str());
	}

	void PlacementSystem::beginRelocation(ecs::EntityID entityId, const std::string& defName) {
		// The relocation takes over from whatever was being placed, so the item is never left waiting for a spot
		// with no ghost to pick one.
		cancel();
		relocatingEntityId = entityId;

		uint32_t turnQuanta = 0;
		if (const auto* rotation = ecsWorld->getComponent<ecs::Rotation>(entityId); rotation != nullptr && turns(defName)) {
			turnQuanta = assets::turnQuantaFromRadians(rotation->radians);
		}
		placementMode.selectItem(defName, turnQuanta);
		dropPress();
		LOG_INFO(Game, "Placing entity '%s' (entity %u)", defName.c_str(), static_cast<uint32_t>(entityId));
	}

	PlacementSystem::PointerState PlacementSystem::pointerState(glm::vec2 pointerMeters, bool freeform) const {
		PointerState state{.pointer = pointerMeters, .freeform = freeform, .pressed = press.has_value()};
		if (constructionWorld != nullptr) {
			state.constructionVersion = constructionWorld->version();
		}
		if (navigation != nullptr) {
			state.navigationGeneration = navigation->generation();
		}
		if (const ecs::PlayerVisibilitySystem* visibility = ecsWorld != nullptr ? ecs::PlayerVisibilitySystem::gate(*ecsWorld) : nullptr) {
			state.seenEpoch = visibility->epoch();
			state.revealed = visibility->revealed();
		}
		return state;
	}

	void PlacementSystem::handleMouseMove(float screenX, float screenY, int viewportW, int viewportH, bool freeform) {
		if (placementMode.state() != PlacementState::Placing || camera == nullptr) {
			return;
		}

		const auto		pointer = camera->screenToWorld(screenX, screenY, viewportW, viewportH, kPixelsPerMeter);
		const glm::vec2 pointerMeters{pointer.x, pointer.y};
		// The preview follows the pointer every frame; with the pointer, Alt, the press and the world as they were,
		// nothing moved.
		const PointerState now = pointerState(pointerMeters, freeform);
		if (lastMove == now && ++movesSinceSettle < kItemRefreshMoves) {
			return;
		}
		lastMove = now;
		movesSinceSettle = 0;
		uint32_t turnQuanta = placementMode.ghostTurnQuanta();
		if (!press.has_value()) {
			rawPosition = {pointer.x, pointer.y};
		} else {
			const float clickDistance = assets::ConstructionRegistry::Get().snapping().doubleClickDistancePx;
			press->turning = press->turning || glm::distance(glm::vec2{screenX, screenY}, press->screen) > clickDistance;
			if (press->turning) {
				const std::optional<uint32_t> alignment =
					constructionWorld != nullptr
						? construction::itemAlignmentQuanta(*constructionWorld, press->pivot, assets::ConstructionRegistry::Get().snapping())
						: std::nullopt;
				turnQuanta = construction::snapItemTurn(
					construction::turnDegreesToward(press->pivot, {pointer.x, pointer.y}), alignment, assets::ConstructionRegistry::Get().snapping(),
					freeform
				);
			}
		}
		settleGhost(turnQuanta);
	}

	bool PlacementSystem::handlePress(float screenX, float screenY, int viewportW, int viewportH) {
		if (placementMode.state() != PlacementState::Placing || camera == nullptr || !turns(placementMode.selectedDefName())) {
			return false;
		}
		const auto pointer = camera->screenToWorld(screenX, screenY, viewportW, viewportH, kPixelsPerMeter);
		press = Press{.pivot = {pointer.x, pointer.y}, .screen = {screenX, screenY}};
		rawPosition = press->pivot;
		settleGhost(placementMode.ghostTurnQuanta());
		return true;
	}

	bool PlacementSystem::handleTurnKey(int direction, bool freeform) {
		if (placementMode.state() != PlacementState::Placing || !turns(placementMode.selectedDefName())) {
			return false;
		}
		const std::optional<uint32_t> alignment =
			constructionWorld != nullptr
				? construction::itemAlignmentQuanta(*constructionWorld, rawPosition, assets::ConstructionRegistry::Get().snapping())
				: std::nullopt;
		settleGhost(construction::stepItemTurn(
			placementMode.ghostTurnQuanta(), direction, alignment, assets::ConstructionRegistry::Get().snapping(), freeform
		));
		return true;
	}

	const std::vector<std::vector<glm::vec2>>& PlacementSystem::reservedOutlinesOf(const std::string& defName) const {
		const auto known = reservedOutlines.find(defName);
		if (known != reservedOutlines.end()) {
			return known->second;
		}
		std::vector<std::vector<glm::vec2>> outlines;
		if (const auto* def = assets::AssetRegistry::Get().getDefinition(defName); def != nullptr && def->parts != nullptr) {
			for (const assets::AreaPolygon& polygon : assets::reservedRegion(*def)) {
				outlines.push_back(polygon.outer);
			}
		}
		return reservedOutlines.emplace(defName, std::move(outlines)).first->second;
	}

	std::vector<std::vector<glm::vec2>> PlacementSystem::otherReservedRings(glm::vec2 pivot, float reachMeters) const {
		std::vector<std::vector<glm::vec2>> rings;
		if (ecsWorld == nullptr) {
			return rings;
		}
		// Every entity is walked, the parts items told by their def's reserved outlines (empty for any other def),
		// read from the cache by name. The placed item spatial query (PlacedItemIndex::query, rotatable-placed-items
		// 11.3: installed and pending items whose reserved region meets a box) answers this from a chunk lookup and
		// replaces the walk.
		for (auto [entity, position, rotation, appearance] : ecsWorld->view<ecs::Position, ecs::Rotation, ecs::Appearance>()) {
			if (entity == relocatingEntityId) {
				continue;
			}
			const std::vector<std::vector<glm::vec2>>& outlines = reservedOutlinesOf(appearance.defName);
			if (outlines.empty()) {
				continue;
			}
			glm::vec2 at = position.value;
			float	  turn = rotation.radians;
			if (const auto* packaged = ecsWorld->getComponent<ecs::Packaged>(entity); packaged != nullptr) {
				// A crate waiting to be placed stands nowhere yet; one with a target stands at its target.
				if (!packaged->targetPosition.has_value()) {
					continue;
				}
				at = *packaged->targetPosition;
				turn = packaged->targetRotation.value_or(turn);
			}
			if (glm::distance(at, pivot) > reachMeters + reachOf(outlines)) {
				continue;
			}
			for (std::vector<glm::vec2>& ring : placedRings(outlines, assets::InstanceTransform(at, turn, 1.0F))) {
				rings.push_back(std::move(ring));
			}
		}
		return rings;
	}

	void PlacementSystem::settleGhost(uint32_t turnQuanta) {
		if (const uint64_t generation = assets::AssetRegistry::Get().generation(); generation != outlinesGeneration) {
			reservedOutlines.clear();
			outlinesGeneration = generation;
		}
		const std::string& defName = placementMode.selectedDefName();
		glm::vec2		   position = rawPosition;
		if (turns(defName)) {
			const std::vector<std::vector<glm::vec2>>& outlines = reservedOutlinesOf(defName);
			if (!outlines.empty()) {
				const assets::SnappingConfig& snapping = assets::ConstructionRegistry::Get().snapping();
				const float					  reach = reachOf(outlines) + snapping.edgeSnapRadiusMeters;
				std::vector<construction::FlushFace> faces;
				if (constructionWorld != nullptr) {
					faces = construction::wallFaces(*constructionWorld, rawPosition, reach);
				}
				for (const std::vector<glm::vec2>& ring : otherReservedRings(rawPosition, reach)) {
					construction::appendRingFaces(ring, faces);
				}
				const assets::InstanceTransform placed(rawPosition, assets::turnRadiansFromQuanta(turnQuanta), 1.0F);
				position += construction::flushSlide(placedRings(outlines, placed), faces, snapping);
			}
		}
		placementMode.updateGhost({position.x, position.y}, turnQuanta, placeable({position.x, position.y}, turnQuanta));
	}

	bool PlacementSystem::insideUnbuiltSite(Foundation::Vec2 worldPos) const {
		return unbuiltSite && unbuiltSite({worldPos.x, worldPos.y});
	}

	bool PlacementSystem::footprintBuildableNow(const std::string& defName, Foundation::Vec2 worldPos, uint32_t turnQuanta) const {
		if (navigation == nullptr || insideUnbuiltSite(worldPos)) {
			return false;
		}
		return std::ranges::all_of(footprintAt(defName, worldPos, turnQuanta), [this](const std::vector<glm::vec2>& ring) {
			return navigation->isAreaBuildableNow(ring);
		});
	}

	std::optional<glm::vec2> PlacementSystem::installTargetNear(const std::string& defName, glm::vec2 stationPos, glm::vec2 aim) const {
		if (navigation == nullptr) {
			return std::nullopt;
		}
		return navigation->findValidPositionNear(stationPos, 1.0F, aim, [&](glm::vec2 candidate) {
			return footprintBuildableNow(defName, {candidate.x, candidate.y}, 0);
		});
	}

	bool PlacementSystem::isFurniture(const std::string& defName) {
		const auto* def = assets::AssetRegistry::Get().getDefinition(defName);
		return def != nullptr && (def->capabilities.craftable.has_value() || def->capabilities.storage.has_value());
	}

	bool PlacementSystem::turns(const std::string& defName) {
		const auto* def = assets::AssetRegistry::Get().getDefinition(defName);
		return def != nullptr && def->parts != nullptr;
	}

	bool PlacementSystem::placeable(Foundation::Vec2 worldPos, uint32_t turnQuanta) const {
		const std::string& defName = placementMode.selectedDefName();
		if (!footprintBuildableNow(defName, worldPos, turnQuanta)) {
			return false;
		}
		const ecs::PlayerVisibilitySystem* visibility = ecsWorld != nullptr ? ecs::PlayerVisibilitySystem::gate(*ecsWorld) : nullptr;
		return visibility == nullptr || std::ranges::all_of(footprintAt(defName, worldPos, turnQuanta), [visibility](const std::vector<glm::vec2>& ring) {
				   return visibility->covers(ring);
			   });
	}

	bool PlacementSystem::handleRelease() {
		if (placementMode.state() != PlacementState::Placing) {
			return false;
		}

		// The record can have moved since the last mouse move; refuse on what is seen now.
		settleGhost(placementMode.ghostTurnQuanta());
		press.reset();
		lastMove.reset();
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

	void PlacementSystem::dropPress() {
		press.reset();
		lastMove.reset();
	}

	bool PlacementSystem::takeReleaseOfCancelledPress() {
		return std::exchange(cancelledPress, false);
	}

	void PlacementSystem::leftButtonUp() {
		if (press.has_value()) {
			dropPress();
		}
		cancelledPress = false;
	}

	void PlacementSystem::cancel() {
		if (!placementMode.isActive()) {
			return;
		}

		placementMode.cancel();
		relocatingEntityId = ecs::EntityID{0};
		cancelledPress = press.has_value();
		dropPress();

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

		ghostRenderer.beginFrame();
		// Render active placement ghost (during placement mode)
		if (placementMode.state() == PlacementState::Placing) {
			ghostRenderer.render(
				placementMode.selectedDefName(), placementMode.ghostPosition(), placementMode.ghostTurnQuanta(), *camera, viewportW, viewportH,
				placementMode.isValidPlacement()
			);
		}

		// Render ghosts for all packaged items awaiting colonist delivery, at their target's turn
		// (Skip items already being carried - they'll be placed shortly)
		if (ecsWorld != nullptr) {
			for (auto [entity, packaged, appearance] : ecsWorld->view<ecs::Packaged, ecs::Appearance>()) {
				if (packaged.targetPosition.has_value() && !packaged.beingCarried) {
					const auto& target = packaged.targetPosition.value();
					const auto* rotation = ecsWorld->getComponent<ecs::Rotation>(entity);
					const float turn = packaged.targetRotation.value_or(rotation != nullptr ? rotation->radians : 0.0F);
					ghostRenderer.render(
						appearance.defName,
						{target.x, target.y},
						assets::turnQuantaFromRadians(turn),
						*camera,
						viewportW,
						viewportH,
						true // Valid placement (already confirmed)
					);
				}
			}
		}
		ghostRenderer.endFrame();
	}

	ecs::EntityID PlacementSystem::spawnEntity(const std::string& defName, Foundation::Vec2 worldPos, uint32_t turnQuanta) {
		if (ecsWorld == nullptr) {
			LOG_ERROR(Game, "PlacementSystem: no world set");
			return ecs::EntityID{0};
		}

		// Create ECS entity with components needed for rendering
		auto entity = ecsWorld->createEntity();

		ecsWorld->addComponent<ecs::Position>(entity, ecs::Position{{worldPos.x, worldPos.y}});
		ecsWorld->addComponent<ecs::Rotation>(entity, ecs::Rotation{assets::turnRadiansFromQuanta(turnQuanta)});
		auto&		assetRegistry = assets::AssetRegistry::Get();
		const auto* assetDef = assetRegistry.getDefinition(defName);
		ecs::Appearance appearance{defName, 1.0F, {1.0F, 1.0F, 1.0F, 1.0F}};
		if (assetDef != nullptr) {
			appearance.traits = assets::computeTreeTraits(*assetDef, {worldPos.x, worldPos.y}, worldSeed);
		}
		ecsWorld->addComponent<ecs::Appearance>(entity, std::move(appearance));

		// Check if this is a crafting station or storage container

		if (assetDef != nullptr && assetDef->capabilities.craftable.has_value()) {
			// Crafting station - add WorkQueue plus a material store Inventory. Colonists haul
			// recipe inputs INTO this store during provisioning (like a build site holds its
			// delivered materials), and the Craft action consumes them from here.
			ecsWorld->addComponent<ecs::WorkQueue>(entity, ecs::WorkQueue{});
			ecsWorld->addComponent<ecs::Inventory>(entity, ecs::Inventory::createForStorage());
			LOG_INFO(Game, "Spawned station '%s' at (%.1f, %.1f) with WorkQueue and material store", defName.c_str(), worldPos.x, worldPos.y);
		} else if (assetDef != nullptr && assetDef->capabilities.storage.has_value()) {
			// Storage container - add Inventory configured from StorageCapability
			const auto&	   storageCap = assetDef->capabilities.storage.value();
			ecs::Inventory inventory{};
			inventory.maxCapacity = storageCap.maxCapacity; // slot count; per-stack cap is each item's own stackSize
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

		// Harvestables with a resource pool need that pool seeded, exactly like the chunk
		// placement path does (PlacementExecutor::storeChunkResult). Without this a dev- or
		// tool-placed tree has no wood to give and a chop withdraws nothing.
		if (placementExecutor != nullptr && assetDef != nullptr && assetDef->capabilities.harvestable.has_value()) {
			const auto& harv = assetDef->capabilities.harvestable.value();
			if (harv.totalResourceMin > 0 && harv.totalResourceMax > 0) {
				// Position-seeded so a given spot rolls the same amount across runs, without a
				// shared RNG. Mix the raw IEEE-754 bits (std::hash<float> is implementation-defined
				// and not stable across platforms); a deterministic bit mix keeps it reproducible.
				const uint32_t bx = std::bit_cast<uint32_t>(worldPos.x);
				const uint32_t by = std::bit_cast<uint32_t>(worldPos.y);
				const uint32_t seed = (bx * 0x9E3779B9U) ^ (by + 0x85EBCA6BU + (bx << 6) + (bx >> 2));
				std::mt19937   rng(seed);
				std::uniform_int_distribution<uint32_t> dist(harv.totalResourceMin, harv.totalResourceMax);
				const uint32_t initialCount = dist(rng);
				placementExecutor->initResourceCount({worldPos.x, worldPos.y}, defName, initialCount);
			}
		}

		return entity;
	}

} // namespace world_sim
