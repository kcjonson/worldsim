#include "GoalTaskRegistry.h"

#include "StoreOrDrop.h"
#include "World.h"
#include "components/Inventory.h"
#include "components/Packaged.h"
#include "components/StorageConfiguration.h"
#include "components/StructureBlueprint.h"
#include "components/WorkQueue.h"

#include <assets/AssetRegistry.h>
#include <assets/RecipeRegistry.h>

#include <algorithm>

namespace ecs {

	namespace {

		uint32_t storageNeed(World& world, EntityID box, uint32_t itemDefNameId, const std::string& item, NeedBasis basis) {
			const auto* config = world.getComponent<StorageConfiguration>(box);
			const auto* inventory = world.getComponent<Inventory>(box);
			const auto* def = assets::AssetRegistry::Get().getDefinitionById(itemDefNameId);
			// A packaged box is being moved: it takes nothing until it is placed again.
			if (config == nullptr || inventory == nullptr || def == nullptr || world.hasComponent<Packaged>(box)) {
				return 0;
			}
			if (basis == NeedBasis::Stock) {
				return storageRoomFor(*config, *inventory, item, def->category);
			}
			const uint32_t minimum = config->getNamedMinAmountFor(item, def->category);
			const uint32_t held = inventory->getQuantity(item);
			return minimum > held ? std::min(minimum - held, inventory->addableCount(item)) : 0U;
		}

		uint64_t needHash(uint64_t root, uint32_t item, NeedBasis basis) {
			const uint64_t hash = (root * 0x9E3779B97F4A7C15ULL) ^ (uint64_t{item} << 1) ^ static_cast<uint64_t>(basis);
			return hash ^ (hash >> 29);
		}

	} // namespace

	uint32_t craftStationNeed(World& world, EntityID station, uint32_t itemDefNameId) {
		const auto*		   queue = world.getComponent<WorkQueue>(station);
		const CraftingJob* job = queue != nullptr ? queue->getNextJob() : nullptr;
		const auto*		   recipe = job != nullptr ? assets::RecipeRegistry::Get().getRecipe(job->recipeDefName) : nullptr;
		if (recipe == nullptr) {
			return 0;
		}
		for (const auto& input : recipe->inputs) {
			if (input.defNameId == itemDefNameId) {
				const auto*	   store = world.getComponent<Inventory>(station);
				const uint32_t inStore = store != nullptr ? store->getQuantity(assets::AssetRegistry::Get().getDefName(itemDefNameId)) : 0U;
				return input.count > inStore ? input.count - inStore : 0U;
			}
		}
		return 0;
	}

	bool goalHasWork(World& world, const GoalTask& goal) {
		if (goal.owner != GoalOwner::StorageGoalSystem) {
			return true;
		}
		const auto& registry = GoalTaskRegistry::Get();
		if (goal.parentGoalId.has_value()) {
			return registry.remainingNeed(world, registry.rootOf(goal.id), goal.yieldDefNameId, NeedBasis::Harvest) > 0;
		}
		if (std::any_of(goal.acceptedDefNameIds.begin(), goal.acceptedDefNameIds.end(), [&](uint32_t item) {
				return registry.remainingNeed(world, goal.id, item, NeedBasis::Stock) > 0;
			})) {
			return true;
		}
		const auto* inventory = world.getComponent<Inventory>(goal.destinationEntity);
		return goal.acceptedCategory != assets::ItemCategory::None && inventory != nullptr &&
			   !world.hasComponent<Packaged>(goal.destinationEntity) && inventory->hasSpace();
	}

	bool FootprintBounds::blockedBy(const assets::AssetRegistry& registry, uint32_t defNameId, glm::vec2 p) const {
		return registry.hasCapability(defNameId, assets::CapabilityType::Harvestable) && contains(p);
	}

	GoalTaskRegistry& GoalTaskRegistry::Get() {
		static GoalTaskRegistry instance;
		return instance;
	}

	void GoalTaskRegistry::clear() {
		goals.clear();
		destinationToGoal.clear();
		typeToGoals.clear();
		ownerToGoals.clear();
		parentToChildren.clear();
		nextGoalId = 1;
		nextClaimSeq = 1;
	}

	uint64_t GoalTaskRegistry::createGoal(GoalTask goal) {
		// Assign ID
		goal.id = nextGoalId++;

		// Check for duplicate destination - only for top-level goals (no parent)
		// Child goals (Harvest/Haul) can share a destination with their parent Craft goal
		if (!goal.parentGoalId.has_value()) {
			auto existing = destinationToGoal.find(goal.destinationEntity);
			if (existing != destinationToGoal.end()) {
				// Goal already exists for this destination - update in place but keep
				// the original ID stable (call sites and indices rely on it).
				uint64_t existingId = existing->second;
				auto	 goalIt = goals.find(existingId);
				if (goalIt != goals.end()) {
					// type/owner/parent may differ, so re-index to avoid stale entries
					removeFromIndices(goalIt->second);
					goal.id = existingId;
					goalIt->second = std::move(goal);
					addToIndices(goalIt->second);
				}
				return existingId;
			}
		}

		// Store goal
		uint64_t goalId = goal.id;
		goals[goalId] = std::move(goal);
		addToIndices(goals[goalId]);

		return goalId;
	}

	void GoalTaskRegistry::updateGoal(uint64_t goalId, const std::function<void(GoalTask&)>& updater) {
		auto it = goals.find(goalId);
		if (it == goals.end()) {
			return;
		}

		// Remove from type index (type might change)
		auto typeIt = typeToGoals.find(it->second.type);
		if (typeIt != typeToGoals.end()) {
			typeIt->second.erase(goalId);
			if (typeIt->second.empty()) {
				typeToGoals.erase(typeIt);
			}
		}

		// Apply update
		updater(it->second);

		// Re-add to type index
		typeToGoals[it->second.type].insert(goalId);
	}

	void GoalTaskRegistry::removeGoal(uint64_t goalId) {
		auto it = goals.find(goalId);
		if (it == goals.end()) {
			return;
		}

		removeFromIndices(it->second);
		goals.erase(it);
	}

	void GoalTaskRegistry::removeGoalByDestination(EntityID destinationEntity) {
		auto it = destinationToGoal.find(destinationEntity);
		if (it != destinationToGoal.end()) {
			removeGoal(it->second);
		}
	}

	uint64_t GoalTaskRegistry::rootOf(uint64_t goalId) const {
		auto it = goals.find(goalId);
		if (it == goals.end()) {
			return 0;
		}
		while (it->second.parentGoalId.has_value()) {
			const auto parent = goals.find(it->second.parentGoalId.value());
			if (parent == goals.end()) {
				break;
			}
			it = parent;
		}
		return it->first;
	}

	uint32_t GoalTaskRegistry::remainingNeed(World& world, uint64_t rootGoalId, uint32_t itemDefNameId, NeedBasis basis) const {
		const auto it = goals.find(rootGoalId);
		if (it == goals.end() || it->second.parentGoalId.has_value() || itemDefNameId == 0) {
			return 0;
		}
		const GoalTask&	   root = it->second;
		const std::string& item = assets::AssetRegistry::Get().getDefName(itemDefNameId);
		switch (root.owner) {
			case GoalOwner::CraftingGoalSystem:
				return root.type == TaskType::Craft ? craftStationNeed(world, root.destinationEntity, itemDefNameId) : 0U;
			case GoalOwner::ConstructionGoalSystem: {
				const auto& accepted = root.acceptedDefNameIds;
				const auto* blueprint = world.getComponent<StructureBlueprint>(root.destinationEntity);
				if (root.type != TaskType::Build || blueprint == nullptr ||
					std::find(accepted.begin(), accepted.end(), itemDefNameId) == accepted.end()) {
					return 0;
				}
				return blueprint->remaining(item);
			}
			case GoalOwner::StorageGoalSystem:
				return storageNeed(world, root.destinationEntity, itemDefNameId, item, basis);
			case GoalOwner::BuildGoalSystem:
				return 0;
		}
		return 0;
	}

	void RemainingNeedCache::invalidate() {
		if (++m_pass == 0) {
			for (Slot& slot : m_slots) {
				slot.pass = 0;
			}
			m_pass = 1;
		}
		m_used = 0;
	}

	uint32_t RemainingNeedCache::get(World& world, uint64_t rootGoalId, uint32_t itemDefNameId, NeedBasis basis) {
		if ((m_used + 1) * 2 > m_slots.size()) {
			grow();
		}
		const size_t mask = m_slots.size() - 1;
		size_t		 index = needHash(rootGoalId, itemDefNameId, basis) & mask;
		for (; m_slots[index].pass == m_pass; index = (index + 1) & mask) {
			const Slot& slot = m_slots[index];
			if (slot.root == rootGoalId && slot.item == itemDefNameId && slot.basis == basis) {
				return slot.value;
			}
		}
#ifdef DEVELOPMENT_BUILD
		++computed;
#endif
		const uint32_t value = GoalTaskRegistry::Get().remainingNeed(world, rootGoalId, itemDefNameId, basis);
		m_slots[index] = {.root = rootGoalId, .item = itemDefNameId, .value = value, .pass = m_pass, .basis = basis};
		++m_used;
		return value;
	}

	void RemainingNeedCache::grow() {
		std::vector<Slot> old;
		old.swap(m_slots);
		m_slots.assign(std::max<size_t>(64, old.size() * 2), Slot{});
		const size_t mask = m_slots.size() - 1;
		for (const Slot& slot : old) {
			if (slot.pass != m_pass) {
				continue;
			}
			size_t index = needHash(slot.root, slot.item, slot.basis) & mask;
			while (m_slots[index].pass == m_pass) {
				index = (index + 1) & mask;
			}
			m_slots[index] = slot;
		}
	}

	const GoalTask* GoalTaskRegistry::getGoal(uint64_t goalId) const {
		auto it = goals.find(goalId);
		if (it != goals.end()) {
			return &it->second;
		}
		return nullptr;
	}

	GoalTask* GoalTaskRegistry::getGoalMutable(uint64_t goalId) {
		auto it = goals.find(goalId);
		if (it != goals.end()) {
			return &it->second;
		}
		return nullptr;
	}

	const GoalTask* GoalTaskRegistry::getGoalByDestination(EntityID destinationEntity) const {
		auto it = destinationToGoal.find(destinationEntity);
		if (it != destinationToGoal.end()) {
			return getGoal(it->second);
		}
		return nullptr;
	}

	std::vector<const GoalTask*> GoalTaskRegistry::getGoalsOfType(TaskType type) const {
		std::vector<const GoalTask*> result;

		auto it = typeToGoals.find(type);
		if (it != typeToGoals.end()) {
			result.reserve(it->second.size());
			for (uint64_t goalId : it->second) {
				auto goalIt = goals.find(goalId);
				if (goalIt != goals.end()) {
					result.push_back(&goalIt->second);
				}
			}
		}

		return result;
	}

	std::vector<const GoalTask*> GoalTaskRegistry::getGoalsMatching(const GoalFilter& filter) const {
		std::vector<const GoalTask*> result;

		for (const auto& [goalId, goal] : goals) {
			if (filter(goal)) {
				result.push_back(&goal);
			}
		}

		return result;
	}

	std::vector<const GoalTask*> GoalTaskRegistry::getGoalsInRadius(const glm::vec2& center, float radius) const {
		std::vector<const GoalTask*> result;
		float						 radiusSq = radius * radius;

		for (const auto& [goalId, goal] : goals) {
			float dx = goal.destinationPosition.x - center.x;
			float dy = goal.destinationPosition.y - center.y;
			if (dx * dx + dy * dy <= radiusSq) {
				result.push_back(&goal);
			}
		}

		return result;
	}

	size_t GoalTaskRegistry::goalCount(TaskType type) const {
		auto it = typeToGoals.find(type);
		if (it != typeToGoals.end()) {
			return it->second.size();
		}
		return 0;
	}

	std::vector<const GoalTask*> GoalTaskRegistry::getGoalsByOwner(GoalOwner owner) const {
		std::vector<const GoalTask*> result;

		auto it = ownerToGoals.find(owner);
		if (it != ownerToGoals.end()) {
			result.reserve(it->second.size());
			for (uint64_t goalId : it->second) {
				auto goalIt = goals.find(goalId);
				if (goalIt != goals.end()) {
					result.push_back(&goalIt->second);
				}
			}
		}

		return result;
	}

	size_t GoalTaskRegistry::goalCount(GoalOwner owner) const {
		auto it = ownerToGoals.find(owner);
		if (it != ownerToGoals.end()) {
			return it->second.size();
		}
		return 0;
	}

	void GoalTaskRegistry::addToIndices(const GoalTask& goal) {
		// Destination index - only for top-level goals
		// Child goals share their parent's destination
		if (!goal.parentGoalId.has_value()) {
			destinationToGoal[goal.destinationEntity] = goal.id;
		}

		// Type index
		typeToGoals[goal.type].insert(goal.id);

		// Owner index
		ownerToGoals[goal.owner].insert(goal.id);

		// Parent-child index
		if (goal.parentGoalId.has_value()) {
			parentToChildren[goal.parentGoalId.value()].insert(goal.id);
		}
	}

	void GoalTaskRegistry::removeFromIndices(const GoalTask& goal) {
		// Destination index - only for top-level goals
		if (!goal.parentGoalId.has_value()) {
			destinationToGoal.erase(goal.destinationEntity);
		}

		// Type index
		auto typeIt = typeToGoals.find(goal.type);
		if (typeIt != typeToGoals.end()) {
			typeIt->second.erase(goal.id);
			if (typeIt->second.empty()) {
				typeToGoals.erase(typeIt);
			}
		}

		// Owner index
		auto ownerIt = ownerToGoals.find(goal.owner);
		if (ownerIt != ownerToGoals.end()) {
			ownerIt->second.erase(goal.id);
			if (ownerIt->second.empty()) {
				ownerToGoals.erase(ownerIt);
			}
		}

		// Parent-child index
		if (goal.parentGoalId.has_value()) {
			auto parentIt = parentToChildren.find(goal.parentGoalId.value());
			if (parentIt != parentToChildren.end()) {
				parentIt->second.erase(goal.id);
				if (parentIt->second.empty()) {
					parentToChildren.erase(parentIt);
				}
			}
		}
	}

	std::vector<const GoalTask*> GoalTaskRegistry::getChildGoals(uint64_t parentId) const {
		std::vector<const GoalTask*> result;

		auto it = parentToChildren.find(parentId);
		if (it != parentToChildren.end()) {
			result.reserve(it->second.size());
			for (uint64_t childId : it->second) {
				auto goalIt = goals.find(childId);
				if (goalIt != goals.end()) {
					result.push_back(&goalIt->second);
				}
			}
		}

		return result;
	}

	void GoalTaskRegistry::removeGoalWithChildren(uint64_t goalId) {
		// Collect this goal and all its descendants (recursively)
		std::vector<uint64_t> toRemove;
		std::vector<uint64_t> queue = {goalId};

		while (!queue.empty()) {
			uint64_t current = queue.back();
			queue.pop_back();
			toRemove.push_back(current);

			auto childIt = parentToChildren.find(current);
			if (childIt != parentToChildren.end()) {
				for (uint64_t childId : childIt->second) {
					queue.push_back(childId);
				}
			}
		}

		for (uint64_t id : toRemove) {
			removeGoal(id);
		}
	}

} // namespace ecs
