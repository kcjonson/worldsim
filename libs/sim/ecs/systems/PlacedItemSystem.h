#pragma once

// Keeps the placed item index (PlacedItemIndex.h, rotatable-placed-items 11.3) for placement, the wall tool, the dev
// spawn, a crafted crate's install target, and whatever sends a colonist to use an item, which reach it through
// World::tryGetSystem.
//
// It runs right after NavigationSystem (51) and before the goal systems (55 on): an item that leaves its crate to be
// installed (the install action, 350) loses its pending target and is read as installed in the same refresh, the next
// tick, so it is never in neither. Spawns and removals the tick defers (DeferredEditsSystem, 800) arrive the tick
// after; an install target handed out in between is held (PlacedItemIndex::hold) until then. The world's input, read
// before World::update, sees the index as of the last tick.
//
// Each refresh it works out again whether a colonist can use an item (section 12, judgeUse) for the items whose use
// areas meet a box something changed in: an entry added, moved, or removed (the index's changed boxes), a nav mesh
// patch that landed, and, for an item whose use area ground is unexplored, any newly seen ground. Nothing else is
// tested, so an item nobody builds near costs nothing a tick. An installed item becoming blocked is told once
// (setBlockedCallback); staying blocked, or blocked again only after it cleared, follows from the state it keeps.

#include "../ISystem.h"
#include "../PlacedItemIndex.h"

#include <cstdint>
#include <functional>
#include <string>

namespace ecs {

	class PlacedItemSystem : public ISystem {
	  public:
		void update(float deltaTime) override;
		[[nodiscard]] int		  priority() const override { return 52; }
		[[nodiscard]] const char* name() const override { return "PlacedItem"; }

		[[nodiscard]] const PlacedItemIndex& items() const { return index; }
		[[nodiscard]] PlacedItemIndex&		 items() { return index; }

		/// Called once when an installed item becomes blocked: its entity and def.
		using BlockedCallback = std::function<void(EntityID, const std::string&)>;
		void setBlockedCallback(BlockedCallback callback) { onBlocked = std::move(callback); }

		/// Use judgements made so far, for tests and the perf line: each one is a room test.
		[[nodiscard]] std::uint64_t judgements() const { return judgementCount; }

	  private:
		PlacedItemIndex index;
		BlockedCallback onBlocked;
		std::uint64_t	seenEpoch = UINT64_MAX;
		std::uint64_t	judgementCount = 0;
	};

	/// Whether `entity` is an item a colonist can't use because it has nowhere to stand (UseState::Blocked).
	[[nodiscard]] bool nowhereToStand(World& world, EntityID entity);

} // namespace ecs
