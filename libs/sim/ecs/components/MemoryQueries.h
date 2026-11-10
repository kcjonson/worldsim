#pragma once

// Memory Query Functions for AI Decision Making
// Free functions that query Memory using AssetRegistry for capability lookups.
// See /docs/design/game-systems/colonists/memory.md for design details.

#include "Memory.h"

#include "assets/AssetDefinition.h"

#include <functional>
#include <optional>
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

/// Find the nearest known world entity with `capability` that `accept` admits and `reachable` confirms.
/// Candidates are tried nearest first (ties broken by memory key, so the pick is deterministic), and
/// `reachable` is asked about at most `maxReachChecks` of them; past that bound the query returns
/// nullopt rather than probing every remembered source. An empty `reachable` admits every candidate.
[[nodiscard]] std::optional<KnownWorldEntity> findNearestReachableWithCapability(
	const Memory&										  memory,
	assets::CapabilityType								  capability,
	const glm::vec2&									  fromPosition,
	const std::function<bool(const KnownWorldEntity&)>&	  accept,
	const std::function<bool(const glm::vec2&)>&		  reachable,
	size_t												  maxReachChecks);

/// Find the entity that minimizes total trip: start -> entity -> destination
/// For multi-leg tasks (gather then deliver, pickup then haul)
/// @param memory The colonist's memory to search
/// @param fromPosition The starting position (colonist)
/// @param destination The final destination (crafting bench, storage, etc.)
/// @param candidateFilter Function returning true for valid candidate entities
/// @returns The optimal entity minimizing totalTrip, or nullopt if none found
[[nodiscard]] std::optional<KnownWorldEntity> findOptimalForTrip(
	const Memory&										  memory,
	const glm::vec2&									  fromPosition,
	const glm::vec2&									  destination,
	const std::function<bool(const KnownWorldEntity&)>&	  candidateFilter);

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
