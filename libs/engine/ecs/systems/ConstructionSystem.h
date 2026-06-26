#pragma once

// ConstructionSystem - drives the structure build lifecycle as a goal source.
//
// Watches every entity carrying Structure + StructureBlueprint and walks it
// through the construction loop (building-construction-architecture.md D7).
// Foundations, walls, and openings share the lifecycle machinery; they differ only
// at the front of the pipeline:
//   Foundations start at Clearing and clear their footprint of choppable blockers.
//   Walls have no clear phase (they sit on a cleared, built foundation), so they
//     start at AwaitingMaterials. Their haul + build goals are GATED: a wall's
//     umbrella stays Blocked, and no material/build goals emit, until the host
//     foundation is Built (design: walls draw on a foundation blueprint but only
//     build once the foundation finishes). isWallHostBuilt owns that gate.
//   Openings (doors/windows) also have no clear phase (they sit on a built wall on a
//     cleared foundation), so they start at AwaitingMaterials too. Same gate shape as
//     walls but one level up: an opening's umbrella stays Blocked, and no material/
//     build goals emit, until its host wall SEGMENT is Built. isOpeningHostSegmentBuilt
//     owns that gate.
//
//   Clearing           -> emit Harvest goals for choppable entities sitting on
//                         the footprint; advance to AwaitingMaterials once the
//                         footprint is clear. (Foundations only.)
//   AwaitingMaterials  -> emit a Harvest+Haul chain per outstanding material (Wood
//                         from trees, hauled to the site where the deposit records it
//                         directly onto the blueprint's delivered[] manifest); advance
//                         to UnderConstruction once materialsComplete().
//   UnderConstruction  -> emit one Build goal; ActionSystem's Build action
//                         advances workDone. Multiple colonists may build the
//                         same blueprint concurrently.
//   Complete           -> stop emitting; the ConstructionWorld state flip and
//                         toast happen via ActionSystem's structure-completed
//                         callback (wired in GameScene).
//
// Goals are emitted through GoalTaskRegistry with owner ConstructionGoalSystem
// so AIDecisionSystem scores and picks them up like any other goal.
//
// Goal-graph shape (mirrors CraftingGoalSystem): ONE top-level umbrella goal per
// blueprint, keyed by destinationEntity, persists across every phase. The umbrella
// is the Build goal. Every phase goal (clear-Harvest, per-material Harvest, per-
// material Haul) is a CHILD of it (parentGoalId = umbrella id), which exempts the
// children from the registry's single-top-level-goal-per-destination guard so a
// multi-material site can carry several Harvest + Haul goals at once. The umbrella
// is Blocked while clearing or awaiting materials and flips Available only at
// UnderConstruction, exactly like a Craft goal. Goals are cleaned up via
// removeGoalWithChildren when a blueprint entity disappears or completes.
//
// This system lives in libs/engine and links ConstructionWorld directly (it is
// part of the engine lib). It does NOT flip ConstructionWorld state itself; that
// is the app's job via the ActionSystem completion callback, keeping the
// cross-layer signal on the same callback pattern ActionSystem already uses.
//
// Priority 58: after BuildGoalSystem (57), before AIDecisionSystem (60), so a
// freshly emitted goal is visible the same frame the AI evaluates.

#include "../EntityID.h"
#include "../ISystem.h"
#include "../components/StructureBlueprint.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_set>
#include <vector>

namespace engine::world {
	struct ChunkCoordinate;
}

namespace engine::assets {
	class PlacementExecutor;
	class AssetRegistry;
} // namespace engine::assets

namespace engine::construction {
	class ConstructionWorld;
}

namespace ecs {

	class World;
	struct Structure;				 // defined in components/Structure.h
	enum class GoalStatus : uint8_t; // defined in GoalTaskRegistry.h

	/// Pure phase-decision result, independent of ECS/placement wiring so it can be
	/// unit-tested directly. Tells the system what the blueprint's phase SHOULD be and
	/// which goal kinds to emit this tick.
	struct ConstructionDecision {
		StructureBlueprint::BuildPhase nextPhase = StructureBlueprint::BuildPhase::Clearing;
		bool						   emitClearGoals = false;	  // footprint not yet clear: emit Harvest for blockers
		bool						   emitMaterialGoals = false; // clear, materials outstanding: emit Harvest+Haul chains
		bool						   emitBuildGoal = false;	  // materials staged: emit a Build goal
	};

	/// Decide forward build progression only, from the two facts the system measures each
	/// tick: whether the footprint is clear of blocking entities, and whether the material
	/// manifest is satisfied. Pure; no side effects.
	///
	/// Does NOT handle demolishing. Callers MUST route a demolishing blueprint to the
	/// deconstruct path (decideDeconstruct) before reaching this helper; passing one here
	/// would mis-decide it as a build.
	[[nodiscard]] ConstructionDecision
	decideConstructionPhase(const StructureBlueprint& blueprint, bool footprintClear, bool materialsComplete);

	/// How much of a material to HARVEST for the NEXT delivery trip, given how much the
	/// site still needs (`remaining`), how much is already carried toward it (`carried`,
	/// summed across colonists), and how much a colonist can carry in one trip
	/// (`carryCapacity`, the inventory stack size).
	///
	/// The demand that matters is "do I need MORE in hand to make a worthwhile delivery
	/// trip", not "is the whole site satisfied". A colonist's stack caps at carryCapacity
	/// (e.g. 99), so for a manifest larger than one stack `carried` can never reach
	/// `remaining`; bounding by `remaining` alone would leave the Harvest goal Available
	/// forever and the colonist would hoard at the stack cap instead of delivering. So the
	/// per-trip target is min(remaining, carryCapacity): once carried reaches it, demand is
	/// 0, the Harvest goal retires, and the Haul wins so the load gets delivered. Empty
	/// hands with work outstanding gives demand > 0 again for the next trip.
	/// Pure; unit-tested directly.
	[[nodiscard]] uint32_t constructionHarvestDemand(uint32_t remaining, uint32_t carried, uint32_t carryCapacity);

	/// ConstructionSystem - foundation lifecycle goal source (see file header).
	class ConstructionSystem : public ISystem {
	  public:
		ConstructionSystem() = default;

		void update(float deltaTime) override;

		[[nodiscard]] int		  priority() const override { return 58; }
		[[nodiscard]] const char* name() const override { return "Construction"; }

		/// Inject the app-owned topology store (foundation footprints) and the placement
		/// data used to detect footprint blockers. Called once from GameScene after both
		/// the ConstructionWorld and PlacementExecutor exist.
		void setConstructionWorld(engine::construction::ConstructionWorld* constructionWorld) { m_constructionWorld = constructionWorld; }
		void setPlacementData(
			const engine::assets::PlacementExecutor*				  placementExecutor,
			const std::unordered_set<engine::world::ChunkCoordinate>* processedChunks
		) {
			m_placementExecutor = placementExecutor;
			m_processedChunks = processedChunks;
		}

		/// Debug: count of blueprints the system is actively driving this tick.
		[[nodiscard]] size_t getActiveBlueprintCount() const { return m_activeBlueprintCount; }

		/// DEV/TEST ONLY. When on, every committed blueprint is driven straight to Built
		/// each tick (materials credited, work completed, phase flipped) without any
		/// colonist clear/haul/build. Inert when off: normal play is untouched and
		/// determinism is unaffected. Toggled from the debug HTTP API (/api/dev/freebuild).
		void			   setFreeBuild(bool on) { m_freeBuild = on; }
		[[nodiscard]] bool freeBuild() const { return m_freeBuild; }

		/// DEV/TEST ONLY. Drive a single blueprint entity straight to Built right now,
		/// independent of the free-build flag: set delivered[] = required to satisfy the
		/// manifest, set workDone = workTotal, flip phase to Complete, and
		/// fire the structure-completed callback so the ConstructionWorld state flip and
		/// render update happen through the SAME path a normal build takes. Returns false
		/// if the entity is not a non-Complete foundation/wall blueprint. Used by the
		/// programmatic-foundation dev endpoint (built=1).
		bool forceCompleteBlueprint(EntityID blueprintEntity);

		/// Set the callback fired when free-build completes a structure. Wired to the
		/// SAME lambda GameScene gives ActionSystem's structure-completed callback, so a
		/// free-built structure is indistinguishable from a normally-built one.
		using StructureCompletedCallback = std::function<void(EntityID)>;
		void setStructureCompletedCallback(StructureCompletedCallback callback) { m_onStructureCompleted = std::move(callback); }

		/// Set the callback fired when a demolishing blueprint has no work to undo (workDone <= 0)
		/// and is therefore removed immediately. Wired to the SAME lambda GameScene gives
		/// ActionSystem's structure-deconstructed callback, so a no-work removal takes the same
		/// topology-removal path a worked deconstruct does.
		using StructureDeconstructedCallback = std::function<void(EntityID)>;
		void setStructureDeconstructedCallback(StructureDeconstructedCallback callback) {
			m_onStructureDeconstructed = std::move(callback);
		}

		/// Set the callback that spawns a loose, haulable resource pile (defName, x, y, qty) on the
		/// ground. Fired when a cancelled/demolished blueprint dumps its staged delivered[] materials
		/// so they are not lost. Wired to the SAME shared drop helper GameScene gives ActionSystem's
		/// drop-resource callback, so there is one ground-pile spawn path.
		using DropResourceCallback = std::function<void(const std::string&, float, float, uint32_t)>;
		void setDropResourceCallback(DropResourceCallback callback) { m_onDropResource = std::move(callback); }

		/// Salvage fraction returned when a worked/built structure is demolished, as a percentage
		/// (config `refundPercent`, default 50). GameScene sets it from the construction config at
		/// startup so the value is live, not hardcoded. A cancelled-but-unbuilt blueprint (no work
		/// done) returns 100% regardless; this governs only the worked path.
		void setRefundPercent(float percent) { m_refundPercent = percent; }

		/// DEV/TEST ONLY. Credit `amount` of `defName` onto the delivered[] manifests of all
		/// active (non-Complete) build sites, capped at each site's outstanding need so no
		/// site is over-filled, and stops once `amount` is exhausted. Returns the total
		/// credited across sites. Reuses StructureBlueprint::recordDelivery(), so it never
		/// duplicates the manifest math. Used by /api/dev/give?where=site.
		uint32_t creditMaterialToSites(const std::string& defName, uint32_t amount);

	  private:
		/// True if no Harvestable placed entity overlaps the foundation's footprint AABB.
		/// v1 clearing rule (see header): only Harvestable blockers gate clearing.
		[[nodiscard]] bool isFootprintClear(uint64_t foundationId) const;

		/// True once the wall segment's host foundation is Built. Walls draw on a
		/// foundation blueprint but only BUILD after the foundation finishes (design:
		/// Walls / Prerequisites), so this gates a wall blueprint's haul + build goals.
		/// True (ungated) when there is no ConstructionWorld wired (headless contexts).
		[[nodiscard]] bool isWallHostBuilt(uint64_t segmentId) const;

		/// True once the opening's host wall segment is Built. Openings sit on a built
		/// wall, so this gates an opening blueprint's haul + build goals exactly as
		/// isWallHostBuilt gates a wall's (one level up the topology: opening -> segment).
		/// True (ungated) when there is no ConstructionWorld wired (headless contexts);
		/// an unknown opening or unknown host segment stays gated (false).
		[[nodiscard]] bool isOpeningHostSegmentBuilt(uint64_t openingId) const;

		/// Create (or fetch) the per-blueprint umbrella goal: a single top-level Build goal
		/// keyed by destinationEntity that every phase goal hangs under as a child. It is
		/// Blocked until materials are staged, then Available at UnderConstruction (like a
		/// Craft goal). Returns the stable umbrella goal id so phase goals can parent to it.
		uint64_t ensureUmbrellaGoal(EntityID blueprintEntity, const StructureBlueprint& blueprint, GoalStatus status);

		/// Drive a demolishing blueprint's Deconstruct goal. Mirrors ensureUmbrellaGoal but the
		/// goal is a top-level Deconstruct keyed by destinationEntity (no children). It is
		/// Available only once the structure's dependents are gone (cascade gate), else Blocked,
		/// so a fully-marked building tears down in order: openings -> walls -> foundation. Any
		/// Build umbrella+children for the entity are dropped first (it's being torn down, not
		/// built). Returns void; it drives the Deconstruct goal as a side effect.
		///
		/// Edge case: a blueprint with workDone <= 0 has no work to undo (startBuildAction would
		/// reject it), so there is nothing for a colonist to deconstruct. Such a structure is
		/// removed immediately via the deconstructed-completion callback (the SAME removal+refund
		/// path a worked deconstruct fires), instead of emitting an un-actionable goal.
		void decideDeconstruct(EntityID blueprintEntity, const Structure& structure, StructureBlueprint& blueprint);

		/// Dump `fraction` of a demolished blueprint's staged delivered[] materials to the ground as
		/// loose, haulable piles at its position (via the drop-resource callback, which splits each
		/// quantity into stacks and scatters them), then clear delivered[]. Per material the dumped
		/// count is floor(qty * fraction), skipped when 0. Both deconstruct branches call it: the
		/// no-work cancel returns everything (fraction 1.0); a worked/built teardown returns the
		/// salvage cut (fraction = refundPercent/100, and delivered == required for a built structure,
		/// so it returns refundPercent% of the manifest). No-op when delivered[] is empty or no drop
		/// callback is wired (headless); delivered[] is cleared regardless so the manifest never re-dumps.
		void dumpDeliveredToGround(EntityID blueprintEntity, StructureBlueprint& blueprint, float fraction);

		/// Cascade gate: true once a demolishing structure's dependents are gone, so its
		/// Deconstruct goal may go Available. A foundation waits until no wall is hosted on it; a
		/// wall waits until no opening sits on it; an opening has no dependents (always cleared).
		/// True (ungated) when there is no ConstructionWorld wired (headless contexts).
		[[nodiscard]] bool deconstructDependentsCleared(const Structure& structure) const;

		/// Emit a clear-Harvest CHILD under the umbrella for Harvestable blockers on the
		/// footprint. Reuses the Harvest goal shape (yield = whatever the blocker yields) so
		/// chopping a tree both clears the site and produces Wood for the manifest.
		void emitClearGoals(EntityID blueprintEntity, uint64_t foundationId, uint64_t umbrellaGoalId);

		/// Emit a Harvest+Haul CHILD chain per outstanding material under the umbrella
		/// (CraftingGoalSystem's pattern): Harvest yields the material into a colonist's
		/// inventory, the Haul carries it to the blueprint and the deposit records it onto
		/// delivered[]. Both stay Available concurrently (no dependency gate) so each trip can deliver.
		void emitMaterialGoals(EntityID blueprintEntity, const StructureBlueprint& blueprint, uint64_t umbrellaGoalId);

		/// Retire any leftover child goals of a kind no longer relevant to the current phase
		/// (e.g. the clear-Harvest child once the footprint is clear, or all material children
		/// once materials are staged). The umbrella itself is left in place.
		void retireChildGoals(EntityID blueprintEntity, uint64_t umbrellaGoalId, bool keepMaterialChildren);

		/// Sum how much of a material colonists are currently carrying (a colonist is NeedsComponent +
		/// Inventory; a build site has neither, so it never counts). Used to bound harvest demand so a
		/// colonist that already carries enough delivers it instead of chopping more.
		[[nodiscard]] uint32_t carriedAmount(const std::string& defName) const;

		/// The most a single colonist can carry in one trip, in kilograms: the largest carry
		/// weight among colonists (a deterministic max, not iteration-order dependent). Divided
		/// by a material's per-unit mass to get the per-trip harvest target, so a manifest
		/// heavier than one load gets delivered in repeated trips rather than hoarded.
		[[nodiscard]] float colonistCarryCapacityKg() const;

		/// Shared by the free-build flag path and forceCompleteBlueprint: stage materials,
		/// finish work, flip phase, fire the completion callback. `blueprint` and `structure`
		/// belong to `entity`. The caller has already confirmed the blueprint is a non-Complete
		/// foundation/wall.
		void completeBlueprintNow(EntityID entity, StructureBlueprint& blueprint);

		engine::construction::ConstructionWorld*				  m_constructionWorld = nullptr;
		const engine::assets::PlacementExecutor*				  m_placementExecutor = nullptr;
		const std::unordered_set<engine::world::ChunkCoordinate>* m_processedChunks = nullptr;

		// DEV/TEST: instant-build flag and the completion callback it (and
		// forceCompleteBlueprint) fire. Off / null in normal play.
		bool					   m_freeBuild = false;
		StructureCompletedCallback m_onStructureCompleted = nullptr;

		// Fired when a demolishing blueprint with no work to undo is removed immediately.
		StructureDeconstructedCallback m_onStructureDeconstructed = nullptr;

		// Spawns loose ground piles for a cancelled blueprint's staged materials. Null in headless
		// contexts (the dump is then skipped; the manifest is cleared regardless).
		DropResourceCallback m_onDropResource = nullptr;

		// Salvage fraction (as a percent) returned when a worked/built structure is demolished. Set
		// from the construction config (constraints.xml refundPercent) by GameScene; the struct
		// default mirrors the config default so headless/test contexts behave without wiring.
		float m_refundPercent = 50.0F;

		// Entities already warned about a missing deconstructed callback. Without the callback the
		// blueprint can't be removed, so it re-enters the no-work branch every tick; this throttles
		// the warning to once per entity (headless/test contexts where no callback is wired).
		std::unordered_set<EntityID> m_warnedNoDeconstructCallback;

		size_t m_activeBlueprintCount = 0;

		// Throttle like the other goal systems: lifecycle changes are slow relative to frames.
		int					 m_frameCounter = 0;
		static constexpr int kUpdateFrameInterval = 30; // ~0.5s at 60fps
	};

} // namespace ecs
