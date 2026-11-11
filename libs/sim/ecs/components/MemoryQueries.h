#pragma once

// Memory Query Functions for AI Decision Making
// Free functions that query Memory using AssetRegistry for capability lookups.
// See /docs/design/game-systems/colonists/memory.md for design details.

#include "Memory.h"

#include "../EntityID.h"

#include "assets/AssetDefinition.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <tuple>
#include <vector>

namespace ecs {

/// Find all known world entities with a specific capability type
/// @param memory The colonist's memory to search
/// @param capability The capability type to search for
/// @returns Vector of known entities with the capability
[[nodiscard]] std::vector<KnownWorldEntity> findKnownWithCapability(const Memory& memory, assets::CapabilityType capability);

/// A source and its squared distance, as nearestReachable orders them. A remembered world entity has
/// `entity`; a storage box the colony knows has `storage` instead, `key` its world key, `item` what it is
/// a source of (also set for a loose pile, whose entity is the item itself), and `storageIndex` its place
/// in the caller's list of boxes.
struct SourceCandidate {
	float					distSq;
	uint64_t				key;
	const KnownWorldEntity* entity;
	glm::vec2				position;
	uint32_t				item = 0;
	EntityID				storage = kInvalidEntity;
	uint32_t				storageIndex = 0;
};

/// Pop `scratch`'s candidates nearest first, ties broken by key and then item (every item in one box shares
/// the box's key) so the pick is deterministic, until
/// `reachable(candidate)` admits one. O(n + k log n) for the k-th nearest, plus k `reachable` calls.
template <typename Reachable>
[[nodiscard]] std::optional<SourceCandidate> nearestReachable(std::vector<SourceCandidate>& scratch, Reachable&& reachable) {
	const auto farther = [](const SourceCandidate& a, const SourceCandidate& b) {
		return std::tie(a.distSq, a.key, a.item) > std::tie(b.distSq, b.key, b.item);
	};
	std::make_heap(scratch.begin(), scratch.end(), farther);
	while (!scratch.empty()) {
		std::pop_heap(scratch.begin(), scratch.end(), farther);
		const SourceCandidate nearest = scratch.back();
		scratch.pop_back();
		if (reachable(nearest)) {
			return nearest;
		}
	}
	return std::nullopt;
}

/// A candidate at `position` seen from `fromPosition`.
[[nodiscard]] inline SourceCandidate sourceCandidate(const glm::vec2& fromPosition, uint64_t key, const glm::vec2& position) {
	const glm::vec2 diff = position - fromPosition;
	return {.distSq = glm::dot(diff, diff), .key = key, .entity = nullptr, .position = position};
}

/// The nearest known world entity with `capability`, ties broken by memory key: what
/// findNearestReachableWithCapability picks when it accepts and reaches every candidate, in one pass
/// with no candidate list.
[[nodiscard]] inline std::optional<KnownWorldEntity>
findNearestWithCapability(const Memory& memory, assets::CapabilityType capability, const glm::vec2& fromPosition) {
	const KnownWorldEntity* best = nullptr;
	SourceCandidate			bestAt{};
	for (uint64_t key : memory.getEntitiesWithCapability(capability)) {
		const auto* entity = memory.getWorldEntity(key);
		if (entity == nullptr) {
			continue;
		}
		const SourceCandidate candidate = sourceCandidate(fromPosition, key, entity->position);
		if (best == nullptr || std::tie(candidate.distSq, candidate.key) < std::tie(bestAt.distSq, bestAt.key)) {
			best = entity;
			bestAt = candidate;
		}
	}
	return best != nullptr ? std::optional<KnownWorldEntity>{*best} : std::nullopt;
}

/// Find the nearest known world entity with `capability` that `accept(memoryKey, entity)` admits and
/// `reachable(memoryKey, position)` confirms. One pass over Memory's capability index collects the
/// accepted candidates into `scratch` (caller-owned and reused, so no allocation once it has grown to
/// fit), then pops them nearest first, ties broken by memory key so the pick is deterministic, until
/// `reachable` admits one. Every accepted candidate is tried before the answer is nullopt: O(n + k log n)
/// for the k-th nearest, plus k `reachable` calls.
template <typename Accept, typename Reachable>
[[nodiscard]] std::optional<KnownWorldEntity> findNearestReachableWithCapability(
	const Memory&				  memory,
	assets::CapabilityType		  capability,
	const glm::vec2&			  fromPosition,
	Accept&&					  accept,
	Reachable&&					  reachable,
	std::vector<SourceCandidate>& scratch) {
	scratch.clear();
	for (uint64_t key : memory.getEntitiesWithCapability(capability)) {
		const auto* entity = memory.getWorldEntity(key);
		if (entity != nullptr && accept(key, *entity)) {
			SourceCandidate candidate = sourceCandidate(fromPosition, key, entity->position);
			candidate.entity = entity;
			scratch.push_back(candidate);
		}
	}
	const auto found = nearestReachable(scratch, [&](const SourceCandidate& candidate) { return reachable(candidate.key, candidate.position); });
	return found.has_value() ? std::optional<KnownWorldEntity>{*found->entity} : std::nullopt;
}

} // namespace ecs
