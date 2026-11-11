#pragma once

// Memory Query Functions for AI Decision Making
// Free functions that query Memory using AssetRegistry for capability lookups.
// See /docs/design/game-systems/colonists/memory.md for design details.

#include "Memory.h"

#include "../EntityID.h"

#include "assets/AssetDefinition.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <tuple>
#include <vector>

namespace ecs {

/// Find all known world entities with a specific capability type
/// @param memory The colonist's memory to search
/// @param capability The capability type to search for
/// @returns Vector of known entities with the capability
[[nodiscard]] std::vector<KnownWorldEntity> findKnownWithCapability(const Memory& memory, assets::CapabilityType capability);

/// A source and its squared distance, as nearestReachable orders them: the straight line until its walk
/// has been measured (`walked`), the walk after. A remembered world entity has
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
	bool					walked		 = false;
	bool					stretched	 = false; // walked once, to kWalkStretch times the straight line, and found longer
};

/// How far a candidate is first walked when no walk has been found yet: this many times its straight-line
/// distance. One that winds farther goes back keyed at that bound, and is walked in full (or to the best
/// walk found by then) when it comes off again.
inline constexpr float kWalkStretch = 2.0F;

/// Pop `scratch`'s candidates nearest by walk first, ties broken by key and then item (every item in one
/// box shares the box's key) so the pick is deterministic, until `reachable(candidate)` admits one.
/// `walk(candidate, maxWalk)` measures the walk to a candidate, nullopt when it is unreachable or longer
/// than maxWalk. A straight line is never longer than the walk, so candidates come off nearest by straight
/// line, each is walked and goes back keyed by its walk, and one that comes off walked is the nearest walk
/// left: a candidate that is no nearer in a straight line than the best walk found is never walked. A
/// walk is asked no longer than the best one found, and one that is goes back keyed at that bound, to
/// be walked in full if it comes off again. An unreachable candidate goes to `reachable` as it comes
/// off, so the caller still sees it.
template <typename Walk, typename Reachable>
[[nodiscard]] std::optional<SourceCandidate>
nearestReachable(std::vector<SourceCandidate>& scratch, Walk&& walk, Reachable&& reachable) {
	const auto farther = [](const SourceCandidate& a, const SourceCandidate& b) {
		return std::tie(a.distSq, a.key, a.item) > std::tie(b.distSq, b.key, b.item);
	};
	constexpr float kAnyWalk = std::numeric_limits<float>::infinity();
	std::make_heap(scratch.begin(), scratch.end(), farther);
	while (!scratch.empty()) {
		std::pop_heap(scratch.begin(), scratch.end(), farther);
		SourceCandidate nearest = scratch.back();
		scratch.pop_back();
		if (!nearest.walked) {
			float best = kAnyWalk; // the nearest walk still to come off, squared
			for (const SourceCandidate& other : scratch) {
				if (other.walked) {
					best = std::min(best, other.distSq);
				}
			}
			// Bounded by the best walk found; with none yet, by kWalkStretch times the straight line the
			// first time, and in full the next.
			const bool				   firstWalk = best == kAnyWalk && !nearest.stretched;
			const float				   bound	 = best != kAnyWalk ? std::sqrt(best) : (firstWalk ? kWalkStretch * std::sqrt(nearest.distSq) : kAnyWalk);
			const std::optional<float> measured	 = walk(nearest, bound);
			if (measured.has_value()) {
				nearest.distSq = *measured * *measured;
				nearest.walked = true;
				scratch.push_back(nearest);
				std::push_heap(scratch.begin(), scratch.end(), farther);
				continue;
			}
			if (bound != kAnyWalk) {
				// Longer than the bound: keyed just past it, which no walk there can be shorter than, so it
				// comes off again before any candidate whose walk is longer.
				nearest.distSq	  = std::nextafter(bound * bound, kAnyWalk);
				nearest.stretched = nearest.stretched || firstWalk;
				scratch.push_back(nearest);
				std::push_heap(scratch.begin(), scratch.end(), farther);
				continue;
			}
		}
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

/// Find the known world entity with `capability` that `accept(memoryKey, entity)` admits and
/// `reachable(memoryKey, position)` confirms, nearest by walk: `walk(memoryKey, position, maxWalk)` is the
/// walk there in metres, nullopt when unreachable or longer than maxWalk. One pass over Memory's capability
/// index collects the accepted candidates into `scratch` (caller-owned and reused, so no allocation once
/// it has grown to fit), then nearestReachable takes them nearest by walk, ties broken by memory key so
/// the pick is deterministic.
template <typename Accept, typename Walk, typename Reachable>
[[nodiscard]] std::optional<KnownWorldEntity> findNearestReachableWithCapability(
	const Memory&				  memory,
	assets::CapabilityType		  capability,
	const glm::vec2&			  fromPosition,
	Accept&&					  accept,
	Walk&&						  walk,
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
	const auto found = nearestReachable(
		scratch, [&](const SourceCandidate& candidate, float maxWalk) { return walk(candidate.key, candidate.position, maxWalk); },
		[&](const SourceCandidate& candidate) { return reachable(candidate.key, candidate.position); });
	return found.has_value() ? std::optional<KnownWorldEntity>{*found->entity} : std::nullopt;
}

} // namespace ecs
