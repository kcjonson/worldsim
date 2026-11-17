#include "ColonistTerms.h"

#include "ConditionVocabulary.h"
#include "SymbolBindings.h"

#include <ecs/GoalTaskRegistry.h>
#include <ecs/World.h>
#include <ecs/components/Attributes.h>
#include <ecs/components/Needs.h>
#include <ecs/components/Task.h>

#include <assets/WorkConfig.h>

#include <algorithm>
#include <cctype>
#include <limits>
#include <memory>
#include <string_view>
#include <utility>

namespace conditions {

	namespace {

		using AttributeMember = float ecs::Attributes::*;

		/// One bit per TaskType: the task types a current_task() name stands for.
		using TaskMask = uint16_t;
		static_assert(
			static_cast<unsigned>(ecs::TaskType::Wander) < std::numeric_limits<TaskMask>::digits, "TaskMask has a bit for every TaskType"
		);
		static_assert(assets::kTaskTypeNames.size() == static_cast<std::size_t>(ecs::TaskType::Wander), "a name for every TaskType");

		/// An engine name as conditions write it: "FulfillNeed" and "Work Ethic" are fulfill_need and work_ethic.
		std::string conditionName(std::string_view engineName) {
			std::string name;
			for (const char character : engineName) {
				const bool upper = std::isupper(static_cast<unsigned char>(character)) != 0;
				if ((character == ' ' || upper) && !name.empty() && name.back() != '_') {
					name += '_';
				}
				if (character != ' ') {
					name += static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
				}
			}
			return name;
		}

		/// The engine's names for `term`'s first parameter, added to its symbol set and bound to their values.
		/// Names the set holds that the engine lacks (a mod's) stay unbound and are reported.
		template <typename Value>
		std::shared_ptr<const SymbolBindings<Value>> bindEngineNames(
			ConditionVocabulary&								vocabulary,
			std::string_view									term,
			const std::vector<std::pair<std::string, Value>>& engineNames,
			std::vector<std::string>&							errors
		) {
			std::vector<std::string> names;
			for (const auto& [name, value] : engineNames) {
				names.push_back(name);
			}
			if (const std::optional<std::string> refused = vocabulary.addSymbols(term, 0, names)) {
				errors.push_back(*refused);
			}
			auto bindings = std::make_shared<SymbolBindings<Value>>();
			for (const DeclaredSymbol& symbol : vocabulary.parameterSymbols(term, 0)) {
				const auto found = std::ranges::find(engineNames, symbol.name, &std::pair<std::string, Value>::first);
				if (found != engineNames.end()) {
					bindings->bind(symbol.id, found->second);
				}
			}
			vocabulary.reportUnbound(term, 0, *bindings);
			return bindings;
		}

		std::vector<std::pair<std::string, ecs::NeedType>> engineNeeds() {
			std::vector<std::pair<std::string, ecs::NeedType>> needs;
			for (std::size_t index = 0; index < ecs::kNeedLabels.size(); ++index) {
				needs.emplace_back(conditionName(ecs::kNeedLabels[index]), static_cast<ecs::NeedType>(index));
			}
			return needs;
		}

		std::vector<std::pair<std::string, AttributeMember>> engineAttributes() {
			std::vector<std::pair<std::string, AttributeMember>> attributes;
			for (const ecs::AttributeField& field : ecs::kAttributeFields) {
				attributes.emplace_back(conditionName(field.label), field.value);
			}
			return attributes;
		}

		TaskMask maskOf(ecs::TaskType type) {
			return static_cast<TaskMask>(1U << static_cast<unsigned>(type));
		}

		std::vector<std::pair<std::string, TaskMask>> engineTaskTypes() {
			std::vector<std::pair<std::string, TaskMask>> types;
			for (std::size_t index = 0; index < assets::kTaskTypeNames.size(); ++index) {
				types.emplace_back(conditionName(assets::kTaskTypeNames[index]), maskOf(static_cast<ecs::TaskType>(index + 1)));
			}
			return types;
		}

		void record(std::vector<std::string>& errors, std::optional<std::string> refused) {
			if (refused.has_value()) {
				errors.push_back(std::move(*refused));
			}
		}

		void provideNeed(ConditionVocabulary& vocabulary, std::vector<std::string>& errors) {
			auto needs = bindEngineNames(vocabulary, "need", engineNeeds(), errors);
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
			auto attributes = bindEngineNames(vocabulary, "attribute", engineAttributes(), errors);
			record(errors, vocabulary.provide("attribute", ValueType::Number, {ValueType::Symbol}, [attributes](const TermCall& call) {
				const AttributeMember* field = attributes->find(call.arguments[0].symbol());
				const ecs::Attributes* component = call.context.world.getComponent<ecs::Attributes>(call.context.colonist);
				if (field == nullptr || component == nullptr) {
					return TermValue::unknown();
				}
				return TermValue::fromNumber(component->*(*field));
			}));
		}

		/// Each task group's mask, from the engine task types it names; mistakes in a group are errors.
		std::vector<std::pair<std::string, TaskMask>>
		taskGroups(const assets::ConditionConfig& config, const std::vector<std::pair<std::string, TaskMask>>& types, std::vector<std::string>& errors) {
			std::vector<std::pair<std::string, TaskMask>> groups;
			for (const assets::ConditionTaskGroupDef& group : config.taskGroups) {
				const std::string where = "task group '" + group.name + "'";
				if (std::ranges::find(types, group.name, &std::pair<std::string, TaskMask>::first) != types.end()) {
					errors.push_back(where + ": its name is already a task type");
					continue;
				}
				TaskMask mask = 0;
				for (const std::string& task : group.tasks) {
					const auto type = std::ranges::find(types, task, &std::pair<std::string, TaskMask>::first);
					if (type == types.end()) {
						errors.push_back(where + ": the sim has no task type '" + task + "'");
					} else {
						mask |= type->second;
					}
				}
				groups.emplace_back(group.name, mask);
			}
			return groups;
		}

		void provideCurrentTask(ConditionVocabulary& vocabulary, const assets::ConditionConfig& config, std::vector<std::string>& errors) {
			std::vector<std::pair<std::string, TaskMask>> names = engineTaskTypes();
			std::vector<std::pair<std::string, TaskMask>> groups = taskGroups(config, names, errors);
			names.insert(names.end(), std::make_move_iterator(groups.begin()), std::make_move_iterator(groups.end()));
			auto kinds = bindEngineNames(vocabulary, "current_task", names, errors);
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
