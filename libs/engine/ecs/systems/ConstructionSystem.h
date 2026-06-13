#pragma once

// ConstructionSystem - drives the foundation build lifecycle as a goal source.
//
// Watches every entity carrying Structure + StructureBlueprint and walks it
// through the construction loop (building-construction-architecture.md D7):
//
//   Clearing           -> emit Harvest goals for choppable entities sitting on
//                         the footprint; advance to AwaitingMaterials once the
//                         footprint is clear.
//   AwaitingMaterials  -> reconcile delivered[] from the blueprint's delivery
//                         Inventory, emit a Harvest+Haul chain per outstanding
//                         material (Wood from trees, hauled into the blueprint
//                         inventory); advance to UnderConstruction once
//                         materialsComplete().
//   UnderConstruction  -> emit one Build goal; ActionSystem's Build action
//                         advances workDone. Multiple colonists may build the
//                         same blueprint concurrently.
//   Complete           -> stop emitting; the ConstructionWorld state flip and
//                         toast happen via ActionSystem's structure-completed
//                         callback (wired in GameScene).
//
// Goals are emitted through GoalTaskRegistry with owner ConstructionGoalSystem
// so AIDecisionSystem scores and picks them up like any other goal. Goals are
// cleaned up when a blueprint entity disappears or completes (BuildGoalSystem's
// watch/emit/cleanup shape).
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

	/// Pure phase-decision result, independent of ECS/placement wiring so it can be
	/// unit-tested directly. Tells the system what the blueprint's phase SHOULD be and
	/// which goal kinds to emit this tick.
	struct ConstructionDecision {
		StructureBlueprint::BuildPhase nextPhase = StructureBlueprint::BuildPhase::Clearing;
		bool emitClearGoals	   = false; // footprint not yet clear: emit Harvest for blockers
		bool emitMaterialGoals = false; // clear, materials outstanding: emit Harvest+Haul chains
		bool emitBuildGoal	   = false; // materials staged: emit a Build goal
	};

	/// Decide the blueprint's phase and which goals to emit, from the two facts the
	/// system measures each tick: whether the footprint is clear of blocking entities,
	/// and whether the material manifest is satisfied. Pure; no side effects.
	///
	/// Demolishing blueprints emit nothing (the Deconstruct path owns them).
	[[nodiscard]] ConstructionDecision decideConstructionPhase(
		const StructureBlueprint& blueprint,
		bool					  footprintClear,
		bool					  materialsComplete
	);

	/// How much of a material a colonist still needs to HARVEST, given how much the
	/// site still needs (`remaining`) and how much is already carried toward it
	/// (`carried`, summed across colonists). Bounds chopping so a colonist that
	/// already carries enough switches to delivering instead of topping up forever.
	/// Pure; unit-tested directly.
	[[nodiscard]] uint32_t constructionHarvestDemand(uint32_t remaining, uint32_t carried);

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
		void setConstructionWorld(engine::construction::ConstructionWorld* constructionWorld) {
			m_constructionWorld = constructionWorld;
		}
		void setPlacementData(
			const engine::assets::PlacementExecutor*					placementExecutor,
			const std::unordered_set<engine::world::ChunkCoordinate>*	processedChunks
		) {
			m_placementExecutor = placementExecutor;
			m_processedChunks	= processedChunks;
		}

		/// Debug: count of blueprints the system is actively driving this tick.
		[[nodiscard]] size_t getActiveBlueprintCount() const { return m_activeBlueprintCount; }

	  private:
		/// True if no Harvestable placed entity overlaps the foundation's footprint AABB.
		/// v1 clearing rule (see header): only Harvestable blockers gate clearing.
		[[nodiscard]] bool isFootprintClear(uint64_t foundationId) const;

		/// Sync the blueprint's delivered[] manifest from its delivery Inventory so
		/// materialsComplete() reflects what is physically on site. Inventory is the
		/// single source of truth; delivered[] is the derived mirror the gate reads.
		void reconcileDelivered(EntityID blueprintEntity, StructureBlueprint& blueprint);

		/// Emit Harvest goals to clear Harvestable blockers off the footprint. Reuses the
		/// Harvest goal shape (yield = whatever the blocker yields) so chopping a tree both
		/// clears the site and produces Wood for the manifest.
		void emitClearGoals(EntityID blueprintEntity, uint64_t foundationId);

		/// Emit a Harvest+Haul chain per outstanding material (CraftingGoalSystem's pattern):
		/// Harvest yields the material into a colonist's inventory, the dependent Haul carries
		/// it to the blueprint and deposits into its Inventory.
		void emitMaterialGoals(EntityID blueprintEntity, const StructureBlueprint& blueprint);

		/// Sum how much of a material colonists are currently carrying (the build site's own
		/// delivery Inventory is excluded). Used to bound harvest demand so a colonist that
		/// already carries enough delivers it instead of chopping more.
		[[nodiscard]] uint32_t carriedAmount(EntityID buildSite, const std::string& defName) const;

		/// Emit a single Build goal at the foundation's work slot (centroid). ActionSystem
		/// turns this into Build actions that advance workDone.
		void emitBuildGoal(EntityID blueprintEntity, const StructureBlueprint& blueprint);

		engine::construction::ConstructionWorld*				 m_constructionWorld = nullptr;
		const engine::assets::PlacementExecutor*				 m_placementExecutor = nullptr;
		const std::unordered_set<engine::world::ChunkCoordinate>* m_processedChunks = nullptr;

		size_t m_activeBlueprintCount = 0;

		// Throttle like the other goal systems: lifecycle changes are slow relative to frames.
		int					 m_frameCounter = 0;
		static constexpr int kUpdateFrameInterval = 30; // ~0.5s at 60fps
	};

} // namespace ecs
