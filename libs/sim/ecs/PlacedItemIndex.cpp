#include "PlacedItemIndex.h"

#include "ColliderEntityIndex.h"
#include "World.h"
#include "components/Appearance.h"
#include "components/Packaged.h"
#include "components/Transform.h"

#include <assets/AssetDefinition.h>
#include <assets/AssetRegistry.h>
#include <assets/PartsAsset.h>
#include <assets/placement/InstanceTransform.h>
#include <world/chunk/ChunkCoordinate.h>

#include <glm/common.hpp>

#include <algorithm>
#include <tuple>
#include <utility>

namespace ecs {

	ItemOutlines itemOutlines(const assets::AssetDefinition& def) {
		ItemOutlines outlines;
		if (def.parts == nullptr) {
			return outlines;
		}
		for (const assets::CollisionShape& collider : def.partsColliders) {
			outlines.collider.push_back(collider.pointsMeters);
		}
		for (const assets::AreaPolygon& polygon : assets::reservedRegion(def)) {
			outlines.reserved.push_back(polygon.outer);
		}
		return outlines;
	}

	ItemOutlines placedOutlines(const ItemOutlines& local, glm::vec2 position, float turnRadians) {
		const assets::InstanceTransform placed(position, turnRadians, 1.0F);
		const auto						 place = [&placed](const std::vector<std::vector<glm::vec2>>& rings) {
			std::vector<std::vector<glm::vec2>> out;
			out.reserve(rings.size());
			for (const std::vector<glm::vec2>& ring : rings) {
				std::vector<glm::vec2>& moved = out.emplace_back();
				moved.reserve(ring.size());
				for (const glm::vec2 point : ring) {
					moved.push_back(placed.toWorld(point));
				}
			}
			return out;
		};
		return {.collider = place(local.collider), .reserved = place(local.reserved)};
	}

	namespace {
		void boxOf(PlacedItemIndex::Entry& entry) {
			entry.lower = entry.position;
			entry.upper = entry.position;
			bool first = true;
			for (const std::vector<std::vector<glm::vec2>>* rings : {&entry.world.collider, &entry.world.reserved}) {
				for (const std::vector<glm::vec2>& ring : *rings) {
					for (const glm::vec2 point : ring) {
						entry.lower = first ? point : glm::min(entry.lower, point);
						entry.upper = first ? point : glm::max(entry.upper, point);
						first = false;
					}
				}
			}
		}

		bool boxesMeet(const PlacedItemIndex::Entry& entry, glm::vec2 lower, glm::vec2 upper) {
			return entry.lower.x <= upper.x && entry.upper.x >= lower.x && entry.lower.y <= upper.y && entry.upper.y >= lower.y;
		}
	} // namespace

	bool PlacedItemIndex::Key::operator<(const Key& other) const {
		return std::tie(entity, pending) < std::tie(other.entity, other.pending);
	}

	const ItemOutlines& PlacedItemIndex::outlinesOf(const std::string& defName, const assets::AssetRegistry& registry) const {
		if (registry.generation() != outlinesGeneration) {
			outlinesByDef.clear();
			outlinesGeneration = registry.generation();
		}
		const auto known = outlinesByDef.find(defName);
		if (known != outlinesByDef.end()) {
			return known->second;
		}
		const assets::AssetDefinition* def = registry.getDefinition(defName);
		return outlinesByDef.emplace(defName, def != nullptr ? itemOutlines(*def) : ItemOutlines{}).first->second;
	}

	bool PlacedItemIndex::place(Key key, const std::string& defName, glm::vec2 position, float turnRadians, const assets::AssetRegistry& registry) {
		const auto held = entries.find(key);
		if (held != entries.end()) {
			Entry& entry = held->second;
			entry.pass = pass;
			if (entry.defName == defName && entry.position == position && entry.turnRadians == turnRadians) {
				return false;
			}
			unlist(key, entry);
			entries.erase(held);
		}
		Entry entry{.entity = key.entity, .defName = defName, .position = position, .turnRadians = turnRadians, .pending = key.pending, .pass = pass};
		entry.world = placedOutlines(outlinesOf(defName, registry), position, turnRadians);
		boxOf(entry);
		eachBucket(entry.lower, entry.upper, [&](BucketKey bucket) { buckets[bucket].push_back(key); });
		entries.emplace(key, std::move(entry));
		return true;
	}

	void PlacedItemIndex::refresh(const ColliderEntityIndex& obstacles, World& world, const assets::AssetRegistry& registry) {
		++pass;
		bool changed = !heldTargets.empty();
		heldTargets.clear();
		// Its own record of the registry's generation: outlinesOf, read by placement between refreshes, keeps another.
		const bool reloaded = registry.generation() != registrySeen;
		registrySeen = registry.generation();

		const bool rereadInstalled = reloaded || obstacles.revision() != obstaclesSeen;
		obstaclesSeen = obstacles.revision();
		if (reloaded) {
			for (const auto& [key, entry] : entries) {
				unlist(key, entry);
			}
			entries.clear();
			changed = true;
		}
		if (rereadInstalled) {
			for (const auto& [entity, obstacle] : obstacles.all()) {
				if (!outlinesOf(obstacle.placed.defName, registry).empty()) {
					changed = place({entity, false}, obstacle.placed.defName, obstacle.placed.position, obstacle.placed.rotation, registry) || changed;
				}
			}
		}
		for (auto [entity, packaged, appearance] : world.view<Packaged, Appearance>()) {
			// A crate waiting for the player stands nowhere yet; one with a target stands at its target.
			if (!packaged.targetPosition.has_value() || outlinesOf(appearance.defName, registry).empty()) {
				continue;
			}
			const Rotation* rotation = world.getComponent<Rotation>(entity);
			const float		turn = packaged.targetRotation.value_or(rotation != nullptr ? rotation->radians : 0.0F);
			changed = place({entity, true}, appearance.defName, *packaged.targetPosition, turn, registry) || changed;
		}
		for (auto held = entries.begin(); held != entries.end();) {
			// An installed entry is kept until the obstacles are read again; a pending one only while its crate is seen.
			const bool seen = held->second.pass == pass || (!held->first.pending && !rereadInstalled);
			if (seen) {
				++held;
				continue;
			}
			unlist(held->first, held->second);
			held = entries.erase(held);
			changed = true;
		}
		if (changed) {
			++revisionCount;
		}
	}

	void PlacedItemIndex::holdTarget(const std::string& defName, glm::vec2 position, float turnRadians, const assets::AssetRegistry& registry) {
		Entry entry{.defName = defName, .position = position, .turnRadians = turnRadians, .pending = true};
		entry.world = placedOutlines(outlinesOf(defName, registry), position, turnRadians);
		boxOf(entry);
		heldTargets.push_back(std::move(entry));
		++revisionCount;
	}

	PlacedItemIndex::BucketKey PlacedItemIndex::bucketKey(std::int32_t column, std::int32_t row) {
		return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(column)) << 32) | static_cast<std::uint32_t>(row);
	}

	template <typename Visit> void PlacedItemIndex::eachBucket(glm::vec2 lower, glm::vec2 upper, Visit&& visit) {
		const world::ChunkCoordinate first = world::worldToChunk({lower.x, lower.y});
		const world::ChunkCoordinate last = world::worldToChunk({upper.x, upper.y});
		for (std::int32_t column = first.x; column <= last.x; ++column) {
			for (std::int32_t row = first.y; row <= last.y; ++row) {
				visit(bucketKey(column, row));
			}
		}
	}

	void PlacedItemIndex::unlist(const Key& key, const Entry& entry) {
		eachBucket(entry.lower, entry.upper, [&](BucketKey bucket) {
			std::vector<Key>& listed = buckets.at(bucket);
			std::erase_if(listed, [&key](const Key& other) { return other.entity == key.entity && other.pending == key.pending; });
			if (listed.empty()) {
				buckets.erase(bucket);
			}
		});
	}

	void PlacedItemIndex::query(glm::vec2 lower, glm::vec2 upper, std::vector<const Entry*>& out) const {
		std::vector<Key> found;
		eachBucket(lower, upper, [&](BucketKey bucket) {
			if (const auto listed = buckets.find(bucket); listed != buckets.end()) {
				found.insert(found.end(), listed->second.begin(), listed->second.end());
			}
		});
		std::sort(found.begin(), found.end());
		const auto sameKey = [](const Key& left, const Key& right) { return left.entity == right.entity && left.pending == right.pending; };
		found.erase(std::unique(found.begin(), found.end(), sameKey), found.end());
		for (const Key& key : found) {
			const Entry& entry = entries.at(key);
			if (boxesMeet(entry, lower, upper)) {
				out.push_back(&entry);
			}
		}
		for (const Entry& held : heldTargets) {
			if (boxesMeet(held, lower, upper)) {
				out.push_back(&held);
			}
		}
	}

	const PlacedItemIndex::Entry* PlacedItemIndex::find(EntityID entity) const {
		const auto installed = entries.find({entity, false});
		if (installed != entries.end()) {
			return &installed->second;
		}
		const auto pending = entries.find({entity, true});
		return pending != entries.end() ? &pending->second : nullptr;
	}

} // namespace ecs
