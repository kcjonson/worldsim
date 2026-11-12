#pragma once

// Per-colonist AI state: the targets a route found no known way to, and the wait on regrowing food.

#include "Memory.h"

#include <glm/vec2.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace ecs {

	/// What a colonist's routes have found about the targets he picks, by target key (a Memory key, or a
	/// point's own key for a spot with none).
	///
	/// A route answer of "no known way" is the truth for this colonist (his width, the walls he believes
	/// in), so the target it was for is a verdict: not asked about again while the mesh's connectivity
	/// epoch (NavigationSystem::connectivityEpoch) and his belief (Memory::beliefVersion) stand as they
	/// were when it was found. Either moving forgets every verdict at the next decision.
	struct SourceReachMemo {
		struct Key {
			std::uint64_t connectivityEpoch = 0;
			std::uint64_t beliefVersion = 0;
			bool		  operator==(const Key&) const = default;
		};

		/// More verdicts than this under one key and they are forgotten, so the memo stays bounded however
		/// many targets fail before the mesh or his belief moves.
		static constexpr std::size_t kMaxVerdicts = 256;

		/// Forget the verdicts unless they were found under `now`.
		void keep(const Key& now) {
			if (key != now) {
				verdicts.clear();
				key = now;
			}
		}

		/// No known way to `target`, found under `foundUnder`. Held only when that is `now`: one found
		/// before the epoch or the belief moved says nothing of either as it stands.
		void recordVerdict(std::uint64_t target, const Key& foundUnder, const Key& now) {
			if (foundUnder != now) {
				return;
			}
			keep(now);
			if (verdicts.size() >= kMaxVerdicts) {
				verdicts.clear();
			}
			const auto at = std::lower_bound(verdicts.begin(), verdicts.end(), target);
			if (at == verdicts.end() || *at != target) {
				verdicts.insert(at, target);
			}
		}

		/// A route found no known way to `target` under the key last kept.
		[[nodiscard]] bool hasVerdict(std::uint64_t target) const { return std::binary_search(verdicts.begin(), verdicts.end(), target); }

		[[nodiscard]] std::size_t verdictCount() const { return verdicts.size(); }

		/// Set when the last food search found every edible source the colonist remembers regrowing:
		/// until the earliest of them is ready, or the colonist remembers or forgets a world entity, the
		/// search would find the same, so it isn't run. Readiness doesn't depend on reach, and a search
		/// that set this saw no source it skipped for a verdict, so nothing ready can hide behind it.
		struct FoodWait {
			std::uint64_t					memoryVersion = 0; // Memory::worldEntityVersion when set
			double							readyAt = 0.0;	   // earliest ready time, on the simulation clock
			std::optional<KnownWorldEntity> nearest;		   // the nearest connected regrowing source, for the trace
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
		std::optional<Key>		   key;
		std::vector<std::uint64_t> verdicts; // sorted
	};

} // namespace ecs
