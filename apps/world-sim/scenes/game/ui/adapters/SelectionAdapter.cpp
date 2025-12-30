#include "SelectionAdapter.h"

#include <ecs/components/Action.h>
#include <ecs/components/Colonist.h>
#include <ecs/components/Inventory.h>
#include <ecs/components/Mood.h>
#include <ecs/components/Needs.h>
#include <ecs/components/Task.h>

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

	std::optional<PanelContent>
	adaptSelection(const Selection& selection, const ecs::World& world, const engine::assets::AssetRegistry& registry) {
		return std::visit(
			[&world, &registry](auto&& sel) -> std::optional<PanelContent> {
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
					return adaptWorldEntity(registry, sel);
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
		auto  items = inventory ? inventory->getAllItems() : std::vector<ecs::ItemStack>{};

		std::vector<std::string> gearItems;
		if (items.empty()) {
			gearItems.push_back("(empty)");
		} else {
			gearItems.reserve(items.size());
			for (const auto& item : items) {
				std::ostringstream oss;
				oss << item.defName;
				if (item.quantity > 1) {
					oss << " x" << item.quantity;
				}
				gearItems.push_back(oss.str());
			}
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

	PanelContent adaptWorldEntity(const engine::assets::AssetRegistry& registry, const WorldEntitySelection& selection) {
		PanelContent content;
		content.layout = PanelLayout::TwoColumn; // Same layout as colonists

		// HEADER: Same slot as colonist portrait - icon placeholder + name
		content.header.name = selection.defName;

		// NOTE: Resource bar shows placeholder values until entity component data is available.
		// Future: growth %, health %, remaining yield, stack quantity based on entity type.
		content.header.moodValue = 100.0F;
		content.header.moodLabel = "Full";

		// Look up asset definition for properties
		const auto* def = registry.getDefinition(selection.defName);
		if (def != nullptr) {
			const auto& capabilities = def->capabilities;

			// Show placeholder labels based on capabilities
			if (capabilities.edible.has_value()) {
				content.header.moodLabel = "Harvestable";
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
		const std::function<void()>&		 onPackage
	) {
		PanelContent content;
		content.layout = PanelLayout::SingleColumn;
		content.title = selection.defName;

		// Store callbacks for UI
		content.onPlace = onPlace;
		content.onPackage = onPackage;

		// Look up asset definition for properties
		const auto* def = registry.getDefinition(selection.defName);

		// Show type info
		if (selection.isPackaged) {
			content.slots.push_back(TextSlot{"Status", "Packaged (ready to place)"});
		} else {
			content.slots.push_back(TextSlot{"Status", "Placed"});
		}

		// Show storage info if it's a storage container
		if (def != nullptr && def->capabilities.storage.has_value()) {
			const auto&		   storage = def->capabilities.storage.value();
			std::ostringstream oss;
			oss << storage.maxCapacity << " slots";
			content.slots.push_back(TextSlot{"Capacity", oss.str()});
		}

		// Add action button based on state
		if (selection.isPackaged) {
			content.slots.push_back(SpacerSlot{.height = 8.0F});
			content.slots.push_back(
				ActionButtonSlot{
					.label = "Place",
					.onClick = onPlace,
				}
			);
		} else {
			content.slots.push_back(SpacerSlot{.height = 8.0F});
			content.slots.push_back(
				ActionButtonSlot{
					.label = "Package",
					.onClick = onPackage,
				}
			);
		}

		return content;
	}

} // namespace world_sim
