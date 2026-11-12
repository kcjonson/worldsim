#pragma once

// Per-colonist AI state: reach verdicts for the sources a colonist remembers, and the wait on its
// regrowing food.

#include "Memory.h"
#include "WalkAnswer.h"

#include <glm/vec2.hpp>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <unordered_map>

namespace ecs {

	/// What a colonist has found about the targets it remembers (NavigationSystem::Reach), by Memory
	/// key, kept three ways.
	///
	/// A walk, its length, "none" (within a bound, or at any length) or "far" (the search gave up), is a
	/// function of where the colonist stands, the structural mesh and its belief, so it is held under a
	/// Key: the start to a NavigationSystem::kReachCellMm step, the structural mesh's epoch (any
	/// structural job anywhere moves it) and the belief version.
	///
	/// A route that failed where the walk said yes is the rare disagreement, and picking that target
	/// again would only fail again, so it is held under an Anchor: the spot it failed at, the stamp of
	/// the structural content near it (NavigationSystem::verdictStamp) and the belief version. It holds
	/// while the colonist stays near the anchor (NavigationSystem::nearAnchor) and neither changes, so
	/// neither ground landing elsewhere nor a colonist wandering about one spot has it pick the failed
	/// target again. Each failed route also keeps the stamp of the ground between the anchor and its
	/// target (NavigationSystem::spanStamp), and the caller forgets it once that moves
	/// (forgetFailedRoutes), so a change near a far target clears it too.
	///
	/// A route whose search gave up (NavigationSystem::PathAnswer::gaveUp) is a question about a far
	/// target, whose answer doesn't change when the colonist takes ten steps, so it isn't held under
	/// the anchor: it keeps where the search started, the target and the stamp of the ground between
	/// them, and holds until that stamp or the belief moves or the colonist is more than
	/// kGaveUpMoveFraction of the way from there to the target away from where it started
	/// (forgetGaveUpRoutes). So a colonist that knows only far targets with no way to them tries each
	/// once, not once per anchor.
	struct SourceReachMemo {
		struct Key {
			std::int64_t  startCell = 0;		// NavigationSystem::Reach::startCell
			std::uint64_t structuralEpoch = 0; // NavigationSystem::Reach::structuralEpoch
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

		/// Forget the walks when the key changes and the failed routes when the anchor does, or any of the
		/// three once it passes maxVerdicts (forgotten sources' included).
		void keep(const Key& nextKey, const Anchor& nextAnchor, std::size_t maxVerdicts) {
			if (key != nextKey || walks.size() > maxVerdicts) {
				walks.clear();
				key = nextKey;
			}
			if (anchor != nextAnchor || failedRoutes.size() > maxVerdicts) {
				failedRoutes.clear();
				anchor = nextAnchor;
			}
			if (gaveUpRoutes.size() > maxVerdicts) {
				gaveUpRoutes.clear();
			}
		}

		/// The reach stamp a route failure gets while ground within the anchor load range of the colonist
		/// is still to mesh (NavigationSystem::Reach::settled): never recorded, and the next decision
		/// picks afresh. NavigationSystem wakes a mover whose failure carries it once that ground lands.
		static constexpr std::uint64_t kUnsettledStamp = std::numeric_limits<std::uint64_t>::max();

		/// Where the failed routes were found, until the first keep().
		[[nodiscard]] const std::optional<Anchor>& heldAnchor() const { return anchor; }

		/// A source already found unreachable, by a walk under the current key or a route under the
		/// current anchor, so a query can drop it up front. Far is not unreachable.
		[[nodiscard]] bool knownUnreachable(std::uint64_t sourceKey) const {
			if (failedRoutes.contains(sourceKey) || gaveUpRoutes.contains(sourceKey)) {
				return true;
			}
			const auto it = walks.find(sourceKey);
			return it != walks.end() && !it->second.walk.has_value() && !it->second.far && it->second.bound == kAnyWalk;
		}

		/// A source already found reachable (or far) under the current key.
		[[nodiscard]] bool knownReachable(std::uint64_t sourceKey) const {
			const auto it = walks.find(sourceKey);
			return it != walks.end() && (it->second.walk.has_value() || it->second.far);
		}

		/// A route to this source failed under the current anchor: it's unreachable whatever a walk says.
		/// `target` and `spanStamp` (NavigationSystem::spanStamp from the anchor) say where and over what
		/// ground.
		struct FailedRoute {
			glm::vec2	  target{0.0F, 0.0F};
			std::uint64_t spanStamp = 0;
		};
		void recordFailedRoute(std::uint64_t sourceKey, const FailedRoute& route) { failedRoutes[sourceKey] = route; }

		/// A route to this source whose search gave up, from `from`: unreachable until it is forgotten.
		struct GaveUpRoute {
			glm::vec2	  from{0.0F, 0.0F};
			glm::vec2	  target{0.0F, 0.0F};
			std::uint64_t spanStamp		= 0; // NavigationSystem::spanStamp(from, target)
			std::uint64_t beliefVersion = 0; // Memory::beliefVersion
		};
		void recordGaveUpRoute(std::uint64_t sourceKey, const GaveUpRoute& route) { gaveUpRoutes[sourceKey] = route; }
		[[nodiscard]] bool searchGaveUpOn(std::uint64_t sourceKey) const { return gaveUpRoutes.contains(sourceKey); }

		/// How far the colonist moves from where a search gave up, as a fraction of the straight line from
		/// there to the target, before the target is asked about again.
		static constexpr float kGaveUpMoveFraction = 0.25F;

		/// Forget the failed and gave-up routes whose span stamp moved: `spanStamp(from, target)` is the
		/// stamp now, read once per structural epoch (no span stamp moves without it). A failed route's
		/// span starts at the anchor.
		template <typename SpanStamp> void forgetFailedRoutes(std::uint64_t structuralEpoch, SpanStamp&& spanStamp) {
			if (spanEpoch == structuralEpoch) {
				return;
			}
			spanEpoch = structuralEpoch;
			if (anchor.has_value()) {
				std::erase_if(failedRoutes, [&](const auto& entry) { return spanStamp(anchor->at, entry.second.target) != entry.second.spanStamp; });
			}
			std::erase_if(gaveUpRoutes, [&](const auto& entry) { return spanStamp(entry.second.from, entry.second.target) != entry.second.spanStamp; });
		}

		/// Forget the gave-up routes the colonist, at `here` with belief `beliefVersion`, has moved or
		/// learned its way out of (kGaveUpMoveFraction).
		void forgetGaveUpRoutes(const glm::vec2& here, std::uint64_t beliefVersion) {
			std::erase_if(gaveUpRoutes, [&](const auto& entry) {
				const GaveUpRoute& route = entry.second;
				const glm::vec2	   moved = here - route.from;
				const glm::vec2	   span	 = route.target - route.from;
				const float		   limit = kGaveUpMoveFraction * kGaveUpMoveFraction * (span.x * span.x + span.y * span.y);
				return route.beliefVersion != beliefVersion || moved.x * moved.x + moved.y * moved.y > limit;
			});
		}

		/// The walk to a source no longer than maxWalk, running probe(position, maxWalk) when what is
		/// held doesn't answer it. A far answer is held like the others.
		template <typename Probe>
		[[nodiscard]] WalkAnswer walk(std::uint64_t sourceKey, const glm::vec2& position, float maxWalk, Probe&& probe) {
			if (failedRoutes.contains(sourceKey) || gaveUpRoutes.contains(sourceKey)) {
				return {};
			}
			const auto it = walks.find(sourceKey);
			if (it != walks.end()) {
				if (it->second.walk.has_value()) {
					return *it->second.walk <= maxWalk ? it->second.walk : std::nullopt;
				}
				if (it->second.bound >= maxWalk) {
					return it->second.far ? WalkAnswer::farAway() : WalkAnswer{};
				}
			}
			const WalkAnswer found = probe(position, maxWalk);
			walks[sourceKey]	   = {found.walk, maxWalk, found.far};
			return found;
		}

		/// Whether a source can be walked to at all (far counts), as walk() with no bound.
		template <typename Probe>
		[[nodiscard]] bool reachable(std::uint64_t sourceKey, const glm::vec2& position, Probe&& probe) {
			return walk(sourceKey, position, kAnyWalk, probe).reachable();
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
			std::optional<float> walk;			// metres, when one was found
			float				 bound = 0.0F; // the longest walk asked about
			bool				 far   = false; // the search gave up
		};
		std::optional<Key>							key;
		std::unordered_map<std::uint64_t, Verdict> walks;
		std::optional<Anchor>						anchor;
		std::unordered_map<std::uint64_t, FailedRoute>	failedRoutes;
		std::unordered_map<std::uint64_t, GaveUpRoute>	gaveUpRoutes;
		std::optional<std::uint64_t>					spanEpoch;
	};

} // namespace ecs
