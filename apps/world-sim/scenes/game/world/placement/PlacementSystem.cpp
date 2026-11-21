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

		// A non-parts def's collision outline at `worldPos` (it never turns); nothing when it has none.
		std::optional<std::vector<glm::vec2>> collisionOutlineAt(const assets::AssetDefinition& def, Foundation::Vec2 worldPos) {
			const assets::InstanceTransform placed = assets::uprightInstance({worldPos.x, worldPos.y});
			const assets::CollisionShape&	footprint = def.collisionFor(0);
			std::vector<glm::vec2>			ring;
			if (footprint.type == assets::CollisionShapeType::Rect) {
				for (const glm::vec2 corner : footprint.rectCornersLocal()) {
					ring.push_back(placed.toWorld(corner));
				}
			} else if (footprint.type == assets::CollisionShapeType::Polygon) {
				for (const glm::vec2 point : footprint.pointsMeters) {
					ring.push_back(placed.toWorld(point));
				}
			}
			return ring.empty() ? std::nullopt : std::optional{std::move(ring)};
		}
	} // namespace

	const ecs::PlacedItemIndex* PlacementSystem::placedItems() const {
		const ecs::PlacedItemSystem* system = ecsWorld != nullptr ? ecsWorld->tryGetSystem<ecs::PlacedItemSystem>() : nullptr;
		return system != nullptr ? &system->items() : nullptr;
	}

	const std::vector<std::vector<glm::vec2>>& PlacementSystem::wallRings() const {
		const std::uint64_t version = constructionWorld != nullptr ? constructionWorld->version() + 1 : 0;
		if (version != wallRingsVersion) {
			wallRingsCache = constructionWorld != nullptr ? construction::wallOutlines(*constructionWorld) : std::vector<std::vector<glm::vec2>>{};
			wallRingsVersion = version;
		}
		return wallRingsCache;
	}

	std::vector<std::vector<glm::vec2>> PlacementSystem::footprintAt(const std::string& defName, Foundation::Vec2 worldPos, uint32_t turnQuanta) const {
		std::vector<std::vector<glm::vec2>> rings = outlinesAt(defName, worldPos, turnQuanta).collider;
		if (rings.empty()) {
			rings.push_back({{worldPos.x, worldPos.y}});
		}
		return rings;
	}

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
		if (const ecs::PlacedItemIndex* items = placedItems(); items != nullptr) {
			state.placedItemsRevision = items->revision();
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
		if (lastMove == now) {
			return;
		}
		lastMove = now;
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

	void PlacementSystem::settleGhost(uint32_t turnQuanta) {
		const std::string&			defName = placementMode.selectedDefName();
		const ecs::PlacedItemIndex* items = placedItems();
		glm::vec2					position = rawPosition;
		if (turns(defName) && items != nullptr) {
			const std::vector<std::vector<glm::vec2>>& outlines = items->outlinesOf(defName, assets::AssetRegistry::Get()).reserved;
			if (!outlines.empty()) {
				const assets::SnappingConfig& snapping = assets::ConstructionRegistry::Get().snapping();
				const float					  reach = reachOf(outlines) + snapping.edgeSnapRadiusMeters;
				std::vector<construction::FlushFace> faces;
				if (constructionWorld != nullptr) {
					faces = construction::wallFaces(*constructionWorld, rawPosition, reach);
				}
				std::vector<const ecs::PlacedItemIndex::Entry*> near;
				items->query(rawPosition - glm::vec2{reach, reach}, rawPosition + glm::vec2{reach, reach}, near);
				for (const ecs::PlacedItemIndex::Entry* other : near) {
					if (relocatingEntityId != ecs::EntityID{0} && other->entity == relocatingEntityId) {
						continue;
					}
					for (const std::vector<glm::vec2>& ring : other->world.reserved) {
						construction::appendRingFaces(ring, faces);
					}
				}
				const ecs::ItemOutlines held = ecs::placedOutlines({.reserved = outlines}, rawPosition, assets::turnRadiansFromQuanta(turnQuanta));
				position += construction::flushSlide(held.reserved, faces, snapping);
			}
		}
		refusal = refusalAt(defName, {position.x, position.y}, turnQuanta, relocatingEntityId, PlacementRules::Player);
		// The slide is a help: one that would take the ghost from a spot it can stand on to one it can't is not taken.
		if (refusal.has_value() && position != rawPosition) {
			if (const std::optional<construction::PlacementRefusal> unslid = refusalAt(defName, {rawPosition.x, rawPosition.y}, turnQuanta, relocatingEntityId, PlacementRules::Player);
				!unslid.has_value()) {
				position = rawPosition;
				refusal.reset();
			}
		}
		placementMode.updateGhost({position.x, position.y}, turnQuanta, !refusal.has_value());
	}

	bool PlacementSystem::insideUnbuiltSite(Foundation::Vec2 worldPos) const {
		return unbuiltSite && unbuiltSite({worldPos.x, worldPos.y});
	}

	bool PlacementSystem::footprintBuildableNow(const std::string& defName, Foundation::Vec2 worldPos, uint32_t turnQuanta) const {
		if (navigation == nullptr || insideUnbuiltSite(worldPos)) {
			return false;
		}
		// Taken in by the flush tolerance: an item may stand flush against another, or against a wall.
		return std::ranges::all_of(footprintAt(defName, worldPos, turnQuanta), [this](const std::vector<glm::vec2>& ring) {
			return navigation->isAreaBuildableNow(construction::takenIn(ring));
		});
	}

	std::optional<glm::vec2> PlacementSystem::installTargetNear(const std::string& defName, glm::vec2 stationPos, glm::vec2 aim) {
		if (navigation == nullptr) {
			return std::nullopt;
		}
		const std::optional<glm::vec2> target = navigation->findValidPositionNear(stationPos, 1.0F, aim, [&](glm::vec2 candidate) {
			return !refusalAt(defName, {candidate.x, candidate.y}, 0, ecs::kInvalidEntity, PlacementRules::Player).has_value();
		});
		// Its crate is spawned later in the tick: held until then, so the next craft finishing this tick keeps off it.
		if (target.has_value()) {
			if (ecs::PlacedItemSystem* system = ecsWorld != nullptr ? ecsWorld->tryGetSystem<ecs::PlacedItemSystem>() : nullptr) {
				system->items().hold(defName, *target, 0.0F, assets::AssetRegistry::Get());
			}
		}
		return target;
	}

	ecs::ItemOutlines PlacementSystem::outlinesAt(const std::string& defName, Foundation::Vec2 worldPos, uint32_t turnQuanta) const {
		const assets::AssetRegistry&   registry = assets::AssetRegistry::Get();
		const assets::AssetDefinition* def = registry.getDefinition(defName);
		if (def == nullptr) {
			return {};
		}
		if (def->parts != nullptr) {
			const ecs::PlacedItemIndex* items = placedItems();
			return ecs::placedOutlines(
				items != nullptr ? items->outlinesOf(defName, registry) : ecs::itemOutlines(*def), {worldPos.x, worldPos.y}, assets::turnRadiansFromQuanta(turnQuanta)
			);
		}
		ecs::ItemOutlines outlines;
		if (std::optional<std::vector<glm::vec2>> outline = collisionOutlineAt(*def, worldPos)) {
			outlines.collider.push_back(*outline);
			outlines.reserved.push_back(std::move(*outline));
		}
		return outlines;
	}

	std::optional<construction::PlacementRefusal>
	PlacementSystem::refusalAt(const std::string& defName, Foundation::Vec2 worldPos, uint32_t turnQuanta, ecs::EntityID ignore, PlacementRules rules) const {
		const ecs::PlacedItemIndex* items = placedItems();
		const ecs::ItemOutlines		held = outlinesAt(defName, worldPos, turnQuanta);
		if (items != nullptr) {
			if (std::optional<construction::PlacementRefusal> body = construction::bodyRefusal(*items, wallRings(), held, ignore)) {
				return body;
			}
		}
		if (!footprintBuildableNow(defName, worldPos, turnQuanta)) {
			return construction::PlacementRefusal::NotBuildableGround;
		}
		const ecs::PlayerVisibilitySystem* visibility =
			rules == PlacementRules::Player && ecsWorld != nullptr ? ecs::PlayerVisibilitySystem::gate(*ecsWorld) : nullptr;
		if (visibility != nullptr && !std::ranges::all_of(footprintAt(defName, worldPos, turnQuanta), [visibility](const std::vector<glm::vec2>& ring) {
				return visibility->covers(ring);
			})) {
			return construction::PlacementRefusal::Unexplored;
		}
		return items != nullptr ? construction::clearanceRefusal(*items, wallRings(), held, ignore) : std::nullopt;
	}

	bool PlacementSystem::isFurniture(const std::string& defName) {
		const auto* def = assets::AssetRegistry::Get().getDefinition(defName);
		return def != nullptr && (def->capabilities.craftable.has_value() || def->capabilities.storage.has_value());
	}

	bool PlacementSystem::turns(const std::string& defName) {
		const auto* def = assets::AssetRegistry::Get().getDefinition(defName);
		return def != nullptr && def->parts != nullptr;
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
		refusal.reset();
		cancelledPress = press.has_value();
		dropPress();

		if (callbacks.onBuildMenuVisibility) {
			callbacks.onBuildMenuVisibility(false);
		}
		if (callbacks.onHideBuildMenu) {
			callbacks.onHideBuildMenu();
		}
	}

	void PlacementSystem::render(int viewportW, int viewportH, float uiScale) {
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
			if (refusal.has_value()) {
				ghostRenderer.renderRefusal(construction::placementRefusalLabel(*refusal), placementMode.ghostPosition(), *camera, viewportW, viewportH, uiScale);
			}
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
