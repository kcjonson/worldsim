#pragma once

// Loose ground piles (ResourceStack entities) by world key, the key Memory files the pile under:
// hashWorldEntity(position, defNameId). A pickup from a remembered pile finds it with one lookup
// instead of a scan of every pile. ActionSystem owns the one instance; piles enter it through
// makeLoosePile and leave it through destroyWorldEntity (WorldSource.h), which the game and tests share.

#include "EntityID.h"
#include "components/Memory.h"

#include <glm/vec2.hpp>

#include <cstdint>
#include <unordered_map>

namespace ecs {

	class LoosePileIndex {
	  public:
		/// File `pile` under its world key. The drop path keeps piles a clearance gap apart, so two in
		/// one 0.1 m cell are rare, but nothing forbids them: both stay filed, and Memory, which keys
		/// the cell once, is served by each in turn until both drain.
		void add(EntityID pile, uint32_t defNameId, glm::vec2 position) {
			const uint64_t key = hashWorldEntity(position, defNameId);
			remove(pile);
			m_byKey.emplace(key, pile);
			m_keyOf[pile] = key;
		}

		/// Forget `entity` if it is a filed pile; any other entity is ignored.
		void remove(EntityID entity) {
			const auto it = m_keyOf.find(entity);
			if (it == m_keyOf.end()) {
				return;
			}
			auto [first, last] = m_byKey.equal_range(it->second);
			for (; first != last; ++first) {
				if (first->second == entity) {
					m_byKey.erase(first);
					break;
				}
			}
			m_keyOf.erase(it);
		}

		/// A pile of `defNameId` filed at `position`'s world key, or kInvalidEntity.
		[[nodiscard]] EntityID find(uint32_t defNameId, glm::vec2 position) const {
			const auto it = m_byKey.find(hashWorldEntity(position, defNameId));
			return it == m_byKey.end() ? kInvalidEntity : it->second;
		}

		[[nodiscard]] size_t size() const { return m_byKey.size(); }

	  private:
		std::unordered_multimap<uint64_t, EntityID> m_byKey;
		std::unordered_map<EntityID, uint64_t>		m_keyOf;
	};

} // namespace ecs
