#include "ColonistTerms.h"

#include "ConditionVocabulary.h"
#include "NamedSymbols.h"

#include <ecs/World.h>
#include <ecs/components/Action.h>
#include <ecs/components/Attributes.h>
#include <ecs/components/Needs.h>
#include <ecs/components/Task.h>

#include <cassert>
#include <memory>

namespace conditions {

	namespace {

		using AttributeField = float ecs::Attributes::*;

		struct TaskKind {
			ecs::TaskType type = ecs::TaskType::None;
			bool		  anyWork = false;
		};

		bool isWork(ecs::TaskType type) {
			switch (type) {
				case ecs::TaskType::Harvest:
				case ecs::TaskType::Craft:
				case ecs::TaskType::Haul:
				case ecs::TaskType::PlacePackaged:
				case ecs::TaskType::Build:
				case ecs::TaskType::Deconstruct:
					return true;
				case ecs::TaskType::None:
				case ecs::TaskType::FulfillNeed:
				case ecs::TaskType::Wander:
					return false;
			}
			return false;
		}

		void provideNeed(ConditionVocabulary& vocabulary) {
			auto needs = std::make_shared<const NamedSymbols<ecs::NeedType>>(
				vocabulary,
				std::initializer_list<std::pair<const char*, ecs::NeedType>>{
					{"hunger", ecs::NeedType::Hunger},
					{"thirst", ecs::NeedType::Thirst},
					{"energy", ecs::NeedType::Energy},
					{"bladder", ecs::NeedType::Bladder},
					{"digestion", ecs::NeedType::Digestion},
					{"hygiene", ecs::NeedType::Hygiene},
					{"recreation", ecs::NeedType::Recreation},
					{"temperature", ecs::NeedType::Temperature},
				}
			);
			[[maybe_unused]] const auto refused = vocabulary.provide(
				{.name = "need", .result = ValueType::Number, .parameters = {ValueType::Symbol}, .parameterSymbols = {needs->accepted()}},
				[needs](std::span<const TermValue> arguments, const ConditionContext& context) {
					const ecs::NeedType*	   need = needs->find(arguments[0].identity);
					const ecs::NeedsComponent* component = context.world.getComponent<ecs::NeedsComponent>(context.colonist);
					if (need == nullptr || component == nullptr) {
						return TermValue::unknown();
					}
					return TermValue::fromNumber(component->get(*need).value);
				}
			);
			assert(!refused.has_value());
		}

		void provideAttribute(ConditionVocabulary& vocabulary) {
			auto attributes = std::make_shared<const NamedSymbols<AttributeField>>(
				vocabulary,
				std::initializer_list<std::pair<const char*, AttributeField>>{
					{"strength", &ecs::Attributes::strength},
					{"agility", &ecs::Attributes::agility},
					{"intelligence", &ecs::Attributes::intelligence},
					{"civility", &ecs::Attributes::civility},
					{"sanity", &ecs::Attributes::sanity},
					{"kindness", &ecs::Attributes::kindness},
					{"work_ethic", &ecs::Attributes::workEthic},
					{"social_need", &ecs::Attributes::socialNeed},
				}
			);
			[[maybe_unused]] const auto refused = vocabulary.provide(
				{.name = "attribute",
				 .result = ValueType::Number,
				 .parameters = {ValueType::Symbol},
				 .parameterSymbols = {attributes->accepted()}},
				[attributes](std::span<const TermValue> arguments, const ConditionContext& context) {
					const AttributeField*  field = attributes->find(arguments[0].identity);
					const ecs::Attributes* component = context.world.getComponent<ecs::Attributes>(context.colonist);
					if (field == nullptr || component == nullptr) {
						return TermValue::unknown();
					}
					return TermValue::fromNumber(component->*(*field));
				}
			);
			assert(!refused.has_value());
		}

		void provideCurrentTask(ConditionVocabulary& vocabulary) {
			auto kinds = std::make_shared<const NamedSymbols<TaskKind>>(
				vocabulary,
				std::initializer_list<std::pair<const char*, TaskKind>>{
					{"work", {.anyWork = true}},
					{"fulfill_need", {.type = ecs::TaskType::FulfillNeed}},
					{"harvest", {.type = ecs::TaskType::Harvest}},
					{"craft", {.type = ecs::TaskType::Craft}},
					{"haul", {.type = ecs::TaskType::Haul}},
					{"place_packaged", {.type = ecs::TaskType::PlacePackaged}},
					{"build", {.type = ecs::TaskType::Build}},
					{"deconstruct", {.type = ecs::TaskType::Deconstruct}},
					{"wander", {.type = ecs::TaskType::Wander}},
				}
			);
			[[maybe_unused]] const auto refused = vocabulary.provide(
				{.name = "current_task",
				 .result = ValueType::Boolean,
				 .parameters = {ValueType::Symbol},
				 .parameterSymbols = {kinds->accepted()}},
				[kinds](std::span<const TermValue> arguments, const ConditionContext& context) {
					const TaskKind*	 kind = kinds->find(arguments[0].identity);
					const ecs::Task* task = context.world.getComponent<ecs::Task>(context.colonist);
					if (kind == nullptr || task == nullptr) {
						return TermValue::unknown();
					}
					return TermValue::fromBoolean(kind->anyWork ? isWork(task->type) : task->type == kind->type);
				}
			);
			assert(!refused.has_value());
		}

		void provideMidCraft(ConditionVocabulary& vocabulary) {
			[[maybe_unused]] const auto refused = vocabulary.provide(
				{.name = "mid_craft", .result = ValueType::Boolean}, [](std::span<const TermValue>, const ConditionContext& context) {
					const ecs::Action* action = context.world.getComponent<ecs::Action>(context.colonist);
					if (action == nullptr) {
						return TermValue::unknown();
					}
					return TermValue::fromBoolean(action->type == ecs::ActionType::Craft);
				}
			);
			assert(!refused.has_value());
		}

	} // namespace

	void provideColonistTerms(ConditionVocabulary& vocabulary) {
		provideNeed(vocabulary);
		provideAttribute(vocabulary);
		provideCurrentTask(vocabulary);
		provideMidCraft(vocabulary);
	}

} // namespace conditions
