#pragma once

// Per-colonist AI state: the targets a route found no known way to, and the wait on regrowing food.

#include "Memory.h"
#include "Needs.h"

#include <glm/vec2.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace ecs {

	/// What a colonist's routes have found about the targets he picks, by target key (a Memory key, or a
	/// point's own key for a spot with none).
	///
	/// A route answer of "no known way" is the truth for this colonist (his width, the walls he believes
	/// in), so the target it was for is a verdict: not asked about again until it may no longer hold. His
	/// belief (Memory::beliefVersion) moving forgets every verdict at once. The mesh's connectivity epoch
	/// (NavigationSystem::connectivityEpoch) moving forgets only the verdicts at least kVerdictMinTicks old:
	/// in a busy colony the epoch moves every few ticks (ground landing for another mover, an obstacle
	/// felled anywhere), so a way that opens is noticed up to that late, and a verdict always outlives the
	/// round that found it.
	struct SourceReachMemo {
		struct Key {
			std::uint64_t connectivityEpoch = 0;
			std::uint64_t beliefVersion = 0;
			bool		  operator==(const Key&) const = default;
		};

		/// More verdicts than this and they are forgotten, so the memo stays bounded however many targets
		/// fail before the mesh or his belief moves.
		static constexpr std::size_t kMaxVerdicts = 256;

		/// The youngest a verdict is forgotten at when the connectivity epoch moves: about 5 s at 60 ticks
		/// a second. A count of AI updates, so when one is forgotten never depends on the machine.
		static constexpr std::uint64_t kVerdictMinTicks = 300;

		/// Brings the verdicts up to `now` at AI update `tick`: every one goes when the belief moved; when
		/// the epoch moved, those recorded before it moved go once they are kVerdictMinTicks old, however
		/// many times it moves meanwhile.
		void keep(const Key& now, std::uint64_t tick) {
			if (!key.has_value() || key->beliefVersion != now.beliefVersion) {
				verdicts.clear();
				key			 = now;
				staleBefore	 = 0;
				sweepPending = false;
				return;
			}
			if (key->connectivityEpoch != now.connectivityEpoch) {
				key->connectivityEpoch = now.connectivityEpoch;
				staleBefore			   = tick;
				sweepPending		   = !verdicts.empty();
			}
			if (!sweepPending) {
				return;
			}
			sweepPending = false;
			std::erase_if(verdicts, [&](const Verdict& verdict) {
				if (verdict.tick >= staleBefore) {
					return false;
				}
				if (tick - verdict.tick >= kVerdictMinTicks) {
					return true;
				}
				sweepPending = true;
				return false;
			});
		}

		/// No known way to `target`, found under his current belief: held from AI update `tick`, under the
		/// key `now`, whatever epoch the route that found it was searched under.
		void recordVerdict(std::uint64_t target, const Key& now, std::uint64_t tick) {
			keep(now, tick);
			if (verdicts.size() >= kMaxVerdicts) {
				verdicts.clear();
			}
			const auto at = std::lower_bound(verdicts.begin(), verdicts.end(), target, [](const Verdict& v, std::uint64_t t) { return v.target < t; });
			if (at != verdicts.end() && at->target == target) {
				at->tick = tick;
			} else {
				verdicts.insert(at, Verdict{target, tick});
			}
		}

		/// A route found no known way to `target`, and the verdict still holds as of the last keep.
		[[nodiscard]] bool hasVerdict(std::uint64_t target) const {
			const auto at = std::lower_bound(verdicts.begin(), verdicts.end(), target, [](const Verdict& v, std::uint64_t t) { return v.target < t; });
			return at != verdicts.end() && at->target == target;
		}

		[[nodiscard]] std::size_t verdictCount() const { return verdicts.size(); }

		/// How many times a need's next nearest-of round is doubled, by AIDecisionSystem::needChoiceSlot:
		/// each round of a need that found no known way names twice the last (kNearestOfCount, then 2x, then
		/// 4x, kWidestRound), so the next sources are reached whether or not the verdicts on the nearer ones
		/// still hold. A round that is routed starts it over.
		std::array<std::uint8_t, static_cast<std::size_t>(NeedType::Count) + 1> roundWidening{};

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
		struct Verdict {
			std::uint64_t target = 0;
			std::uint64_t tick	 = 0; // the AI update it was recorded in
		};
		std::optional<Key>	 key;
		std::vector<Verdict> verdicts; // sorted by target
		std::uint64_t		 staleBefore  = 0;	   // the AI update the epoch last moved in
		bool				 sweepPending = false; // a verdict from before that move is still held
	};

} // namespace ecs
