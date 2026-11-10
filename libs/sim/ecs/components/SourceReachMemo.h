#pragma once

// Per-colonist AI state: reach verdicts for the sources a colonist remembers.

#include <glm/vec2.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <unordered_map>

namespace ecs {

	/// Reach verdicts for sources in the colonist's own nav region, by Memory key. Such a verdict depends
	/// on the source's position, that region's mesh, the colonist's structural belief and which reach
	/// subtree the colonist stands in, not on where in the subtree it stands or on what else it
	/// remembers. So verdicts hold while the key does, and a colonist among unreachable sources tests each
	/// one once, not every decision. A source in another region is never held here: the region a point
	/// falls in can change without this region's mesh changing.
	struct SourceReachMemo {
		struct Key {
			std::int32_t  regionId = -1;		 // NavigationSystem::Reach::startRegionId
			std::uint64_t regionGeneration = 0; // that region's mesh swap count
			std::uint64_t beliefVersion = 0;	 // Memory::beliefVersion
			std::int32_t  reachRoot = -1;
			std::int32_t  startTri = -1; // set only without a reach root: such a start reaches just its own triangle
			bool		  operator==(const Key&) const = default;
		};

		/// Forget every verdict when the key changes, or once verdicts (forgotten sources' included)
		/// pass maxVerdicts.
		void keep(const Key& next, std::size_t maxVerdicts) {
			if (key != next || verdicts.size() > maxVerdicts) {
				verdicts.clear();
				key = next;
			}
		}

		/// A source already found unreachable under the current key, so a query can drop it up front.
		[[nodiscard]] bool knownUnreachable(std::uint64_t sourceKey) const {
			const auto it = verdicts.find(sourceKey);
			return it != verdicts.end() && !it->second;
		}

		/// A route to this source failed under the current key: it's unreachable whatever a probe said.
		void recordUnreachable(std::uint64_t sourceKey) { verdicts[sourceKey] = false; }

		/// The remembered verdict for a source, running probe(position) the first time it's asked.
		template <typename Probe>
		[[nodiscard]] bool reachable(std::uint64_t sourceKey, const glm::vec2& position, Probe&& probe) {
			const auto [it, added] = verdicts.try_emplace(sourceKey, false);
			if (added) {
				it->second = probe(position);
			}
			return it->second;
		}

	  private:
		std::optional<Key>						key;
		std::unordered_map<std::uint64_t, bool> verdicts;
	};

} // namespace ecs
