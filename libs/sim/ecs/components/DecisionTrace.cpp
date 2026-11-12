#include "DecisionTrace.h"

#include "../World.h"
#include "Appearance.h"
#include "Colonist.h"

#include "assets/AssetRegistry.h"
#include "assets/RecipeRegistry.h"

#include <format>

namespace ecs {

	namespace {

		const char* needName(NeedType need) {
			switch (need) {
				case NeedType::Hunger:
					return "Hunger";
				case NeedType::Thirst:
					return "Thirst";
				case NeedType::Energy:
					return "Energy";
				case NeedType::Bladder:
					return "Bladder";
				case NeedType::Digestion:
					return "Digestion";
				case NeedType::Hygiene:
				case NeedType::Recreation:
				case NeedType::Temperature:
				case NeedType::Count:
					break;
			}
			return "Unknown";
		}

		/// A definition's label, or its name when it has none.
		const std::string& labelOf(const assets::AssetRegistry& registry, uint32_t defNameId) {
			const auto* def = registry.getDefinitionById(defNameId);
			return def != nullptr && !def->label.empty() ? def->label : registry.getDefName(defNameId);
		}

		/// Who holds a Reserved option's target, by name.
		std::string holderName(const World& world, const EvaluatedOption& option) {
			const auto* colonist = world.getComponent<Colonist>(option.reservedBy);
			return colonist != nullptr && !colonist->name.empty() ? colonist->name : "another colonist";
		}

		std::string describeNeed(const World& world, const EvaluatedOption& option) {
			std::string text = std::format("{} at {}%", needName(option.needType), static_cast<int>(option.needValue));
			if (option.status == OptionStatus::NoSource && !option.targetPosition.has_value()) {
				text += " (no known source)";
			} else if (option.status == OptionStatus::Regrowing) {
				text += " (source regrowing)";
			} else if (option.status == OptionStatus::SearchGaveUp) {
				text += std::format("{} (gave up searching for a way there)", option.needValue < 10.0F ? " (critical)" : "");
			} else if (option.status == OptionStatus::Reserved) {
				text += std::format("{} (reserved by {})", option.needValue < 10.0F ? " (critical)" : "", holderName(world, option));
			} else if (option.needValue < 10.0F) {
				text += " (critical)";
			} else if (option.status == OptionStatus::Satisfied) {
				text += " (satisfied)";
			} else if (option.distanceToTarget > 0.0F) {
				text += std::format(" ({}m away)", static_cast<int>(option.distanceToTarget));
			} else if (option.targetPosition.has_value()) {
				text += " (using ground)";
			}
			const bool actionable = option.status == OptionStatus::Available || option.status == OptionStatus::Selected;
			if (actionable && *option.reason != '\0') {
				text += std::format(" ({})", option.reason);
			}
			return text;
		}

		std::string describeBase(const World& world, const EvaluatedOption& option) {
			const auto& registry = assets::AssetRegistry::Get();
			switch (option.taskType) {
				case TaskType::FulfillNeed:
					return option.gathersFood ? std::string(option.reason) : describeNeed(world, option);
				case TaskType::Haul:
					return std::vformat(option.reason, std::make_format_args(registry.getDefName(option.haulItemDefNameId)));
				case TaskType::Harvest: {
					const std::string& source = labelOf(registry, option.targetDefNameId.value_or(0));
					const std::string& yield = labelOf(registry, option.harvestYieldDefNameId);
					std::string		   text = std::vformat(option.reason, std::make_format_args(source, yield));
					if (option.status == OptionStatus::Regrowing) {
						text += " (regrowing)";
					}
					return text;
				}
				case TaskType::Craft: {
					const auto*		   recipe = assets::RecipeRegistry::Get().getRecipe(option.craftRecipeDefName);
					const std::string& label = recipe != nullptr ? recipe->label : option.craftRecipeDefName;
					return std::vformat(option.reason, std::make_format_args(label));
				}
				case TaskType::PlacePackaged: {
					const auto*		   appearance = world.getComponent<Appearance>(static_cast<EntityID>(option.placePackagedEntityId));
					static const std::string kUnknown = "item";
					const std::string& name = appearance != nullptr ? appearance->defName : kUnknown;
					return std::vformat(option.reason, std::make_format_args(name));
				}
				case TaskType::Build:
				case TaskType::Deconstruct:
				case TaskType::Wander:
				case TaskType::None:
					break;
			}
			return option.reason;
		}

	} // namespace

	std::string describeOption(const World& world, const EvaluatedOption& option) {
		std::string text = describeBase(world, option);
		// A need names its holder in place of where its source is; everything else adds it.
		const bool needNamesHolder = option.taskType == TaskType::FulfillNeed && !option.gathersFood;
		if (option.status == OptionStatus::Reserved && !needNamesHolder) {
			const std::string holder = holderName(world, option);
			text += option.taskType == TaskType::Craft ? std::format(" (station in use by {})", holder) : std::format(" (reserved by {})", holder);
		}
		if (option.status == OptionStatus::SearchGaveUp && !needNamesHolder) {
			text += " (gave up searching for a way there)";
		}
		return text;
	}

} // namespace ecs
