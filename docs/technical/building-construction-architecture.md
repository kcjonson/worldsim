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
| World units | 1 tile = 1 m (`kTileSize = 1.0F`, `libs/world/world/chunk/ChunkCoordinate.h`); entities use continuous `Vec2` meters | Design spec's meters map 1:1 to world units, no conversion layer |
| Geometry utilities | Tessellator (monotone decomposition) in `libs/renderer/vector/`; **no** point-in-polygon, segment intersection, polygon offset, or boolean ops anywhere | Must be built; new dependencies needed |
| Pathfinding | **None.** `MovementSystem` beelines to `MovementTarget`; `PhysicsSystem` is Euler integration; no collision, no obstacle queries; colonists walk through everything including water | Construction development proceeds without it; nav must land before walls ship as gameplay (see D5) |
| Spatial indexing | `SpatialIndex` (grid hash) in `libs/assets/assets/placement/`, used for entity discovery/placement, not movement | Reusable for footprint-clearing queries |
| Task system | `GlobalTaskRegistry` + reservations + chain infra; work tiers config-driven; `Build` action and Construction tier 5 already defined in `assets/config/` XML | Construction plugs in as a goal source; aligns with the planned Goal-Driven Task Generation refactor |
| Rendering | Three base-shader paths (batched / instanced / baked world-space); EntityRenderer baked path = precedent for cached static world geometry with sub-chunk culling; `GhostRenderer` for previews; `RenderTarget` available | Foundations/walls fit a new baked pass between terrain and entities |
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
- **ECS mirror.** Every foundation, segment, and opening also exists as an ECS entity carrying gameplay components: `StructureBlueprint { required[], delivered[], workTotal, workDone }`, `Structure { kind, graphId }`, HP, selection metadata. Bidirectional maps connect `graphId ↔ EntityID`. The graph answers "what is connected to what"; the ECS answers "what state is this thing in" and lets tasks, selection, vision, and rendering treat structures like any other entity. The `delivered[]` / `recordDelivery` / `materialsComplete()` metered-deposit model is shared with crafting: a crafting station provisions its recipe bill of materials by the same mechanism (deposit metered to remaining need, no leftover ever). See [Crafting & Material Provisioning](./crafting-provisioning-architecture.md).
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

- **Trigger:** wall segment transitions to built, or a built segment is demolished. Each change of the construction version re-runs `detectRooms` over the whole world (`RoomDetectionSystem::update` calls `reconcile()`), then matches the new rooms to the previous records.
- **Rooms with holes:** a room is an outer ring (counter-clockwise) minus zero or more holes (each clockwise, as the arrangement walks it). A wall loop standing free inside a room, touching none of its walls, is a separate wall group; its outline becomes a hole of the smallest room that contains it, and only that one, so in a three-level nest each room holds just the room directly inside it. The inner loop is its own room. A room's area is net of its holes (a 1 x 1 m room inside a 5 x 5 m room leaves the outer room 24 m²), and its representative point lies strictly inside the ring and outside every hole (unless the scan finds no such point, for a room only millimetres tall, in which case the old point is kept unverified). Containment is `construction::pointInRoom`: inside the ring and outside every hole, with any ring or hole edge reading as the boundary. The rooms overlay tessellates the ring and its holes together, so every floor is tinted once, outlines the holes as well as the ring, and picks a clicked room with the same test: a click strictly inside a room selects it, which makes the inner room of a nest the one selected, and a click on a boundary two rooms share goes to the higher room id.
- **Room identity:** matched against the previous records in passes, each record inherited by at most one new room. First, a room whose outer ring is a record's stored ring (the same vertex cycle) keeps that record, so building or removing a room inside another never renames the outer room. Then a room inherits a record whose stored representative point is strictly inside it; then, for a point a new wall now runs through, one whose point is on its boundary (the first room in canonical order that still has none takes it, so one side of a divider keeps the name). When a room holds several stored points (a merge), the largest stored area wins, then the lowest id. Anything left unmatched is a new room with a new id and name. When several walls change in one update so that a room closes round another room's stored point while that room's outline also changes, the inner room takes the name (WOR-875).
- **Failure feedback:** when a wall completes and almost-closes a loop, nothing happens silently (the Prison Architect confusion). Cheap heuristic for later UX polish: surface near-misses in the rooms overlay.

### D7: Construction lifecycle — goal source into the task system

Construction is a **goal source** in the sense of the planned Goal-Driven Task Generation refactor (`/docs/technical/task-generation-architecture.md`): blueprints create goal-level tasks; reservations stay item-level.

- **`ConstructionSystem`** (ECS system, logic band): watches blueprint entities and emits goals through their lifecycle:
  1. *Clear:* query `SpatialIndex` for entities on the foundation footprint (position inside the ring, the box as broad phase) → chop/mine/haul-away goals. Blueprint blocked until clear. As built: harvestables hold the site and are cleared by a colonist; when the foundation enters construction the rest of what is on the footprint (grass, stones, rocks, loose piles) is taken off it, so it is built on bare ground (`docs/technical/task-claims.md`, "Build-site clear").
  2. *Deliver:* blueprint advertises a material manifest (computed from geometry × material config). The blueprint entity carries a delivery inventory (reuses `Inventory` + a restricted accept list); standard haul chains (`Chain_PickupDeposit`) bring materials. Site-staged materials render as piles.
  3. *Build:* when the manifest is complete (and, for walls, the host foundation is built), emit build goals. `Build` action already exists (`needsHands=true`); `ActionSystem` ticks `workDone += rate(skill) × dt` and marks render progress dirty. Multi-builder (as built for foundations): N builder spots spaced round the ring, 0.5 m in from their own edge (along the bisector at a corner, on the ring where that would not be inside), N from `constraints.xml` scaled by area and capped by a minimum spacing; each builder holds one spot as a numbered slot claim and stands where the nav mesh gives for it, and once every spot is held the Build reads Reserved for the rest (`docs/technical/task-claims.md`, "Build and Deconstruct"). Walls and openings have no spots and no cap.
  4. *Demolish:* mirrored `Deconstruct` work; refund percentage from config; cascade ("Demolish building") queues openings → walls → foundation as ordered goals.
- **Ordering dependency:** the goal-driven refactor is in flight as draft PR #115, which already includes a `BuildGoalSystem` — the exact hook this design plugs into. If it hasn't landed when construction starts, construction rides the current registry the way Haul does today and migrates with everything else.
- **Pathfinding contract:** builders must *reach* work slots; goal validity includes a region-level reachability check, which is exactly what D5's regions make cheap.

### D8: Rendering — existing draw system, new mesh source

Structures are tessellated vector meshes like everything else and render through the **existing pipeline**: the baked world-space base-shader path the render perf overhaul just optimized (cached world-space vertex buffers, sub-region culling, no per-frame tessellation). No new renderer, no new shader path. The new code is mesh **generation** only.

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
- `snapping.xml`: angle increment, snap radii, origin-close radius, the guide keys (range, tolerance, angle window, merge distance, crossing angle and radius, direction tolerance, nearby-direction and corner limits, priority rule), common lengths, length rounding, and the rectangle tool's adjacency switch (D12, D13), plus the Drawing Settings defaults (D14).

Cross-validation: every material referenced by a thickness preset exists; opening types reference valid materials; constraint values are internally consistent (e.g., clearance ≤ segment clearance).

### D11: UI — DrawingSystem beside PlacementSystem, selection extension

- **`DrawingSystem`** (new GameScene subsystem, sibling of PlacementSystem): owns tool state machines (`FoundationTool` with its rectangle and freeform modes (D13), `WallTool`, `OpeningTool`, `EditTool` for vertex/add/subtract editing), a shared **`SnapEngine`** (candidate snap targets queried from `ConstructionWorld`: endpoints > vertices > points-on-segments > edges, then guides, then the angle step, then the length step, each helper gated by the player's Drawing Settings; D12), and a shared **`ConstructionValidator`** (constraint checks returning a violation enum that drives both the red colorizing and the reason text). Input order becomes: GameUI -> DrawingSystem -> PlacementSystem -> SelectionSystem; the drawing tool consumes clicks while active. `InputEvent.modifiers` already carries Alt/Ctrl/Shift.
- **Selection:** new `Selection` variants (`FoundationSelection`, `WallSegmentSelection`, `OpeningSelection`) with priority constants slotted per the design spec's click-priority stack, plus two new hit tests backed by `ConstructionWorld` queries: point-in-polygon (foundations) and point-to-band distance (segments, openings). Repeated-click cycling is new SelectionSystem behavior (currently first-hit-by-priority only).
- **Info panels:** new adapters (`adaptFoundation`, `adaptWallSegment`, `adaptOpening`) emitting existing slot types plus a `ConstructionProgressSlot`; action buttons reuse `ActionButtonSlot` (Edit shape, Cancel, Add, Demolish, Demolish building).
- **Config strip:** new docked `UI::Component` above the GameplayBar (LayoutContainer horizontal: the foundation tool picker, material cards, thickness presets, readouts, validity message), shown while a structure tool is active.
- **Notifications:** room formed via existing `ToastStack`.

### D12: Snap engine (helpers, guides, lengths)

Player-facing behaviour: design spec, Foundations > Drawing and Foundations > Snapping and guides, and UI & Selection > Drawing Settings and Key guide. This decision replaces the world-axis-only alignment with one formulation in which the world axes are one guide source of three, puts every helper behind a player setting, and adds common lengths and length rounding. The rectangle tool's corner snap is D13, the settings' storage D14, the key guide D15, and the look D16. The values were tuned in an interactive prototype outside the repository; every case in the test list below passes in it.

**Today (main at a64e7a0f).** `libs/sim/construction/SnapEngine.cpp`: `snap()` (lines 382 to 417) tries origin-close (3+ points; it ignores `freeform`), foundation vertex, foundation edge, `axisAlign`, `snapAngle`, raw. `snapWall()` (465 to 529) tries, in order, the chain's first point (kind `Origin`, once the chain has `kMinClosingWallChainPoints` points and the cursor is within the origin close radius; skipped under `freeform`), wall endpoint, foundation vertex (inset), wall segment interior, foundation edge (inset), `axisAlign` and `snapAngle` (both skipped under `freeform`), then the raw cursor. `axisAlign` (296 to 371) locks the cursor's X and/or Y to the nearest node within `axisGuideToleranceMeters`; nodes are the in-progress points plus, when `alignToExisting`, committed foundation ring vertices within `smartGuideRangeMeters` (raw corners in both tools; wall vertices are not nodes). `snapAngle` (260 to 294) reads `points.back()` unguarded (its callers check `!points.empty()`) and returns the raw cursor as `None` early in two cases, a cursor at zero distance from that point (about line 265) and an increment of zero or less (about line 277); otherwise it snaps the direction from it to the nearest `angleIncrementDegrees` step off the previous segment (off +x for the first), keeping distance; it has no capture window, so past those two cases it is the unconditional fallback. Nothing reads an existing edge's direction or the shape's first edge, nothing intersects two lines, and nothing touches a length. `axisAlign` runs before `snapAngle`, so a world-axis hit within 0.3 m pre-empts the 90-degree turn on a rotated shape (the reported bug).

#### The engine

One pure function per tool: `snap()` (Freeform foundation tool), `snapWall()` (wall tool), and `snapRectangleCorner()` (D13). Inputs: the pointer (world metres), the placed points, the existing geometry (`ConstructionWorld`'s foundations, blueprints included, with their placement order; in the wall tool also its wall vertices and segments), the snapping config (`SnappingConfig`), the player's drawing aids (`DrawingAids`, the engine's part of the Drawing Settings; D12 and D14), `freeform` (Alt held), the wall half-thickness (integer millimetres, as today), and the origin-close radius (widened with zoom by `DrawingSystem`, as today). Output: the snapped point, what it landed on, up to two guides to draw, what the length step did, the kept common lengths (so labels can show `=`), and whether a click closes. The engine is stateless and const: `DrawingSystem` builds a `SnapEngine` on every move (`handleMouseMove`, `handleWallMove`), as today. A second pure helper, `cornerAngles()`, computes the angle readout from the placed points and the snapped point; it snaps nothing.

#### Snap order per tool

First rule that applies wins. A rule marked with a setting is skipped when that Drawing Setting is off; every rule marked `!freeform` is skipped while Alt is held.

| Freeform tool (`snap()`) | Wall tool (`snapWall()`) | Needs |
|---|---|---|
| 1. Close: 3+ points and the cursor within `originCloseRadiusMeters` of `points[0]`; kind `Origin` | 1. Close, the same, once the chain has `kMinClosingWallChainPoints` (3) points | wall tool: `!freeform` |
| | 2. Wall endpoint: nearest wall vertex closer than `vertexSnapRadiusMeters`; `WallEndpoint` | |
| 2. Foundation corner: nearest ring vertex within `vertexSnapRadiusMeters`; `Vertex` | 3. The same, returned at `outerFaceFlushCorner(ring, index, wallHalfThicknessMm)`, which adds the 2 mm `kFlushInsetSafetyMm` itself | |
| | 4. Wall segment: nearest foot strictly inside a segment, closer than `edgeSnapRadiusMeters`; `WallSegment`; then squaring | squaring: `guidesFromShape`, `!freeform` |
| 3. Foundation edge: nearest foot on a ring edge within `edgeSnapRadiusMeters`; `Edge`; then squaring | 5. The same on the edge's inset line (edge and foot moved by the inset along the inward normal); then squaring | squaring: `guidesFromShape`, `!freeform` |
| 4. Guides (below); then the first-point landing; then the length step on a single guide | 6. The same | a guide source on; `!freeform` |
| 5. Angle step: with a point placed, the direction from `points.back()` to the cursor snapped to the nearest `angleIncrementDegrees` step off the previous segment (off +x for the first), distance kept; kind `Angle`; then the length step | 7. The same off the previous chain point | `angleSteps`, `!freeform` |
| 6. Raw cursor; kind `None`; then the length step along the raw direction | 8. The same | length step: `!freeform` |

With every helper off the Freeform tool is rules 1 to 3 and 6 with length rounding, and the wall tool rules 1 to 5 and 8 with length rounding; neither has a grid. The rectangle tool's order is D13's.

#### Guides

A **line** is a node (a point) and a direction, compared modulo 180 degrees; directions come in **pairs**, a direction and its perpendicular. The engine builds nodes, directions, and the lines below; captures lines near the cursor; merges near-duplicates; forms crossings; ranks every candidate with one key.

**Nodes.**

- Shape points: `points`. A foundation has at most `maxPoints` (32, constraints config; the validator refuses more). A wall chain has no such cap, so its lines use `points[0]` and its most recent `maxPoints - 1` points.
- Committed corners, only when `alignToExistingBuildings`. Freeform tool: foundation ring vertices. Wall tool: each foundation ring vertex at `outerFaceFlushCorner(ring, index, wallHalfThicknessMm)` (the function adds the 2 mm safety, `kFlushInsetSafetyMm`, itself), where a perimeter wall's centerline turns, plus wall vertices. Those within `guideRangeMeters` of the cursor are taken nearest first, at most `guideCornerLimit`; ties fall to `foundations()` order and ring index, then `vertices()` ascending id. Each corner remembers its structure (its foundation, or the wall segments at that vertex) and its edge directions (a foundation corner: toward the previous ring vertex, then toward the next; a wall vertex: along its segments in ascending id). Using the inset corner in the wall tool keeps a foundation corner's guides on the perimeter wall's centerline instead of half a wall thickness outside it, where they would run parallel to the wall vertex's own guides and fight them.

**Directions.**

- Shape first: `points[0]` to `points[1]`, once there are two points.
- Shape previous: `points[n-2]` to `points[n-1]`, once there are three points and it differs from the first by more than `guideDirectionToleranceDegrees` (mod 90 degrees).
- Nearby, only when `alignToExistingBuildings`: committed edges whose closest point is within `guideRangeMeters` of the cursor (Freeform tool: foundation ring edges; wall tool: those plus wall segment centerlines), zero-length edges skipped, walked by that distance, ties by foundation id and ring index, then segment id. An edge within `guideDirectionToleranceDegrees` (mod 90 degrees) of the world axes or of a direction already kept is skipped; the first `nearbyGuideDirectionLimit` are kept, each with the structure that supplied it. A skipped duplicate still counts for corners: a kept direction also takes every structure in range that has an edge within `guideDirectionToleranceDegrees` (mod 90 degrees) of it, so two parallel sheds both get the lines. Blueprints count; they are what the player is lining up with.
- World: +x.

**Lines, the closed list.** Nothing else generates a line. Generation order is this list, then node order, then a pair's direction before its perpendicular.

1. The shape first pair through every shape point. `DrawnShape`. Needs `guidesFromShape`.
2. The shape previous pair through `points.back()`. `DrawnShape`. Needs `guidesFromShape`.
3. Each nearby pair through `points.back()` (when there is one) and through the committed corners of every structure in range that has an edge within `guideDirectionToleranceDegrees` (mod 90 degrees) of it, the one that supplied it included. `NearbyStructure`. Needs `alignToExistingBuildings`. The line bound is unchanged, since the corners are capped at `guideCornerLimit` however many structures share a direction. Case: two parallel sheds at 37 degrees; shed B's edge is nearer and supplies the direction, shed A's edge is skipped as a duplicate, and A's corners still get 37-degree lines, so the player can line up with A's face.
4. The world pair through every shape point and every committed corner (there are corners only with `alignToExistingBuildings`). `WorldAxes`. Needs `guidesFromWorldAxes`.

Left out on purpose: the shape's directions through committed corners (a building parallel to the shape brings its own direction through its own corners, and no worked case needs more), nearby directions through shape points other than the previous one, and directions from shape edges other than the first and previous.

**Capture.** A line is captured when the cursor is within `guideToleranceMeters` of it. A `DrawnShape` line that passes within 1 mm of `points.back()` or (with two or more points) of `points[0]` is also captured when the direction from that point to the cursor is within the angle window of the line (mod 180 degrees, so a cursor behind the point catches too); the window is `angleIncrementDegrees / 2` while `tieAngleWindowToStep` holds, else `guideAngleWindowDegrees`. The test is geometric, not by node: the first edge's own line is generated through `points[0]` (and through `points[1]`, then dropped as a duplicate by the merge), and it still catches by angle from `points[1]` while that is the previous point. A single line's snapped point is the foot of the perpendicular from the cursor.

The window through the previous point is the angle step's own, so the 0, 90, 180, and 270-degree turns off the previous edge are caught exactly where `snapAngle` caught them, and come back as `GuideLine` with a marker. The window through `points[0]` catches the closing corner: on the 30-degree rectangle below, the cursor (-1.6, 3.8) is 0.51 m off the guide through `points[0]` (outside the 0.3 m tolerance) but 7.2 degrees off it as seen from `points[0]`, so the guide is captured and the crossing forms. Nearby and world lines are distance-only, so they never crowd out the other 15 degree steps. The angle step governs only when no line is captured; its result keeps kind `Angle`.

**Merge.** Captured lines are walked in rank order (source, then shape point before committed corner, then generation order). A line within `guideDirectionToleranceDegrees` (mod 180 degrees) of a kept line, and closer to it than the merge distance at the kept line's foot of the cursor, is dropped into that kept line, which records the dropped line's node. The merge distance is `guideToleranceMeters` while `tieMergeDistanceToTolerance` holds, else `guideMergeDistanceMeters`. A committed corner's line 0.25 m from the shape's own parallel line no longer takes over when the cursor drifts nearer to it.

**Crossings.** Only with `guideCrossings`. Each pair of kept lines that meet at the crossing minimum angle or more (mod 180 degrees; `angleIncrementDegrees` while `tieCrossingAngleToStep` holds, else `guideCrossingMinAngleDegrees`) gives its intersection, kept when it lies within `guideCrossingRadiusMeters` of the cursor, unless both lines pass within 1 mm of a node that one of them was generated through: its own node, or the node of a line merged into it. Two lines through a shared node always cross on it, which would otherwise make each node a magnet out to the crossing radius: with the shape at 30 degrees, a cursor 0.5 m from `points[1]` at 112 degrees is 0.070 m from the 120-degree line and 0.187 m from the 90-degree world line through `points[1]`, and their crossing is `points[1]` itself. With the rule it lands on the 120-degree line. The rule is by generating node, not by any node on both lines: read the second way it would also drop a rectangle's closing corner that falls on an existing building's corner, which the lines through `points[2]` and `points[0]` reach without being generated there; kept, it still snaps there when the cursor is 0.4 to 0.52 m off, past the 0.4 m vertex snap. The angle minimum drops near-parallel crossings, which hold the point through a dead zone (a 30-degree guide and a 31.5 degree nearby line are both captured over about 23 m of their length).

**Ranking.** One lexicographic key, smaller wins. With `priorityRule` `sourceFirst` (the default):

1. The best source among the candidate's lines (`DrawnShape` < `NearbyStructure` < `WorldAxes`)
2. Crossing before single line
3. The best line's node: shape point before committed corner
4. Distance from the cursor to the snapped point (differences within 1e-9 m are ties)
5. Generation order: a single line by its own; a crossing by its earlier line, then its later

A crossing's best line is the higher-ranked of its two by source, then node, then generation. So a `DrawnShape` single line beats a `NearbyStructure` crossing, and the crossing of the shape's own two lines (the rectangle's closing corner) beats everything else in the guide step. There is no key for a crossing's second line: an earlier draft of this decision had one (the other line's source, third), dropped so the two docs and the tuned prototype rank alike. With `priorityRule` `closest` the key is crossing before single line, then distance, then generation; it exists for comparison in tuning (the same cursor in `GuideDrawnShapeBeatsCloserWorldAxis` goes to the world line under it).

**Squaring on existing edges.** In `snap()`'s edge rule and in `snapWall()`'s wall-segment and foundation-edge rules, the hit is found as above, then, unless `freeform` or `guidesFromShape` is off: each captured `DrawnShape` line, in generation order, that meets the hit's line (the foundation edge; in the wall tool the inset edge line; the wall segment's centerline) at the crossing minimum angle or more, at a point on the edge (end points included; strictly interior for a wall segment, so it stays a T-junction) within `edgeSnapRadiusMeters` of the cursor, is a candidate, and the nearest to the cursor is returned instead of the foot (generation order on ties). Kind and hit ids are unchanged (`Edge`; `WallSegment` with `hitSegment`), and `guides[0]` carries the line with its marker. Case: `points` (0, 0), (5.196, 3.000), a foundation edge along y = 6, cursor (3.580, 5.8) on the 120-degree guide 0.2 m short of the edge. The foot is (3.580, 6.000), whose edge direction is 88.3 degrees off the first edge's, leaving a 91.69-degree interior corner at `points[1]`; the squared point is (3.464, 6.000), 0.23 m from the cursor, on the edge, 90 degrees. The vertex rules stay ahead and unchanged; a corner beating a guide is intended.

**Landing on the first point.** With three or more points, a guide result (a single line's foot, or a crossing) within `originCloseRadiusMeters` of `points[0]` snaps to `points[0]` and is returned as `Origin`, so the click closes; its guides still draw. In `snapWall()` the same rule applies once the chain has `kMinClosingWallChainPoints` points; because the first-point close runs first there, a guide result lands on the first point only when the cursor is outside the close radius. No length step follows. Case: closing the 30-degree rectangle with the cursor at (0.214, 0.47), 0.516 m from `points[0]`, just outside the origin-close radius. The shape's perpendicular through `points[0]` (which also runs through `points[3]`) is captured by angle, 6.5 degrees off as seen from `points[3]`, and its foot lands 0.30 m from `points[0]`, so without this the preview shows a too-short closing edge and only `DrawingSystem::handleMouseMove`'s rescue-close (about line 416) would save the click.

**Right-angle marker.** For each returned guide, the edges at its node are tested in order: for a shape point, the shape's edge into it (toward the point before), then out of it (toward the point after, when there is one); for a committed corner, its edge directions in the order above. The first within `guideDirectionToleranceDegrees` of perpendicular to the guide sets `rightAngle` and `rightAngleEdgeDirection` (unit, from the node along that edge). The marker sits at the guide's `from`. A guide parallel to the first edge gets a marker too when the edge into its point is perpendicular to it, as at `points[2]` of a rectangle; through `points[0]` or `points[1]` it gets none, since the only edges there are the first edge itself.

**Where it sits.** The guide step takes the world-axis lock's slot (`axisAlign`, renamed `alignToWorldAxes` by the engine reshape) in both entry points, after the geometry rules and before the angle step. Real geometry stays ahead (zero-gap adjacency is a hard rule). `alignToWorldAxes` is deleted in the same change (one path); its behaviour is line list item 4 with the same 0.3 m tolerance, except that the wall tool's foundation corners move to their inset points.

#### Common lengths

Only with `commonLengths` and not `freeform`. Computed once per move, before the rules; used by the length step.

1. **Entries.** With `commonLengthSourceShape`, each edge between consecutive placed points (owner: the shape; it ranks newest of all; owner distance 0). With `commonLengthSourceBuildings`, every foundation, each with its owner distance (the cursor's distance to its nearest ring edge) and its placement order: each ring edge, or in the wall tool, while `commonLengthWallsReadInsetFoundation` holds, each edge between consecutive inset corners (`outerFaceFlushCorner(ring, index, wallHalfThicknessMm)`). In the wall tool, also each wall segment, owned by the chain it was drawn in, which its placement order identifies (below). A chain's owner distance is its nearest segment's.

**Placement order.** There is no separate counter: the order is `ConstructionWorld::version()` read at the start of the commit. `version_` is a monotonic `uint64_t`, never reset, and every successful `commitFoundation`, `commitExtension`, and `commitSegment` bumps it, so no two commits read the same value; values are sparse (state and entity changes bump it too), which is harmless, since only their order is used.

- *New stored fields.* `Foundation::placementOrder` and `WallSegment::placementOrder`, both `std::uint64_t`. A future save format carries both and restores `version()` above every stored value, or a building placed after a load ranks older than the ones loaded.
- *Foundations.* `commitFoundation` records `version_` before its bump. So does `commitExtension`: an extension is a placement, and ranks as its own building while it is pending. `mergeExtension` removes the extension record and the target keeps its own order (a merge reshapes the target; the player placed nothing new then). Add and Subtract in place on a blueprint (`addToFoundation`, `subtractFromFoundation`) keep the foundation's order.
- *Walls.* A chain is many `commitSegment` calls, each bumping the version, so the order is passed in: `commitSegment` gains a `std::uint64_t placementOrder` parameter, and `DrawingSystem::commitWallChain` reads `version()` once before its first segment and passes that value for every segment of the chain. One committed chain shares one value, which is how a segment knows its chain. Segments the new pair is split into (drawn through an existing vertex) take the chain's value; the two halves of an existing segment that a T-junction splits keep that segment's value. Walls re-hosted by a merge keep theirs.
- *Ctrl+click edge fill* is a chain of its own: it reads `version()` before its commit.
- *Tests, benches, and dev verbs.* Every `commitSegment` caller passes a value: `DrawingSystem::devCommitWalls` (the `/api/dev` wall path) reads `version()` once per call, so a dev-stamped loop is one chain; tests and the two benches (`NavigationSystem.bench.cpp`, `PlayerVisibilitySystem.bench.cpp`) pass `version()` the same way, or an explicit value when a case is about order. The dev foundation path goes through `commitFoundation` and gets its order there.
2. **Filter.** An entry shorter than `commonLengthMinimumMeters` is dropped. A building or chain entry whose owner distance is over `commonLengthRangeMeters` is dropped, unless `commonLengthAllBuildings`.
3. **Grouping.** Entries are walked newest owner first (the shape, then the highest placement order), then nearest owner, then generation order. An entry within `commonLengthMergeMeters` (plus 1e-9 m) of an existing group's length joins the first group created that is within that distance; otherwise it starts a group with its own length. A group's length is its first entry's.
4. **Ranking.** Groups sort by newest member (descending), then member count (descending), then nearest owner (ascending), then length (ascending). The first `commonLengthMaxCount` are kept.
5. **Catch along a line.** Given the segment's start `start` (`points.back()`), a line (origin `origin`, unit direction `direction`), and the current point `current` on it: for each kept group of length `length`, let `foot` be the foot of `start` on the line and `offset = |start - foot|`. If `offset > length` the length is out of reach. Otherwise the target is whichever of `foot + direction * sqrt(length^2 - offset^2)` and `foot - direction * sqrt(length^2 - offset^2)` is nearer `current`, and the slide is `|target - current|`. A group catches when its slide is at most `commonLengthCatchMeters`; of those, the smallest slide wins, then the better group rank.

#### The length step

`refine(origin, direction, current, allowRounding)`, applied where the point can still slide along a line:

1. With no point placed, or `freeform`, nothing.
2. A common length that catches (above) moves the point to its target. Kind unchanged; the result records `CommonLength`, the length, and the group (its edges, for the ticks).
3. Else, when `allowRounding`, `roundLengths` is on, and `lengthIncrementMeters` > 0: `length = round(|start - current| / increment) * increment`; if `length` > 0 and reachable on the line (as in the catch), the point moves to that target and the result records `Rounded`.
4. Else the point stays.

Where it runs:

- **A single guide through the previous point** (its node within 1 mm of `points.back()`): `refine(node, direction, foot, true)`.
- **A single guide through any other point**, with `lengthRoundingOnGuides` `guideDistance` (the default): `refine(node, direction, foot, false)`; if no common length caught and rounding is on, the distance along the guide from its own node rounds instead: `along = (foot - node) . direction`, `rounded = round(|along| / increment) * increment`, and if `rounded` > 0 the point is `node + sign(along) * rounded * direction`, recorded as `RoundedAlongGuide`. This runs with no point placed too. With `segmentLength`, `refine(node, direction, foot, true)` instead.
- **A crossing**: fixed. With `commonLengthBeatsCrossing` on, `refine` runs from the cursor's foot on each of the crossing's two lines with `allowRounding` false; if either catches, the smaller slide wins (the best line on a tie), and the result becomes a `GuideLine` on that line alone at the common length.
- **The angle step**: `refine(points.back(), stepDirection, point, true)`.
- **The raw cursor**, while `commonLengthAfterRawCursor` holds and the cursor is off the previous point: `refine(points.back(), unit(cursor - points.back()), cursor, true)`.
- **Never** after the close, a corner, an edge (squared or not), a wall endpoint or segment, or a guide result that landed on the first point.

#### What is drawn

The result carries what the preview needs; D16 has the look. Not drawn yet, later slices add them: the " =" and accent colour on a matched length, the common-length marks, and the angle readout.

- **Guides.** `guideCount` guides (2 for a crossing, 1 for a single line, the angle step, a squared `Edge` or `WallSegment`, and an `Origin` from a single guide; 2 for an `Origin` from a crossing), each from its node to the snapped point, with its source, marker, and `distanceFromNodeMeters` (`|to - from|`). The angle step's guide is the ray from `points.back()`, source `DrawnShape` (`WorldAxes` for the first segment), no marker, no distance label. A guide's stroke draws only where it lies more than a pixel from the segment being drawn (the previous point to the snapped point), since dots that close would bead the solid line. Every guide ends at the snapped point, the segment's end, so the stroke runs from its node to where the guide first comes within a pixel of the segment. A guide through the previous point (the angle step's ray included) has nothing left: no stroke of its own, only its right-angle marker if it has one. A guide from an older node along the segment keeps its stroke to a pixel short of the previous point, and its distance label sits beside that stroke; a guide a fraction of a degree off the segment stops where it comes within the pixel.
- **A crossing dot** at the point for `GuideCrossing`, drawn over the snapped-point marker's dot.
- **The segment label**, beside the segment's middle or at the snapped end (D16 has the rule): `|point - points.back()|` to `guideDistanceDecimals` places and " m", with " =" and the accent colour when the length step recorded `CommonLength` or the length is within `commonLengthMergeMeters` of a kept group.
- **Common-length marks**, when the length step recorded `CommonLength`: a tick across the segment at the snapped end, and on every edge of the matched group a faint highlight and a double tick at its middle.
- **Guide distance labels** (`guideDistances` on): for each guide whose `relation` is not `AngleStep` and whose stroke draws with at least 40 px of it on screen, `distanceFromNodeMeters` the same way, with " =" on a kept-group match. The label draws only with the stroke, so a guide the segment covers entirely (one through the previous point, or one whose node lies on the segment) has none; the segment label reads that length. Placed beside the middle of the part of the stroke on screen, while its text stays the guide's whole distance from its node, 10 px off it on the side away from the segment's middle (the other side when that leaves the screen). When the segment's middle is within half a pixel of the stroke's line, as it is for a guide along the segment, neither side is away from it, and the label takes the side facing up the screen, or the left one when the guide is within about 11 degrees of vertical. If its plate overlaps the segment label, an earlier guide label, the snapped-point marker out to its ticks, the close halo while it is drawn, the wall tool's snap-target ring, or the box the preview keeps clear (a rejected edit's reason), it shifts along the guide by its own width plus 8 px, first back toward its node, then forward, alternately, up to six times, never past either end of the stroke's part on screen (a shift that would is cut to that end), and is dropped when no spot along it is clear.
- **The angle readout** (`angleReadout` on, not `freeform`, not the rectangle tool), from `cornerAngles(points, point, closesShape, tool, 2 * originCloseRadiusMeters, 0.5 * 10^-angleReadoutDecimals)`, where `originCloseRadiusMeters` is the radius passed in `SnapInput`: the game widens the close radius at low zoom, so the closing reach widens with it (the prototype has no such widening). The path is `points` plus the snapped point, or `points` alone when the click closes. It reads as a polygon when it has three or more vertices and the tool is the Freeform tool or the chain is closing. The corner at `points.back()` is always read; the corner at `points[0]` too when there are three or more points, the path reads as a polygon, and the click closes or the snapped point is within the closing reach of `points[0]`. A polygon corner is the interior angle: the sweep from the edge toward the previous vertex to the edge toward the next, taken the other way round when a probe along its bisector (1% of the shorter edge out) falls outside the path. An open-chain corner is the unsigned angle between its two segments. A corner with an edge under 1 mm is skipped; a corner within the square tolerance of 90 degrees draws the right-angle marker; any other draws an arc and its degrees. Nothing shows with fewer than two points.

#### API

Public `snap()` and `snapWall()` take one input struct in place of today's trailing parameters (`freeform`, `originCloseRadiusMeters`, `alignToExisting`, and in `snapWall()` `wallHalfThicknessMm`). Both signatures in full:

```cpp
[[nodiscard]] SnapResult snap(
    const std::vector<::Foundation::Vec2>& points, ::Foundation::Vec2 cursor, const SnapInput& input) const;
[[nodiscard]] SnapResult snapWall(
    const std::vector<::Foundation::Vec2>& points, ::Foundation::Vec2 cursor, const SnapInput& input) const;
```

The types:

```cpp
enum class SnapKind {
    None, Angle, Vertex, Edge, Origin, WallEndpoint, WallSegment,
    GuideLine,      // locked onto one guide line
    GuideCrossing,  // snapped to the crossing of two guide lines; point is the crossing
    Grid,           // rectangle tool: a grid point (D13)
};                  // AxisGuide is removed

enum class GuideSource { DrawnShape, NearbyStructure, WorldAxes };

enum class GuideRelation { Parallel, Perpendicular, AngleStep };  // AngleStep: the angle snap's ray from the previous point, not a line through a node

struct SnapGuide {
    Vec2          from;                     // the guide's node; the marker sits here
    Vec2          to;                       // the snapped point
    GuideSource   source = GuideSource::DrawnShape;
    GuideRelation relation = GuideRelation::Parallel;
    bool          rightAngle = false;       // draw the right-angle marker
    Vec2          rightAngleEdgeDirection;  // unit, from `from` along the perpendicular edge
    float         distanceFromNodeMeters = 0.0F;  // |to - from|, for the guide distance label
    bool          throughPreviousPoint = false;   // the node is the last placed point
};

inline constexpr std::size_t kMaxSnapGuides = 2;

enum class LengthSnapKind { None, Rounded, RoundedAlongGuide, CommonLength };

struct CommonLength {
    float                            lengthMeters = 0.0F;
    std::vector<std::array<Vec2, 2>> edges;  // every edge in the group, for the ticks
};

// libs/sim/construction/DrawingAids.h: plain data holding only what the engine obeys,
// every switch false in the struct. The app builds it from the player's Drawing
// Settings (D14); tests fill their own.
struct DrawingAids {
    bool  angleSteps = false;
    bool  guidesFromShape = false;
    bool  alignToExistingBuildings = false;
    bool  guidesFromWorldAxes = false;
    bool  guideCrossings = false;
    bool  commonLengths = false;
    bool  roundLengths = false;
    float rectangleGridSpacingMeters = 1.0F;
};

struct SnapInput {
    const DrawingAids& aids;                   // D14; a reference, never null
    bool         freeform = false;             // Alt
    float        originCloseRadiusMeters = -1.0F;  // zoom-widened by DrawingSystem; negative uses the config, as today
    std::int64_t wallHalfThicknessMm = 0;      // wall tool only; the inset helpers take millimetres, as today
};

struct SnapResult {
    // point, kind, hitSegment, hitVertex, closesShape() as today
    std::array<SnapGuide, kMaxSnapGuides>  guides{};
    std::size_t               guideCount = 0;
    LengthSnapKind            lengthSnap = LengthSnapKind::None;
    std::optional<CommonLength> matched;      // set with LengthSnapKind::CommonLength
    std::vector<float>        commonLengths;  // the kept groups' lengths, for "=" on labels
};

struct RectangleShape {
    geometry::Ring ring;   // (left, bottom) first, counter-clockwise, millimetres
    std::string    label;  // "4 m x 3 m"
};

// D13: the rectangle spanned by two corners; nullopt when either side is under 1 mm.
[[nodiscard]] std::optional<RectangleShape> rectangleFromCorners(
    ::Foundation::Vec2 firstCorner, ::Foundation::Vec2 secondCorner);
```

`guides` replaces `guideFrom`, `guideTo`, `guideFromAlt`, and `guideToAlt`. Private: the engine reshape renames `axisAlign` to `alignToWorldAxes` (still world-only there), and the guides step (step 4) replaces it with `alignToGuides(...)` carrying the full guide rules, with `studyCommonLengths(...)` and `refineLength(...)` beside it; the line, capture, crossing, length, and marker helpers stay in the .cpp's anonymous namespace. `cornerAngles()` and `rectangleFromCorners()` are free functions in the same header.

#### Config

`assets/config/construction/snapping.xml`, `assets::SnappingConfig`, loaded in `ConstructionRegistry.cpp`. `ConfigValidator.cpp` (about lines 392 to 414) checks only `vertexSnapRadiusMeters`, `edgeSnapRadiusMeters`, and `originCloseRadiusMeters` > 0 today; every other check in the table is new. These are designer tuning, never shown to the player; the player's switches are D14's.

| Key | Default | Meaning | Change | Validator check |
|-----|---------|---------|--------|-----------------|
| `angleIncrementDegrees` | 15 | The angle step | none | > 0 and <= 90 |
| `vertexSnapRadiusMeters` | 0.4 | Snap to an existing corner or wall endpoint (all three tools) | none | > 0 |
| `edgeSnapRadiusMeters` | 0.3 | Snap to an existing edge or wall; also the squaring reach | none | > 0 |
| `originCloseRadiusMeters` | 0.5 | Close onto the first point; also the reach of a guide result that closes | none | > 0 |
| `guideRangeMeters` | 8.0 | Committed corners and nearby edges are read within this of the cursor | renamed from `smartGuideRangeMeters` | > 0 |
| `guideToleranceMeters` | 0.3 | A guide catches the cursor within this | renamed from `axisGuideToleranceMeters` | > 0 |
| `tieAngleWindowToStep` | true | The angle catch window is half the angle step | new | none |
| `guideAngleWindowDegrees` | 7.5 | The angle catch window when the tie is off | new | >= 0 and < 90 |
| `tieCrossingAngleToStep` | true | The crossing minimum angle is the angle step | new | none |
| `guideCrossingMinAngleDegrees` | 15 | The crossing minimum angle when the tie is off; also the squaring minimum | new | > the direction tolerance and <= 90 |
| `tieMergeDistanceToTolerance` | true | The merge distance is the catch distance | new | none |
| `guideMergeDistanceMeters` | 0.3 | The merge distance when the tie is off | new | >= 0 |
| `guideCrossingRadiusMeters` | 1.0 | A crossing farther than this from the cursor is ignored | new | >= `guideToleranceMeters` |
| `guideDirectionToleranceDegrees` | 1.0 | Same direction: nearby de-duplication, merge, marker | new | > 0 and < the angle window in force |
| `nearbyGuideDirectionLimit` | 4 | Nearby directions kept, integer | new | >= 0 (0 turns nearby directions off) |
| `guideCornerLimit` | 16 | Committed corners used, integer | new | >= 0 (0 turns committed corners off) |
| `priorityRule` | `sourceFirst` | Ranking among guides; `closest` is the tuning alternative | new | one of the two |
| `commonLengthSourceBuildings` | true | Buildings (and walls, in the wall tool) lend lengths | new | none |
| `commonLengthSourceShape` | true | The shape being drawn lends its earlier edges | new | none |
| `commonLengthRangeMeters` | 30 | A building lends lengths when any edge is within this of the cursor | new | >= 0 |
| `commonLengthAllBuildings` | false | Ignore the range | new | none |
| `commonLengthCatchMeters` | 0.3 | A length catches when the point slides this far or less along its direction | new | >= 0 |
| `commonLengthMergeMeters` | 0.05 | Lengths this close are one | new | >= 0 and < `commonLengthMinimumMeters` |
| `commonLengthMaxCount` | 6 | Lengths kept after ranking, integer | new | >= 0 |
| `commonLengthMinimumMeters` | 0.5 | Shorter edges lend nothing | new | >= 0 |
| `commonLengthAfterRawCursor` | true | The length step also runs on the raw direction (how plain freeform rounds) | new | none |
| `commonLengthBeatsCrossing` | false | A caught common length beats a crossing | new | none |
| `commonLengthWallsReadInsetFoundation` | true | The wall tool reads foundation edges at their inset lengths | new | none |
| `lengthIncrementMeters` | 0.1 | The rounding increment; 0 turns rounding off | new | >= 0 |
| `lengthRoundingOnGuides` | `guideDistance` | On a single guide through another point, round the distance along the guide; `segmentLength` rounds the segment | new | one of the two |
| `rectangleAdjacencySnaps` | true | The rectangle tool lands on existing corners and edges (D13) | new | none |
| `doubleClickSeconds` | 0.4 | Two left releases on the world this close in time are a double-click (D13; the colonist roster's `kDoubleClickSeconds`) | new | > 0 |
| `doubleClickDistancePx` | 4 | The second release must also be this close to the first, in logical pixels (D13; the rectangle tool's click-versus-drag threshold) | new | >= 0 |
| `alignToExistingDefault` | true | Default of the old setting | deleted; the Drawing Settings defaults are D14's block | none |

The renames make every guide key read `guide...`; the old names described the world-axis-only feature. `smartGuideRangeMm` and `axisGuideToleranceMm` are computed and never read; they are deleted, not renamed. The three ties keep D12's single-value behaviour as the default while letting the values be tuned apart. Not config, on purpose: catching by angle, squaring on edges, and a guide result closing on the first point are the design (the prototype had switches for them only to compare); the 2 mm inset safety stays the code constant `kFlushInsetSafetyMm`; the wall thickness comes from the material's presets; `maxPoints` stays in `constraints.xml`. The look keys are D16's, in `rendering.xml`.

#### Units, determinism, cost

Positions are float world metres, like every other snap. Float spacing is under 0.5 mm below 4096 m from the world origin and doubles with each power of two beyond it; within that range a guided corner is exact to float rounding (about 0.25 mm), and the millimetre quantization on commit (D1) adds up to 0.5 mm per axis, so a guided right angle over a 3 m edge is square to a few hundredths of a degree. Farther out the error grows with the spacing. Tests compare positions to 1e-3 m unless they say otherwise. Snapping runs on the local cursor only and the command carries the quantized point, so float differences between machines never reach sim state. On one machine the result is a pure function of the inputs: every walk is over `foundations()`, `segments()`, `vertices()` (ascending id) or `points`, never an unordered container, and every tie has a fixed winner. Among guides a tie falls to the generation order (Ranking, key 5). The foundation vertex and edge snaps keep the last of equal-distance hits, as today (`<=` in `snapToVertex` and `snapToEdge`, and in the prototype). The wall endpoint and wall segment snaps keep the first (strict `<`, so the lowest id), as today, and so does the rectangle tool's adjacency (D13).

Per cursor move the line list holds at most 298 lines: 66 from the shape (32 points by one pair, plus the previous pair), 136 nearby (4 directions by the previous point and up to 16 corners, by a pair: 4 x (1 + 16) x 2), and 96 world (48 nodes by a pair). Each is one point-to-line distance. After the merge at most 48 lines stay captured (14 directions, at most three parallel lines per direction inside the band twice the tolerance wide, plus the 6 shape lines through `points[0]` and `points.back()` that catch by angle), and crossings are formed among those; in practice two or three. Common lengths add one entry per edge of the shape and of each building in range (walls too, in the wall tool), grouped against at most a few dozen groups; in practice a few buildings, well under a hundred entries. Finding the corners, nearby edges, and length entries is a walk over the committed foundations and walls, as `axisAlign` and `snapToEdge` do today. `Perf: none` is the expectation; nothing on the sim or render hot path changes.

#### Tests

`libs/sim/construction/SnapEngine.test.cpp`. Unless a case says otherwise: every helper on (`angleSteps`, `guidesFromShape`, `alignToExistingBuildings`, `guidesFromWorldAxes`, `guideCrossings`, `commonLengths`) and `roundLengths` on, every config key at its default, wall thickness 0.2 m (inset 0.102 m), foundations placed in the order listed (ids ascending), positions compared to 1e-3 m. A `DrawingAids` is passed explicitly, never read from the player's defaults, which have the helpers off. `rect(x, y, angle, length, depth)` is the ring (x, y), then `length` along `angle`, then `depth` along `angle` + 90, then back, each corner rounded to the millimetre; `polar(origin, angle, distance)` is the point `distance` from `origin` at `angle`; angles in degrees from +x. The worked rectangle is p0 (0, 0), p1 (5.196, 3.000), p2 (3.196, 6.464), p3 (-2.000, 3.464).

Existing tests whose expectations change:

- `AngleSnapRelativeToPreviousSegment` (line 63) and `WallAngleSnapOffPreviousChainPoint` (line 419): a 90-degree turn is now a shape line caught by angle, so `Angle` becomes `GuideLine`; the point is unchanged. Rename them `GuideRightAngleOffPreviousSegment` and `WallGuideRightAngleOffPreviousChainPoint`. Prototype case: points (0, 0), (6, 0); cursor (6.2, 4); `GuideLine` at (6, 4).
- The seven axis tests (lines 171 to 264), ported to the inputs below (they differ from the existing tests' inputs): `AxisAlignsXToInProgressNode` and `AxisAlignsYToInProgressNode` become `GuideWorldAxisLocksX` and `GuideWorldAxisLocksY` (`GuideLine`; prototype: points (0, 0), cursor (0.2, 5) gives (0, 5), and (5, 0.2) gives (5, 0)); `AxisAlignsBothAxesToTwoNodes` becomes `GuideShapeLinesCrossAtSquareCorner` (`GuideCrossing` of two `DrawnShape` lines at (0, 6), `guideCount == 2` in place of the alt-pair checks; prototype: points (0, 0), (6, 0), (6, 6), cursor (0.15, 5.9)); `AxisAlignToleranceBoundary` becomes `GuideToleranceBoundary` (points (0, 0), cursor (0.35, 5): 0.35 m falls through to `Angle` at (0, 5)); `AxisAlignFreeformSuppresses` becomes `GuideFreeformSuppresses` (cursor (0.2, 5) under `freeform`: `None` at the cursor); `AxisAlignTogglesCommittedFoundationTargets` becomes `GuideCommittedCornersFollowAlignToExisting` (foundation (3, 10), (6, 10), (6, 12), (3, 12), no points, cursor (-2, 10.2): `GuideLine` at (-2, 10); with `alignToExistingBuildings` off, `None`); `AxisAlignCommittedRespectsRange` becomes `GuideRangeExcludesFarCorners` (the same foundation, cursor (-6, 10.2), 9 m away: `None`).
- Every existing case that checks a free length (a point from the angle step, a single guide, or the raw cursor): `roundLengths` now rounds it to 0.1 m. Each is checked; one whose expected length is not a multiple of 0.1 m sets `roundLengths` off or takes the rounded point.
- `libs/assets/assets/ConstructionRegistry.test.cpp`: `Snapping_Parsed` reads `guideRangeMeters`; the XML in `Validation_RejectsNegativeSnapRadius` uses the new key name.

Checked and unchanged: `WallOriginCloseBeatsFoundationVertex`, `WallOriginCloseBeatsWallEndpointAndSegment`, and `WallFreeformSkipsOriginClose` (the first-point close stays first and stays off under `freeform`); `NoOriginCloseBeforeThreePoints` (prototype: points (0, 0), (6, 0), cursor (0.3, 0.2)) and `OriginCloseRadiusDefaultIgnoresOverride` now land on a `GuideLine` instead of `Angle`, still not `Origin`; `FoundationSnapUnaffectedByWalls` (the Freeform tool still reads no walls) and the other wall tests (no points placed, or freeform) keep their kinds.

New, worked example (driven click by click, each placed point rounded to the millimetre):

- `WorkedExampleRectangleAt30Degrees`: cursor (0.02, -0.01), no points: `None`; place p0. Cursor `polar(p0, 28.5, 6)`: `Angle` at p1. Cursor p2 + `polar(0, 30, 0.5)` with p0, p1 placed: `GuideLine` at p2, marked; and for each of the 12 directions every 30 degrees, a cursor 0.5 m from p2 lands on the 120-degree line through p1. Cursor (-1.6, 3.8) with p0, p1, p2 placed: `GuideCrossing` at p3, two guides through p2 (as point 3) and p0 (as point 1), both marked. Cursor (0.2, -0.15) with p0 to p3 placed: `Origin`. The closed ring's corners read 90.00 degrees (two decimals) and its edges 6.000, 4.000, 6.000, 4.000 m.

New, Freeform tool:

- `GuidePerpendicularOffFirstEdgeAt30Degrees`: p0, p1; cursor `polar(p1, 118, 4)`; `GuideLine` on the 120-degree line through p1 (at (3.196, 6.464), the length rounded to 4.0 m), marker at p1 pointing at -150 degrees, toward p0.
- `GuidePerpendicularCapturedByAngleFarOut`: p0, p1; cursor `polar(p1, 115, 10)`, 0.872 m off the line; still `GuideLine` on the 120-degree line, marked.
- `GuideTwoLinesThroughOneNodeDoNotCross`: p0, p1; cursor `polar(p1, 112, 0.5)`, 0.070 m off the 120-degree line and 0.187 m off the world line through p1; `GuideLine` on the 120-degree line at (4.946, 3.433), not p1.
- `GuideClosingCornerLandsOnExistingCorner`: a committed foundation (-2, 3.464), (-2, 0.464), (-5, 0.464), (-5, 3.464), its first corner at p3; p0, p1, p2 placed; cursor 0.45 m from p3 toward (-1.6, 3.8); `GuideCrossing` at p3 of two `DrawnShape` lines (the lines through p2 and p0 meet there; neither was generated through the building's corner).
- `GuideCrossingOutsideRadiusIgnored`: a committed foundation (1.1, 15), (3, 15), (3, 17), (1.1, 17); p0, p1; cursor `polar(p1, 115, 10)`; `GuideLine`; the crossing it would make is 1.97 m from the cursor.
- `GuideNearParallelLinesNeverCross`: a committed `rect(8, 9, 31.5, 4, 3)`; p0, p1; cursor `polar(p1, 30.75, 10)`; a `NearbyStructure` line is captured, and the result is a `GuideLine` on the shape's line (`DrawnShape`).
- `GuideParallelMergeKeepsShapePoint`: a committed foundation (0.25, 10), (3.25, 10), (3.25, 13), (0.25, 13); p0; cursor (0.2, 5); `GuideLine` at (0, 5) through p0; the north-south line through the corner (0.25, 10) is merged into it.
- `GuideParallelToNearbyRotatedFoundation`: a committed `rect(6, -2, 37, 4, 3)`; p0; cursor `polar(p0, 39, 5)`; `GuideLine` on the 37-degree line through p0, `NearbyStructure`, at (3.993, 3.009) (5.0 m, rounded; the bare angle step would give 45 degrees).
- `GuideDrawnShapeBeatsCloserWorldAxis`: the reported bug. p0, p1; cursor `polar(p1, 120, 0.533)` moved 0.25 m along 30 degrees, so 0.050 m from the world line through p1 and 0.250 m from the shape's perpendicular; the point lies on the perpendicular, at (4.946, 3.433), and `guides[0].source == DrawnShape`. With `priorityRule` `closest`, the same cursor lands on the world line at (5.196, 3.600).
- `GuideNearbyStructureBeatsWorldAxes`: a committed `rect(5, -4, 37, 4, 3)`; p0; cursor `polar(p0, 37, 5)` moved 0.1 m along 127 degrees; a second foundation (9, y + 0.12), (11, y + 0.12), (11, y + 2), (9, y + 2) with y the cursor's y; the result's first guide is `NearbyStructure`, the 37-degree line through p0 (here a `GuideCrossing` with the world line through the second foundation's corner, at (4.258, 3.209)).
- `GuideParallelNeighboursBothGetLines`: A = `rect(-10, 3, 37, 4, 3)`, B = `rect(-1.5, -0.5, 37, 4, 3)`; no points; cursor at x = -2.6, 0.1 m (in y) above A's 37-degree line through its second corner; B supplies the direction, four `NearbyStructure` lines run through A's corners, and the result is a `GuideLine` through A's second corner.
- `GuideNearbyDirectionLimit`: six foundations `rect(4 cos(60 index), 4 sin(60 index), angle, 2, 1.5)` for index = 0 to 5, with angles 10, 22, 35, 50, 65, and 78; no points; cursor (0, 0); four directions kept, 78, 65, 50, and 10 degrees (mod 90), and some edges are over the limit.
- `GuideCornerLimit`: five foundations (2 index - 4, 2), (2 index - 3, 2), (2 index - 3, 3 + 0.1 index), (2 index - 4, 3 + 0.1 index) for index = 0 to 4; no points; cursor (0, 0); twenty corners in range, sixteen used.
- `GuideAlignToExistingOffDropsBuildingGuides`: as `GuideParallelToNearbyRotatedFoundation` with `alignToExistingBuildings` off; no `NearbyStructure` line and no line through a committed corner; `Angle`.
- `GuideSquaresPointOnExistingEdge`: a committed foundation (-2, 6), (10, 6), (10, 10), (-2, 10); p0, p1; cursor (3.580, 5.8); `Edge` at (3.464, 6.000), `guides[0]` the 120-degree line with its marker; the corner at p1 is 90.00 degrees (the foot would give 91.69). Replaces the draft's `GuideLosesToExistingEdge`.
- `GuideOnFirstPointReportsOrigin`: p0 to p3 placed; cursor (0.214, 0.47), 0.516 m from p0 (outside the 0.5 m origin-close radius); the shape's perpendicular through p0 is captured by angle from p3 and its foot is 0.30 m from p0; the result is `Origin` at p0, from the guide rule.
- `GuideNoMarkerOnParallel`: p0, p1; cursor (7.796, 4.6); `GuideLine` on the line through p0 parallel to the first edge, `rightAngle == false` (the only edge at p0 is the first edge). The worked example's step 4 is the counter-case: the guide through p2 parallel to the first edge is marked, since the edge into p2 is perpendicular to it.
- `GuideDeterministicTieBreak`: no points; committed foundations (0, 0), (2, 0), (2, -2), (0, -2) and (5, 0.4), (7, 0.4), (7, 2.4), (5, 2.4); cursor (3.5, 0.2), midway between their east-west lines; `GuideLine` at (3.5, 0) through the first foundation's corner (2, 0).

New, wall tool:

- `WallGuideClosesRotatedRectangleNotInset`: p0, p1, p2; cursor (-1.6, 3.8); `GuideCrossing` at p3, not offset by half the thickness. With p0 to p3, cursor (0.2, 0.1): `Origin`.
- `WallGuideParallelToHostFoundationEdge`: a host `rect(0, 0, 25, 10, 6)`; chain start (3, 3.5); cursor `polar(start, 27, 4)`; the first guide is `NearbyStructure`, the 25-degree line through the start.
- `WallGuidePerpendicularToNearbyWallSegment`: a wall from (0, 0) to `polar((0, 0), 20, 6)`; chain start (1, 2.5); cursor `polar(start, 112, 4)`; `GuideLine`, `NearbyStructure`, the 110-degree line through the start.
- `WallGuideUsesInsetCorners`: a foundation `rect(0, 0, 20, 12, 8)`; a wall from its second corner's inset point `corner` to `corner` - (3, 5); no points; cursor `corner` + (3, 0.05); two east-west lines are captured (the inset corner's and the wall vertex's) and one is kept, the other merged.
- `WallGuideSquaresTJunction`: a wall (-2, 6) to (10, 6); p0, p1; cursor (3.580, 5.8); `WallSegment` at (3.464, 6.000) with `hitSegment` set.
- `WallGuideSquaresInsetEdge`: a foundation (-2, 6), (10, 6), (10, 10), (-2, 10); p0, p1; cursor (3.580, 5.95); `Edge` at (3.405, 6.102), where the 120-degree guide through p1 meets the inset line (within 2e-3).
- `WallEndpointBeatsGuideCrossing`: a wall from (-1.7, 3.564) to (-6, 8); p0, p1, p2; cursor (-1.75, 3.584); `WallEndpoint` at (-1.7, 3.564).
- `WallGuideFreeformSuppresses`: p0, p1, p2, cursor (-1.6, 3.8), `freeform`: `None`; p0 to p3, cursor (0.1, 0.1), `freeform`: `None` (the close is skipped). The Freeform tool with the same input closes (`Origin`).
- `WallGuideOnFirstPointReportsOrigin`: p0 to p3; cursor (0.214, 0.47); `Origin` at p0 from the guide rule.
- `WallFoundationCornerSnapsToInset`: a foundation `rect(0, 0, 20, 12, 8)`; no points; cursor 0.1 m left of and 0.15 m above its second corner (11.276, 4.104); `Vertex` at the inset corner (11.145, 4.165), within 1e-6.

New, common lengths (`CommonLengths.*` in the same file):

- `FirstShackGivesTwoLengths`: a committed shack `rect(0, 0, 20, 4, 3)`; start (7.177, -2.177); cursor `polar(start, 20, 3.85)`; the kept lengths are exactly 4.000 and 3.000, two edges each.
- `CatchFromShortAndLong`: the same; cursors at 3.85 m and 4.2 m along 20 degrees both give a 4.000 m segment (`CommonLength`) on the `NearbyStructure` 20-degree line through the start; at 4.5 m, no catch, and the length is 4.500 (`Rounded`).
- `SecondEdgeCatchesOnPerpendicular`: the start and the 4 m point placed; cursor `polar(second, 110, 2.85)`; `GuideLine` on the shape's perpendicular through the second point, 3.000 m, `CommonLength`.
- `SecondShackClosesSquare`: then a cursor `polar(third, 200, 3.9)` (a `GuideCrossing`), then start + (0.12, 0.05) (`Origin`); the closed ring's edges are 4.000, 3.000, 4.000, 3.000 m, its corners 90.00 degrees, and every edge matches a length of the first shack.
- `NearerOfTwoLengthsWins`: a committed `rect(0, 0, 0, 4.0, 4.25)`; start (0, -10); cursor (4.15, -10); both lengths are within the catch, and 4.25 wins at (4.25, -10).
- `MergeKeepsHigherPriorityLength`: a committed `rect(0, 0, 0, 4.0, 4.03)`; start (0, -10); cursor (4.1, -10); one group of four edges, length 4.000; the point is (4, -10).
- `CrossingKeepsItsPoint`: a committed `rect(-3, 7, 0, 5.8, 2)`; points (0, 0), (6, 0), (6, 4); cursor (0.18, 4.05); `GuideCrossing` at (0, 4). With `commonLengthBeatsCrossing` on: `GuideLine` at (0.2, 4), 5.80 m along the guide through (6, 4).
- `ExistingCornerKeepsItsPoint`: a committed `rect(0, 0, 0, 4, 3)`; start (0, -5); cursor (4.2, 0.1); `Vertex` at (4, 0), no length snap.
- `FreeformDropsLengths`: a committed `rect(0, 0, 0, 4, 3)`; start (0, -10); cursor (3.9, -10), `freeform`; `None` at the cursor, no common lengths computed.
- `RankingNewestBuildingFirst`: a shack `rect(0, 0, 0, 4, 3)`, then a longhouse `rect(10, 0, 0, 10, 5)`; start (0, -8); cursor (3.9, -8); kept in rank order 5, 10, 3, 4, and the cursor catches 4.0. With `commonLengthMaxCount` 2: 5 and 10 kept, 3 and 4 dropped, and the point stays (3.9, -8). With the two placed in the other order: 3, 4, 5, 10, and 4.0 catches.
- `MinimumAndRange`: committed `rect(0, 0, 0, 4, 0.4)` and `rect(60, 0, 0, 7, 7)`; start (0, -8); cursor (3.9, -8); the two 0.4 m edges are dropped under the minimum and the far building's four edges out of range. With `commonLengthAllBuildings`, cursor (6.9, -8) catches 7.00 m.
- `SwitchOffLeavesLength`: the same with `commonLengths` off; cursor (3.9, -8); the point is (3.9, -8), no common lengths computed.
- `ShapeLendsItsOwnEdge`: points (0, 0), (5.5, 0), (5.5, 2), nothing committed; cursor (0.2, 2.15); `GuideCrossing` at (0, 2) (the 5.50 m length would also catch, and the crossing wins). With `guideCrossings` off: `GuideLine` at (0, 2), `CommonLength` 5.50 m matching the shape's first edge.
- `WallToolReadsInsetAndWalls`: a committed `rect(0, 0, 0, 4, 3)` and a wall (0, 10) to (2.5, 10); wall tool; start (-0.5, -6); cursor (3.2, -6); the kept lengths include 3.796, 2.796, and 2.500, and the point is (3.296, -6), 3.796 m from the start.

New, levels (`DrawingLevels.*`):

- `IncrementRoundsAngleStep`: start (0, 0); cursors `polar(start, 31, 4.97)` and `polar(start, 31, 5.04)` both give `Angle` with a 5.0 m segment (`Rounded`); `polar(start, 31, 5.06)` gives 5.1 m; with `lengthIncrementMeters` 0, 5.06 m stays.
- `CommonLengthBeatsIncrement`: a committed (10, 10), (13.37, 10), (13.37, 12), (10, 12); start (0, 4); cursor (3.42, 4); `CommonLength` 3.37 m at (3.37, 4).
- `IncrementLeavesCrossing`: `commonLengths` off; points (0, 0), (6.03, 0), (6.03, 4.07); cursor (0.15, 4.0); `GuideCrossing` at (0, 4.07), 6.03 m unrounded.
- `IncrementLeavesExistingCorner`: `commonLengths` off; a committed (3.03, 0.07), (7, 0.07), (7, 3), (3.03, 3); start (0, -4); cursor (3.2, 0.2); `Vertex` at (3.03, 0.07).
- `IncrementOffUnderAlt`: start (0, 0); cursor `polar(start, 31, 5.06)`, `freeform`; 5.06 m.
- `HelpersOffRawDirectionRounded`: every helper off, `roundLengths` on; points (0, 0), (6, 0); cursor (6.2, 4.04); `None` on the raw direction from (6, 0), length 4.0 m, at (6.198, 3.995). With every helper on: `GuideLine` at (6, 4).
- `HelpersOffKeepAdjacency`: every helper off; points (0, 0), (6, 0); a committed (9, 4), (12, 4), (12, 7), (9, 7); cursor (9.2, 4.15); `Vertex` at (9, 4).
- `EachHelperAloneTurnsItsCandidatesOn`: a committed `rect(9, 6, 37, 3, 2)`; points (0, 0), (6, 0); cursor (3.3, 2.9). With every helper off: no shape, nearby, or world lines, no committed corners, kind not `Angle`, no common lengths. With only `guidesFromShape`: shape lines exist. Only `alignToExistingBuildings`: nearby lines and committed corners exist. Only `guidesFromWorldAxes`: world lines exist. Only `angleSteps`: `Angle`. Only `commonLengths`: common lengths are computed.
- `CrossingsSwitch`: points (0, 0), (6, 0), (6, 4); cursor (0.15, 3.95); only `guidesFromShape`: `GuideLine`; `guidesFromShape` and `guideCrossings`: `GuideCrossing` at (0, 4).
- `RectangleOnGrid`, `RectangleHalfMetreGrid`, `RectangleAdjacency`: D13's cases.
- `AngleReadout`: points (0, 0), (5, 0); snapped point `polar((5, 0), 105, 3)`: one corner, 75.0 degrees, not square; `polar((5, 0), 90, 3)`: one corner, square; points (0, 0) alone with `polar(start, 30, 3)`: no corner. Points (0, 0), (5, 0), (5, 3), (0.5, 3) closing at (0, 0): the closing corner is read (80.54 degrees at the first point; 99.46 at the previous).

New, rounding on guides (`GuideRounding.*`): a committed (0, 10), (3, 10), (3, 13), (0, 13), whose corner (0, 10) is the guide's node.

- `NoPointPlacedRoundsGuideDistance`: no points; cursor (-3.67, 10.05); `GuideLine` at (-3.7, 10), `RoundedAlongGuide`, 3.7 m from (0, 10).
- `GuideThroughPreviousRoundsSegment`: nothing committed; points (0, 0), (6, 0); cursor (6.1, 3.67); `GuideLine` at (6, 3.7), `Rounded`.
- `GuideThroughOtherPointRoundsGuideDistance`: point (-6, 6); cursor (-3.67, 10.05); (-3.7, 10), 3.7 m from the corner (the segment is 4.614 m). With `lengthRoundingOnGuides` `segmentLength`: (-3.728, 10), a 4.6 m segment.
- `RoundingLeavesCrossing`: nothing committed; points (0, 0), (6.03, 0), (6.03, 4.07); cursor (0.15, 4.0); `GuideCrossing` at (0, 4.07).
- `RoundingOffLeavesRaw`: no points; cursor (-3.67, 10.05); with `lengthIncrementMeters` 0, or `roundLengths` off: (-3.67, 10); with `freeform`: (-3.67, 10.05).
- `CommonLengthStillWins`: a committed (20, 20), (23.37, 20), (23.37, 22), (20, 22) in place of the other; point (0, 4); cursor (3.42, 4.05); `CommonLength` at (3.37, 4).

`ConstructionRegistry.test.cpp` also: renamed keys load; new keys load with defaults; the Drawing Settings defaults block (D14) loads; the validator rejects each bound in the table. Check for an existing case on the same path before adding one.

Manual regression: `docs/testing/scenarios/03-build-wood-foundation.md` gains the rectangle tool (two clicks on the grid, the size label, a corner on an existing corner) and, with the helpers on in Drawing Settings, the 30-degree rectangle (the marker at corner 3, the dot at corner 4, close) and a second shack catching the first's lengths (`=` and the ticks); `04-build-walls.md` gains a rotated chain squaring its corners on guides, Alt dropping them, and a wall with every helper off keeping its adjacency snaps.

#### Build steps

Each step is a commit that builds and passes the tests on its own.

1. **Snapping config.** `assets/config/construction/snapping.xml` (rename two keys, add the new ones, move `alignToExistingDefault` into the D14 block), `libs/assets/assets/ConstructionRegistry.h` (`SnappingConfig` fields, `DrawingSettingsDefaults`; delete the two unused Mm copies), `ConstructionRegistry.cpp` (load), `ConfigValidator.cpp` (the new checks), `ConstructionRegistry.test.cpp`. `SnapEngine.cpp`'s `axisAlign` and the comments in `SnapEngine.h` and `SnapEngine.test.cpp` move to the renamed fields in the same commit. The D14 block ships with today's values, not the spec's defaults: angle steps, align to existing buildings, and world-axis guides on (today's `snapAngle`, `alignToExistingDefault` true, and `axisAlign`), so play is unchanged. Step 5, which adds the rectangle tool, flips the block to the spec's defaults (helpers off).
2. **Drawing Settings.** D14: `libs/sim/construction/DrawingAids.h` (`DrawingAids`), `apps/world-sim/UserSettings.h` (`DrawingSettings`, defaults from the block, and `drawingAids()`, which builds the engine's struct from it), `apps/world-sim/scenes/settings/SettingsScene.cpp` (the Drawing Settings pane), and `DrawingSystem::snapMemoKey`, which passes `UserSettings::Get().drawingAids()` to both tools' snaps. Behaviour unchanged until the engine reads them, except the align toggle's new label.
3. **Look keys.** D16: `rendering.xml` `<Preview>` keys with their `ConstructionRegistry.h` fields and loader, and `Rendering_RealFileValues` extended. Data only.
4. **Engine and its drawing.** One commit, since `SnapResult` changes under `DrawingSystem`. `libs/sim/construction/SnapEngine.h` and `.cpp` (the helper gating, lines, capture, merge, crossings, ranking, edge squaring, the first-point rule, the marker, common lengths, the length step, `cornerAngles`; `alignToWorldAxes`, the reshape's name for `axisAlign`, deleted); `ConstructionWorld` (`placementOrder` on foundations and wall segments, `commitSegment`'s new parameter, and every caller of it, per Common lengths > Placement order); `SnapEngine.test.cpp` (the changed and new cases above); `apps/world-sim/scenes/game/world/construction/DrawingFeedback.h` and `.cpp` (`planSnapFeedback`, `drawSnapFeedback`, and the labels, D16) with `DrawingFeedback.test.cpp`, and `DrawingSystem.cpp` and `.h` calling them (both old guide blocks deleted).
5. **Rectangle tool and its input.** D13: the tool, `snapRectangleCorner()` and `rectangleFromCorners()`, `GameScene` forwarding a left `PointerDown` to the drawing tool while the rectangle tool is active, and the wall tool's double-click (`GameScene` detects it on the world while a wall chain is active), with the Drawing Settings block flipped to the spec's defaults.
6. **Key guide.** D15.
7. **Scenarios.** The two scenario files above; run 03 and 04 as the regression pass.

No perf capture is needed; the PR's line is `Perf: none`.

#### Open points

- The crossing radius (1.0 m) and corner limit (16) are tuned in the prototype, not in the game; the radius has to stay above the 0.52 m the closing corner needs in the worked example.
- Whether the catch distance and the length increment should be zoom-stable (pixels, like the origin-close radius the tool already widens with zoom) is left as metres; the design spec's open questions carry the increment.
- The design spec's open questions (Foundations > Snapping and guides) are answered here as the prototype answers them; a ruling against any of them changes a default, not the mechanism.

### D13: Rectangle tool

Player-facing behaviour: design spec, Foundations > Drawing > Rectangle tool. The default foundation tool, for the player who wants a square building and nothing else.

- **A mode of the foundation tool.** `ToolKind::FoundationRectangle` beside the polygon foundation tool (`ToolKind::Foundation`) in `DrawingSystem`. It shares material, validation, commit, and the config strip with the polygon tool; only point placement differs.
- **Choosing it.** The Build menu's Foundation entry opens the foundation tool last picked this session, Rectangle the first time; the construction strip's tool picker switches between Rectangle and Freeform. In code: `activateFoundationTool()` keeps meaning the freeform polygon tool, since `DrawingSystem.test.cpp` calls it and then clicks polygon points; a new `activateRectangleTool()` opens the rectangle tool; `DrawingSystem` holds the session's last pick (set by either call), and `GameScene`'s `onStructureSelected("foundation")`, today a direct `activateFoundationTool()` call and its only caller outside the tests, calls whichever that pick is. No dev verb activates a foundation tool: `/api/dev/foundation` commits through `ConstructionWorld::commitFoundation` directly.
- **Corner snap.** `SnapEngine::snapRectangleCorner(cursor, freeform, gridSpacingMeters)`, pure like the others; first that applies wins. Under `freeform`: the raw cursor, `None`. Else, while `rectangleAdjacencySnaps` holds: the nearest foundation ring vertex within `vertexSnapRadiusMeters` (`Vertex`); else the nearest foot on a ring edge within `edgeSnapRadiusMeters` (`Edge`), and when that edge is axis-aligned (its ends equal in y, or in x, within 1e-9 m) the foot's free coordinate also rounds to the grid, kept only if the rounded point is still on the edge. Equal-distance vertices or edges keep the first found (strict `<`, as in the prototype). Else the grid point, each coordinate `round(value / spacing) * spacing` (`Grid`). The grid is anchored at the world origin; `gridSpacingMeters` is the Drawing Setting (1.0 or 0.5).
- **Input.** A press places the first corner (when none is placed). A release more than 4 px from that press places the second (a drag); a release within 4 px leaves the first corner waiting, and with a corner waiting the next click places the second on its release. The 4 px are logical pixels, the space `InputEvent::position` is in. Esc, right-click, or Backspace drops the first corner; a second Esc exits the tool, as for the other tools.
- **Delivering the press.** `GameScene::handleEvent` passes the drawing tool only `PointerUp` today (about lines 736 to 760; every other pointer type returns false before it). It gains one case: while the rectangle tool is active, a left `PointerDown` goes to a new `DrawingSystem::handlePress(...)` and is taken; every other tool still sees only the release.
- **Double-click ends a wall chain** (new; built in this step because it shares the input routing). The world input has no double-click today, and salal's `InputEvent` reports none (no click count), so `GameScene` detects it: while the wall tool is active, it records the time and logical position of each left release it hands the tool. A left release within `doubleClickSeconds` (0.4 s, the colonist roster's `kDoubleClickSeconds`) and `doubleClickDistancePx` (4 logical px) of the previous one is the second click of a double-click: it places no point, and if a chain is in progress it calls `DrawingSystem::finishChain()`, as Enter does. The rule is the prototype's: the browser's first click places the final point, the second click (`event.detail >= 2`) places nothing, and the `dblclick` handler commits the chain. A third release starts over (the pair is consumed, as in the roster). Both values are config (D12's table).
- **The rectangle.** `rectangleFromCorners(firstCorner, secondCorner)` (D12's API) builds the ring and the label from the first corner and the snapped point: left = min x, right = max x, bottom = min y, top = max y, the ring (left, bottom), (right, bottom), (right, top), (left, top), counter-clockwise as in the prototype (and `commitFoundation` normalises every ring to counter-clockwise anyway). When either side is under 1 mm it returns nothing, there is no rectangle, and the click places nothing. Each corner is quantized to the millimetre (D1) and the ring goes through `ConstructionValidator` and the same commit as a closed polygon, so constraints, buildability, and the red preview with its reason are one path.
- **Preview.** The pending rectangle's fill and outline in the blueprint preview style (D8's immediate-mode preview), a size label at the cursor (`4 m x 3 m`: each side rounded to the millimetre, printed without trailing zeros), and, when the grid placed the point, a small cross on the grid point (5 px arms, the world guide colour at 0.55 opacity). No guides, no angle steps, no length step, no helper drawings, no key guide legend lines.
- **Tests** (`SnapEngine.test.cpp`, `DrawingLevels.*`): `RectangleOnGrid`: a committed (0, 0), (4, 0), (4, 3), (0, 3); first corner cursor (6.3, 0.8) gives `Grid` (6, 1); second cursor (9.6, 3.7) gives (10, 4), and `rectangleFromCorners((6, 1), (10, 4))` gives the ring (6, 1), (10, 1), (10, 4), (6, 4) and the label `4 m x 3 m`. `RectangleHalfMetreGrid`: spacing 0.5, cursor (6.3, 0.8) gives (6.5, 1). `RectangleAdjacency`: cursor (4.2, 0.25) gives `Vertex` (4, 0); with `rectangleAdjacencySnaps` off, `Grid` (4, 0).

### D14: Drawing Settings storage versus construction config

Two homes, split by who sets the value.

- **Drawing Settings** (the player): the eight helper switches, Round lengths, Show key guide, and the rectangle grid spacing. They live in `UserSettings` as one member, `drawing`, of type `DrawingSettings`, replacing `alignSnapToExistingFoundations`:

  ```cpp
  struct DrawingSettings {
      bool  angleSteps = false;
      bool  guidesFromShape = false;
      bool  alignToExistingBuildings = false;
      bool  guidesFromWorldAxes = false;
      bool  guideCrossings = false;
      bool  commonLengths = false;
      bool  guideDistances = false;
      bool  angleReadout = false;
      bool  roundLengths = true;
      bool  showKeyGuide = true;
      float rectangleGridSpacingMeters = 1.0F;  // 1.0 or 0.5
  };
  ```

  `DrawingSettings` is the superset the player sees. The engine never includes it (`libs/sim` cannot reach an app header); it takes `construction::DrawingAids`, which holds the same switches minus the display-only ones (`guideDistances`, `angleReadout`, `showKeyGuide`), and `UserSettings::drawingAids()` builds that struct from `drawing`. The display-only switches never enter sim; the drawing code reads them.

  The initializers above are only the struct's; the values come from a `<DrawingSettingsDefaults>` block in `snapping.xml`, one element per field with the defaults above, read once when `UserSettings` is first touched, as `alignToExistingDefault` is today (that key moves into the block as `alignToExistingBuildings`). `UserSettings` stays session-scoped until a user-settings file lands; then this struct is what it saves. The validator checks that `rectangleGridSpacingMeters` is 1.0 or 0.5.
- **The Drawing Settings pane.** `SettingsScene` replaces its one construction toggle with a "Drawing Settings" group: the helper toggles in the design spec's order, then Round lengths (its label built from `lengthIncrementMeters`, "Round lengths to 0.1 m"), Show key guide while drawing, and the grid spacing as a two-choice control. Each row has the one-line description from the design spec's table; the "15 degree" in Angle steps' line comes from `angleIncrementDegrees`.
- **Construction config** (the designer): everything in D12's table and D16's look keys. Never shown to the player.
- **Reading them.** `DrawingSystem` builds a `DrawingAids` from `UserSettings::Get().drawingAids()` and passes it by const reference in `SnapInput` on every move, and reads the display-only switches from `drawing` itself; the engine never reads `UserSettings`, so tests pass their own `DrawingAids`.

### D15: Key guide panel

Player-facing behaviour: design spec, UI & Selection > Key guide.

- **Where.** A HUD component, `DrawingKeyGuide`, shown while any `DrawingSystem` tool is active (`FoundationRectangle`, `Foundation`, `Wall`, `Opening`, or `FoundationEdit`) and `showKeyGuide` is on. It is the first child of `GameUI`'s `bottom_dock`, the stack that holds the construction config strip and then the gameplay bar (`GameUI.cpp` about lines 62 to 78), so the dock lays it out directly above the strip, centred like it, one `space1` gap apart; being in the same stack, it can never overlap the strip, and it rises when the strip appears. The bottom left is the selected entity's panel and the task list, and the bottom right the zoom panel, so neither corner is free. It takes no keyboard focus (the HUD focus policy) and takes pointer events only inside its own bounds, so clicks elsewhere reach the tool.
- **Content.** Built from the active `ToolKind` and `DrawingSettings` each time either changes: the tool's key rows from the design spec's table (key chips, then the action text), then the legend rows for the helpers that are on, each with a small glyph (32 by 14 px) drawn with the same helpers as the preview (D16), so it follows the look keys, then the foot line. Legend rows appear for the tools that run `snap()` or `snapWall()` (Freeform, Wall, and FoundationEdit); the rectangle and opening tools have none. There is no row group for blueprint vertex editing, which the game does not have yet.
- **Collapse.** `?` or `H` toggles it between the panel and a small "Keys ?" chip; clicking the chip opens it. `?` is the `?` character event (`InputEvent::Type::Char`, codepoint 63), whatever key and Shift state produce it on the player's layout, never a key code; `H` is the `Key::H` key-down. `GameScene::handleEvent` takes both while a drawing tool is active and nothing else handled them (world hotkeys stay in `handleEvent`; today it ignores `Char` events, so it gains that case). The collapsed state is session state in `DrawingSystem`, open at start, shared across tools, not saved.
- **Tests.** A unit case for the content builder: each tool's rows, and the legend rows switched by each helper, against the design spec's tables. The scenario passes check that it is there and collapses.

### D16: Guide rendering look

Player-facing behaviour: design spec, Foundations > Snapping and guides, under Look. One rule: solid is geometry, dotted and part transparent is alignment, small solid markers state a fact. Not drawn yet, later slices add them: a matched length's bold accent label and " =", the common-length marks and their ticks, and the angle readout with its arc.

- **One helper.** `DrawingFeedback` (`apps/world-sim/scenes/game/world/construction/DrawingFeedback.h`): `planSnapFeedback`, a pure function from the snap result, points, camera, and style to the strokes, rings, dots, and labels of one frame (tested), and `drawSnapFeedback`, which draws that plan; used by the foundation preview and the wall preview, it replaced the two near-identical guide blocks `DrawingSystem` had, one per preview, and their alt-pair branch. It draws after each preview's geometry and above it: the guides, the markers, the labels, and the snapped-point marker all sit over the placed edges, the rubber band, and the vertex dots (and over the wall preview's band), where the guides used to draw under them. It draws `guideCount` guides whatever the kind (a squared `Edge` or `WallSegment` carries one), each from `from` to `to`, skipping one whose ends are not finite; a stroke or right-angle marker under half a pixel long on screen draws nothing. A second guide whose ends both lie within a pixel of the first's draws no stroke or label of its own, but its right-angle marker draws unless the first marks the same edge, so each distinct edge direction among duplicates gets one marker. A guide's stroke draws only where it lies more than a pixel from the segment being drawn (the previous point to the snapped point); the segment's solid line already shows the rest, and dots that close would bead it (teal beads on red when it is invalid). A guide ends at the snapped point, the segment's end, so its stroke runs from its node to where it first comes within a pixel of the segment, and nothing draws when that is shorter than half a pixel. A guide through the previous point draws no stroke this way, and its right-angle marker still draws; so the angle step's ray draws nothing beyond the segment, a world-axis guide from an older corner that runs on through the previous point is dotted to a pixel short of it and solid beyond, and a guide a fraction of a degree off the segment stops where it comes within the pixel instead of running along the solid line. A guide's distance label draws only when its stroke draws, beside the middle of the part of the stroke on screen (D12).
- **Alignment strokes.** Each guide is a `Primitives::drawPolyline` of its two points with `width = guideWidth`, the guide colour at `guideOpacity`, and `dash = DashPattern{guideDashPx, guideGapPx, 0, DashCap::Round}` for `dotted` (round caps make each 1 px dash a dot), the same with `DashCap::Butt` for `dashed`, no dash for `solid`. The pattern starts at the guide's node (phase 0 at `from`), so the dots don't crawl as the cursor moves. The origin's close-radius ring (three or more points placed) uses the same dash, in `originHaloColor` at that colour's own alpha (so the config sets its opacity), 1.5 px wide (1 px at 0.42 opacity was invisible over grass); it replaced the solid 1.5 px ring the previews drew there before. It draws only while the close halo does not: once the click will close and the halo is drawn, the halo alone marks the first point, and where the close verdict refuses the click, so no halo is drawn, the ring stays.
- **Markers, solid at `markerOpacity`.** The right-angle marker in screen space: `from`, `from` + the edge direction, and `from` + the unit direction toward `to` go through `toScreen`, the two screen directions are normalised (`edge` and `guide`), and with `size` = `rightAngleMarkerPx` the mark is two 1.5 px lines from `corner + size*edge` to `corner + size*edge + size*guide` to `corner + size*guide`. A right-angle marker whose edge is within about 3 degrees of parallel to its guide draws nothing. A `GuideCrossing` adds a filled dot of radius `guideCrossingDotPx` (3 px), centred on the point and drawn above the snapped-point marker, inside its white dot, so the marker reads as a target; it takes the first guide's colour (its source colour under `bySource`) at `markerOpacity`. Common lengths: a tick across the segment at the snapped end, 8 px each side, 2 px wide; on each matched edge a 6 px band at 0.22 opacity and, at its middle, two 1.75 px ticks 4 px apart, 6 px each side. The angle readout's arc is 18 px in radius, 1.5 px wide; its square corner is the right-angle marker.
- **Labels.** The segment label, 12 px text (bold and in the accent colour when matched): beside the segment's middle, 10 px off it on the side away from the placed points' centre, measured from the previous point in world metres so that along a ray it does not wander with the snapped point's rounding (with the centre within half a pixel of the line, the side facing up the screen, or left when the segment is within about 11 degrees of vertical, where its normal's y is under `kFacingUpMinimumNormalY`, 0.2), when that keeps it at least 40 px from the snapped point, where the pointer sits; otherwise at the snapped end, 12 px diagonally off it in the quarter that points most along the segment, of up right, up left, and down left (near-ties go to the first in that order), never the lower right one, where the pointer hangs. There is no segment label while its length rounds to zero (the snapped point on the previous point): there is nothing to measure. Every placement, at the middle and at the end, is judged on a box the width of a "999.00 m" label (or the label's own width, when wider), anchored as the label is (at the end, by its corner nearest the snapped point), and the label draws at its own width inside that box, so under 1000 m a digit added at 10 m or 100 m changes neither whether the label fits a placement nor which one it takes. It keeps off the snapped-point marker (its dot and its ticks, a cross rather than a square, so the end placements fit between the ticks), the close halo while that is drawn, the wall tool's snap-target ring while that is drawn, and a box the preview asks it to keep clear: a rejected Add or Subtract edit's red reason beside the pointer, measured with the same text measure as the labels. Each placement is checked where it finally lands, after any move onto the screen, and one that would cover any of them, or a middle one moved back within 40 px of the snapped point, gives way to the next in order. The end placements sit 12 px diagonally off the snapped point, or off the close halo's box when the halo is drawn round the snapped point (the click closes on the first point), and off the snap-target ring's box when the ring is drawn. With no clear placement, which happens with the snapped point near the top-left corner of the screen, where only the lower right would fit, with the label squeezed between a screen edge and a close halo drawn off the snapped point (the near-start rescue), or with it squeezed between a screen edge and the reason, it takes the first end placement whose box fits the screen unmoved, even where that covers what it keeps off, and moves the first one just inside only when none fits (then it can sit to the lower right in the top-left corner). That corner lies under the game's top bar, where the drawing preview does not update. When the segment's middle is off the screen, a middle placement is moved inside to the nearest screen edge, and taken only if it is still clear and 40 px from the snapped point there. Sweeps of rays from points across the screen (three screen sizes, zooms from 0.25 to 20, one, two, or six placed points) show this much, and no more: with the snapped point at least 120 px and the previous point at least 100 px from every screen edge, and no close halo or edit reason drawn, the placement changes at most once along a ray, from the end to the middle; and a digit gained at 10 m or 100 m never changes the placement, anywhere (six million checked pairs, no violation). Two cases break the first claim, and both are accepted as rare and cosmetic. With two or more points placed, when the placed points' centre lies almost exactly half a pixel off the segment's line (within about a thousandth of a pixel of the edge of the dead zone), the label can change sides every few centimetres along the whole ray; by a simulation of pointer-placed points that is about one two-point segment in fifty thousand, and no stateless rule avoids it, since any threshold has an edge that rounding can straddle, so removing it would take the label remembering its last side. And more than about a kilometre and a half from the world origin, float rounding of the snapped point on a very short segment can change the end placement once within the first ten centimetres of a ray. Outside that, the label gives way to the screen edge, the marker, the halo, the wall tool's snap-target ring, and the reason, and may change sides more than once and back. Guide distance labels at `guideLabelPx`, in the guide's colour at full opacity (accent when matched); the angle readout at `guideLabelPx`, `angleReadoutDecimals` places. Every label sits on a dark plate (the prototype's dark, `#0A0C10` at 0.7 opacity, 3 px larger than the text all round, 3 px corners) with no text halo, since the renderer caps a halo at about a pixel and thin halos read badly over the tan floor. The plate, sized by the font's line height, is the box labels keep apart. A guide label whose plate would leave the screen goes to the other side of its guide, and is moved just inside only when both sides leave it; it sits beside the part of its stroke on screen and shifts no further than that part's ends (D12), and with no clear spot along it, it is dropped. Lengths show `guideDistanceDecimals` places; a guide distance that rounds to zero prints unsigned.
- **The cursor.** The game shows the operating system's cursor at the raw position, so, departing from the prototype on purpose, it draws no raw-pointer ring and no pull line from the raw pointer to the snapped point. It draws the snapped-point marker where the point will go, at full opacity as the prototype does: a white dot of radius `max(4, cursorRadiusPx)` (6 px by default) with a 2.5 px black outline, and four ticks, left, right, up, and down, each from `radius + 5` px (11 px) to 16 px from the point, a 2 px white stroke over a 6 px black one. Off buildable ground (and not closing), or while closing with the close refused, the marker's dot takes the invalid colour, keeping its outline and ticks, in the foundation tools and the wall tool alike; it is the one marker, replacing the separate red dot the foundation tools drew at the cursor (the wall tool had none, so its marker turning red off buildable ground is new). With the wall tool snapped to a wall endpoint or foundation vertex (amber, `snapVertexColor`) or to a wall segment or foundation edge (cyan, `snapEdgeColor`), a 2 px snap-target ring is drawn above the marker at 4 px outside its ticks (20 px at the default radius), replacing the old ring under it, whose radius was half the vertex snap radius on screen and so grew and shrank with zoom (6 px at the least). Whenever the next click closes (an `Origin` result or the near-start rescue close) and the close verdict accepts it, the first-point halo: a ring of radius `max(ticks + 6, originCloseRadiusMeters * pixelsPerMeter / 2)` screen pixels (half the close radius on screen, with the zoom-widened radius from `SnapInput`; the floor, 22 px at the default radius, is its own constant so the halo rings outside the marker's ticks, and is not `originHaloMinRadiusPx`, which floors the catch radius and so the close ring), 3 px wide, in the shape guide colour (`guideColor` under `one`, `guideColorShape` under `bySource`) at `markerOpacity`. It took the place of a 3 px ring in the valid colour at the full close radius. While the next click closes, the preview's colour, the halo, the snapped-point marker's dot, the status strip, and an Add or Subtract's outline colour and reason text all show the one pending close, the result of `DrawingSystem::closeVerdict()` kept while the next click closes (the strip reads it directly, the rest through `previewVerdict()`). For a foundation and for Add and Subtract it is the check the closing click runs before it commits (the ring's validation and its whole footprint for a foundation; for an edit, the outline's validation and that it is seen, and for Add the ground it adds, since a Subtract builds nothing): a close the click would commit draws the halo, and the rubber band and fill read valid even when the pointer sits where a point would be refused; a close it would refuse draws no halo, the band and fill read invalid, the marker's dot is red, and the strip and the edit's reason text say why. For the wall tool it is the chain with the closing point added: its ground, and every segment through the check `commitWallChain` gives it (`commitWallSegments`, run on a copy of the world, so each segment is judged against the ones committed before it). A halo means the whole chain gets built; a refused verdict means not all of it will be. The wall click does not run the verdict, and still builds the segments that pass. The verdict's shape part (a foundation's ring validation, the wall chain's segment checks) is kept with the snap while the snap memo holds, since it reads only what the memo's key covers; an edit's outline reads the target's blueprint, so it is validated once per move and shared with the edit preview and the commit; the ground checks run on every move, since they read player visibility and the navigation mesh, which the key does not cover, the cost the placement cursor's own ground check already pays. The picture can lag a world change by a frame, and a commit can still fail after these checks in the rare case the topology refuses it (a toast says so). Otherwise the colour is the point's, as before.
- **Keys.** `assets/config/construction/rendering.xml` `<Preview>`; all sizes are screen pixels and never scale with zoom. Everything `planSnapFeedback` sizes (the look keys below and its own pixel constants) is interface the player reads at the cursor, so it is in ui px and follows the Interface size: `DrawingFeedbackInput::uiScale` (the scale `renderPreview` gets) multiplies every one of them in one place, as it does the edit reason's text and offset; ratios, angles, opacities, colours, and world lengths don't scale, nor do the preview's own lines and vertex dots. The close catch's pixel floor, `originHaloMinRadiusPx`, decides what a click does and is not scaled, so at a larger interface size the dotted close ring, drawn at the catch radius, keeps its size while the halo grows.

| Key | Default | Meaning |
|-----|---------|---------|
| `guideLineStyle` | `dotted` | `dotted` (round caps), `dashed`, or `solid` |
| `guideDashPx` | 1.0 | Dash length; with `dotted` and round caps, each dot is about the line width across |
| `guideGapPx` | 4.5 | Gap between dashes |
| `guideWidth` | 1.5 | Guide line width (was 1.0) |
| `guideOpacity` | 0.6 | Opacity of every alignment line |
| `markerOpacity` | 0.9 | Opacity of the right-angle marker, the crossing dot, and the close halo (labels and the snapped-point marker draw at full opacity) |
| `guideColorMode` | `one` | `one` colour for every guide, or `bySource` |
| `guideColor` | `#5FB3C8FF` | The one guide colour, a muted teal (was `#B3D9FF66`; opacity is now `guideOpacity`) |
| `guideColorShape` | `#8CC4FFFF` | By source: shape guides |
| `guideColorNearby` | `#FFAD5CFF` | By source: nearby-building guides |
| `guideColorWorld` | `#7FE0A6FF` | By source: world-axis guides |
| `guideColorLength` | `#F29AD8FF` | By source: common-length marks (with `one`, they take `guideColor`) (not drawn yet) |
| `guideDistanceDecimals` | 2 | Decimals of the segment and guide distance labels |
| `guideLabelPx` | 11 | Guide distance and angle readout (not drawn yet) text size |
| `angleReadoutDecimals` | 1 | Decimals of the angle readout; also sets its square tolerance, half a unit in the last place (0.05 degrees) (not drawn yet) |
| `rightAngleMarkerPx` | 8.0 | The right-angle marker's leg length |
| `guideCrossingDotPx` | 3.0 | The crossing dot's radius |
| `cursorRadiusPx` | 6.0 | The snapped-point dot's radius |

Colours are eight hex digits, `#RRGGBBAA`, the only form `ConstructionRegistry::parseColor` accepts (anything else logs a warning and loads opaque black). The alpha is FF; opacity comes from `guideOpacity` and `markerOpacity`, and a guide label draws its guide's colour at full opacity on its plate. The accent colour for a matched label comes from the UI theme, as the validity colours do. The existing `originHaloColor`, `snapVertexColor`, `snapEdgeColor`, `fillPreviewAlpha`, `bandPreviewAlpha`, `lineWidth`, `vertexRadiusPx`, `invalidVertexRadiusPx`, and `originHaloMinRadiusPx` stay as they are. The `guideColor` comment changes from "angle-snap guide line" to "guide lines, right-angle markers, crossing dots; common-length marks are not drawn yet".

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
| **Cities: Skylines II** road tools | The interaction grammar checklist for freeform drawing: angle snap, geometry snap, projected guidelines, length/angle readouts, validity coloring. Matches the design spec's drawing UX. Its several separately toggled snap options ([road tools](https://paradoxinteractive.com/games/cities-skylines-ii/features/road-tools)) are close to D12's helper switches; D12 still ranks the sources among themselves, so any set of them switched on resolves by one key. ([dev diary](https://forum.paradoxplaza.com/forum/developer-diary/development-diary-1-road-tools.1590300/)) |
| **SketchUp** inference engine | Point inferences (endpoint, midpoint, intersection) and linear inferences (axis, "from point" along an axis, parallel/perpendicular to an edge, shown in magenta); Alt cycles all on, all off, and parallel/perpendicular only; Shift or an arrow key locks one. Our D12 guides are its linear inferences, the crossing is its intersection point, and Alt maps to "all off". The lock key is left out. ([drawing basics](https://help.sketchup.com/en/sketchup/introducing-drawing-basics-and-concepts)) |
| **AutoCAD** polar and object snap tracking | Polar tracking shows temporary alignment paths at set angle increments; object snap tracking acquires up to seven points and shows paths through them; the pick goes where two paths cross. The bounded acquired-point set and path-crossing pick are the shape of D12's nodes and crossings. ([polar tracking](https://help.autodesk.com/cloudhelp/2026/ENU/AutoCAD-LT/files/GUID-7EC3C63D-EA4E-4E65-A676-C3A3627E3F19.htm), [object snap tracking](https://help.autodesk.com/cloudhelp/2026/ENU/AutoCAD-LT/files/GUID-665DC37F-8C3E-414A-9369-72A13C0BE07A.htm)) |
| **Figma** snapping | A red line shows alignment to other objects' centres and edges while moving; holding a modifier turns snapping off for the drag. Hold-to-disable matches D12's Alt; D12's guide is a dotted, part-transparent line in one muted colour rather than a solid red one. ([alignment and position](https://help.figma.com/hc/en-us/articles/360039956914-Adjust-alignment-rotation-position-and-dimensions)) |
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

**G. Editing & polish.** Foundation add/subtract (the in-house ring booleans + re-validation), vertex editing on blueprints, demolish-building cascade, multi-select batch actions, click-cycling refinement.

*G3 as built (foundation Add / Subtract).* The edit is a `ToolKind::FoundationEdit` mode of `DrawingSystem`, not a separate tool class: it reuses the foundation tool's snap, click, close, Backspace, and Esc flow, activated from the foundation panel's Add / Subtract buttons. The drawn polygon is a cutter, so per point it only has to stay simple with spaced vertices (`ConstructionValidator::validateEditPoint`); the foundation rules apply to the *resulting outline*. `ConstructionValidator::validateFoundationEdit` decides everything in one place: a blueprint is editable only while `StructureBlueprint::shapeEditable` holds (nothing delivered, no work, no demolish order) and is then unioned or carved in place (`ConstructionWorld::addToFoundation` / `subtractFromFoundation`, with the mirror entity's manifest, work, and HP re-sized by the same sizing helper spawn uses and sent back to Clearing); a built foundation never shrinks, and an Add onto it becomes an **extension**, a separate blueprint foundation whose ring is the drawn polygon minus the target, linked by `Foundation::mergeTarget` (`ConstructionWorld::commitExtension`). One extension per target at a time, so merges never depend on order. The validator judges the outline the extension's merge will actually produce, and `commitExtension` refuses a ring whose union with the target fails, so `mergeExtension` cannot fail later. When the extension's build completes, the structure-completed callback merges it: the target's ring becomes the union, the extension's walls re-host onto the target (a wall on the old edge stays put and becomes interior), the target entity absorbs the extension's manifest, work, and HP, and the extension entity goes through the deferred-destroy queue. A pending extension counts as a dependent of its target for demolition: plain Demolish and Demolish building both take it down, and the deconstruct cascade gate holds the target until it is gone. Outline checks (`checkOutline`, shared with `validateRing`) run the full closed-shape constraints, the point cap, min/max area, and overlap plus pathing clearance against every other foundation: every vertex of either ring closer than `pathingClearance` to the other must itself touch it (snapped edge to edge, within 2 mm), so touching in one place never excuses a sliver elsewhere. An Add checks buildability only on the ground it adds (`FoundationEditResult::buildsOn`: the extension for a built target, the drawn polygon for a blueprint). A Subtract must also keep each hosted wall's full-thickness band inside the remainder. `/api/dev/foundation-edit` runs the same `DrawingSystem::applyFoundationEdit` the tool's close runs.

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
