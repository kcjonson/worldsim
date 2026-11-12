#include "MemoryQueries.h"

namespace ecs {

	std::vector<KnownWorldEntity> findKnownWithCapability(const Memory& memory, assets::CapabilityType capability) {
		std::vector<KnownWorldEntity> result;

		// Use capability index for O(1) set access instead of iterating all entities
		const auto& entityKeys = memory.getEntitiesWithCapability(capability);
		result.reserve(entityKeys.size());

		for (uint64_t key : entityKeys) {
			const auto* entity = memory.getWorldEntity(key);
			if (entity != nullptr) {
				result.push_back(*entity);
			}
		}

		return result;
	}

} // namespace ecs
