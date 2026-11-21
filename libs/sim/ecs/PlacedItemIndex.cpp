#include "PlacedItemIndex.h"

#include "World.h"
#include "components/AgentRadius.h"
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
		for (const assets::ItemArea& area : def.parts->useAreas) {
			if (!area.polygons.empty()) {
				outlines.useAreas.push_back(area.polygons);
			}
		}
		return outlines;
	}

	ItemOutlines placedOutlines(const ItemOutlines& local, glm::vec2 position, float turnRadians) {
		const assets::InstanceTransform placed(position, turnRadians, 1.0F);
		const auto						 moveRing = [&placed](const std::vector<glm::vec2>& ring) {
			std::vector<glm::vec2> moved;
			moved.reserve(ring.size());
			for (const glm::vec2 point : ring) {
				moved.push_back(placed.toWorld(point));
			}
			return moved;
		};
		const auto place = [&moveRing](const std::vector<std::vector<glm::vec2>>& rings) {
			std::vector<std::vector<glm::vec2>> out;
			out.reserve(rings.size());
			for (const std::vector<glm::vec2>& ring : rings) {
				out.push_back(moveRing(ring));
			}
			return out;
		};
		ItemOutlines out{.collider = place(local.collider), .reserved = place(local.reserved)};
		for (const std::vector<assets::AreaPolygon>& area : local.useAreas) {
			std::vector<assets::AreaPolygon>& moved = out.useAreas.emplace_back();
			for (const assets::AreaPolygon& polygon : area) {
				moved.push_back({.outer = moveRing(polygon.outer), .holes = place(polygon.holes)});
			}
		}
		return out;
	}

	namespace {
		void boxOf(PlacedItemIndex::Entry& entry) {
			const auto grow = [](glm::vec2& lower, glm::vec2& upper, bool& first, const std::vector<glm::vec2>& ring) {
				for (const glm::vec2 point : ring) {
					lower = first ? point : glm::min(lower, point);
					upper = first ? point : glm::max(upper, point);
					first = false;
				}
			};
			entry.useLower = entry.position;
			entry.useUpper = entry.position;
			bool firstUse = true;
			for (const std::vector<assets::AreaPolygon>& area : entry.world.useAreas) {
				for (const assets::AreaPolygon& polygon : area) {
					grow(entry.useLower, entry.useUpper, firstUse, polygon.outer);
				}
			}
			entry.lower = entry.useLower;
			entry.upper = entry.useUpper;
			bool first = firstUse;
			for (const std::vector<std::vector<glm::vec2>>* rings : {&entry.world.collider, &entry.world.reserved}) {
				for (const std::vector<glm::vec2>& ring : *rings) {
					grow(entry.lower, entry.upper, first, ring);
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
		// A moved item keeps its use state until it is worked out again, so leaving one blocked spot for another is no
		// new blocking.
		UseState use = UseState::Usable;
		if (held != entries.end()) {
			Entry& entry = held->second;
			entry.pass = pass;
			if (entry.defName == defName && entry.position == position && entry.turnRadians == turnRadians) {
				return false;
			}
			use = entry.use;
			unlist(key, entry);
			entries.erase(held);
		}
		Entry entry{.entity = key.entity, .defName = defName, .position = position, .turnRadians = turnRadians, .pending = key.pending, .use = use, .pass = pass};
		entry.world = placedOutlines(outlinesOf(defName, registry), position, turnRadians);
		boxOf(entry);
		eachBucket(entry.lower, entry.upper, [&](BucketKey bucket) { buckets[bucket].push_back(key); });
		changed.emplace_back(entry.lower, entry.upper);
		entries.emplace(key, std::move(entry));
		return true;
	}

	void PlacedItemIndex::refresh(World& world, const assets::AssetRegistry& registry) {
		++pass;
		changed.clear();
		bool moved = !heldTargets.empty();
		for (const Entry& held : heldTargets) {
			changed.emplace_back(held.lower, held.upper);
		}
		heldTargets.clear();
		// Its own record of the registry's generation: outlinesOf, read by placement between refreshes, keeps another.
		if (registry.generation() != registrySeen) {
			registrySeen = registry.generation();
			for (const auto& [key, entry] : entries) {
				unlist(key, entry);
			}
			entries.clear();
			moved = true;
		}
		for (auto [entity, position, appearance] : world.view<Position, Appearance>()) {
			// A mover is no item; a crate stands at its target, or nowhere while it waits for the player.
			if (world.getComponent<AgentRadius>(entity) != nullptr || outlinesOf(appearance.defName, registry).empty()) {
				continue;
			}
			const Rotation* rotation = world.getComponent<Rotation>(entity);
			const float		turn = rotation != nullptr ? rotation->radians : 0.0F;
			if (const Packaged* packaged = world.getComponent<Packaged>(entity)) {
				if (packaged->targetPosition.has_value()) {
					moved = place({entity, true}, appearance.defName, *packaged->targetPosition, packaged->targetRotation.value_or(turn), registry) || moved;
				}
				continue;
			}
			moved = place({entity, false}, appearance.defName, position.value, turn, registry) || moved;
		}
		for (auto held = entries.begin(); held != entries.end();) {
			if (held->second.pass == pass) {
				++held;
				continue;
			}
			unlist(held->first, held->second);
			held = entries.erase(held);
			moved = true;
		}
		if (moved) {
			++revisionCount;
		}
	}

	void PlacedItemIndex::hold(const std::string& defName, glm::vec2 position, float turnRadians, const assets::AssetRegistry& registry) {
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
		changed.emplace_back(entry.lower, entry.upper);
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

	const PlacedItemIndex::Entry* PlacedItemIndex::installedAt(glm::vec2 position) const {
		std::vector<const Entry*> near;
		query(position, position, near);
		for (const Entry* entry : near) {
			if (!entry->pending && entry->entity != kInvalidEntity && entry->position == position) {
				return entry;
			}
		}
		return nullptr;
	}

	void PlacedItemIndex::setUse(EntityID entity, bool pending, UseState use, assets::StandingRegion standing) {
		const auto found = entries.find({entity, pending});
		if (found == entries.end()) {
			return;
		}
		if (found->second.use != use) {
			++revisionCount;
		}
		found->second.use = use;
		found->second.standing = std::move(standing);
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
