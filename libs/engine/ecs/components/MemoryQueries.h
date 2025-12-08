#pragma once

// Memory Query Functions for AI Decision Making
// Free functions that query Memory using AssetRegistry for capability lookups.
// See /docs/design/game-systems/colonists/memory.md for design details.

#include "Memory.h"

#include "assets/AssetDefinition.h"

#include <optional>
#include <vector>

namespace engine::assets {
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
	const engine::assets::AssetRegistry&  registry,
	engine::assets::CapabilityType		  capability);

/// Find the nearest known world entity with a specific capability
/// @param memory The colonist's memory to search
/// @param registry Asset registry for capability lookups
/// @param capability The capability type to search for
/// @param fromPosition The position to measure distance from
/// @returns The nearest matching entity, or nullopt if none found
[[nodiscard]] std::optional<KnownWorldEntity> findNearestWithCapability(
	const Memory&						  memory,
	const engine::assets::AssetRegistry&  registry,
	engine::assets::CapabilityType		  capability,
	const glm::vec2&					  fromPosition);

/// Count how many known entities have a specific capability
[[nodiscard]] size_t countKnownWithCapability(
	const Memory&						  memory,
	const engine::assets::AssetRegistry&  registry,
	engine::assets::CapabilityType		  capability);

/// Find the nutrition value for an edible entity at a target position
/// @param memory The colonist's memory to search
/// @param registry Asset registry for capability lookups
/// @param targetPos Position to search at (with tolerance)
/// @returns Nutrition value (0.0-1.0) or nullopt if no edible found
[[nodiscard]] std::optional<float> findNutritionAtPosition(
	const Memory&						  memory,
	const engine::assets::AssetRegistry&  registry,
	const glm::vec2&					  targetPos);

} // namespace ecs
