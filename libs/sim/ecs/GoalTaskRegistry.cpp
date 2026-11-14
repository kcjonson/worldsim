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
#include <core/Vec2d.h>
#include <core/Vec2i64.h>
#include <predicates/Predicates.h>

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>

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

	FootprintBounds::FootprintBounds(geometry::Ring footprintRing)
		: ring(std::move(footprintRing)) {
		if (ring.empty()) {
			return;
		}
		geometry::ensureCounterClockwise(ring);
		geometry::Vec2i64 lowest = ring.front();
		geometry::Vec2i64 highest = ring.front();
		for (const geometry::Vec2i64& corner : ring) {
			lowest = {std::min(lowest.x, corner.x), std::min(lowest.y, corner.y)};
			highest = {std::max(highest.x, corner.x), std::max(highest.y, corner.y)};
		}
		const auto lowestMeters = geometry::dequantize(lowest);
		const auto highestMeters = geometry::dequantize(highest);
		min = {lowestMeters.x, lowestMeters.y};
		max = {highestMeters.x, highestMeters.y};
	}

	bool FootprintBounds::contains(glm::vec2 position) const {
		if (position.x < min.x || position.x > max.x || position.y < min.y || position.y > max.y) {
			return false;
		}
		return geometry::pointInPolygon(geometry::quantize({position.x, position.y}), ring) != geometry::PointInPolygon::Outside;
	}

	glm::vec2 FootprintBounds::interiorPoint() const {
		if (ring.size() < 3) {
			return min;
		}
		const geometry::Vec2d inside = geometry::interiorPoint(ring);
		const auto			  millimetersPerMeter = static_cast<double>(geometry::kMillimetersPerMeter);
		return {static_cast<float>(inside.x / millimetersPerMeter), static_cast<float>(inside.y / millimetersPerMeter)};
	}

	glm::vec2 FootprintBounds::nearestOutside(glm::vec2 position, float margin) const {
		if (!contains(position)) {
			return position;
		}
		const geometry::Vec2i64 point = geometry::quantize({position.x, position.y});
		struct Edge {
			double		distanceMm;
			double		along;
			std::size_t start;
		};
		std::vector<Edge> edges;
		edges.reserve(ring.size());
		for (std::size_t index = 0; index < ring.size(); ++index) {
			const geometry::SegmentPoint nearest = geometry::closestOnSegment(point, ring[index], ring[(index + 1) % ring.size()]);
			edges.push_back({nearest.distanceMm, nearest.t, index});
		}
		std::sort(edges.begin(), edges.end(), [](const Edge& nearer, const Edge& farther) {
			return nearer.distanceMm != farther.distanceMm ? nearer.distanceMm < farther.distanceMm : nearer.start < farther.start;
		});
		const auto millimetersPerMeter = static_cast<double>(geometry::kMillimetersPerMeter);
		const double marginMm = static_cast<double>(margin) * millimetersPerMeter;
		// The ring is counter-clockwise, so the outside lies to the right of each edge.
		const auto outwardNormal = [&](std::size_t start) {
			const geometry::Vec2i64& from = ring[start];
			const geometry::Vec2i64& to = ring[(start + 1) % ring.size()];
			const double			 edgeX = static_cast<double>(to.x - from.x);
			const double			 edgeY = static_cast<double>(to.y - from.y);
			const double			 edgeLength = std::sqrt(edgeX * edgeX + edgeY * edgeY);
			return glm::dvec2{edgeY / edgeLength, -edgeX / edgeLength};
		};
		const auto pastEdge = [&](const Edge& edge) {
			const geometry::Vec2i64& from = ring[edge.start];
			const geometry::Vec2i64& to = ring[(edge.start + 1) % ring.size()];
			const double edgeX = static_cast<double>(to.x - from.x);
			const double edgeY = static_cast<double>(to.y - from.y);
			const double spotX = static_cast<double>(from.x) + edgeX * edge.along;
			const double spotY = static_cast<double>(from.y) + edgeY * edge.along;
			// The push is the boundary's outward direction at the spot: the edge's normal along its interior,
			// and at a vertex the bisector of the two edges meeting there, which leaves a reflex corner into
			// its notch and a convex one diagonally.
			const double edgeLength = std::sqrt(edgeX * edgeX + edgeY * edgeY);
			const double alongMm = edge.along * edgeLength;
			glm::dvec2	 push = outwardNormal(edge.start);
			if (alongMm <= 1.0 || edgeLength - alongMm <= 1.0) {
				const std::size_t other = alongMm <= 1.0 ? (edge.start + ring.size() - 1) % ring.size() : (edge.start + 1) % ring.size();
				const glm::dvec2  bisector = push + outwardNormal(other);
				if (glm::length(bisector) > 1e-6) {
					push = glm::normalize(bisector);
				}
			}
			const double pastX = spotX + push.x * marginMm;
			const double pastY = spotY + push.y * marginMm;
			return glm::vec2{static_cast<float>(pastX / millimetersPerMeter), static_cast<float>(pastY / millimetersPerMeter)};
		};
		for (const Edge& edge : edges) {
			const glm::vec2 out = pastEdge(edge);
			if (!contains(out)) {
				return out;
			}
		}
		return pastEdge(edges.front());
	}

	bool FootprintBounds::blockedBy(const assets::AssetRegistry& registry, uint32_t defNameId, glm::vec2 position) const {
		return registry.hasCapability(defNameId, assets::CapabilityType::Harvestable) && contains(position);
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

	void RemainingNeedCache::refresh(World& world, uint64_t rootGoalId) {
		for (Slot& slot : m_slots) {
			if (slot.pass == m_pass && slot.root == rootGoalId) {
				slot.value = GoalTaskRegistry::Get().remainingNeed(world, slot.root, slot.item, slot.basis);
			}
		}
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
