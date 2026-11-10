#pragma once

// Getting rid of something a colonist holds: put it away in storage within reach first, and
// only drop what storage can't take. Every path where a colonist frees its hands or overflows
// its pack (hand clearing, stowing a tool for an armful, a chain-interrupted carry, crafted
// output that doesn't fit) goes through storeOrDrop, so the rule lives in one place.

#include "DropResourceCallback.h"

#include "assets/AssetDefinition.h" // ItemCategory

#include <glm/vec2.hpp>

#include <cstdint>
#include <string>

namespace ecs {

	class World;
	struct Inventory;
	struct StorageConfiguration;

	/// How close (meters, from where the item would land) a storage container must be for a
	/// colonist to put the item in it instead of dropping it. "At their feet or very close": the
	/// colonist does not walk for this.
	inline constexpr float kStoreWithinReachMeters = 1.5F;

	/// How many more units of `defName` a storage container takes: 0 unless its configuration
	/// accepts the item; otherwise its physical room (stack headroom + free slots, the Inventory's
	/// addableCount) clamped to the configured max for the item. The one "what does this box
	/// take" rule, shared by the storage-to-storage pull and storeOrDrop.
	[[nodiscard]] uint32_t storageRoomFor(
		const StorageConfiguration& config, const Inventory& inventory, const std::string& defName, assets::ItemCategory category
	);

	/// Get rid of `quantity` units of `defName` held by a colonist standing at `position`: deposit
	/// as much as fits into placed storage within kStoreWithinReachMeters (nearest first; not
	/// Packaged, not a crafting station, not behind a built wall), then drop the rest through
	/// `drop`. Safe inside a system's view loop: it only mutates existing Inventory components and
	/// the drop callback enqueues. Returns units stored.
	uint32_t storeOrDrop(
		World& world, const std::string& defName, uint32_t quantity, glm::vec2 position, const DropResourceCallback& drop
	);

} // namespace ecs
