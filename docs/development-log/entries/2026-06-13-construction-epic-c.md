# 2026-06-13 - Construction Epic C: foundations end-to-end

## Summary

Implemented the freeform foundation construction vertical slice on top of `libs/geometry` (Epic A, merged as #135) and the goal-driven task system (#115). A player can open the Build menu, draw a polygon foundation click-by-click with live validation and snapping, commit it (quantized to integer mm in ConstructionWorld, spawning a blueprint ECS entity), select it for an info panel, and demolish it. The colony-build lifecycle — clear the footprint, haul Wood, build with skill-scaled work, flip to Built — is implemented and wired through the goal system, with a material economy extension (a Wood item and choppable trees) to feed it. 434 engine tests pass. The one unproven link is the full chop→haul→build loop in a live world, blocked by the treeless quickstart spawn (see Next Steps).

## Details

Built as a sequence of merged sub-branches, each reviewed and tested before merge:

- **Config (C1):** `assets/config/construction/{materials,constraints,snapping}.xml` + `ConstructionRegistry` (pugixml singleton, meters with pre-quantized mm mirrors) + `ConfigValidator::validateConstruction`, loaded at startup. Wood and Stone materials; pathing-clearance/min-angle/spacing/area/refund/snapping constants.
- **ConstructionWorld (C2):** `libs/engine/construction/` topology store — stable FoundationId, int64-mm rings, commit/add/subtract/remove via the geometry ring booleans, interior-overlap rejection (edge/vertex adjacency allowed), point-in-foundation hit test, footprint AABB, a version counter for consumers, reject-don't-repair structural invariants. 21 tests.
- **Components (C3):** `Structure {kind, graphId}`, `StructureBlueprint {required/delivered manifest, workTotal/workDone, progress(), BuildPhase}`, `StructureHealth`. 21 tests.
- **Drawing tool (C4):** `DrawingSystem` GameScene subsystem (sibling to PlacementSystem), `FoundationTool`, exact-logic `ConstructionValidator` and `SnapEngine` in `libs/engine/construction` (18 tests), and a docked config strip. Click-by-click polygon, rubber-band preview, origin-close, Alt for freeform, live area/point readouts, validity coloring. Commit quantizes and spawns the blueprint entity with a manifest (area x material cost) and work total. Verified in the sandbox end to end.
- **Selection (C7):** `FoundationSelection` variant (lowest priority), point-in-polygon hit test via ConstructionWorld, `adaptFoundation` info panel (material, area, state, work progress bar, materials summary, Demolish), gold selection outline. Immediate whole-foundation demolish. Verified in the sandbox.
- **Material economy (C5a):** a `Wood` RawMaterial item, and Harvestable capabilities on Oak/Maple trees yielding Wood (reusing the same path WoodyBush uses for Stick — no new action or work type). The config material name "Wood" aligns with the item defName so the manifest resolves to haul goals.
- **Build/Deconstruct actions (C5b):** `ActionType::Build`/`Deconstruct`, the previously-stubbed `ProgressEffect` implemented as continuous `workDone += constructionWorkRate(skill) x dt` (untrained still progresses, ~2.6x at master), completion flips BuildPhase and fires `setStructureCompletedCallback`/`setStructureDeconstructedCallback` (the cross-layer signal pattern ActionSystem already uses). 14 tests.
- **ConstructionSystem (C5c):** ECS system (priority 58) watching blueprints and emitting goals per phase via GoalTaskRegistry (new `GoalOwner::ConstructionGoalSystem`): Clearing emits Harvest goals for Harvestable footprint blockers; AwaitingMaterials emits per-material Harvest+Haul goals sized to live remaining; UnderConstruction emits a Build goal. The blueprint entity carries a restricted Inventory; hauled Wood deposits into it and `reconcileDelivered` derives `delivered[]` from inventory each tick (inventory is the source of truth). AIDecisionSystem gained Build-option evaluation and surfaces construction hauls from carried inventory. GameScene registers the system and wires both completion callbacks (Built state flip + toast; deconstruct removal). Build progress renders as an opacity ramp on the committed-foundation fill. 6 tests.

## Key decisions

- Build work is a continuous tick advancing `workDone`, not a re-issued quantum — matches D7 verbatim and the render progress-prefix model, and lets multiple builders/sessions accumulate.
- Material delivery uses the blueprint entity's Inventory as the single source of truth, with `delivered[]` a derived mirror; the strict harvest-all-then-haul dependency was dropped so colonists don't hoard random tree yields.
- Cross-layer completion (libs/engine ActionSystem/ConstructionSystem flipping app-owned ConstructionWorld state) goes through callbacks, consistent with existing ActionSystem callback wiring.
- Demolish is immediate for Epic C; the work-driven Deconstruct with refund/cascade is wired at the action layer but the refund/cascade is deferred.

## Related Documentation

- [Building & Construction Architecture](../../technical/building-construction-architecture.md)
- [Building & Construction design spec](../../design/game-systems/world/building-construction.md)
- [Geometry Foundations dev log](./2026-06-12-geometry-foundations.md)

## Next Steps

- **Prove the end-to-end loop.** The quickstart lands in a treeless Beach biome with no Wood source, so the chop→haul→build loop can't complete in a live sim there (each link is unit-tested or confirmed in isolation, but the integrated loop is unverified). Needs a forested spawn or a debug affordance (grant Wood at a site / spawn trees) to demonstrate and tune. This gates calling Epic C done.
- Polish deferred from this slice: baked element-emitter index-prefix progress render (D8), a `ConstructionProgressSlot` in the info panel (D11), N discrete builder work slots from `builderCap`, deconstruct material refund + cascade, mining/haul-away clearing of non-harvestable obstructions.
- Then Epics D (walls), E (rooms), F (openings), G (editing). Navigation remains a separate epic gating walls as gameplay.
