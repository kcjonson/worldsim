#pragma once

// StructureBlueprint Component
//
// Tracks build-progress state for a blueprint entity (foundation, wall segment,
// or opening). Carries:
//   - Material manifest: what is required vs. what has been hauled to site so far.
//   - Work progress: how much construction work has been done out of the total.
//   - Lifecycle phase: which stage of the clear → deliver → build → complete
//     pipeline the blueprint is currently in.
//
// Design notes:
//   - materialsComplete() is the gate for build-task emission: all required items
//     must be on site (delivered >= required for every defName) before builders
//     are sent. Over-delivery is accepted and ignored.
//   - remaining(defName) is used by haul-goal generation to know how many more
//     units of a given material need to be hauled.
//   - progress() is clamped to [0, 1]. workTotal == 0 returns 0 (not 1) because
//     a freshly created blueprint with no geometry yet computed has done no work;
//     the caller must set workTotal before build tasks begin.
//   - demolishing is a flag rather than a separate BuildPhase value so that
//     demolish state is orthogonal to the forward progression: a Complete
//     structure can be demolished, as can a partially-built one (cancel + remove).
//     A separate DemolishPhase enum or component would be heavier with no benefit
//     at this scope.
//
// Related docs:
//   - /docs/technical/building-construction-architecture.md  (D4, D7)
//   - /docs/design/game-systems/world/building-construction.md  (Construction Loop)

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace ecs {

	struct StructureBlueprint {
		// =========================================================================
		// Lifecycle phase
		// =========================================================================

		enum class BuildPhase {
			Clearing,		   // waiting for footprint to be cleared of obstacles
			AwaitingMaterials, // clear; haul tasks active, build not yet started
			UnderConstruction, // all materials on site; build tasks active
			Complete,		   // fully built
		};

		BuildPhase phase = BuildPhase::Clearing;

		/// True when a deconstruction order is in effect on this entity.
		/// Orthogonal to phase: any phase can be demolished (cancel a blueprint,
		/// or tear down a built structure). ConstructionSystem handles the cascade.
		bool demolishing = false;

		// =========================================================================
		// Material manifest
		// =========================================================================

		/// defName → quantity required to begin construction.
		/// Set once when the blueprint is placed (geometry × material config).
		std::vector<std::pair<std::string, uint32_t>> required;

		/// defName → quantity that has been hauled to the site so far.
		/// Incremented by ActionSystem when a haul task deposits materials.
		std::vector<std::pair<std::string, uint32_t>> delivered;

		// =========================================================================
		// Work progress
		// =========================================================================

		/// Total work units to complete construction (area/length × material factor).
		float workTotal = 0.0F;

		/// Work units completed so far by builders.
		float workDone = 0.0F;

		// =========================================================================
		// Query helpers
		// =========================================================================

		/// True when every required defName has been delivered in sufficient quantity.
		/// Over-delivery (delivered > required) is accepted.
		[[nodiscard]] bool materialsComplete() const {
			for (const auto& [defName, qty] : required) {
				uint32_t have = deliveredQuantity(defName);
				if (have < qty) {
					return false;
				}
			}
			return true;
		}

		/// How many more units of defName still need to be hauled to the site.
		/// Returns 0 if the requirement is already met or if defName is not in
		/// the required manifest at all.
		[[nodiscard]] uint32_t remaining(const std::string& defName) const {
			uint32_t need = 0;
			for (const auto& [name, qty] : required) {
				if (name == defName) {
					need = qty;
					break;
				}
			}
			if (need == 0) {
				return 0;
			}
			uint32_t have = deliveredQuantity(defName);
			return (have >= need) ? 0 : (need - have);
		}

		/// Build progress in [0, 1]. Returns 0 when workTotal == 0 (blueprint
		/// freshly created; total has not been computed yet).
		[[nodiscard]] float progress() const {
			if (workTotal <= 0.0F) {
				return 0.0F;
			}
			return std::clamp(workDone / workTotal, 0.0F, 1.0F);
		}

		/// Fraction to SHOW on a work meter, in [0, 1]. Build fills up (raw progress);
		/// Deconstruct counts workDone back down from workTotal, so its meter must be the
		/// complement -- 1 at the start of demolition draining to 0 -- not the raw progress.
		[[nodiscard]] float displayProgress(bool deconstruct) const {
			return deconstruct ? 1.0F - progress() : progress();
		}

	  private:
		[[nodiscard]] uint32_t deliveredQuantity(const std::string& defName) const {
			for (const auto& [name, qty] : delivered) {
				if (name == defName) {
					return qty;
				}
			}
			return 0;
		}
	};

} // namespace ecs
