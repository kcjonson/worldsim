#include "StoreOrDrop.h"

#include "World.h"
#include "components/Inventory.h"
#include "components/Packaged.h"
#include "components/StorageConfiguration.h"
#include "components/Transform.h"
#include "components/WorkQueue.h"
#include "systems/VisionSystem.h"

#include "assets/AssetRegistry.h"

#include <nav/NavCoords.h>
#include <utils/Log.h>
#include <visibility/Visibility.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace ecs {

	uint32_t storageRoomFor(
		const StorageConfiguration& config, const Inventory& inventory, const std::string& defName, assets::ItemCategory category
	) {
		if (!config.acceptsItem(defName, category)) {
			return 0;
		}
		const uint32_t physical = inventory.addableCount(defName);
		const uint32_t maxAmount = config.getMaxAmountFor(defName, category);
		if (maxAmount == 0) {
			return physical;
		}
		const uint32_t held = inventory.getQuantity(defName);
		const uint32_t underMax = maxAmount > held ? maxAmount - held : 0U;
		return std::min(physical, underMax);
	}

	uint32_t storeOrDrop(
		World& world, const std::string& defName, uint32_t quantity, glm::vec2 position, const DropResourceCallback& drop,
		const std::function<void(EntityID box)>& stored
	) {
		if (quantity == 0) {
			return 0;
		}

		uint32_t remaining = quantity;
		if (const auto* def = assets::AssetRegistry::Get().getDefinition(defName)) {
			struct Candidate {
				float	   distSq = 0.0F;
				EntityID   entity = kInvalidEntity;
				Inventory* inventory = nullptr;
				uint32_t   room = 0;
			};
			// Reach is a straight arm's length, so a built wall between the colonist and the box
			// rules the box out. Doors and windows are gaps in the occluders, as they are for sight.
			const geometry::Vec2i64				   fromMm = nav::toMm(position);
			const auto							   reachMm = static_cast<std::int64_t>(std::ceil(kStoreWithinReachMeters * 1000.0F)) + 1;
			std::vector<geometry::OccluderSegment> walls;
			if (const auto* vision = world.tryGetSystem<VisionSystem>()) {
				vision->geometry().queryOccluders(fromMm, reachMm, walls);
			}

			std::vector<Candidate> inReach;
			for (auto [entity, config, inventory, pos] : world.view<StorageConfiguration, Inventory, Position>()) {
				// A packaged box isn't set down yet and a station's store only takes its recipe's bill.
				if (world.hasComponent<Packaged>(entity) || world.hasComponent<WorkQueue>(entity)) {
					continue;
				}
				const float dx = pos.value.x - position.x;
				const float dy = pos.value.y - position.y;
				const float distSq = dx * dx + dy * dy;
				if (distSq > kStoreWithinReachMeters * kStoreWithinReachMeters) {
					continue;
				}
				if (!walls.empty() && !geometry::hasLineOfSight(fromMm, nav::toMm(pos.value), reachMm, walls)) {
					continue;
				}
				const uint32_t room = storageRoomFor(config, inventory, defName, def->category);
				if (room > 0) {
					inReach.push_back({distSq, entity, &inventory, room});
				}
			}
			std::sort(inReach.begin(), inReach.end(), [](const Candidate& a, const Candidate& b) {
				return a.distSq != b.distSq ? a.distSq < b.distSq : a.entity < b.entity;
			});
			for (const Candidate& box : inReach) {
				if (remaining == 0) {
					break;
				}
				const uint32_t added = box.inventory->addItem(defName, std::min(remaining, box.room));
				remaining -= added;
				if (added > 0 && stored) {
					stored(box.entity);
				}
				LOG_DEBUG(
					Engine, "[Action] Stored %u x %s in storage %llu within reach", added, defName.c_str(), static_cast<unsigned long long>(box.entity)
				);
			}
		}

		if (remaining > 0) {
			if (drop) {
				drop(defName, position.x, position.y, remaining);
			} else {
				LOG_WARNING(Engine, "[Action] Cannot drop %u x %s - drop callback not configured (lost)", remaining, defName.c_str());
			}
		}
		return quantity - remaining;
	}

} // namespace ecs
