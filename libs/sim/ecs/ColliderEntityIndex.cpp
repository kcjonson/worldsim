#include "ColliderEntityIndex.h"

#include "World.h"
#include "components/AgentRadius.h"
#include "components/Appearance.h"
#include "components/Packaged.h"
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

		bool collidesAtAnyAge(const assets::AssetDefinition* def) {
			if (def == nullptr) {
				return false;
			}
			for (std::size_t ageClass = 0; ageClass < def->collisions.size(); ++ageClass) {
				if (def->collisions[ageClass].blocks()) {
					return true;
				}
			}
			return false;
		}
	} // namespace

	bool ColliderEntityIndex::mayCollide(EntityID entity, const std::string& defName, const assets::AssetRegistry& registry) {
		auto [known, inserted] = defsSeen.try_emplace(entity);
		if (inserted || known->second.defName != defName) {
			known->second.defName  = defName;
			known->second.collides = collidesAtAnyAge(registry.getDefinition(defName));
		}
		known->second.pass = pass;
		return known->second.collides;
	}

	ColliderEntityIndex::Changes ColliderEntityIndex::refresh(World& world, const assets::AssetRegistry& registry) {
		++pass;
		std::map<EntityID, Entry> current;
		for (auto [entity, position, appearance] : world.view<Position, Appearance>()) {
			// A mover is never an obstacle, and a packaged thing is not installed: it is carried or waits
			// to be placed, and leaves the mesh until it is.
			if (world.getComponent<AgentRadius>(entity) != nullptr || world.getComponent<Packaged>(entity) != nullptr) {
				continue;
			}
			if (!mayCollide(entity, appearance.defName, registry)) {
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
		std::erase_if(defsSeen, [this](const auto& known) { return known.second.pass != pass; });
		return changes;
	}

	const ColliderEntityIndex::Entry* ColliderEntityIndex::find(EntityID entity) const {
		const auto it = entries.find(entity);
		return it != entries.end() ? &it->second : nullptr;
	}

	void ColliderEntityIndex::queryRect(glm::vec2 lower, glm::vec2 upper, std::vector<const Entry*>& out) const {
		for (const auto& held : entries) {
			const glm::vec2 at = held.second.placed.position;
			if (at.x >= lower.x && at.x <= upper.x && at.y >= lower.y && at.y <= upper.y) {
				out.push_back(&held.second);
			}
		}
	}

} // namespace ecs
