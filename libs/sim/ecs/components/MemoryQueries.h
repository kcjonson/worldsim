#pragma once

// Memory Query Functions for AI Decision Making
// Free functions that query Memory using AssetRegistry for capability lookups.
// See /docs/design/game-systems/colonists/memory.md for design details.

#include "Memory.h"
#include "Task.h"

#include "../EntityID.h"

#include "assets/AssetDefinition.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cstddef>
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

/// How many sources one need decision asks a route for at once: its nearest-of request names this many
/// goals, the nearest by straight line that pass its filter, and the task holds them (SourceChoices).
/// A round that finds no known way widens the next (SourceReachMemo::roundWidening) up to kWidestRound.
inline constexpr std::size_t kNearestOfCount = 8;
inline constexpr std::size_t kWidestRound	 = SourceChoices::kCapacity;

/// A source and its squared straight-line distance. A remembered world entity has `entity`; a storage
/// box the colony knows has `storage` instead, `key` its world key, `item` what it is a source of (also
/// set for a loose pile, whose entity is the item itself), and `storageIndex` its place in the caller's
/// list of boxes. `goal` is where a route to it ends: the source itself, or the stand point beside the
/// placed obstacle it lies in, set by whoever admits it.
struct SourceCandidate {
	float					distSq;
	uint64_t				key;
	const KnownWorldEntity* entity;
	glm::vec2				position;
	uint32_t				item = 0;
	EntityID				storage = kInvalidEntity;
	uint32_t				storageIndex = 0;
	glm::vec2				goal{0.0F, 0.0F};
};

/// A candidate at `position` seen from `fromPosition`.
[[nodiscard]] inline SourceCandidate sourceCandidate(const glm::vec2& fromPosition, uint64_t key, const glm::vec2& position) {
	const glm::vec2 diff = position - fromPosition;
	return {.distSq = glm::dot(diff, diff), .key = key, .entity = nullptr, .position = position, .goal = position};
}

/// The candidate keyed `keepKey` (the source the colonist is heading for) comes off first, whatever its
/// distance.
inline void putKeptFirst(std::vector<SourceCandidate>& scratch, std::optional<uint64_t> keepKey) {
	if (!keepKey.has_value()) {
		return;
	}
	for (SourceCandidate& candidate : scratch) {
		if (candidate.key == *keepKey) {
			candidate.distSq = -1.0F;
		}
	}
}

/// Hand `scratch`'s candidates to `visit` nearest by straight line first, ties broken by key and then
/// item (every item in one box shares the box's key) so the order is deterministic, until it returns
/// false. Only the candidates handed over are ordered (a heap), and `scratch` is consumed. True when
/// every candidate was handed over.
template <typename Visit> bool visitNearestFirst(std::vector<SourceCandidate>& scratch, Visit&& visit) {
	const auto farther = [](const SourceCandidate& a, const SourceCandidate& b) {
		return std::tie(a.distSq, a.key, a.item) > std::tie(b.distSq, b.key, b.item);
	};
	std::make_heap(scratch.begin(), scratch.end(), farther);
	while (!scratch.empty()) {
		std::pop_heap(scratch.begin(), scratch.end(), farther);
		SourceCandidate nearest = scratch.back();
		scratch.pop_back();
		if (!visit(nearest)) {
			return scratch.empty();
		}
	}
	return true;
}

/// Into `chosen`, up to `limit` of `scratch`'s candidates that `admit(candidate)` takes (it may set the
/// candidate's goal), nearest by straight line first. The candidate keyed `keepKey` is asked first, and
/// when it is taken it is chosen alone: a colonist heading for a source keeps it while it stands.
template <typename Admit>
void nearestAdmitted(
	std::vector<SourceCandidate>& scratch, std::vector<SourceCandidate>& chosen, Admit&& admit, std::size_t limit,
	std::optional<uint64_t> keepKey = std::nullopt
) {
	chosen.clear();
	putKeptFirst(scratch, keepKey);
	visitNearestFirst(scratch, [&](SourceCandidate& candidate) {
		if (!admit(candidate)) {
			return true;
		}
		chosen.push_back(candidate);
		return keepKey != candidate.key && chosen.size() < limit;
	});
}

/// Every known world entity with `capability` that `accept(memoryKey, entity)` admits, as candidates seen
/// from `fromPosition`, into `scratch` (caller-owned and reused, so no allocation once it has grown).
template <typename Accept>
void collectWithCapability(
	const Memory& memory, assets::CapabilityType capability, const glm::vec2& fromPosition, Accept&& accept, std::vector<SourceCandidate>& scratch
) {
	scratch.clear();
	for (uint64_t key : memory.getEntitiesWithCapability(capability)) {
		const auto* entity = memory.getWorldEntity(key);
		if (entity != nullptr && accept(key, *entity)) {
			SourceCandidate candidate = sourceCandidate(fromPosition, key, entity->position);
			candidate.entity = entity;
			scratch.push_back(candidate);
		}
	}
}

/// The nearest known world entity with `capability` by straight line, ties broken by memory key, in one
/// pass with no candidate list.
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

} // namespace ecs
