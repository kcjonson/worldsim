#pragma once

// Loose ground piles (ResourceStack entities) by world key, the key Memory files the pile under:
// hashWorldEntity(position, defNameId). A pickup from a remembered pile finds it with one lookup
// instead of a scan of every pile. ActionSystem owns the one instance; whoever spawns a pile adds it
// and whoever destroys one removes it.

#include "EntityID.h"
#include "components/Memory.h"

#include <glm/vec2.hpp>

#include <cstdint>
#include <unordered_map>

namespace ecs {

	class LoosePileIndex {
	  public:
		/// File `pile` under its world key. A later pile in the same 0.1 m cell of the same item
		/// replaces it.
		void add(EntityID pile, uint32_t defNameId, glm::vec2 position) {
			const uint64_t key = hashWorldEntity(position, defNameId);
			if (const auto it = m_byKey.find(key); it != m_byKey.end()) {
				m_keyOf.erase(it->second);
			}
			m_byKey[key] = pile;
			m_keyOf[pile] = key;
		}

		/// Forget `entity` if it is a filed pile; any other entity is ignored.
		void remove(EntityID entity) {
			const auto it = m_keyOf.find(entity);
			if (it == m_keyOf.end()) {
				return;
			}
			m_byKey.erase(it->second);
			m_keyOf.erase(it);
		}

		/// The pile of `defNameId` filed at `position`'s world key, or kInvalidEntity.
		[[nodiscard]] EntityID find(uint32_t defNameId, glm::vec2 position) const {
			const auto it = m_byKey.find(hashWorldEntity(position, defNameId));
			return it == m_byKey.end() ? kInvalidEntity : it->second;
		}

		[[nodiscard]] size_t size() const { return m_byKey.size(); }

	  private:
		std::unordered_map<uint64_t, EntityID> m_byKey;
		std::unordered_map<EntityID, uint64_t> m_keyOf;
	};

} // namespace ecs
