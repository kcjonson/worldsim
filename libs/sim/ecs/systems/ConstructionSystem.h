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
//                         footprint is known clear (every chunk under it placed).
//                         (Foundations only.)
//   AwaitingMaterials  -> accept and emit a Harvest per outstanding material (Wood
//                         from trees, carried to the site where the deposit records it
//                         directly onto the blueprint's delivered[] manifest); advance
//                         to UnderConstruction once materialsComplete(), after one more
//                         footprint check (a blocker found then sends it back to Clearing).
//                         A foundation is built on bare ground: entering UnderConstruction,
//                         what still lies or grows on its footprint leaves it (bareSites).
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
// is the Build goal and the root material deliveries count toward. Every phase goal
// (clear-Harvest, per-material Harvest) is a CHILD of it (parentGoalId = umbrella id), which exempts the
// children from the registry's single-top-level-goal-per-destination guard so a
// multi-material site can carry several Harvest goals at once. The umbrella
// is Blocked while clearing or awaiting materials and flips Available only at
// UnderConstruction, exactly like a Craft goal. Goals are cleaned up via
// removeGoalWithChildren when a blueprint entity disappears or completes.
//
// This system lives in libs/sim and links ConstructionWorld directly (it is
// part of the sim lib). It does NOT flip ConstructionWorld state itself; that
// is the app's job via the ActionSystem completion callback, keeping the
// cross-layer signal on the same callback pattern ActionSystem already uses.
//
// Priority 58: after BuildGoalSystem (57), before AIDecisionSystem (60), so a
// freshly emitted goal is visible the same frame the AI evaluates.

#include "../DropResourceCallback.h"
#include "../EntityID.h"
#include "../ISystem.h"
#include "../components/StructureBlueprint.h"

#include <glm/vec2.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_set>
#include <vector>

namespace world {
	struct ChunkCoordinate;
}

namespace assets {
	class PlacementExecutor;
	class AssetRegistry;
} // namespace assets

namespace construction {
	class ConstructionWorld;
}

namespace ecs {

	class World;
	struct Structure;				 // defined in components/Structure.h
	enum class GoalStatus : uint8_t; // defined in GoalTaskRegistry.h
	struct FootprintBounds;			 // defined in GoalTaskRegistry.h

	/// Pure phase-decision result, independent of ECS/placement wiring so it can be
	/// unit-tested directly. Tells the system what the blueprint's phase SHOULD be and
	/// which goal kinds to emit this tick.
	struct ConstructionDecision {
		StructureBlueprint::BuildPhase nextPhase = StructureBlueprint::BuildPhase::Clearing;
		bool						   emitClearGoals = false;	  // footprint not yet clear: emit Harvest for blockers
		bool						   emitMaterialGoals = false; // clear, materials outstanding: accept materials, emit their Harvests
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
		void setConstructionWorld(construction::ConstructionWorld* constructionWorld) { m_constructionWorld = constructionWorld; }
		void setPlacementData(assets::PlacementExecutor* placementExecutor, const std::unordered_set<world::ChunkCoordinate>* processedChunks) {
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
		void setDropResourceCallback(DropResourceCallback callback) { m_onDropResource = std::move(callback); }

		/// Drop every construction goal a blueprint owns (the umbrella and its phase
		/// children). The next tick rebuilds the tree from the blueprint's current
		/// footprint, manifest, and position; used when a foundation is reshaped in place.
		void resetBlueprintGoals(EntityID blueprintEntity);

		/// DEV/TEST ONLY. Credit `amount` of `defName` onto the delivered[] manifests of all
		/// active (non-Complete) build sites, capped at each site's outstanding need so no
		/// site is over-filled, and stops once `amount` is exhausted. Returns the total
		/// credited across sites. Reuses StructureBlueprint::recordDelivery(), so it never
		/// duplicates the manifest math. Used by /api/dev/give?where=site.
		uint32_t creditMaterialToSites(const std::string& defName, uint32_t amount);

	  private:
		/// The foundation's footprint AABB in world meters: the one definition of "inside the
		/// footprint" for both the clear gate and the clear goal a colonist serves.
		[[nodiscard]] FootprintBounds footprintBounds(uint64_t foundationId) const;

		/// A runtime-spawned harvestable (an ECS entity, not in the placement index): a colonist
		/// remembers it like placed flora, so it blocks a site and serves its clear goal the same way.
		struct SpawnedHarvestable {
			uint32_t  defNameId = 0;
			glm::vec2 position{0.0F, 0.0F};
		};

		/// Collect every spawned harvestable into m_spawnedHarvestables, once per pass that has a
		/// site to check (clearing, or entering UnderConstruction), so each site's check is a scan of
		/// those few rather than of every entity.
		void gatherSpawnedHarvestables();

		/// True while nothing in the world blocks the footprint (FootprintBounds::blockedBy), placed
		/// flora and spawned harvestables (m_spawnedHarvestables, gathered this pass) alike: the set
		/// the clear goal is served by. A footprint over a chunk whose flora isn't placed yet is not known
		/// clear and reads false.
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
		/// removed immediately via the deconstructed-completion callback (the SAME removal path a
		/// worked deconstruct fires on completion), instead of emitting an un-actionable goal. Its
		/// staged materials are dumped here at order-time (100%, delivered[] cleared) rather than at
		/// the callback, since the site vanishes at once.
		void decideDeconstruct(EntityID blueprintEntity, const Structure& structure, StructureBlueprint& blueprint);

		/// Dump ALL of a demolished blueprint's staged delivered[] materials to the ground as loose,
		/// haulable piles at its position (via the drop-resource callback, which splits each quantity
		/// into stacks and scatters them), then clear delivered[]. Only the no-work cancel branch calls
		/// it: there's no structure to tear down, so the site is removed promptly and its in-flight
		/// materials go loose at once. A worked/built teardown does NOT call this; its salvage drop
		/// happens at deconstruct-COMPLETION (the structure is removed) via GameScene's deconstructed
		/// callback, which reads the still-intact delivered[] and returns only the refundPercent% cut.
		/// No-op when delivered[] is empty or no drop callback is wired (headless); delivered[] is
		/// cleared regardless so the manifest never re-dumps.
		void dumpDeliveredToGround(EntityID blueprintEntity, StructureBlueprint& blueprint);

		/// Cascade gate: true once a demolishing structure's dependents are gone, so its
		/// Deconstruct goal may go Available. A foundation waits until no wall is hosted on it and
		/// no extension waits to merge into it; a wall waits until no opening sits on it; an
		/// opening has no dependents (always cleared).
		/// True (ungated) when there is no ConstructionWorld wired (headless contexts).
		[[nodiscard]] bool deconstructDependentsCleared(const Structure& structure) const;

		/// Emit the blueprint's one clear-Harvest CHILD under the umbrella, unless it already has one.
		/// Any Harvestable a colonist knows inside the footprint serves it; each served harvest
		/// removes its blocker but records nothing, so the goal stands until the footprint is clear
		/// and update() retires it then.
		void emitClearGoals(EntityID blueprintEntity, uint64_t foundationId, uint64_t umbrellaGoalId);

		/// While materials are due, the umbrella accepts each material the site still lacks (any
		/// colonist carrying it may deliver it), and one Harvest CHILD per such material marks that
		/// cutting it is open. Each cut carries its yield to the blueprint, where the deposit records it
		/// onto delivered[].
		void emitMaterialGoals(EntityID blueprintEntity, const StructureBlueprint& blueprint, uint64_t umbrellaGoalId);

		/// Retire any leftover child goals of a kind no longer relevant to the current phase
		/// (e.g. the clear-Harvest child once the footprint is clear, or all material children
		/// once materials are staged). The umbrella itself is left in place.
		void retireChildGoals(EntityID blueprintEntity, uint64_t umbrellaGoalId, bool keepMaterialChildren);
		/// Shared by the free-build flag path and forceCompleteBlueprint: stage materials,
		/// finish work, flip phase, fire the completion callback. `blueprint` and `structure`
		/// belong to `entity`. The caller has already confirmed the blueprint is a non-Complete
		/// foundation/wall.
		void completeBlueprintNow(EntityID entity, StructureBlueprint& blueprint);

		construction::ConstructionWorld*				  m_constructionWorld = nullptr;
		assets::PlacementExecutor*						  m_placementExecutor = nullptr;
		const std::unordered_set<world::ChunkCoordinate>* m_processedChunks = nullptr;

		// DEV/TEST: instant-build flag and the completion callback it (and
		// forceCompleteBlueprint) fire. Off / null in normal play.
		bool					   m_freeBuild = false;
		StructureCompletedCallback m_onStructureCompleted = nullptr;

		// Fired when a demolishing blueprint with no work to undo is removed immediately.
		StructureDeconstructedCallback m_onStructureDeconstructed = nullptr;

		// Spawns loose ground piles for a cancelled blueprint's staged materials. Null in headless
		// contexts (the dump is then skipped; the manifest is cleared regardless).
		DropResourceCallback m_onDropResource = nullptr;

		// Entities already warned about a missing deconstructed callback. Without the callback the
		// blueprint can't be removed, so it re-enters the no-work branch every tick; this throttles
		// the warning to once per entity (headless/test contexts where no callback is wired).
		std::unordered_set<EntityID> m_warnedNoDeconstructCallback;

		size_t m_activeBlueprintCount = 0;

		// This pass's spawned harvestables, gathered by the first site still clearing (reused storage)
		std::vector<SpawnedHarvestable> m_spawnedHarvestables;

		/// Footprints of foundations that entered construction this pass, bared by bareSites
		std::vector<FootprintBounds> m_sitesEnteringConstruction;
		static constexpr float		 kPileMoveMargin = 0.5F;

		/// Bare the ground of each site entering construction, so nothing is built over. Harvestables
		/// are gone by then (they hold the site in Clearing). Something to carry, a loose pile or a
		/// placed stone, is dropped again just outside; with no drop path wired it stays where it is.
		/// Every other placed static (groundcover, a rock) is removed.
		void bareSites();
		void removeStaticsFromSites();
		void movePilesOffSites();

		// Throttle like the other goal systems: lifecycle changes are slow relative to frames.
		int					 m_frameCounter = 0;
		static constexpr int kUpdateFrameInterval = 30; // ~0.5s at 60fps
	};

} // namespace ecs
