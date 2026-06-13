# Building & Construction — Technical Architecture

**Status:** Design
**Created:** 2026-06-12
**Design spec:** [/docs/design/game-systems/world/building-construction.md](../design/game-systems/world/building-construction.md)

How we build the freeform construction system: geometry foundations, data model, pathfinding, room detection, task integration, rendering, UI, and the phasing. Player-facing behavior lives in the design spec; this doc is the how.

---

## 1. Current State (Baseline)

What the codebase gives us today, from exploration of the relevant systems:

| Area | State | Relevance |
|------|-------|-----------|
| World units | 1 tile = 1 m (`kTileSize = 1.0F`, `libs/engine/world/chunk/ChunkCoordinate.h`); entities use continuous `Vec2` meters | Design spec's meters map 1:1 to world units, no conversion layer |
| Geometry utilities | Tessellator (monotone decomposition) in `libs/renderer/vector/`; **no** point-in-polygon, segment intersection, polygon offset, or boolean ops anywhere | Must be built; new dependencies needed |
| Pathfinding | **None.** `MovementSystem` beelines to `MovementTarget`; `PhysicsSystem` is Euler integration; no collision, no obstacle queries; colonists walk through everything including water | Construction development proceeds without it; nav must land before walls ship as gameplay (see D5) |
| Spatial indexing | `SpatialIndex` (grid hash) in `libs/engine/assets/placement/`, used for entity discovery/placement, not movement | Reusable for footprint-clearing queries |
| Task system | `GlobalTaskRegistry` + reservations + chain infra; work tiers config-driven; `Build` action and Construction tier 5 already defined in `assets/config/` XML | Construction plugs in as a goal source; aligns with the planned Goal-Driven Task Generation refactor |
| Rendering | Three uber-shader paths (batched / instanced / baked world-space); EntityRenderer baked path = precedent for cached static world geometry with sub-chunk culling; `GhostRenderer` for previews; `RenderToTexture` available | Foundations/walls fit a new baked pass between terrain and entities |
| Asset system | Folder-per-asset XML + SVG, Lua procedural generators with static params, template caching in AssetRegistry; **no per-instance parameterization** | Doors/windows need a parameter mechanism (thickness, material) |
| UI | PlacementSystem state machine + GhostRenderer; SelectionSystem with `Selection` variant + priority constants + point-radius hit test; adapter/ViewModel/slot info panels; `InputEvent` carries modifiers; ToastStack | Drawing tools follow the PlacementSystem pattern; selection extends with polygon/segment hit tests |
| Persistence | None | Structure data must be designed serialization-friendly now, even though save/load comes later |

---

## 2. Architecture Decisions

### D1: Integer-quantized committed geometry

UI and previews work in float world meters. Geometry that gets **committed** (a placed vertex, a closed polygon) is quantized to integer millimeters (`int64` per axis). Rationale:

- Exact vertex equality: merging snapped points is `==`, not epsilon comparison. Epsilon merging is not transitive (A~B, B~C, but not A~C) and produces inconsistent topology; this failure mode is the recurring theme in robust-geometry literature.
- Exact predicates: orientation and intersection tests on integer coordinates are exact with wide multiplication (128-bit intermediates). No epsilon zoo, no Shewchuk machinery.
- Clipper2 (see D2) operates on int64 natively; quantization is its robustness model, not an extra step.
- Deterministic across platforms, which matters for the multiplayer future.

1 mm resolution is far below any gameplay constraint (smallest constraint is the 0.3 m opening margin) and invisible at render scale.

### D2: No new dependencies — geometry built in-house, concepts borrowed

We roll our own core systems; the Tessellator already did this (monotone decomposition with concepts borrowed from Lyon, no library taken). Geometry follows the same pattern. The decisive fact: we need a narrow slice of what libraries like Clipper2 offer, and the hard substrate is something we must build anyway.

- **Triangulation:** the existing Tessellator covers runtime-generated polygons (foundation fills, room tints). Our generated geometry is well-formed by construction (D4 invariant), so no second triangulator is needed.
- **Booleans:** the only consumers are foundation Add (union of two simple polygons), blueprint Subtract (difference), and overlap rejection. Implemented as **arrangement insert + face classification** on the same planar-graph / face-extraction core room detection needs (D6): insert both rings' segments, split at intersections with exact integer predicates, walk faces, classify by containment, take the result boundary. One core powers booleans and rooms. We can also reject any result that violates constraints (holes, slivers) instead of repairing it, which removes most of what makes general-purpose clippers hard.
- **Offsetting (wall bands):** straight segments only, miter joins. The 30° minimum-angle constraint bounds miter length (≤ ~3.9× half-thickness), and minimum clearance prevents band self-overlap, so the general algorithm's hard cases (arcs, self-intersection trimming, collapsing loops) are unreachable. A small custom implementation.
- **Concepts borrowed from Clipper2** (the library itself not adopted): integer coordinates as the robustness model, miter limit with square fallback, and a simplification pass after boolean ops as the second line of defense against slivers.

Risk acknowledged: hand-rolled booleans are the riskiest in-house piece; coincident-edge unions are the classic failure mode and Add-mode hits them constantly (snapped shared edges are the normal case, not the exception). Mitigation: exact integer predicates make coincidence testable as equality, the input domain is narrow (two simple polygons, editor-validated), and this code gets the densest test suite in the feature, with shared-edge and shared-vertex cases enumerated first. Record the considered-and-rejected libraries (Clipper2, earcut.hpp, CGAL, CavalierContours) in `library-decisions.md`.

### D3: New library `libs/geometry`

Pure math, depends only on `foundation` (and the two new third-party libs). Contents:

- `Vec2i64` + quantize/dequantize helpers
- Exact predicates: orientation, segment-segment intersection, point-in-polygon, point-to-segment distance
- Polygon ops: signed area, winding, self-intersection test, min interior angle, min vertex spacing, edge-to-edge clearance (the constraint primitives from the design spec)
- Planar graph / half-edge structure with face extraction (the shared core: room detection in D6, boolean ops in D2)
- Boolean ops (union/difference/overlap) via arrangement + face classification, per D2
- Straight-polyline offsetting with miter joins (wall bands) + post-op simplification pass

Unit-tested in isolation; this library is where geometric correctness lives, so it gets the densest test coverage of the feature.

### D4: Data model — topology graph as source of truth, ECS for gameplay state

Structures are both topology (vertices, segments, junctions, attachment points) and gameplay objects (HP, blueprint progress, selection, tasks). One representation can't serve both well, so:

**`ConstructionWorld`** (new, owned alongside the ECS world) holds the topology:

```
ConstructionWorld
├── foundations:  FoundationId → { ring: [Vec2i64], material, state, entity: EntityID }
├── vertices:     VertexId     → { pos: Vec2i64, segments: [SegmentId] }
├── segments:     SegmentId    → { v0, v1: VertexId, material, thicknessPreset,
│                                  hostFoundation: FoundationId, state, entity: EntityID }
├── openings:     OpeningId    → { segment: SegmentId, t: float (param along segment),
│                                  type, material, state, entity: EntityID }
└── rooms:        RoomId       → { face: [SegmentId], area, entity: EntityID }  (derived)
```

- **Stable IDs.** A T-junction snap splits a host segment into two; both halves get new IDs but openings re-attach by parameter range, and the ECS entity of the demolished/split piece is handled explicitly. Stable IDs are what make door/window attachment survive topology edits (the lesson from Tiny Glade's "five brainstorming sessions for doors": openings interact with every wall invariant, so attachment must be a first-class modeled relationship, not a position lookup).
- **ECS mirror.** Every foundation, segment, and opening also exists as an ECS entity carrying gameplay components: `StructureBlueprint { required[], delivered[], workTotal, workDone }`, `Structure { kind, graphId }`, HP, selection metadata. Bidirectional maps connect `graphId ↔ EntityID`. The graph answers "what is connected to what"; the ECS answers "what state is this thing in" and lets tasks, selection, vision, and rendering treat structures like any other entity.
- **Invariant: the editor never commits invalid topology.** All constraint checks (min angle, spacing, clearance, self-intersection, no X-crossings) run at draw time and reject the candidate point with a reason. This is the Manor Lords pattern (reject degenerate plots with feedback, never repair), and it is what keeps every downstream consumer (face extraction, nav rasterization, band generation) simple. Repair algorithms are explicitly out of scope.
- **Serialization-friendly by construction:** POD records, stable integer IDs, versioned container. No save system exists yet; this shape costs nothing now and avoids a painful retrofit.

### D5: Navigation — interface contract only; nav itself is its own spec

There is no pathfinding today (colonists beeline through everything, including water). Navigation is a major system with its own design conversation and its own spec, deliberately **not** designed here. Two things are fixed in this doc; everything else is deferred to that spec:

**1. Sequencing.** Navigation is not a build-order prerequisite for construction. Foundations, the construction loop, drawing tools, rendering, rooms, and openings are all buildable and testable without it. What nav gates is walls *shipping as gameplay*: until it lands, built walls don't block movement. The construction epics proceed; nav is scheduled independently and must land before walls are player-facing.

**2. The contract construction provides and consumes.** Construction is designed so the nav implementation choice doesn't leak into it:

- Built walls publish their **exact band polygons** (integer coords, from D2 offsetting) as obstacles, carrying their **stable structure IDs** so navigation can belief-filter per agent (memory-aware pathfinding: agents only "see" structures they've discovered). Water and other blockers publish similarly. Blueprints publish nothing.
- Openings publish **portals** (position on a segment, width, passability).
- Construction consumes a **reachability query** (for goal validity: can any builder reach this work slot?) and a **nav version counter** (so MovementSystem replans when construction changes the world).
- The **pathing clearance constant** lives in construction config and is shared; nav honors it, the editor enforces it, and the design spec's solid-or-passable guarantee is what makes the two ends meet.

The nav spec now exists: [Pathfinding Architecture](./pathfinding-architecture.md). It commits to the vector-native direction (dynamic constrained-Delaunay navmesh consuming our exact wall-band polygons directly, no rasterization), inside a four-tier model (planet hex graph → chunk components → CDT navmesh → physical agents). The contract above is what construction publishes into it; its P4 phase is the gate for walls shipping as gameplay.

### D6: Room detection — half-edge face extraction, incremental

Rooms are faces of the planar graph formed by **built** wall segments (centerlines). Algorithm (standard, cp-algorithms construction): at each vertex sort outgoing half-edges by polar angle, link twins to the angularly-next edge, walk the links to enumerate faces; faces with negative signed area are outer faces, the rest are rooms. Openings live on segments and do not break edges, so a doored wall still encloses.

The editor invariants (no X-crossings, T-junctions pre-split, min angle/spacing enforced) remove everything that makes arrangements hard; with integer coordinates the whole extractor is exact and small. CGAL-class machinery is unnecessary precisely because D4 forbids invalid input.

- **Trigger:** wall segment transitions to built, or a built segment is demolished. Re-extract faces for the affected connected component only (a demolition merges exactly two faces; O(local)).
- **Room identity:** persisted by maximum area overlap with the previous face set, so names survive edits.
- **Failure feedback:** when a wall completes and almost-closes a loop, nothing happens silently (the Prison Architect confusion). Cheap heuristic for later UX polish: surface near-misses in the rooms overlay.

### D7: Construction lifecycle — goal source into the task system

Construction is a **goal source** in the sense of the planned Goal-Driven Task Generation refactor (`/docs/technical/task-generation-architecture.md`): blueprints create goal-level tasks; reservations stay item-level.

- **`ConstructionSystem`** (ECS system, logic band): watches blueprint entities and emits goals through their lifecycle:
  1. *Clear:* query `SpatialIndex` for entities intersecting the foundation footprint → chop/mine/haul-away goals. Blueprint blocked until clear.
  2. *Deliver:* blueprint advertises a material manifest (computed from geometry × material config). The blueprint entity carries a delivery inventory (reuses `Inventory` + a restricted accept list); standard haul chains (`Chain_PickupDeposit`) bring materials. Site-staged materials render as piles.
  3. *Build:* when the manifest is complete (and, for walls, the host foundation is built), emit build goals. `Build` action already exists (`needsHands=true`); `ActionSystem` ticks `workDone += rate(skill) × dt` and marks render progress dirty. Multi-builder: N work slots distributed along the perimeter, N from config scaled by size.
  4. *Demolish:* mirrored `Deconstruct` work; refund percentage from config; cascade ("Demolish building") queues openings → walls → foundation as ordered goals.
- **Ordering dependency:** the goal-driven refactor is in flight as draft PR #115, which already includes a `BuildGoalSystem` — the exact hook this design plugs into. If it hasn't landed when construction starts, construction rides the current registry the way Haul does today and migrates with everything else.
- **Pathfinding contract:** builders must *reach* work slots; goal validity includes a region-level reachability check, which is exactly what D5's regions make cheap.

### D8: Rendering — existing draw system, new mesh source

Structures are tessellated vector meshes like everything else and render through the **existing pipeline**: the baked world-space uber-shader path the render perf overhaul just optimized (cached world-space vertex buffers, sub-region culling, no per-frame tessellation). No new renderer, no new shader path. The new code is mesh **generation** only.

- **Mesh source:** structure geometry comes from generators (pattern emitters, wall bands) instead of SVG templates, but the output is the same `TessellatedMesh` shape the pipeline already consumes. Open integration question, resolved with profiling during Epic C: inject structure geometry into the per-chunk entity bake, or give structures their own baked buffer set on the same path. Either way it's the existing machinery. One genuinely new requirement: a draw-order guarantee that floors and wall tops render above terrain and below colonists/items.
- **Procedural patterns as ordered element emission.** Each material defines an element emitter (plank runs, stone courses) that fills a polygon or band with discrete vector elements, deterministically seeded per structure. Elements are emitted **in build order** (the design spec's progressions: sills → joists → decking; corners → courses → infill), and the index buffer is laid out in that order. **Construction progress then renders a prefix of the index buffer** (`drawElements` with count = f(workDone/workTotal)). No rebaking during construction, no animation system, and progress is deterministic and save-stable by construction.
- **Wall bands and junctions.** Per-segment band = centerline offset by half thickness (Clipper2). At junctions, segment bands are trimmed against a computed junction polygon (miter for 2-way joins within miter limit, bevel/fan for 3+ way), so chains read as one continuous wall top. This junction-resolution step is custom code in `libs/geometry`; offsetting each segment independently and overlapping them is the naive fallback but produces visible double-draw seams with translucent blueprint rendering, so trimming is the plan of record.
- **Blueprint state:** dashed outline + faint fill, rendered from the same geometry with a blueprint style, via the structure pass (not GhostRenderer, which stays cursor-preview only).
- **Drawing preview:** the in-progress polygon/polyline (rubber band, fill preview, guides, origin halo) renders through `Primitives` immediate mode, like GhostRenderer does today.

### D9: Doors/windows — parameterized procedural assets

Requirement: one asset per opening type, adapting to wall thickness and material. The asset system has a Lua procedural-generation path with static params; extend it with **call-time parameters**:

- Generator signature gains a params table: `generate(seed, { thickness, materialPalette })`.
- Cache key extends from `defName` to `(defName, thicknessPreset, material)`. Cardinality is small and bounded (opening types × thickness presets × materials), so every combination is cached as a normal template; no shader work, no per-instance vertex stretching.
- Material palettes come from the construction material config (D10), so a wood door and wood wall agree on color.

Rejected: vertex-shader stretch axis + palette swap (more moving parts for the same small variant set), and per-combination authored assets (explicitly forbidden by the design spec).

### D10: Config — `assets/config/construction/`

Follows the `assets/config/work/` pattern: XML, loaded at game start, validated fail-fast by `ConfigValidator` extensions.

- `materials.xml`: per material per structure kind — cost rate, work rate, HP, insulation, flammability, beauty, speed modifier (foundations), thickness presets (walls), pattern parameters (element emitter type, palette, seeds).
- `constraints.xml`: pathing clearance, min corner angle, min vertex spacing, segment clearance, min/max area, max points, opening margins, refund percentage, builder caps.
- `snapping.xml`: angle increments, snap radii, smart-guide range.

Cross-validation: every material referenced by a thickness preset exists; opening types reference valid materials; constraint values are internally consistent (e.g., clearance ≤ segment clearance).

### D11: UI — DrawingSystem beside PlacementSystem, selection extension

- **`DrawingSystem`** (new GameScene subsystem, sibling of PlacementSystem): owns tool state machines (`FoundationTool`, `WallTool`, `OpeningTool`, `EditTool` for vertex/add/subtract editing), a shared **`SnapEngine`** (angle snap relative to previous segment, smart guides, candidate snap targets queried from `ConstructionWorld`: endpoints > vertices > points-on-segments > edges), and a shared **`ConstructionValidator`** (constraint checks returning a violation enum that drives both the red colorizing and the reason text). Input order becomes: GameUI → DrawingSystem → PlacementSystem → SelectionSystem; the drawing tool consumes clicks while active. `InputEvent.modifiers` already carries Alt/Ctrl/Shift.
- **Selection:** new `Selection` variants (`FoundationSelection`, `WallSegmentSelection`, `OpeningSelection`) with priority constants slotted per the design spec's click-priority stack, plus two new hit tests backed by `ConstructionWorld` queries: point-in-polygon (foundations) and point-to-band distance (segments, openings). Repeated-click cycling is new SelectionSystem behavior (currently first-hit-by-priority only).
- **Info panels:** new adapters (`adaptFoundation`, `adaptWallSegment`, `adaptOpening`) emitting existing slot types plus a `ConstructionProgressSlot`; action buttons reuse `ActionButtonSlot` (Edit shape, Cancel, Add, Demolish, Demolish building).
- **Config strip:** new docked `UI::Component` above the GameplayBar (LayoutContainer horizontal: material cards, thickness presets, readouts, validity message), shown while a structure tool is active.
- **Notifications:** room formed via existing `ToastStack`.

---

## 3. Prior Art (Research Summary)

What other teams did, and what we took from each. Full recommendations absorbed into the decisions above.

| Source | Takeaway |
|--------|----------|
| **Manor Lords** (freeform burgage plots) | Reject invalid polygons at draw time with feedback; never generate-then-repair. Our D4 invariant. ([wiki](https://wiki.hoodedhorse.com/Manor_Lords/Burgage_plot)) |
| **Tiny Glade** (freeform wall drawing, ex-Embark) | Many small tailored generators beat one general system; doors touch every wall invariant and deserve first-class modeling (their doors took "five brainstorming sessions"). Our stable-ID attachment in D4. ([80.lv interview](https://80.lv/articles/exclusive-tiny-glade-developers-discuss-bevy-proceduralism-publishers-cozy-games)) |
| **RimWorld** region system (Tynan Sylvester talk) | Flood-filled regions per map section, doors as own regions, incremental dirty rebuild, cheap unreachability rejection. Our D5 region layer. ([talk](https://www.youtube.com/watch?v=RMBQn_sg7DA), [Red Dust writeup with numbers](https://www.lorinatzberger.com/articles/custom-navigation-in-red-dust)) |
| **jdxdev dynamic CDT navmesh** | Full architecture for vector-native pathfinding: CDT over fixed-point coords, local re-triangulation, ~50-100x faster than Recast for dynamic obstacles. Leading candidate for the future nav spec (D5). ([post](https://www.jdxdev.com/blog/2021/07/06/rts-pathfinding-2-dynamic-navmesh-with-constrained-delaunay-triangles/)) |
| **Clipper2 docs** | Concepts borrowed, library not adopted (D2): integer coordinates as the robustness model, miter limits, simplification pass after booleans, don't offset self-intersecting paths. ([docs](https://www.angusj.com/clipper2/Docs/Overview.htm)) |
| **The Sims** wall system | Cautionary: special-casing diagonals on a grid created a permanently second-class wall type with a decades-long bug tail. Validates true planar-graph representation. |
| **Townscaper** | The "considered and rejected" alternative: irregular grids keep adjacency trivial but we need continuous coordinates. ([writeup](https://www.gamedeveloper.com/game-platforms/how-townscaper-works-a-story-four-games-in-the-making)) |
| **Cities: Skylines II** road tools | The interaction grammar checklist for freeform drawing: angle snap, geometry snap, projected guidelines, length/angle readouts, validity coloring. Matches the design spec's drawing UX. ([dev diary](https://forum.paradoxplaza.com/forum/developer-diary/development-diary-1-road-tools.1590300/)) |
| **Prison Architect** room enclosure | Silent room-detection failure confuses players for years; surface near-misses. Our D6 feedback note. |
| **Factorio FFF-317 / AoE4 flow fields** | Scale techniques (hierarchical chunk pathfinding, flow fields) we don't need at colony-sim agent counts; regions suffice. ([FFF-317](https://factorio.com/blog/post/fff-317)) |

---

## 4. Phasing

Dependency-ordered epics. Each lands independently testable; later epics consume earlier ones.

```
A. Geometry foundations ──> C. Foundations end-to-end ──> D. Walls ──> E. Rooms
                                                            └──> F. Openings
                                                                 G. Editing & polish

Navigation & pathfinding: own spec, scheduled independently (D5).
Must land before walls ship as player-facing gameplay; blocks nothing above.
```

**A. Geometry foundations.** `libs/geometry`: quantization, predicates, constraint primitives, planar graph + face extraction, arrangement-based booleans, band offsetting + junction trimming. Pure library, no new dependencies, exhaustively unit-tested (shared-edge boolean cases first). Record considered-and-rejected libraries in `library-decisions.md`.

**C. Foundations end-to-end.** ConstructionWorld (foundations only), FoundationTool + SnapEngine + ConstructionValidator + config strip, blueprint lifecycle components, ConstructionSystem goals (clear → deliver → build), StructureRenderer (fill patterns, progress prefix rendering, blueprint style), selection + info panels + adapters, `assets/config/construction/`. At the end: draw a foundation, colonists clear/haul/build it, it renders progressively, it's selectable. No walls yet.

**D. Walls.** Wall graph (vertices/segments/junction splitting), WallTool (chain drawing, snap targets, edge fill via Ctrl+click), band + junction rendering, obstacle publication per the D5 contract (consumed once nav exists), per-segment demolition with refund. Walls don't block movement until the nav epic lands.

**E. Rooms.** Face extraction on built/demolished events, room entities, room-formed toast. (Overlay UI ships with the broader overlay system per the design spec.)

**F. Openings.** OpeningTool, parameter-extended procedural assets (D9), wall-blueprint reservation + retrofit cut tasks, portal publication per the D5 contract.

**G. Editing & polish.** Foundation add/subtract (Clipper2 booleans + re-validation), vertex editing on blueprints, demolish-building cascade, multi-select batch actions, click-cycling refinement.

---

## 5. Risks & Open Technical Questions

1. **Junction band geometry.** Clean trimming at 3+ way junctions with mixed thicknesses is the fiddliest geometry in the feature. Mitigation: prototype in `libs/geometry` tests with golden-image cases before renderer integration; naive overlap as a temporary fallback is acceptable for opaque built walls but not translucent blueprints.
2. **Progress-prefix rendering vs. vertex format.** Element-ordered index buffers must coexist with the baked path's per-vertex colors and any per-material batching. If a structure's elements span draw batches, the prefix counter needs to be per-batch. Resolve during C.
3. **Hand-rolled boolean ops.** The riskiest in-house geometry (see D2). Coincident-edge unions are the normal case for Add-mode, not the edge case. Mitigation: exact integer predicates, narrow validated input domain, reject-don't-repair, and the densest test suite in the feature with shared-edge/shared-vertex cases enumerated first.
4. **Sequencing against Goal-Driven Task Generation.** Construction goals are designed for the refactored model. If construction starts first, it temporarily rides the current registry like Haul does. Decision point at the start of C.
5. **Coordinate range.** int64 millimeters is comfortable for any plausible world size; intermediates in predicates use 128-bit math. Confirm MSVC `__int128` story (or use a two-limb helper) in A.
6. **Determinism.** Integer geometry and seeded element emitters are deterministic by design; keep any iteration-order dependence (e.g., unordered_map walks) out of geometry-affecting code paths for multiplayer's sake.
7. **Walls-before-nav window.** Walls can be built in dev before pathfinding exists, but they won't block movement, so any playtesting in that window misrepresents the gameplay. The nav epic must be scheduled before walls are treated as shipped; the D5 contract keeps the two workstreams from blocking each other in the meantime.
8. **Structures vs chunk unload (settled).** Chunks regenerate deterministically on reload and evict their placed entities; player structures cannot regenerate and don't need to: `ConstructionWorld` stays fully resident — structure counts and per-structure data are small. Only derived data (baked meshes, nav tiles, component graphs) unloads with chunks and rebuilds from the graph on reload.

---

## 6. Related Documents

- [Building & Construction design spec](../design/game-systems/world/building-construction.md)
- [Pathfinding Architecture](./pathfinding-architecture.md) — consumes this doc's obstacle/portal contract (D5)
- [Task Generation Architecture](./task-generation-architecture.md) — goal-driven model construction plugs into
- [Vector Graphics Architecture](./vector-graphics/architecture.md) — render tiers
- [Entity Placement System](./entity-placement-system.md) — SpatialIndex
- [Library Decisions](./library-decisions.md) — to record considered-and-rejected geometry libraries (Clipper2, earcut.hpp, CGAL, CavalierContours)
