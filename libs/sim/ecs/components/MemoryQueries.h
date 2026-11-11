#pragma once

// Memory Query Functions for AI Decision Making
// Free functions that query Memory using AssetRegistry for capability lookups.
// See /docs/design/game-systems/colonists/memory.md for design details.

#include "Memory.h"

#include "assets/AssetDefinition.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <tuple>
#include <vector>

namespace assets {
	class AssetRegistry;
}

namespace ecs {

/// Find all known world entities with a specific capability type
/// @param memory The colonist's memory to search
/// @param registry Asset registry for capability lookups
/// @param capability The capability type to search for
/// @returns Vector of known entities with the capability
[[nodiscard]] std::vector<KnownWorldEntity> findKnownWithCapability(
	const Memory&						  memory,
	const assets::AssetRegistry&  registry,
	assets::CapabilityType		  capability);

/// A remembered source and its squared distance, as findNearestReachableWithCapability orders them.
struct SourceCandidate {
	float					distSq;
	uint64_t				key;
	const KnownWorldEntity* entity;
};

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
		if (entity == nullptr || !accept(key, *entity)) {
			continue;
		}
		const glm::vec2 diff = entity->position - fromPosition;
		scratch.push_back({glm::dot(diff, diff), key, entity});
	}
	const auto farther = [](const SourceCandidate& a, const SourceCandidate& b) {
		return std::tie(a.distSq, a.key) > std::tie(b.distSq, b.key);
	};
	std::make_heap(scratch.begin(), scratch.end(), farther);
	while (!scratch.empty()) {
		std::pop_heap(scratch.begin(), scratch.end(), farther);
		const SourceCandidate nearest = scratch.back();
		scratch.pop_back();
		if (reachable(nearest.key, nearest.entity->position)) {
			return *nearest.entity;
		}
	}
	return std::nullopt;
}

/// Count how many known entities have a specific capability
[[nodiscard]] size_t countKnownWithCapability(
	const Memory&						  memory,
	const assets::AssetRegistry&  registry,
	assets::CapabilityType		  capability);

/// Find the nutrition value for an edible entity at a target position
/// @param memory The colonist's memory to search
/// @param registry Asset registry for capability lookups
/// @param targetPos Position to search at (with tolerance)
/// @returns Nutrition value (0.0-1.0) or nullopt if no edible found
[[nodiscard]] std::optional<float> findNutritionAtPosition(
	const Memory&						  memory,
	const assets::AssetRegistry&  registry,
	const glm::vec2&					  targetPos);

} // namespace ecs
