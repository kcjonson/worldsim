#include "SelectionAdapter.h"

#include <ecs/components/Action.h>
#include <ecs/components/Colonist.h>
#include <ecs/components/Needs.h>
#include <ecs/components/Task.h>

#include <iomanip>
#include <sstream>

namespace world_sim {

namespace {
	// Need labels matching NeedType order
	constexpr std::array<const char*, 4> kNeedLabels = {"Hunger", "Thirst", "Energy", "Bladder"};
	constexpr size_t					 kNeedCount = 4;

	// Visual spacing between need bars and status section
	constexpr float kStatusSectionSpacing = 8.0F;

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
	const Selection& selection,
	const ecs::World& world,
	const engine::assets::AssetRegistry& registry
) {
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
				return adaptColonist(world, sel.entityId);
			} else if constexpr (std::is_same_v<T, WorldEntitySelection>) {
				return adaptWorldEntity(registry, sel);
			}
		},
		selection
	);
}

PanelContent adaptColonist(const ecs::World& world, ecs::EntityID entityId) {
	PanelContent content;

	// Get colonist name for title
	if (auto* colonist = world.getComponent<ecs::Colonist>(entityId)) {
		content.title = colonist->name;
	} else {
		content.title = "Colonist";
	}

	// Add need bars
	if (auto* needs = world.getComponent<ecs::NeedsComponent>(entityId)) {
		for (size_t i = 0; i < kNeedCount; ++i) {
			auto needType = static_cast<ecs::NeedType>(i);
			content.slots.push_back(ProgressBarSlot{
				.label = kNeedLabels[i],
				.value = needs->get(needType).value,
			});
		}
	}

	// Add spacer before status
	content.slots.push_back(SpacerSlot{.height = kStatusSectionSpacing});

	// Add task status
	if (auto* task = world.getComponent<ecs::Task>(entityId)) {
		content.slots.push_back(TextSlot{
			.label = "Task",
			.value = formatTask(*task),
		});
	}

	// Add action status
	if (auto* action = world.getComponent<ecs::Action>(entityId)) {
		content.slots.push_back(TextSlot{
			.label = "Action",
			.value = formatAction(*action),
		});
	}

	return content;
}

PanelContent adaptWorldEntity(
	const engine::assets::AssetRegistry& registry,
	const WorldEntitySelection& selection
) {
	PanelContent content;
	content.title = selection.defName;

	// Add position
	content.slots.push_back(TextSlot{
		.label = "Position",
		.value = formatPosition(selection.position),
	});

	// Look up asset definition for capabilities
	const auto* def = registry.getDefinition(selection.defName);
	if (def == nullptr) {
		return content;
	}

	// Build capability list
	std::vector<std::string> caps;
	const auto&				 capabilities = def->capabilities;

	if (capabilities.edible.has_value()) {
		std::ostringstream oss;
		oss << "Edible (nutrition: " << std::fixed << std::setprecision(1) << capabilities.edible->nutrition << ")";
		caps.push_back(oss.str());
	}
	if (capabilities.drinkable.has_value()) {
		caps.push_back("Drinkable");
	}
	if (capabilities.sleepable.has_value()) {
		std::ostringstream oss;
		oss << "Sleepable (recovery: " << std::fixed << std::setprecision(1) << capabilities.sleepable->recoveryMultiplier
			<< "x)";
		caps.push_back(oss.str());
	}
	if (capabilities.toilet.has_value()) {
		caps.push_back("Toilet");
	}

	// Add capabilities list if any
	if (!caps.empty()) {
		content.slots.push_back(TextListSlot{
			.header = "Capabilities",
			.items = std::move(caps),
		});
	}

	return content;
}

} // namespace world_sim
