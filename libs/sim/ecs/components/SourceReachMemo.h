#pragma once

// Per-colonist AI state: reach verdicts for the sources a colonist remembers.

#include <glm/vec2.hpp>

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace ecs {

	/// Reach verdicts for sources inside the nav regions holding the colonist, by Memory key. Such a
	/// verdict depends on the source's position, those regions' meshes and rects, the colonist's
	/// structural belief and which reach subtree it stands in within each, not on where in those subtrees
	/// it stands or on what else it remembers. So verdicts hold while the key does, and a colonist among
	/// unreachable sources tests each one once, not every decision. For a source outside every region
	/// holding the colonist only a failed route is held (the route to the area's edge toward it depends
	/// on those regions alone); a probe's answer for it isn't, since whether a region covers it changes
	/// on its own.
	struct SourceReachMemo {
		/// One region holding the colonist, as far as its verdicts can tell (NavigationSystem::RegionReach).
		struct RegionKey {
			std::int32_t  regionId = -1;
			std::uint64_t meshGeneration = 0;
			std::int64_t  minX = 0, minY = 0, maxX = 0, maxY = 0; // its rect, mm: moves on a recenter
			std::int32_t  reachRoot = -1;
			std::int32_t  startTri = -1; // set only without a reach root: such a start reaches just its own triangle
			bool		  operator==(const RegionKey&) const = default;
		};
		struct Key {
			std::uint64_t		   beliefVersion = 0; // Memory::beliefVersion
			std::vector<RegionKey> regions;			  // every region holding the colonist, by ascending id
			bool				   operator==(const Key&) const = default;
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
		Key										key;
		std::unordered_map<std::uint64_t, bool> verdicts;
	};

} // namespace ecs
