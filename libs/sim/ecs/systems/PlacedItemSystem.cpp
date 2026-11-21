#include "PlacedItemSystem.h"

#include "NavigationSystem.h"
#include "PlayerVisibilitySystem.h"
#include "UseRoom.h"

#include "../World.h"

#include <assets/AssetRegistry.h>

#include <utility>
#include <vector>

namespace ecs {

	namespace {
		bool boxesMeet(glm::vec2 lower, glm::vec2 upper, const std::pair<glm::vec2, glm::vec2>& box) {
			return lower.x <= box.second.x && upper.x >= box.first.x && lower.y <= box.second.y && upper.y >= box.first.y;
		}
	} // namespace

	void PlacedItemSystem::update(float /*deltaTime*/) {
		if (world == nullptr) {
			return;
		}
		index.refresh(*world, assets::AssetRegistry::Get());
		// With no nav system there is no ground to stand on to test, and every item stays usable.
		NavigationSystem* navigation = world->tryGetSystem<NavigationSystem>();
		if (navigation == nullptr) {
			return;
		}
		std::vector<std::pair<glm::vec2, glm::vec2>> touched = navigation->takeLandedPatchBoxes();
		touched.insert(touched.end(), index.changedBoxes().begin(), index.changedBoxes().end());
		const PlayerVisibilitySystem* visibility = PlayerVisibilitySystem::gate(*world);
		const std::uint64_t			  epoch = visibility != nullptr ? visibility->epoch() : 0;
		const bool					  seenMoved = epoch != seenEpoch;
		seenEpoch = epoch;
		if (touched.empty() && !seenMoved) {
			return;
		}

		// A box with some margin: a disc reaching into the use area from an obstacle just outside it counts.
		const glm::vec2 reach{assets::kLargestColonistRadiusMeters, assets::kLargestColonistRadiusMeters};
		std::vector<const PlacedItemIndex::Entry*> due;
		index.forEach([&](const PlacedItemIndex::Entry& entry) {
			if (entry.world.useAreas.empty()) {
				return;
			}
			const glm::vec2 lower = entry.useLower - reach;
			const glm::vec2 upper = entry.useUpper + reach;
			const bool		near = std::ranges::any_of(touched, [&](const auto& box) { return boxesMeet(lower, upper, box); });
			if (near || (seenMoved && entry.use == UseState::Unexplored)) {
				due.push_back(&entry);
			}
		});
		for (const PlacedItemIndex::Entry* entry : due) {
			const EntityID	  entity = entry->entity;
			const bool		  pending = entry->pending;
			const std::string defName = entry->defName;
			const UseState	  before = entry->use;
			UseVerdict		  verdict = judgeUse(*navigation, index, entry->world, entry->position, {}, visibility, entity, kInvalidEntity);
			++judgementCount;
			const UseState after = verdict.use;
			index.setUse(entity, pending, after, std::move(verdict.standing));
			if (after == UseState::Blocked && before != UseState::Blocked && !pending && onBlocked) {
				onBlocked(entity, defName);
			}
		}
	}

	bool nowhereToStand(World& world, EntityID entity) {
		const PlacedItemSystem* system = world.tryGetSystem<PlacedItemSystem>();
		if (system == nullptr) {
			return false;
		}
		const PlacedItemIndex::Entry* entry = system->items().find(entity);
		return entry != nullptr && !entry->pending && entry->use == UseState::Blocked;
	}

} // namespace ecs
