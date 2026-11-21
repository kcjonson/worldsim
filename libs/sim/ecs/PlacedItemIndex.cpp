#include "PlacedItemIndex.h"

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
		// What an entry's outlines are built from.
		bool sameSpot(const PlacedItemIndex::Entry& left, const PlacedItemIndex::Entry& right) {
			return left.defName == right.defName && left.position == right.position && left.turnRadians == right.turnRadians &&
				   left.pending == right.pending;
		}

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
	} // namespace

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

	void PlacedItemIndex::refresh(World& world, const assets::AssetRegistry& registry) {
		++pass;
		const bool reloaded = registry.generation() != outlinesGeneration;
		if (reloaded) {
			outlinesByDef.clear();
			outlinesGeneration = registry.generation();
		}
		std::map<EntityID, Entry> current;
		for (auto [entity, position, appearance] : world.view<Position, Appearance>()) {
			auto [seen, inserted] = defsSeen.try_emplace(entity);
			if (inserted || seen->second.defName != appearance.defName || reloaded) {
				const assets::AssetDefinition* def = registry.getDefinition(appearance.defName);
				seen->second.defName = appearance.defName;
				seen->second.parts = def != nullptr && def->parts != nullptr;
			}
			seen->second.pass = pass;
			if (!seen->second.parts) {
				continue;
			}
			Entry entry{.entity = entity, .defName = appearance.defName, .position = position.value};
			if (const Rotation* rotation = world.getComponent<Rotation>(entity); rotation != nullptr) {
				entry.turnRadians = rotation->radians;
			}
			if (const Packaged* packaged = world.getComponent<Packaged>(entity); packaged != nullptr) {
				// A crate waiting for the player stands nowhere yet; one with a target stands at its target.
				if (!packaged->targetPosition.has_value()) {
					continue;
				}
				entry.position = *packaged->targetPosition;
				entry.turnRadians = packaged->targetRotation.value_or(entry.turnRadians);
				entry.pending = true;
			}
			current.emplace(entity, std::move(entry));
		}
		std::erase_if(defsSeen, [this](const auto& known) { return known.second.pass != pass; });

		bool changed = false;
		for (auto held = entries.begin(); held != entries.end();) {
			const auto now = current.find(held->first);
			if (now == current.end() || reloaded || !sameSpot(held->second, now->second)) {
				unlist(held->second);
				held = entries.erase(held);
				changed = true;
			} else {
				++held;
			}
		}
		for (auto& [entity, entry] : current) {
			if (entries.contains(entity)) {
				continue;
			}
			entry.world = placedOutlines(outlinesOf(entry.defName, registry), entry.position, entry.turnRadians);
			boxOf(entry);
			list(entries.emplace(entity, std::move(entry)).first->second);
			changed = true;
		}
		if (changed) {
			++changes;
		}
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

	void PlacedItemIndex::list(const Entry& entry) {
		eachBucket(entry.lower, entry.upper, [&](BucketKey key) { buckets[key].push_back(entry.entity); });
	}

	void PlacedItemIndex::unlist(const Entry& entry) {
		eachBucket(entry.lower, entry.upper, [&](BucketKey key) {
			std::vector<EntityID>& listed = buckets.at(key);
			std::erase(listed, entry.entity);
			if (listed.empty()) {
				buckets.erase(key);
			}
		});
	}

	void PlacedItemIndex::query(glm::vec2 lower, glm::vec2 upper, std::vector<const Entry*>& out) const {
		std::vector<EntityID> found;
		eachBucket(lower, upper, [&](BucketKey key) {
			if (const auto bucket = buckets.find(key); bucket != buckets.end()) {
				found.insert(found.end(), bucket->second.begin(), bucket->second.end());
			}
		});
		std::ranges::sort(found);
		const auto [duplicates, end] = std::ranges::unique(found);
		found.erase(duplicates, end);
		for (const EntityID entity : found) {
			const Entry& entry = entries.at(entity);
			if (entry.lower.x <= upper.x && entry.upper.x >= lower.x && entry.lower.y <= upper.y && entry.upper.y >= lower.y) {
				out.push_back(&entry);
			}
		}
	}

	const PlacedItemIndex::Entry* PlacedItemIndex::find(EntityID entity) const {
		const auto it = entries.find(entity);
		return it != entries.end() ? &it->second : nullptr;
	}

} // namespace ecs
