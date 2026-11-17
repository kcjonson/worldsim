#include "ColonistTerms.h"

#include "ConditionVocabulary.h"
#include "SymbolBindings.h"

#include <ecs/GoalTaskRegistry.h>
#include <ecs/World.h>
#include <ecs/components/Attributes.h>
#include <ecs/components/Needs.h>
#include <ecs/components/Task.h>

#include <algorithm>
#include <array>
#include <memory>
#include <string_view>
#include <utility>

namespace conditions {

	namespace {

		using AttributeField = float ecs::Attributes::*;

		/// One bit per TaskType: the task types a current_task() name stands for.
		using TaskMask = uint16_t;

		constexpr std::array<std::pair<std::string_view, ecs::NeedType>, 8> kNeeds = {{
			{"hunger", ecs::NeedType::Hunger},
			{"thirst", ecs::NeedType::Thirst},
			{"energy", ecs::NeedType::Energy},
			{"bladder", ecs::NeedType::Bladder},
			{"digestion", ecs::NeedType::Digestion},
			{"hygiene", ecs::NeedType::Hygiene},
			{"recreation", ecs::NeedType::Recreation},
			{"temperature", ecs::NeedType::Temperature},
		}};

		constexpr std::array<std::pair<std::string_view, AttributeField>, 8> kAttributes = {{
			{"strength", &ecs::Attributes::strength},
			{"agility", &ecs::Attributes::agility},
			{"intelligence", &ecs::Attributes::intelligence},
			{"civility", &ecs::Attributes::civility},
			{"sanity", &ecs::Attributes::sanity},
			{"kindness", &ecs::Attributes::kindness},
			{"work_ethic", &ecs::Attributes::workEthic},
			{"social_need", &ecs::Attributes::socialNeed},
		}};

		constexpr std::array<std::pair<std::string_view, ecs::TaskType>, 8> kTaskTypes = {{
			{"fulfill_need", ecs::TaskType::FulfillNeed},
			{"harvest", ecs::TaskType::Harvest},
			{"craft", ecs::TaskType::Craft},
			{"haul", ecs::TaskType::Haul},
			{"place_packaged", ecs::TaskType::PlacePackaged},
			{"build", ecs::TaskType::Build},
			{"deconstruct", ecs::TaskType::Deconstruct},
			{"wander", ecs::TaskType::Wander},
		}};

		/// The sim's value for a declared name, by the engine's own names; nothing for a name the sim lacks.
		template <typename Value, std::size_t Count>
		const Value* engineValue(const std::array<std::pair<std::string_view, Value>, Count>& table, std::string_view name) {
			const auto found = std::ranges::find(table, name, &std::pair<std::string_view, Value>::first);
			return found == table.end() ? nullptr : &found->second;
		}

		template <typename Value, std::size_t Count>
		SymbolBindings<Value> bindParameter(
			const ConditionVocabulary&									 vocabulary,
			std::string_view											 term,
			const std::array<std::pair<std::string_view, Value>, Count>& table
		) {
			SymbolBindings<Value> bindings;
			for (const DeclaredSymbol& symbol : vocabulary.parameterSymbols(term, 0)) {
				if (const Value* value = engineValue(table, symbol.name)) {
					bindings.bind(symbol.id, *value);
				}
			}
			return bindings;
		}

		TaskMask maskOf(ecs::TaskType type) {
			return static_cast<TaskMask>(1U << static_cast<unsigned>(type));
		}

		void record(std::vector<std::string>& errors, std::optional<std::string> refused) {
			if (refused.has_value()) {
				errors.push_back(std::move(*refused));
			}
		}

		void provideNeed(ConditionVocabulary& vocabulary, std::vector<std::string>& errors) {
			auto needs = std::make_shared<const SymbolBindings<ecs::NeedType>>(bindParameter(vocabulary, "need", kNeeds));
			record(errors, vocabulary.provide("need", ValueType::Number, {ValueType::Symbol}, [needs](const TermCall& call) {
				const ecs::NeedType*	   need = needs->find(call.arguments[0].symbol());
				const ecs::NeedsComponent* component = call.context.world.getComponent<ecs::NeedsComponent>(call.context.colonist);
				if (need == nullptr || component == nullptr) {
					return TermValue::unknown();
				}
				return TermValue::fromNumber(component->get(*need).value);
			}));
		}

		void provideAttribute(ConditionVocabulary& vocabulary, std::vector<std::string>& errors) {
			auto attributes = std::make_shared<const SymbolBindings<AttributeField>>(bindParameter(vocabulary, "attribute", kAttributes));
			record(errors, vocabulary.provide("attribute", ValueType::Number, {ValueType::Symbol}, [attributes](const TermCall& call) {
				const AttributeField*  field = attributes->find(call.arguments[0].symbol());
				const ecs::Attributes* component = call.context.world.getComponent<ecs::Attributes>(call.context.colonist);
				if (field == nullptr || component == nullptr) {
					return TermValue::unknown();
				}
				return TermValue::fromNumber(component->*(*field));
			}));
		}

		void provideCurrentTask(ConditionVocabulary& vocabulary, const assets::ConditionConfig& config, std::vector<std::string>& errors) {
			auto kinds = std::make_shared<SymbolBindings<TaskMask>>();
			for (const DeclaredSymbol& symbol : vocabulary.parameterSymbols("current_task", 0)) {
				const auto group = std::ranges::find(config.taskGroups, symbol.name, &assets::ConditionTaskGroupDef::name);
				if (group == config.taskGroups.end()) {
					if (const ecs::TaskType* type = engineValue(kTaskTypes, symbol.name)) {
						kinds->bind(symbol.id, maskOf(*type));
					}
					continue;
				}
				TaskMask mask = 0;
				for (const std::string& task : group->tasks) {
					if (const ecs::TaskType* type = engineValue(kTaskTypes, task)) {
						mask |= maskOf(*type);
					} else {
						errors.push_back("task group '" + group->name + "': the sim has no task type '" + task + "'");
					}
				}
				kinds->bind(symbol.id, mask);
			}
			record(errors, vocabulary.provide("current_task", ValueType::Truth, {ValueType::Symbol}, [kinds](const TermCall& call) {
				const TaskMask*	 mask = kinds->find(call.arguments[0].symbol());
				const ecs::Task* task = call.context.world.getComponent<ecs::Task>(call.context.colonist);
				if (mask == nullptr || task == nullptr) {
					return TermValue::unknown();
				}
				return TermValue::fromTruth((*mask & maskOf(task->type)) != 0);
			}));
		}

		/// A craft is under way while the colonist works its recipe at the station, and while he harvests or
		/// hauls inputs for a craft goal.
		bool isMidCraft(const ecs::Task& task) {
			const auto servesCraft = [](uint64_t goalId) {
				const ecs::GoalTaskRegistry& goals = ecs::GoalTaskRegistry::Get();
				const ecs::GoalTask*		 root = goalId == 0 ? nullptr : goals.getGoal(goals.rootOf(goalId));
				return root != nullptr && root->type == ecs::TaskType::Craft;
			};
			switch (task.type) {
				case ecs::TaskType::Craft:
					return !task.craftRecipeDefName.empty();
				case ecs::TaskType::Haul:
					return servesCraft(task.haulGoalId);
				case ecs::TaskType::Harvest:
					return servesCraft(task.harvestGoalId);
				default:
					return false;
			}
		}

		void provideMidCraft(ConditionVocabulary& vocabulary, std::vector<std::string>& errors) {
			record(errors, vocabulary.provide("mid_craft", ValueType::Truth, {}, [](const TermCall& call) {
				const ecs::Task* task = call.context.world.getComponent<ecs::Task>(call.context.colonist);
				return task == nullptr ? TermValue::unknown() : TermValue::fromTruth(isMidCraft(*task));
			}));
		}

	} // namespace

	void provideColonistTerms(ConditionVocabulary& vocabulary, const assets::ConditionConfig& config, std::vector<std::string>& errors) {
		provideNeed(vocabulary, errors);
		provideAttribute(vocabulary, errors);
		provideCurrentTask(vocabulary, config, errors);
		provideMidCraft(vocabulary, errors);
	}

} // namespace conditions
