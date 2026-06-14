#include "SelectionAdapter.h"

#include <assets/ConstructionRegistry.h>
#include <core/Vec2i64.h>
#include <ecs/components/Action.h>
#include <ecs/components/Colonist.h>
#include <ecs/components/Inventory.h>
#include <ecs/components/Mood.h>
#include <ecs/components/Needs.h>
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

		// Format action description with progress
		std::string formatAction(const ecs::Action& action) {
			if (!action.isActive()) {
				return "Idle";
			}

			std::ostringstream oss;
			oss << ecs::actionTypeName(action.type);

			// Add progress percentage
			int progressPercent = static_cast<int>(action.progress() * 100.0F);
			oss << " (" << progressPercent << "%)";

			return oss.str();
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
				case ecs::TaskType::Gather:
					return "Gathering";
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

		// Format position for display
		std::string formatPosition(Foundation::Vec2 pos) {
			std::ostringstream oss;
			oss << std::fixed << std::setprecision(1);
			oss << "(" << pos.x << ", " << pos.y << ")";
			return oss.str();
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
					// Validate entity still exists
					if (!world.isAlive(sel.entityId)) {
						return std::nullopt;
					}
					return adaptColonistStatus(world, sel.entityId);
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
				}
			},
			selection
		);
	}

	PanelContent adaptColonistStatus(const ecs::World& world, ecs::EntityID entityId, const std::function<void()>& onDetails) {
		PanelContent content;
		content.layout = PanelLayout::TwoColumn;

		// Store onDetails callback for the Details button
		content.onDetails = onDetails;

		// HEADER: Portrait area with name and mood
		auto* colonist = world.getComponent<ecs::Colonist>(entityId);
		content.header.name = colonist ? colonist->name : "Colonist";

		// Get mood value and label
		float moodValue = 50.0F;
		if (auto* needs = world.getComponent<ecs::NeedsComponent>(entityId)) {
			moodValue = ecs::computeMood(*needs);
		}
		content.header.moodValue = moodValue;
		content.header.moodLabel = moodToLabel(moodValue);

		// LEFT COLUMN: Current task, Next task, Gear list
		// Current task
		std::string currentTask = "Idle";
		if (auto* task = world.getComponent<ecs::Task>(entityId)) {
			currentTask = formatTask(*task);
		}
		content.leftColumn.push_back(
			TextSlot{
				.label = "Current",
				.value = currentTask,
			}
		);

		// Next task (placeholder - would need task queue to implement properly)
		std::string nextTask = "--";
		if (auto* action = world.getComponent<ecs::Action>(entityId)) {
			if (action->isActive()) {
				nextTask = formatAction(*action);
			}
		}
		content.leftColumn.push_back(
			TextSlot{
				.label = "Next",
				.value = nextTask,
			}
		);

		// Gear list (from inventory) - always show, even if empty
		content.leftColumn.push_back(SpacerSlot{.height = 8.0F});

		auto* inventory = world.getComponent<ecs::Inventory>(entityId);

		std::vector<std::string> gearItems;

		// Hand items first (what colonist is holding)
		if (inventory != nullptr) {
			bool hasLeft = inventory->leftHand.has_value();
			bool hasRight = inventory->rightHand.has_value();

			if (hasLeft && hasRight && inventory->leftHand->defName == inventory->rightHand->defName) {
				// Same item in both hands (2-handed carry)
				gearItems.push_back("[Holding] " + inventory->leftHand->defName);
			} else if (hasLeft || hasRight) {
				if (hasLeft) {
					gearItems.push_back("[L] " + inventory->leftHand->defName);
				}
				if (hasRight) {
					gearItems.push_back("[R] " + inventory->rightHand->defName);
				}
			}
		}

		// Backpack items
		auto backpackItems = inventory ? inventory->getAllItems() : std::vector<ecs::ItemStack>{};
		for (const auto& item : backpackItems) {
			std::ostringstream oss;
			oss << item.defName;
			if (item.quantity > 1) {
				oss << " x" << item.quantity;
			}
			gearItems.push_back(oss.str());
		}

		// Show "(empty)" only if nothing in hands or backpack
		if (gearItems.empty()) {
			gearItems.push_back("(empty)");
		}
		content.leftColumn.push_back(
			TextListSlot{
				.header = "Gear",
				.items = std::move(gearItems),
			}
		);

		// RIGHT COLUMN: "Needs:" header + need bars
		// The "Needs:" header is rendered by the view, not as a slot
		if (auto* needs = world.getComponent<ecs::NeedsComponent>(entityId)) {
			for (size_t i = 0; i < kNeedCount; ++i) {
				auto needType = static_cast<ecs::NeedType>(i);
				content.rightColumn.push_back(
					ProgressBarSlot{
						.label = ecs::needLabel(needType), // Uses bounds-checked helper
						.value = needs->get(needType).value,
					}
				);
			}
		}

		return content;
	}

	PanelContent adaptWorldEntity(
		const engine::assets::AssetRegistry& registry,
		const WorldEntitySelection&			 selection,
		const ResourceQueryCallback&		 queryResources
	) {
		PanelContent content;
		content.layout = PanelLayout::TwoColumn; // Same layout as colonists

		// HEADER: Same slot as colonist portrait - icon placeholder + name
		content.header.name = selection.defName;

		// Default values
		content.header.moodValue = 100.0F;
		content.header.moodLabel = "Full";

		// Look up asset definition for properties
		const auto* def = registry.getDefinition(selection.defName);
		if (def != nullptr) {
			const auto& capabilities = def->capabilities;

			// Check if this is a harvestable entity with resource pool
			if (capabilities.harvestable.has_value()) {
				const auto& harvestable = capabilities.harvestable.value();

				// Try to get actual resource count
				if (queryResources) {
					auto resourceCount = queryResources(selection.defName, selection.position);
					if (resourceCount.has_value()) {
						// Show remaining count with yield item name
						// Use format "X remaining (ItemName)" to avoid naive pluralization issues
						std::string yieldName = harvestable.yieldDefName;
						content.header.moodLabel = std::to_string(resourceCount.value()) + " remaining (" + yieldName + ")";

						// Calculate percentage based on max possible resources
						uint32_t maxResources = harvestable.totalResourceMax;
						if (maxResources > 0) {
							content.header.moodValue =
								(static_cast<float>(resourceCount.value()) / static_cast<float>(maxResources)) * 100.0F;
						}
					} else {
						// No resource pool - just show as harvestable
						content.header.moodLabel = "Harvestable";
					}
				} else {
					// No callback - fallback to simple label
					content.header.moodLabel = "Harvestable";
				}
			} else if (capabilities.edible.has_value()) {
				content.header.moodLabel = "Edible";
			} else if (capabilities.drinkable.has_value()) {
				content.header.moodLabel = "Available";
			}
		}

		// LEFT/RIGHT COLUMNS: Empty for now (same height as colonist, just unused space)
		// Will be populated with entity-specific info in future updates

		return content;
	}

	PanelContent adaptFurniture(
		const engine::assets::AssetRegistry& registry,
		const FurnitureSelection&			 selection,
		const std::function<void()>&		 onPlace,
		const std::function<void()>&		 onPackage,
		const std::function<void()>&		 onConfigure
	) {
		PanelContent content;
		content.layout = PanelLayout::SingleColumn;
		content.title = selection.defName;

		// Store callbacks for UI
		content.onPlace = onPlace;
		content.onPackage = onPackage;
		content.onConfigure = onConfigure;

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

		// Add action buttons
		content.slots.push_back(SpacerSlot{.height = 8.0F});

		// Configure button for storage containers (only when placed)
		if (isStorage && !selection.isPackaged && onConfigure) {
			content.slots.push_back(
				ActionButtonSlot{
					.label = "Configure",
					.onClick = onConfigure,
				}
			);
		}

		// Place/Package button based on state
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
					.label = "Package",
					.onClick = onPackage,
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
		content.layout = PanelLayout::SingleColumn;

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

		// Build state + progress come from the ECS mirror's blueprint. A built
		// foundation has no blueprint progress to show; a blueprint shows phase,
		// a 0-100 work bar, and the delivered/required materials summary.
		const ecs::StructureBlueprint* blueprint =
			(foundation != nullptr) ? world.getComponent<ecs::StructureBlueprint>(foundation->entity) : nullptr;

		if (blueprint != nullptr) {
			content.slots.push_back(TextSlot{"State", buildPhaseLabel(blueprint->phase, blueprint->demolishing)});
			content.slots.push_back(TextSlot{"Materials", materialsSummary(*blueprint)});
			content.slots.push_back(ProgressBarSlot{.label = "Work", .value = blueprint->progress() * 100.0F});
		} else {
			content.slots.push_back(TextSlot{"State", built ? "Built" : "Blueprint"});
		}

		// Demolish action. A foundation that still hosts walls can't be removed on
		// its own (the walls would be orphaned), so offer the cascade instead;
		// ActionButtonSlot has no disabled flag, so swap the button rather than
		// graying it out. A clear or blueprint foundation gets the plain Demolish.
		const bool hasWalls = !constructionWorld.segmentsOnFoundation(selection.id).empty();
		content.slots.push_back(SpacerSlot{.height = 8.0F});
		if (hasWalls) {
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
		content.layout = PanelLayout::SingleColumn;

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

		// Build state + progress come from the ECS mirror's blueprint, exactly like a
		// foundation: a built wall has no blueprint progress; a blueprint shows phase,
		// a 0-100 work bar, and the delivered/required materials summary.
		const ecs::StructureBlueprint* blueprint =
			(segment != nullptr) ? world.getComponent<ecs::StructureBlueprint>(segment->entity) : nullptr;

		if (blueprint != nullptr) {
			content.slots.push_back(TextSlot{"State", buildPhaseLabel(blueprint->phase, blueprint->demolishing)});
			content.slots.push_back(TextSlot{"Materials", materialsSummary(*blueprint)});
			content.slots.push_back(ProgressBarSlot{.label = "Work", .value = blueprint->progress() * 100.0F});
		} else {
			content.slots.push_back(TextSlot{"State", built ? "Built" : "Blueprint"});
		}

		// Demolish action. Per-segment removal is the design's wall demolition unit;
		// GameScene's handler removes only this segment. Immediate, mirroring the
		// foundation precedent (see GameScene::handleDemolishWallSegment).
		content.slots.push_back(SpacerSlot{.height = 8.0F});
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
		content.layout = PanelLayout::SingleColumn;

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

		if (blueprint != nullptr) {
			content.slots.push_back(TextSlot{"State", buildPhaseLabel(blueprint->phase, blueprint->demolishing)});
			content.slots.push_back(TextSlot{"Materials", materialsSummary(*blueprint)});
			content.slots.push_back(ProgressBarSlot{.label = "Work", .value = blueprint->progress() * 100.0F});
		} else {
			content.slots.push_back(TextSlot{"State", built ? "Built" : "Blueprint"});
		}

		// Demolish action. The opening is its own demolition unit (independent of the
		// wall it sits on); GameScene's handler removes just this opening. Immediate,
		// mirroring the wall precedent (see GameScene::handleDemolishOpening).
		content.slots.push_back(SpacerSlot{.height = 8.0F});
		content.slots.push_back(
			ActionButtonSlot{
				.label = "Demolish",
				.onClick = onDemolish,
			}
		);

		return content;
	}

} // namespace world_sim
