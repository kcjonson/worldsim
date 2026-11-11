# World-space depth sorting (2.5D layering)

**Status:** implemented — Story A (entity Y-sort) landed in PR #248; Story B (committed construction to the ground sub-layer + world/UI flush split) landed 2026-07-03. Walls were cut from the Y-sorted stream (angled walls need per-segment anchoring, not one anchorY); committed walls draw in the ground sub-layer instead, below all uprights.
**Epic:** Handle Visual Layering in the game world (Specboard, Worldsim)
**Type:** technical

## Context & Problem

The world camera is a slightly-off-axis 2.5D view. The tilt is faked entirely in the art: an asset draws its canopy/top up at -Y and its ground contact (trunk base, feet) down at +Y. The camera itself is a pure scaled top-down orthographic projection with no tilt, rotation, or Z axis (`WorldCamera::worldToScreen`, `libs/world/world/camera/WorldCamera.h:145` computes `normalizedY = (worldY - rect.y) / rect.height` then scales to the viewport). So screen-Y is already a linear function of world-Y, and a larger world-Y draws lower on screen, toward the viewer.

Today entities are not depth-ordered by position, so occlusion is wrong. The canonical failure: a colonist standing north of a tree's base (behind it) still paints over the tree's canopy instead of being hidden by it. Desired behavior: for any two upright renderables, the one whose ground-contact point is further south (larger world-Y, nearer the viewer) draws in front. A colonist walks behind a tree when its feet are north of the trunk base, and in front when south of it.

This is world-space 2.5D depth sorting keyed on each entity's ground-contact Y. It is distinct from the UI panel z-index that floats popups over siblings; that machinery is not reused here.

## Current state

Draw order is decided purely by a fixed pass sequence, not by any per-entity key, and there is no depth buffer (`GL_DEPTH_TEST` is disabled in every world pass, so it is pure painter's algorithm, last draw wins).

Per frame, `GameScene::render()` (`apps/world-sim/scenes/game/GameScene.cpp:888`) runs, in order: terrain tiles (`ChunkRenderer`, :903), one monolithic `EntityRenderer` pass (:910), then committed foundations + in-progress construction preview (`m_drawingSystem->render`, :916), room/nav/vision overlays, selection indicator, placement ghost, and finally `gameUI->render()` (:936).

Inside `EntityRenderer::renderFrame` (`libs/world-view/world/rendering/EntityRenderer.cpp`) the entity sub-passes are hardcoded: baked static flora/rocks via one `glDrawElements` per chunk, then GPU-instanced groundcover grass, then dynamic ECS entities. Because dynamic entities are a strictly later pass than static flora, a colonist always paints over any tree regardless of north/south position. That is the root defect.

Supporting facts:
- `assets::PlacedEntity` (`libs/assets/assets/placement/SpatialIndex.h`) carries only `defName`, `position`, `rotation`, `scale`, `colorTint`, and an optional `partTransforms` pointer. No depth/anchor field.
- Static entities bake once per chunk in spatial-index insertion order, split into two height buckets (short <1m, tall >=1m) purely to drive a far-zoom impostor fade, not ordering (`BakedEntityMesh.cpp`).
- `DynamicEntityRenderSystem` builds the per-frame render vector in raw ECS-view order; animated colonists accumulate into a CPU triangle batch flushed dead-last (`InstancedEntityRenderer.cpp`) "so limbs sit on top."
- Anchor semantics are inconsistent: static/baked entities use `position` as the mesh origin (trunk base near origin), while `DynamicEntityRenderSystem` re-centers colonists to their mesh bbox center and treats packaged items as a bottom baseline.
- Committed foundations and walls render in the construction pass **after** all entities (`GameScene.cpp:916`), so a floor paints over a colonist standing on it, and construction can appear over UI in some cases.

The only sort primitive in the codebase, `BatchRenderer`'s DrawGroup z-index stable-sort, lives in `libs/renderer` but is fed only by the UI component layer and is bypassed entirely by the raw-GL baked and instanced world paths.

## Goals & non-goals

**Goals**
- Correct north/south occlusion among upright world entities (trees, bushes, rocks/boulders, walls, colonists, dropped/packaged items), driven by a single ground-contact Y key.
- Walls and foundations become ECS world entities in the game layer, at the back, instead of a post-entity construction overlay. Fixes the over-UI bug as a side effect.
- No framerate regression. Preserve GPU instancing and per-chunk baking for the geometry that does not need reordering.
- No shader, camera, or depth-buffer changes.

**Non-goals**
- Canopy fade / transparency when a colonist is behind a tree. Cut, not deferred. Correct ordering is enough; a pawn may be fully hidden behind a tree.
- Per-pixel occlusion, per-tuft groundcover ordering, multi-tile footprint occluders.

## Sort key definition

Add `float anchorY` to `assets::PlacedEntity` = the bottom-most (max) world-Y of the renderable's mesh, its ground-contact line. Invariant: larger `anchorY` draws later, so it appears in front.

Per-type derivation reconciles the two current position conventions into one rule:
- **Static occluders** (trees, bushes, rocks): `anchorY = position.y + anchorLocalY * scale`. `anchorLocalY` (`WorldDepthSort.h`) is the lower edge of the def's footprint for the instance's age class (`offsetMeters.y + halfExtentsMeters.y` of a rect, the lowest point of a polygon) when the def has one, so a tree sorts by its trunk foot and the contact shadow, root flare, and grass it draws below the foot do not pull it forward. A def without a footprint uses the bottom of its own (def, age class, variant) template mesh. A simple (SVG) def's footprint is authored about its drawing's bbox centre and is converted into the mesh's frame, the one the entity's position is the origin of; a procedural def's already is in it. Computed once at placement and stored on the `PlacedEntity`, not baked into the frozen mesh. Rotation is not applied to either, as before (only scatter rotates, and of the footprinted defs only the big rock).
- **Dynamic entities**: the same rule through `anchorLocalY`. Colonists have no footprint, so it is their feet, `anchorY = position.y + meshMaxY * scale`, reusing the mesh bbox min/max already scanned in `DynamicEntityRenderSystem`; a dev-spawned tree sorts by its trunk foot like a placed one.
- **Packaged furniture**: three sprites from `packagedLayout`, crate back, item (fitted inside), crate front, all with `anchorY` = the crate's bottom baseline. Equal keys + the stable sort keep that build order after the global sort, so the item draws inside the crate.
- **Loose ground piles** (`ResourceStack`): each of the pile's 1-5 sprites (`pileLayout`) is its own renderable with the colonist rule applied at that sprite's offset center, so the back rows sort behind the front rows and the heap interleaves correctly with anything standing in it.

Ties break by stable sort (prior order preserved); add a deterministic secondary key (defName or entity id) if a stable order across frames is needed.

## Layer model

From back to front:

1. Terrain tiles (`ChunkRenderer`), unchanged, never sorted.
2. Committed foundations, a flat ground sub-layer (ECS floor entities), above terrain and below all uprights.
3. Groundcover grass + baked short flora (<1m), single unsorted passes on their existing fast paths.
4. The Y-sorted upright stream: tall flora, rocks, walls, colonists, dropped/packaged items, merged and drawn in ascending `anchorY`.
5. In-progress construction preview, placement ghost, selection indicator.
6. UI.

Deliberate simplification: a colonist always covers sub-1m grass and short flora even when standing slightly south of it, because those stay on the fast background path. Acceptable, and consistent with the far-zoom impostor-cutoff rationale that already excludes dense cheap geometry from per-entity work.

## Proposed approach

Global painter's Y-sort of the "upright occluders + actors" subset only.

Each frame, in `EntityRenderer::renderFrame`, after drawing the background (terrain, foundations, groundcover, short flora):

1. **Gather.** `WorldDepthGather` keeps each chunk's tall statics in a list stable-sorted by `anchorY`, rebuilt only when that chunk's `SpatialIndex::version()` changes, and culls them by origin against `computeVisibleBounds` each frame; the dynamic ECS entities are culled the same way. Items are lightweight `{anchorY, const PlacedEntity*, isAnimated}`.
2. **Sort.** Each chunk's culled stretch is already in order, so the stretches (chunks in (y, x) order, then the dynamic entities, stable-sorted on their own) are merged pairwise with `std::inplace_merge`. The result is the sequence one `std::stable_sort` of all of them would give. Querying and sorting every visible static each frame cost 17.8 ms at zoom 0.25 on the river view (46,000 trees, each chunk's query walking the whole view's cells); this costs 0.8 ms.
3. **Submit in order.** Emit the entities so any two whose drawn areas can overlap are submitted in depth order; two that cannot touch may swap, since the result is the same. `RunPlanner` picks each non-animated entity's `drawInstanced` run. The frame's runs form a timeline of layers, and in each layer every template mesh has one place, in the order meshes first appeared that frame. An entity takes the earliest place of its mesh at or after the latest place taken by anything that shares a cell of a screen-space overlap grid with it (`OverlapGrid`: bounds from the drawn level's local extent, scale, and rotation, projected to the screen and shrunk by half a pixel a side; cells 4 px square): the same layer when that place's mesh comes earlier in the layer or is its own, the next layer otherwise. An entity that overlaps nothing goes to the first layer however late it sorts, and within a run instances keep the stream's order. Two entities whose screen bounds are both at most 8 px on the longer side (`kUnorderedFootprintPixels`) may draw in either order; order against anything larger is kept. A world grid sized to the view had 5 m cells at zoom 0.25, where a tree was 1 m, so nearly every tree split a run (7,616 draws); the screen grid alone left 6,505, and the small-pair rule 550. At the river view, zoom 0.25, letting small pairs swap changed 154 of 5.3 million pixels against exact order. After trees grew 2.25x and doubled in density (2026-11-10) the rule before this one, which let an entity join only its mesh's latest open run, drew 5,410 runs at zoom 0.5 (27.6 ms a frame); the layered timeline drew 2,018 with order exact (a footprint widened by 1 px), and the half-pixel shrink 603, changing 15 of 5.3 million pixels at zoom 0.5 (one- and two-pixel specks of one leaf tone for another), 119 at zoom 0.25, and none at zooms 1 and 3. `/api/state?what=uprights` reports `uprightOverlapSplits`, the runs opened for a mesh that already had an open one. An animated colonist or the selection outline draws every open run first, in timeline order, then flushes its CPU batch at its sorted position, keeping the existing 16-bit index-overflow flush; everything after it goes in later layers.

Overlapping pairs keep the painter's order, so the UI `BatchRenderer` DrawGroup z-sort is not reused for the world; it stays a clean UI-only primitive.

To make trees interleavable, stop routing the tall bucket into the baked mesh (or skip `kTallFloraBucket` at draw in `BakedChunkRenderer`); short flora keeps its bake and impostor fade. Tall occluders are gathered live from the spatial index into the sorted stream.

Rejected alternatives, for the record:
- **GL depth buffer.** Every world pass draws alpha-blended, anti-aliased cel-shaded edges; `GL_DEPTH_TEST` hard-rejects fragments and tears transparent/AA edges and overlapping canopies. The pure scaled-ortho camera makes a CPU painter sort exact anyway, so a depth buffer adds hazard with no benefit.
- **Reuse the UI DrawGroup z-sort for the world.** The baked and instanced paths bypass `BatchRenderer` entirely; forcing all entities through per-`add*` groups defeats GPU instancing and, once any explicit z is set, pays a full `stable_sort` plus a complete emit-index-buffer rebuild every frame (measured in `libs/renderer/primitives/ZSort.bench.cpp`).
- **Zoom-scaled depth bands.** More machinery for only approximate within-band ordering, plus band-boundary popping. Kept in reserve as a perf fallback (see Phasing).

## Templates, routing, and the gather margin

Added with tree variety (`docs/technical/asset-system/tree-variety.md`, R3). Every placed static and dynamic entity carries `PlacedEntity::templateId`, the registry's integer id for its (def, age class, variant) template, resolved once where the entity is made (placement, or `DynamicEntityRenderSystem` for actors). `InstancedEntityRenderer` keys its GPU meshes and its instanced runs by that id, so `emitSorted` does no string work per entity. A mesh handle starts with room for 4,096 instances and is re-uploaded at the next power of two when a run outgrows it. A template has a mesh per detail level (`<detailLevels>`); its reference level uploads the first frame the template is drawn, outside the upload budget, and only the other levels are built off the main thread and queue behind the budget, the nearest ready level drawing meanwhile.

Tall or short is decided per def, not per instance: `AssetRegistry::isTallDef` measures the def's mature variant 0, unscaled, against `kShortFloraMaxHeight` (`FloraRouting.h`). Placement stores the answer in `isTallOccluder` and the bake skips exactly the entities that have it set, so the two can't disagree.

The visible-rect gather is padded by `entityCullMarginMeters(AssetRegistry::maxReachMeters())` (`RenderContext.h`): the farthest any non-groundcover template draws from its origin times its def's largest placement scale, never under 2 m (a def with detail levels is measured at its reference level and widened by `kLevelReachMargin`). Statics and dynamic entities are culled by origin against it, so a wide crown whose trunk is outside the view is still gathered.

## Walls & foundations as ECS entities

Committed foundations and walls are "essentially just dynamically drawn entities." Represent them as ECS world entities drawn through the standard entity path (`DynamicEntityRenderSystem`), not the `DrawingSystem` overlay. Once they are entities they inherit `anchorY` and the sort:
- Foundations carry a floor/ground render role and draw in the ground sub-layer (layer 2 above), above terrain and below uprights.
- Walls carry an upright-occluder role with an `anchorY` at their base and join the Y-sorted stream, so a colonist sorts behind or in front of a wall by base-Y.

`ConstructionWorld` (the topology store that `ConstructionSystem`, `NavigationSystem`, and `WallCollisionSystem` read, wired at `GameScene.cpp:335-340`) stays the gameplay/nav/collision source of truth. Two candidate mechanisms for the render representation, to decide during implementation:
- Mirror committed segments as ECS render entities, spawned on commit and synced on demolish/edit.
- A thin adapter that emits committed segments as dynamic `PlacedEntity` records each frame.

Either way, removing the committed-construction draw from the post-entity overlay pass fixes the bug where construction paints over UI, because it is no longer a late pass. The in-progress preview/ghost stays near the top (below UI). Document what `DrawingSystem::render` draws today and exactly what moves.

**Implementation decision (Story B, 2026-07-03):** neither candidate mechanism. With walls cut from the Y-sorted stream, nothing in committed construction needs an `anchorY`, so the layer outcome is achieved by pass order alone: `DrawingSystem::render` split into `renderCommitted` (foundations, walls, openings — called between the terrain and entity passes and flushed immediately, forming the ground sub-layer) and `renderPreview` (in-progress feedback, after entities with the other ghosts). A `Primitives::flush()` before `gameUI->render()` separates the world and UI z-sort domains, so no world primitive can sort above UI regardless of zIndex — the over-UI bug is closed structurally, not by z-value bookkeeping. Consequence of the walls cut: a colonist never hides behind a wall (construction is always behind entities); revisit if/when walls get per-segment anchoring. The ECS-entity render representation is deferred to C6 (baked element-emitter), which replaces this interim `DrawingSystem` render path wholesale.

**City-scale follow-up (2026-07-03):** the interim path no longer re-triangulates per frame. Committed geometry (foundation rings, wall bands/junctions, opening footprints) is cached in world space against `ConstructionWorld::version()` and culled per item to the viewport (`CommittedGeometryCache`, app construction folder), so steady-state cost scales with visible construction, not total built. Measured at 90 buildings: scene render 3.11 -> 1.93 ms on-screen, 0.71 ms off-screen. resolveWallBands now runs once per construction change. Build progress stays a per-frame ECS read; C6 still supersedes this path eventually.

## Edge cases

- **Canopy vertical extent.** A single base-Y compare is an approximation. It resolves the target case correctly, but where two wide canopies overlap across a large base-Y gap there can be minor ordering artifacts. Accepted for Phase 1.
- **Overlapping/identical bases.** Stable sort keeps deterministic order; define the secondary tie-break.
- **Animated colonists.** Must fold into the ordered stream while preserving per-part deformation and the 16-bit index-overflow flush.
- **Crate layers + item.** Preserve the back, item, front order after the global sort.
- **Multi-tile objects.** Point-anchor only in Phase 1; footprint occluders deferred.
- **Sort direction.** Confirm +Y is toward-viewer / lower-on-screen and that there is no GL row-flip on the on-screen world FBO path (the PNG readback path flips, the on-screen path must not), so the sort direction is correct.

## Performance

Framerate is a hard gate: no regression, benchmarked before and after.

Two cost components:
1. The sort itself is negligible. The sortable set is the frustum-culled visible occluders + actors, realistically hundreds to low thousands; a `std::stable_sort` of `{float, pointer}` is tens of microseconds.
2. The real cost is pulling tall occluders off the baked/instanced fast path into the sorted stream, the same per-entity work any entity in the stream costs. Bounded by frustum culling and the far-zoom impostor cutoff.

Levers to hold the budget:
- Keep the un-reordered majority (groundcover ~486k tufts, short flora, terrain, foundations) on untouched fast paths.
- Preserve instanced runs in the sorted stream.
- If dense forests regress, pull only trees near an actor into the sorted stream while far trees stay baked (near-actor neighborhood via `queryRect` around each visible actor).

Benchmark methodology: capture frame time, draw calls, and entity/triangle counts at a representative scene and a zoomed-out dense forest before any change; compare after. Add a world-sort micro-bench mirroring `ZSort.bench.cpp` for the gather + sort + emit cost at representative visible-entity counts. Capture and report any incidental framerate wins found while restructuring.

## Testing & verification

- **Unit tests.** `anchorY` derivation for a static tree, a colonist, and a packaged item matches the canonical bottom-most-world-Y rule; given a set of `anchorY`s, the emitted order is ascending, stable, and deterministic.
- **Walk past a tree.** In world-sim, script a colonist walking a straight north->south line through a single tree; confirm it is occluded by the canopy while its feet are north of the trunk base, and renders in front once south. Before/after screenshots are the primary proof.
- **Walk past a wall.** Same check against a committed wall.
- **Foundations behind.** A colonist standing on a foundation draws over it, not under it.
- **Dense-forest stress.** Zoom out over a dense forest with several colonists; confirm correct interleaving, no per-frame ordering flicker at rest, and no frame-time regression vs baseline.
- **Fast-path regression.** Terrain, groundcover, and short-flora impostor fade render identically before/after (screenshot diff at multiple zooms).
- **Nav/collision intact.** `ConstructionWorld`-driven navmesh and wall collision still behave after walls/foundations become entities.

## Phasing

- **Phase 1.** `anchorY` field + producers, tall-occluder exclusion from the bake, merged gather/sort/submit in `EntityRenderer`, walls/foundations as ECS entities, tests + before/after benchmarks.
- **Phase 2 (only if a concrete need or perf regression appears).** Trunk/canopy split-sprite, multi-tile footprint occluders, zoom-scaled depth-band bucketing as the instancing-preserving perf fallback. No canopy-fade phase.

## Key files

- `apps/world-sim/scenes/game/GameScene.cpp` — per-frame pass order; the construction/foundation draw to relocate.
- `libs/world-view/world/rendering/EntityRenderer.cpp` — the choke point where the sorted gather/emit lives.
- `libs/world-view/world/rendering/InstancedEntityRenderer.cpp` — extract an "emit one entity" helper; fold animated colonists in at sorted position.
- `libs/assets/assets/placement/BakedEntityMesh.cpp` / `BakedChunkRenderer.cpp` — expose template maxY, exclude tall occluders from the baked draw.
- `libs/world-view/world/rendering/DynamicEntityRenderSystem.cpp` — dynamic `anchorY`.
- `libs/assets/assets/placement/SpatialIndex.h` — `PlacedEntity.anchorY`, `templateId`, `queryRect`.
- `libs/assets/assets/placement/FloraRouting.h` — `kShortFloraMaxHeight` and the per-def tall rule.
- `scenes/game/world/construction/DrawingSystem.*`, `ConstructionWorld`, `ecs/systems/ConstructionSystem`, `NavigationSystem`, `WallCollisionSystem` — walls/foundations as entities while keeping topology as nav/collision truth.
- `libs/renderer/primitives/ZSort.bench.cpp` — benchmark reference.
