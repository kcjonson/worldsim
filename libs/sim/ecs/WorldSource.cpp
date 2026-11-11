#include "WorldSource.h"

#include "World.h"
#include "components/Appearance.h"
#include "components/Transform.h"
#include "systems/TimeSystem.h"

#include <assets/AssetRegistry.h>
#include <assets/placement/PlacementExecutor.h>
#include <world/chunk/ChunkCoordinate.h>

#include <algorithm>

namespace ecs {

	std::optional<double>
	worldSourceRegrowingUntil(assets::PlacementExecutor& placement, World& world, uint32_t defNameId, glm::vec2 at) {
		return placement.regrowthReadyAt(at, defNameId, simulationSeconds(world));
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
