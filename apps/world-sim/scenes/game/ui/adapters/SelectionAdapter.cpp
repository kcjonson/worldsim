#include "SelectionAdapter.h"

#include "scenes/game/ui/adapters/ColonistAdapter.h"

#include <assets/ConstructionRegistry.h>
#include <core/Vec2i64.h>
#include <ecs/InventoryMass.h>
#include <ecs/components/Colonist.h>
#include <ecs/components/Inventory.h>
#include <ecs/components/Mood.h>
#include <ecs/components/Needs.h>
#include <ecs/components/PlayerControlled.h>
#include <ecs/components/Room.h>
#include <ecs/components/StorageConfiguration.h>
#include <ecs/components/StructureBlueprint.h>
#include <ecs/components/Task.h>

#include <cmath>
#include <iomanip>
#include <sstream>

namespace world_sim {

	namespace {
		// Use ecs::kNeedLabels from Needs.h - single source of truth
		constexpr size_t kNeedCount = ecs::kNeedLabels.size();

		// Convert mood value (0-100) to descriptive label
		std::string moodToLabel(float moodValue) {
			if (moodValue >= 80.0F) {
				return "Happy";
			}
			if (moodValue >= 60.0F) {
				return "Content";
			}
			if (moodValue >= 40.0F) {
				return "Neutral";
			}
			if (moodValue >= 20.0F) {
				return "Stressed";
			}
			return "Miserable";
		}

		// Format task description
		std::string formatTask(const ecs::Task& task) {
			if (!task.isActive()) {
				return "No task";
			}

			if (!task.reason.empty()) {
				return task.reason;
			}

			// Fallback to task type name
			switch (task.type) {
				case ecs::TaskType::None:
					return "None";
				case ecs::TaskType::FulfillNeed:
					return "Fulfilling need";
				case ecs::TaskType::Craft:
					return "Crafting";
				case ecs::TaskType::Haul:
					return "Hauling";
				case ecs::TaskType::PlacePackaged:
					return "Placing";
				case ecs::TaskType::Wander:
					return "Wandering";
			}
			return "Unknown";
		}

	} // namespace

	std::optional<PanelContent> adaptSelection(
		const Selection&							   selection,
		const ecs::World&							   world,
		const engine::assets::AssetRegistry&		   registry,
		const ResourceQueryCallback&				   queryResources,
		const engine::construction::ConstructionWorld* constructionWorld,
		const std::function<void()>&				   onDemolish,
		const std::function<void()>&				   onDemolishWallSegment,
		const std::function<void()>&				   onDemolishOpening
	) {
		return std::visit(
			[&world, &registry, &queryResources, constructionWorld, &onDemolish, &onDemolishWallSegment, &onDemolishOpening](auto&& sel)
				-> std::optional<PanelContent> {
				using T = std::decay_t<decltype(sel)>;
				if constexpr (std::is_same_v<T, NoSelection>) {
					return std::nullopt;
				} else if constexpr (std::is_same_v<T, ColonistSelection>) {
					// Colonists need a non-const world (activity progress);
					// EntityInfoModel::refresh calls adaptColonistStatus directly.
					// This arm just keeps the visit exhaustive.
					return std::nullopt;
				} else if constexpr (std::is_same_v<T, WorldEntitySelection>) {
					return adaptWorldEntity(registry, sel, queryResources);
				} else if constexpr (std::is_same_v<T, FoundationSelection>) {
					if (constructionWorld == nullptr || constructionWorld->get(sel.id) == nullptr) {
						return std::nullopt;
					}
					return adaptFoundation(world, *constructionWorld, sel, onDemolish);
				} else if constexpr (std::is_same_v<T, WallSegmentSelection>) {
					if (constructionWorld == nullptr || constructionWorld->getSegment(sel.id) == nullptr) {
						return std::nullopt;
					}
					return adaptWallSegment(world, *constructionWorld, sel, onDemolishWallSegment);
				} else if constexpr (std::is_same_v<T, OpeningSelection>) {
					if (constructionWorld == nullptr || constructionWorld->getOpening(sel.id) == nullptr) {
						return std::nullopt;
					}
					return adaptOpening(world, *constructionWorld, sel, onDemolishOpening);
				} else if constexpr (std::is_same_v<T, CraftingStationSelection>) {
					// Validate entity still exists
					if (!world.isAlive(sel.entityId)) {
						return std::nullopt;
					}
					// For now, show basic station info - will be expanded with CraftingAdapter
					PanelContent content;
					content.title = sel.defName;
					content.slots.push_back(TextSlot{"Type", "Crafting Station"});
					content.slots.push_back(TextSlot{"Status", "Ready"});
					return content;
				} else if constexpr (std::is_same_v<T, FurnitureSelection>) {
					// Validate entity still exists
					if (!world.isAlive(sel.entityId)) {
						return std::nullopt;
					}
					return adaptFurniture(registry, sel);
				} else if constexpr (std::is_same_v<T, RoomSelection>) {
					// Rooms need the RoomDetectionSystem records (not available on this
					// path); EntityInfoModel::refresh resolves the record and calls
					// adaptRoom directly. This arm just keeps the visit exhaustive.
					return std::nullopt;
				}
			},
			selection
		);
	}

	PanelContent adaptColonistStatus(ecs::World& world, ecs::EntityID entityId) {
		PanelContent content;

		ColonistPanelData data;
		data.id = entityId;

		auto* colonist = world.getComponent<ecs::Colonist>(entityId);
		data.name = colonist ? colonist->name : "Colonist";
		content.title = data.name;

		if (auto* needs = world.getComponent<ecs::NeedsComponent>(entityId)) {
			data.moodValue = ecs::computeMood(*needs);
		} else {
			data.moodValue = 50.0F;
		}
		data.moodLabel = moodToLabel(data.moodValue);

		data.controlled = world.getComponent<ecs::PlayerControlled>(entityId) != nullptr;

		// Current task. Under direct player control the colonist's autonomous task is
		// suspended, so show "Controlled" rather than the cleared-task "Idle".
		if (data.controlled) {
			data.currentTask = "Controlled";
		} else if (auto* task = world.getComponent<ecs::Task>(entityId)) {
			data.currentTask = formatTask(*task);
		} else {
			data.currentTask = "Idle";
		}
		// Running action progress for the header task meter (blueprint-aware; <0 while
		// traveling or idle, which the meter renders as an empty track).
		data.taskProgress = adapters::getColonistActivity(world, entityId).progress;

		// Needs bars
		if (auto* needs = world.getComponent<ecs::NeedsComponent>(entityId)) {
			data.needs.reserve(kNeedCount);
			for (size_t i = 0; i < kNeedCount; ++i) {
				auto needType = static_cast<ecs::NeedType>(i);
				data.needs.push_back(
					ProgressBarSlot{
						.label = ecs::needLabel(needType), // Uses bounds-checked helper
						.value = needs->get(needType).value,
					}
				);
			}
		}

		// Gear: hands line, belt chips, backpack lines, carry mass
		auto* inventory = world.getComponent<ecs::Inventory>(entityId);
		if (inventory != nullptr) {
			const bool hasLeft = inventory->leftHand.has_value();
			const bool hasRight = inventory->rightHand.has_value();
			if (hasLeft && hasRight && inventory->leftHand->defName == inventory->rightHand->defName) {
				// Same item in both hands: a two-hand armful (counted once)
				data.hands = inventory->leftHand->defName;
				if (inventory->leftHand->quantity > 1) {
					data.hands += " x" + std::to_string(inventory->leftHand->quantity);
				}
				data.hands += " (both hands)";
			} else if (hasLeft || hasRight) {
				data.hands = "L: " + (hasLeft ? inventory->leftHand->defName : std::string{"--"}) +
							 "  R: " + (hasRight ? inventory->rightHand->defName : std::string{"--"});
			} else {
				data.hands = "(empty)";
			}

			for (const auto& slot : inventory->belt) {
				if (slot.has_value()) {
					data.belt.push_back(slot->defName);
				}
			}

			for (const auto& item : inventory->getAllItems()) {
				std::string line = item.defName;
				if (item.quantity > 1) {
					line += " x" + std::to_string(item.quantity);
				}
				data.backpack.push_back(std::move(line));
			}

			data.carriedKg = ecs::carriedCargoMassKg(*inventory, engine::assets::AssetRegistry::Get());
			data.capacityKg = inventory->carryCapacityKg;
		} else {
			data.hands = "(empty)";
		}

		content.colonist = std::move(data);
		return content;
	}

	PanelContent adaptWorldEntity(
		const engine::assets::AssetRegistry& registry,
		const WorldEntitySelection&			 selection,
		const ResourceQueryCallback&		 queryResources
	) {
		PanelContent content;
		content.title = selection.defName;

		const auto* def = registry.getDefinition(selection.defName);
		if (def == nullptr) {
			return content;
		}
		const auto& capabilities = def->capabilities;

		if (capabilities.harvestable.has_value()) {
			const auto& harvestable = capabilities.harvestable.value();
			std::optional<uint32_t> resourceCount;
			if (queryResources) {
				resourceCount = queryResources(selection.defName, selection.position);
			}
			if (resourceCount.has_value()) {
				// "X remaining (ItemName)" avoids naive pluralization issues
				content.subtitle =
					std::to_string(resourceCount.value()) + " remaining (" + harvestable.yieldDefName + ")";
				if (harvestable.totalResourceMax > 0) {
					content.slots.push_back(
						ProgressBarSlot{
							.label = "Resources",
							.value = (static_cast<float>(resourceCount.value()) /
									  static_cast<float>(harvestable.totalResourceMax)) *
									 100.0F,
						}
					);
				}
			} else {
				content.subtitle = "Harvestable";
			}
		} else if (capabilities.edible.has_value()) {
			content.subtitle = "Edible";
		} else if (capabilities.drinkable.has_value()) {
			content.subtitle = "Available";
		}

		return content;
	}

	PanelContent adaptFurniture(
		const engine::assets::AssetRegistry& registry,
		const FurnitureSelection&			 selection,
		const std::function<void()>&		 onPlace,
		const std::function<void()>&		 onMoveFurniture,
		const std::function<void()>&		 onConfigure
	) {
		PanelContent content;
		content.title = selection.defName;

		// Look up asset definition for properties
		const auto* def = registry.getDefinition(selection.defName);

		// Show type info
		if (selection.isPackaged) {
			content.slots.push_back(TextSlot{"Status", "Packaged (ready to place)"});
		} else {
			content.slots.push_back(TextSlot{"Status", "Placed"});
		}

		// Check if this is a storage container
		bool isStorage = (def != nullptr && def->capabilities.storage.has_value());

		// Show storage info if it's a storage container
		if (isStorage) {
			const auto&		   storage = def->capabilities.storage.value();
			std::ostringstream oss;
			oss << storage.maxCapacity << " slots";
			content.slots.push_back(TextSlot{"Capacity", oss.str()});
		}

		// Configure button for storage containers (only when placed)
		if (isStorage && !selection.isPackaged && onConfigure) {
			content.slots.push_back(
				ActionButtonSlot{
					.label = "Configure",
					.onClick = onConfigure,
				}
			);
		}

		// Place (packaged) vs Move (installed). Move re-packages the box in place and immediately
		// enters relocation, so the player picks a new spot and a colonist carries + reinstalls it.
		if (selection.isPackaged) {
			content.slots.push_back(
				ActionButtonSlot{
					.label = "Place",
					.onClick = onPlace,
				}
			);
		} else {
			content.slots.push_back(
				ActionButtonSlot{
					.label = "Move",
					.onClick = onMoveFurniture,
				}
			);
		}

		return content;
	}

	namespace {
		// "AwaitingMaterials" -> "Awaiting Materials" is overkill; the design panel
		// wants a state word, so map the build phase to a plain label.
		std::string buildPhaseLabel(ecs::StructureBlueprint::BuildPhase phase, bool demolishing) {
			if (demolishing) {
				return "Demolishing";
			}
			switch (phase) {
				case ecs::StructureBlueprint::BuildPhase::Clearing:
					return "Clearing site";
				case ecs::StructureBlueprint::BuildPhase::AwaitingMaterials:
					return "Awaiting materials";
				case ecs::StructureBlueprint::BuildPhase::UnderConstruction:
					return "Under construction";
				case ecs::StructureBlueprint::BuildPhase::Complete:
					return "Built";
			}
			return "Unknown";
		}

		// "2/5 Wood, 1/3 Stone" style summary of delivered vs required materials.
		std::string materialsSummary(const ecs::StructureBlueprint& blueprint) {
			if (blueprint.required.empty()) {
				return "none";
			}
			std::ostringstream oss;
			bool first = true;
			for (const auto& [defName, need] : blueprint.required) {
				uint32_t have = need - blueprint.remaining(defName); // clamps internally
				if (!first) {
					oss << ", ";
				}
				oss << have << "/" << need << " " << defName;
				first = false;
			}
			return oss.str();
		}
	} // namespace

	PanelContent adaptFoundation(
		const ecs::World&							   world,
		const engine::construction::ConstructionWorld& constructionWorld,
		const FoundationSelection&					   selection,
		const std::function<void()>&				   onDemolish,
		const std::function<void()>&				   onDemolishBuilding
	) {
		PanelContent content;

		const auto* foundation = constructionWorld.get(selection.id);
		const std::string material = (foundation != nullptr) ? foundation->material : std::string{"Foundation"};
		content.title = material + " Foundation";

		content.slots.push_back(TextSlot{"Material", material});

		std::ostringstream areaText;
		// "m\xC2\xB2" is UTF-8 for "m²" (matches ConstructionConfigStrip's readout).
		areaText << std::fixed << std::setprecision(1) << constructionWorld.areaSquareMeters(selection.id) << " m\xC2\xB2";
		content.slots.push_back(TextSlot{"Area", areaText.str()});

		const bool built =
			(foundation != nullptr && foundation->state == engine::construction::FoundationState::Built);

		// Build state + progress (phase, delivered/required materials, work bar) come
		// from the ECS mirror's blueprint and are only meaningful WHILE BUILDING. A
		// finished foundation shows none of them -- its title, material, and area say
		// what it is; State/Materials/Work would just be noise.
		const ecs::StructureBlueprint* blueprint =
			(foundation != nullptr) ? world.getComponent<ecs::StructureBlueprint>(foundation->entity) : nullptr;

		if (!built && blueprint != nullptr) {
			content.slots.push_back(TextSlot{"State", buildPhaseLabel(blueprint->phase, blueprint->demolishing)});
			content.slots.push_back(TextSlot{"Materials", materialsSummary(*blueprint)});
			content.slots.push_back(ProgressBarSlot{.label = "Work", .value = blueprint->displayProgress(blueprint->demolishing) * 100.0F});
		}

		// Demolish action. A foundation that still hosts walls can't be removed on
		// its own (the walls would be orphaned), so offer the cascade instead;
		// ActionButtonSlot has no disabled flag, so swap the button rather than
		// graying it out. A clear or blueprint foundation gets the plain Demolish.
		// Offer "Demolish building" (cascade) only when there are walls AND a cascade
		// callback is wired; otherwise the plain Demolish. The adaptSelection fallback
		// path passes no onDemolishBuilding, so guard against a dead (null-callback)
		// button there by falling back to plain Demolish.
		const bool hasWalls = constructionWorld.foundationHasWalls(selection.id);
		if (hasWalls && onDemolishBuilding) {
			content.slots.push_back(
				ActionButtonSlot{
					.label = "Demolish building",
					.onClick = onDemolishBuilding,
				}
			);
		} else {
			content.slots.push_back(
				ActionButtonSlot{
					.label = "Demolish",
					.onClick = onDemolish,
				}
			);
		}

		return content;
	}

	PanelContent adaptWallSegment(
		const ecs::World&								world,
		const engine::construction::ConstructionWorld& constructionWorld,
		const WallSegmentSelection&						selection,
		const std::function<void()>&					onDemolish
	) {
		PanelContent content;

		const auto* segment = constructionWorld.getSegment(selection.id);
		const std::string material = (segment != nullptr) ? segment->material : std::string{"Wall"};
		content.title = material + " Wall";

		content.slots.push_back(TextSlot{"Material", material});

		// Thickness: the topology stores the preset NAME; resolve the meters value
		// through the registry (same lookup the band render and hit-test use).
		if (segment != nullptr) {
			const auto* preset =
				engine::assets::ConstructionRegistry::Get().getThicknessPreset(segment->material, segment->thicknessPreset);
			std::ostringstream thicknessText;
			thicknessText << segment->thicknessPreset;
			if (preset != nullptr) {
				thicknessText << " (" << std::fixed << std::setprecision(2) << preset->thicknessMeters << " m)";
			}
			content.slots.push_back(TextSlot{"Thickness", thicknessText.str()});
		}

		// Length from the segment's two vertices (integer mm -> meters).
		const engine::construction::Vertex* v0 = (segment != nullptr) ? constructionWorld.getVertex(segment->v0) : nullptr;
		const engine::construction::Vertex* v1 = (segment != nullptr) ? constructionWorld.getVertex(segment->v1) : nullptr;
		if (v0 != nullptr && v1 != nullptr) {
			const double dx = static_cast<double>(v1->pos.x - v0->pos.x);
			const double dy = static_cast<double>(v1->pos.y - v0->pos.y);
			const double lengthMeters = std::sqrt(dx * dx + dy * dy) / 1000.0;
			std::ostringstream lengthText;
			lengthText << std::fixed << std::setprecision(2) << lengthMeters << " m";
			content.slots.push_back(TextSlot{"Length", lengthText.str()});
		}

		const bool built =
			(segment != nullptr && segment->state == engine::construction::FoundationState::Built);

		// Build state + progress (phase, materials, work bar) are only meaningful WHILE
		// BUILDING; a finished wall shows none of them (its material, thickness, and
		// length already say what it is). Same rule as the foundation panel.
		const ecs::StructureBlueprint* blueprint =
			(segment != nullptr) ? world.getComponent<ecs::StructureBlueprint>(segment->entity) : nullptr;

		if (!built && blueprint != nullptr) {
			content.slots.push_back(TextSlot{"State", buildPhaseLabel(blueprint->phase, blueprint->demolishing)});
			content.slots.push_back(TextSlot{"Materials", materialsSummary(*blueprint)});
			content.slots.push_back(ProgressBarSlot{.label = "Work", .value = blueprint->displayProgress(blueprint->demolishing) * 100.0F});
		}

		// Demolish action. Per-segment removal is the design's wall demolition unit;
		// GameScene's handler removes only this segment. Immediate, mirroring the
		// foundation precedent (see GameScene::handleDemolishWallSegment).
		content.slots.push_back(
			ActionButtonSlot{
				.label = "Demolish",
				.onClick = onDemolish,
			}
		);

		return content;
	}

	PanelContent adaptOpening(
		const ecs::World&							   world,
		const engine::construction::ConstructionWorld& constructionWorld,
		const OpeningSelection&						   selection,
		const std::function<void()>&				   onDemolish
	) {
		PanelContent content;

		const auto*		  opening = constructionWorld.getOpening(selection.id);
		const std::string typeName = (opening != nullptr) ? opening->type : std::string{"Opening"};
		content.title = typeName;

		content.slots.push_back(TextSlot{"Type", typeName});

		// Material and pathability come from the type def (the topology stores the
		// type NAME; resolve the rest through the registry, same as the renderer).
		const auto*		  type = (opening != nullptr) ? engine::assets::ConstructionRegistry::Get().getOpeningType(opening->type) : nullptr;
		const std::string material = (type != nullptr) ? type->material : (opening != nullptr ? opening->material : std::string{"--"});
		content.slots.push_back(TextSlot{"Material", material});
		if (type != nullptr) {
			content.slots.push_back(TextSlot{"Pathable", type->pathable ? "Yes" : "No"});
		}

		const bool built = (opening != nullptr && opening->state == engine::construction::FoundationState::Built);

		// Build state + progress come from the ECS mirror's blueprint, exactly like a
		// wall: a built opening has no blueprint progress; a blueprint shows phase, a
		// 0-100 work bar, and the delivered/required materials summary. Guard the entity
		// handle: getComponent indexes by entity index without a generation check, so a
		// kInvalidEntity (0) or stale handle would alias entity 0's components.
		const bool hasMirror = opening != nullptr && opening->entity != ecs::kInvalidEntity && world.isAlive(opening->entity);
		const ecs::StructureBlueprint* blueprint = hasMirror ? world.getComponent<ecs::StructureBlueprint>(opening->entity) : nullptr;

		// Progress (state, materials, work bar) only while building; a finished opening
		// shows none of it (type, material, pathable already describe it). Same rule as
		// the foundation/wall panels.
		if (!built && blueprint != nullptr) {
			content.slots.push_back(TextSlot{"State", buildPhaseLabel(blueprint->phase, blueprint->demolishing)});
			content.slots.push_back(TextSlot{"Materials", materialsSummary(*blueprint)});
			content.slots.push_back(ProgressBarSlot{.label = "Work", .value = blueprint->displayProgress(blueprint->demolishing) * 100.0F});
		}

		// Demolish action. The opening is its own demolition unit (independent of the
		// wall it sits on); GameScene's handler removes just this opening. Immediate,
		// mirroring the wall precedent (see GameScene::handleDemolishOpening).
		content.slots.push_back(
			ActionButtonSlot{
				.label = "Demolish",
				.onClick = onDemolish,
			}
		);

		return content;
	}

	PanelContent adaptRoom(const ecs::World& world, const ecs::RoomDetectionSystem::RoomRecord& record) {
		PanelContent content;
		content.title = record.name;

		content.slots.push_back(TextSlot{"Name", record.name});

		std::ostringstream areaText;
		// "m\xC2\xB2" is UTF-8 for "m²" (matches adaptFoundation's readout).
		areaText << std::fixed << std::setprecision(1) << record.area << " m\xC2\xB2";
		content.slots.push_back(TextSlot{"Area", areaText.str()});

		// Enclosing-wall count from the Room ECS component on the record's mirror
		// entity. The component carries the bounding segment ids (the record does
		// not), so this is the one fact sourced from the component rather than the
		// detection record. Guard the entity handle: a record whose entity has been
		// destroyed shows no count rather than aliasing entity 0's components.
		const ecs::Room* room =
			(record.entity != ecs::kInvalidEntity && world.isAlive(record.entity)) ? world.getComponent<ecs::Room>(record.entity) : nullptr;
		if (room != nullptr) {
			content.slots.push_back(TextSlot{"Enclosing walls", std::to_string(room->boundingSegmentIds.size())});
		}

		// Read-only: a room is demolished by removing its walls, not the room itself,
		// so there is no demolish action here.
		return content;
	}

} // namespace world_sim
