#include "WorldSource.h"

#include "World.h"
#include "components/Appearance.h"
#include "components/ResourceStack.h"
#include "components/Transform.h"
#include "systems/TimeSystem.h"

#include <assets/AssetRegistry.h>
#include <assets/placement/PlacementExecutor.h>
#include <world/chunk/ChunkCoordinate.h>

#include <algorithm>

namespace ecs {

	std::optional<double>
	worldSourceRegrowingUntil(assets::PlacementExecutor& placement, World& world, uint32_t defNameId, glm::vec2 at) {
		if (placement.regrowthEntryCount() == 0) {
			return std::nullopt; // nothing has started regrowing, so there is no clock to read
		}
		return placement.regrowthReadyAt(at, defNameId, simulationSeconds(world));
	}

	void makeLoosePile(World& world, EntityID entity, uint32_t defNameId, glm::vec2 position, uint32_t quantity) {
		world.addComponent<ResourceStack>(entity, ResourceStack{quantity});
		if (auto* actions = world.tryGetSystem<ActionSystem>()) {
			actions->loosePiles().add(entity, defNameId, position);
		}
	}

	void destroyWorldEntity(World& world, EntityID entity) {
		if (auto* actions = world.tryGetSystem<ActionSystem>()) {
			actions->loosePiles().remove(entity);
		}
		world.destroyEntity(entity);
	}

	ActionSystem::SourceState takeWorldSource(
		assets::PlacementExecutor& placement,
		World&					   world,
		std::vector<EntityID>&	   pendingRemoval,
		const std::string&		   defName,
		glm::vec2				   at,
		ActionSystem::SourceTake   take,
		float					   regrowthSeconds
	) {
		using State = ActionSystem::SourceState;
		using Take = ActionSystem::SourceTake;
		const auto	coord = world::worldToChunk({at.x, at.y});
		const auto* index = placement.getChunkIndex(coord);
		const bool	placed = index != nullptr && index->contains(at, defName);

		EntityID spawned = kInvalidEntity;
		if (!placed) {
			constexpr float kSpawnedMatchEps = 0.25F;
			for (auto [entity, position, appearance] : world.view<Position, Appearance>()) {
				// A loose pile is found only through LoosePileIndex: one missing from it is gone, and
				// taking a neighbouring pile here would remove it whole for one pickup's pay.
				if (world.getComponent<ResourceStack>(entity) != nullptr) {
					continue;
				}
				const glm::vec2 d = position.value - at;
				if (appearance.defName == defName && d.x * d.x + d.y * d.y <= kSpawnedMatchEps * kSpawnedMatchEps &&
					std::ranges::find(pendingRemoval, entity) == pendingRemoval.end()) {
					spawned = entity;
					break;
				}
			}
		}
		if (!placed && spawned == kInvalidEntity) {
			return State::Gone;
		}

		const uint32_t defNameId = assets::AssetRegistry::Get().getDefNameId(defName);
		const bool	   regrowing = worldSourceRegrowingUntil(placement, world, defNameId, at).has_value();
		if (take == Take::Remove) {
			if (placed) {
				placement.removeEntity(coord, at, defName);
			} else {
				pendingRemoval.push_back(spawned);
				placement.forgetEntityState(at, defName);
			}
		} else if (take == Take::Regrow && !regrowing) {
			placement.startRegrowth(at, defNameId, simulationSeconds(world), regrowthSeconds);
		}
		return regrowing ? State::OnCooldown : State::Ready;
	}

} // namespace ecs
