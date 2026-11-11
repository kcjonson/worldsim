#pragma once

// Per-colonist AI state: reach verdicts for the sources a colonist remembers, and the wait on its
// regrowing food.

#include "Memory.h"

#include <glm/vec2.hpp>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace ecs {

	/// What a colonist has found about the targets it remembers in its own reach square
	/// (NavigationSystem::Reach), by Memory key, kept two ways.
	///
	/// A walk, its length or "none" (within a bound, or at any length), is a function of where the
	/// colonist stands, the structural mesh in its square and its belief, so it is held under a Key:
	/// the start to a NavigationSystem::kReachCellMm step, the stamp of the structural content of the
	/// chunks its square touches, and the belief version. It is cheap to walk again.
	///
	/// A route that failed where the walk said yes is the rare disagreement, and picking that target
	/// again would only fail again, so it is held under an Anchor: the spot it failed at, the stamp of
	/// the structural content a colonist anywhere in that spot's square could walk over
	/// (NavigationSystem::verdictStamp) and the belief version. It holds while the colonist stays in
	/// the anchor's square and neither changes, so a colonist wandering about one spot doesn't pick the
	/// failed target again.
	struct SourceReachMemo {
		struct Key {
			std::int64_t  startCell = 0;		// NavigationSystem::Reach::startCell
			std::uint64_t reachGeneration = 0; // NavigationSystem::Reach::startRegionGeneration
			std::uint64_t beliefVersion = 0;	// Memory::beliefVersion
			bool		  operator==(const Key&) const = default;
		};

		struct Anchor {
			glm::vec2	  at{0.0F, 0.0F};
			std::uint64_t structural = 0;	 // NavigationSystem::verdictStamp(at)
			std::uint64_t beliefVersion = 0; // Memory::beliefVersion
			bool		  operator==(const Anchor&) const = default;

			/// A fingerprint, small enough to stamp on a task: equal anchors, equal stamps.
			[[nodiscard]] std::uint64_t stamp() const {
				std::uint64_t h = 1469598103934665603ULL;
				for (const std::uint64_t v : {static_cast<std::uint64_t>(std::bit_cast<std::uint32_t>(at.x)),
											  static_cast<std::uint64_t>(std::bit_cast<std::uint32_t>(at.y)), structural, beliefVersion}) {
					h = (h ^ v) * 1099511628211ULL;
				}
				return h;
			}
		};

		/// Forget the walks when the key changes and the failed routes when the anchor does, or either
		/// once it passes maxVerdicts (forgotten sources' included).
		void keep(const Key& nextKey, const Anchor& nextAnchor, std::size_t maxVerdicts) {
			if (key != nextKey || walks.size() > maxVerdicts) {
				walks.clear();
				key = nextKey;
			}
			if (anchor != nextAnchor || failedRoutes.size() > maxVerdicts) {
				failedRoutes.clear();
				anchor = nextAnchor;
			}
		}

		/// Where the failed routes were found, until the first keep().
		[[nodiscard]] const std::optional<Anchor>& heldAnchor() const { return anchor; }

		/// A source already found unreachable, by a walk under the current key or a route under the
		/// current anchor, so a query can drop it up front.
		[[nodiscard]] bool knownUnreachable(std::uint64_t sourceKey) const {
			if (failedRoutes.contains(sourceKey)) {
				return true;
			}
			const auto it = walks.find(sourceKey);
			return it != walks.end() && !it->second.walk.has_value() && it->second.bound == kAnyWalk;
		}

		/// A source already found reachable under the current key.
		[[nodiscard]] bool knownReachable(std::uint64_t sourceKey) const {
			const auto it = walks.find(sourceKey);
			return it != walks.end() && it->second.walk.has_value();
		}

		/// A route to this source failed under the current anchor: it's unreachable whatever a walk says.
		void recordFailedRoute(std::uint64_t sourceKey) { failedRoutes.insert(sourceKey); }

		/// The walk to a source no longer than maxWalk, running probe(position, maxWalk) when what is
		/// held doesn't answer it.
		template <typename Probe>
		[[nodiscard]] std::optional<float> walk(std::uint64_t sourceKey, const glm::vec2& position, float maxWalk, Probe&& probe) {
			if (failedRoutes.contains(sourceKey)) {
				return std::nullopt;
			}
			const auto it = walks.find(sourceKey);
			if (it != walks.end()) {
				if (it->second.walk.has_value()) {
					return *it->second.walk <= maxWalk ? it->second.walk : std::nullopt;
				}
				if (it->second.bound >= maxWalk) {
					return std::nullopt;
				}
			}
			const std::optional<float> found = probe(position, maxWalk);
			walks[sourceKey]				 = {found, maxWalk};
			return found;
		}

		/// Whether a source can be walked to at all, as walk() with no bound.
		template <typename Probe>
		[[nodiscard]] bool reachable(std::uint64_t sourceKey, const glm::vec2& position, Probe&& probe) {
			return walk(sourceKey, position, kAnyWalk, probe).has_value();
		}

		/// Set when the last food search found every edible source the colonist remembers regrowing:
		/// until the earliest of them is ready, or the colonist remembers or forgets a world entity, the
		/// search would find the same, so it isn't run. Readiness doesn't depend on reach, and a search
		/// that set this saw no source it skipped for reach, so nothing ready can hide behind it.
		struct FoodWait {
			std::uint64_t					memoryVersion = 0; // Memory::worldEntityVersion when set
			double							readyAt = 0.0;	   // earliest ready time, on the simulation clock
			std::optional<KnownWorldEntity> nearest;		   // the nearest reachable regrowing source, for the trace
		};
		std::optional<FoodWait> foodWait;

		/// The last spot the toilet search found, so the search, asked at every decision while the need
		/// waits behind other work, is logged only when its answer changes
		std::optional<glm::vec2> lastToiletSpot;

		/// The wait, while it still holds at `now` for a memory at `memoryVersion`
		[[nodiscard]] const FoodWait* waitingForFood(std::uint64_t memoryVersion, double now) const {
			return foodWait.has_value() && foodWait->memoryVersion == memoryVersion && now < foodWait->readyAt ? &*foodWait : nullptr;
		}

	  private:
		static constexpr float kAnyWalk = std::numeric_limits<float>::infinity();
		struct Verdict {
			std::optional<float> walk;	 // metres, when one was found
			float				 bound = 0.0F; // the longest walk asked about
		};
		std::optional<Key>							key;
		std::unordered_map<std::uint64_t, Verdict> walks;
		std::optional<Anchor>						anchor;
		std::unordered_set<std::uint64_t>			failedRoutes;
	};

} // namespace ecs
