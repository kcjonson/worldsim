#include "ColliderEntityIndex.h"

#include "World.h"
#include "components/AgentRadius.h"
#include "components/Appearance.h"
#include "components/Transform.h"

#include <assets/AssetDefinition.h>
#include <assets/AssetRegistry.h>

#include <utility>

namespace ecs {

	namespace {
		// What the collider's outline is built from (nav::placedObstacleOutline).
		bool sameObstacle(const assets::PlacedEntity& left, const assets::PlacedEntity& right) {
			return left.defName == right.defName && left.position == right.position && left.rotation == right.rotation &&
				   left.scale == right.scale && left.traits.ageClass == right.traits.ageClass;
		}
	} // namespace

	ColliderEntityIndex::Changes ColliderEntityIndex::refresh(World& world, const assets::AssetRegistry& registry) {
		std::map<EntityID, Entry> current;
		for (auto [entity, position, appearance] : world.view<Position, Appearance>()) {
			if (world.getComponent<AgentRadius>(entity) != nullptr) {
				continue;
			}
			const assets::AssetDefinition* def = registry.getDefinition(appearance.defName);
			if (def == nullptr || !def->collisionFor(appearance.traits.ageClass).blocks()) {
				continue;
			}
			Entry entry{.entity = entity};
			entry.placed.defName = appearance.defName;
			entry.placed.position = position.value;
			entry.placed.scale = appearance.scale;
			entry.placed.traits = appearance.traits;
			if (const Rotation* rotation = world.getComponent<Rotation>(entity); rotation != nullptr) {
				entry.placed.rotation = rotation->radians;
			}
			current.emplace(entity, std::move(entry));
		}

		Changes changes;
		auto	before = entries.begin();
		auto	now = current.begin();
		while (before != entries.end() || now != current.end()) {
			if (now == current.end() || (before != entries.end() && before->first < now->first)) {
				changes.removed.push_back(std::move(before->second));
				++before;
			} else if (before == entries.end() || now->first < before->first) {
				changes.added.push_back(now->second);
				++now;
			} else {
				if (!sameObstacle(before->second.placed, now->second.placed)) {
					changes.removed.push_back(std::move(before->second));
					changes.added.push_back(now->second);
				}
				++before;
				++now;
			}
		}
		entries = std::move(current);
		return changes;
	}

	const ColliderEntityIndex::Entry* ColliderEntityIndex::find(EntityID entity) const {
		const auto it = entries.find(entity);
		return it != entries.end() ? &it->second : nullptr;
	}

	void ColliderEntityIndex::queryRect(glm::vec2 lo, glm::vec2 hi, std::vector<const Entry*>& out) const {
		for (const auto& [entity, entry] : entries) {
			const glm::vec2 at = entry.placed.position;
			if (at.x >= lo.x && at.x <= hi.x && at.y >= lo.y && at.y <= hi.y) {
				out.push_back(&entry);
			}
		}
	}

} // namespace ecs
