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

- **Trigger:** wall segment transitions to built, or a built segment is demolished. Re-extract faces for the affected connected component only (a demolition merges exactly two faces; O(local)).
- **Room identity:** persisted by maximum area overlap with the previous face set, so names survive edits.
- **Failure feedback:** when a wall completes and almost-closes a loop, nothing happens silently (the Prison Architect confusion). Cheap heuristic for later UX polish: surface near-misses in the rooms overlay.

### D7: Construction lifecycle — goal source into the task system

Construction is a **goal source** in the sense of the planned Goal-Driven Task Generation refactor (`/docs/technical/task-generation-architecture.md`): blueprints create goal-level tasks; reservations stay item-level.

- **`ConstructionSystem`** (ECS system, logic band): watches blueprint entities and emits goals through their lifecycle:
  1. *Clear:* query `SpatialIndex` for entities intersecting the foundation footprint → chop/mine/haul-away goals. Blueprint blocked until clear. As built: harvestables hold the site and are cleared by a colonist; when the foundation enters construction the rest of what is on the footprint (grass, stones, rocks, loose piles) is taken off it, so it is built on bare ground (`docs/technical/task-claims.md`, "Build-site clear").
  2. *Deliver:* blueprint advertises a material manifest (computed from geometry × material config). The blueprint entity carries a delivery inventory (reuses `Inventory` + a restricted accept list); standard haul chains (`Chain_PickupDeposit`) bring materials. Site-staged materials render as piles.
  3. *Build:* when the manifest is complete (and, for walls, the host foundation is built), emit build goals. `Build` action already exists (`needsHands=true`); `ActionSystem` ticks `workDone += rate(skill) × dt` and marks render progress dirty. Multi-builder: N work slots distributed along the perimeter, N from config scaled by size.
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
- `snapping.xml`: angle increment, snap radii, origin-close radius, and the guide keys (range, tolerance, crossing radius, direction tolerance, nearby-direction and corner limits; D12).

Cross-validation: every material referenced by a thickness preset exists; opening types reference valid materials; constraint values are internally consistent (e.g., clearance ≤ segment clearance).

### D11: UI — DrawingSystem beside PlacementSystem, selection extension

- **`DrawingSystem`** (new GameScene subsystem, sibling of PlacementSystem): owns tool state machines (`FoundationTool`, `WallTool`, `OpeningTool`, `EditTool` for vertex/add/subtract editing), a shared **`SnapEngine`** (candidate snap targets queried from `ConstructionWorld`: endpoints > vertices > points-on-segments > edges, then guides (D12), then the angle increment relative to the previous segment), and a shared **`ConstructionValidator`** (constraint checks returning a violation enum that drives both the red colorizing and the reason text). Input order becomes: GameUI → DrawingSystem → PlacementSystem → SelectionSystem; the drawing tool consumes clicks while active. `InputEvent.modifiers` already carries Alt/Ctrl/Shift.
- **Selection:** new `Selection` variants (`FoundationSelection`, `WallSegmentSelection`, `OpeningSelection`) with priority constants slotted per the design spec's click-priority stack, plus two new hit tests backed by `ConstructionWorld` queries: point-in-polygon (foundations) and point-to-band distance (segments, openings). Repeated-click cycling is new SelectionSystem behavior (currently first-hit-by-priority only).
- **Info panels:** new adapters (`adaptFoundation`, `adaptWallSegment`, `adaptOpening`) emitting existing slot types plus a `ConstructionProgressSlot`; action buttons reuse `ActionButtonSlot` (Edit shape, Cancel, Add, Demolish, Demolish building).
- **Config strip:** new docked `UI::Component` above the GameplayBar (LayoutContainer horizontal: material cards, thickness presets, readouts, validity message), shown while a structure tool is active.
- **Notifications:** room formed via existing `ToastStack`.

### D12: Snap guides (parallel, perpendicular, crossings)

Player-facing behaviour: design spec, Foundations > Snapping and guides. This decision replaces the world-axis-only alignment with one formulation in which the world axes are one source of three.

**Today (main at 4f0fc266).** `libs/sim/construction/SnapEngine.cpp`: `snap()` (lines 370 to 411) tries origin-close (3+ points), foundation vertex, foundation edge, `axisAlign`, `snapAngle`, raw. `snapWall()` (465 to 523) tries wall endpoint, foundation vertex (inset), wall segment interior, foundation edge (inset), `axisAlign`, `snapAngle`, raw. `axisAlign` (293 to 368) locks the cursor's X and/or Y to the nearest node within `axisGuideToleranceMeters`; nodes are the in-progress points plus, when `alignToExisting`, committed foundation ring vertices within `smartGuideRangeMeters` (raw corners in both tools; wall vertices are not nodes). `snapAngle` (257 to 291) always snaps the direction from the previous point to the nearest `angleIncrementDegrees` step off the previous segment (off +x for the first), keeping distance; it has no capture window, so it is the unconditional fallback. Nothing reads an existing edge's direction or the shape's first edge, and nothing intersects two lines. `axisAlign` runs before `snapAngle`, so a world-axis hit within 0.3 m pre-empts the 90° turn on a rotated shape (the reported bug).

#### Formulation

A **line** is a node (a point) and a direction, compared modulo 180°; directions come in **pairs**, a direction and its perpendicular. The engine builds nodes, directions, and the lines below; captures lines near the cursor; merges near-duplicates; forms crossings; ranks every candidate with one key.

**Nodes.**

- Shape points: `points`. A foundation has at most `maxPoints` (32, constraints config; the validator refuses more). A wall chain has no such cap, so its lines use `points[0]` and its most recent `maxPoints - 1` points.
- Committed corners, only when `alignToExisting`. Foundation tool: foundation ring vertices. Wall tool: each foundation ring vertex at `outerFaceFlushCorner(ring, i, wallHalfThicknessMm)`, where a perimeter wall's centerline turns, plus wall vertices. Those within `guideRangeMeters` of the cursor are taken nearest first, at most `guideCornerLimit`; ties fall to `foundations()` order and ring index, then `vertices()` ascending id. Each corner remembers its structure: its foundation, or the wall segments at that vertex. Using the inset corner in the wall tool keeps a foundation corner's guides on the perimeter wall's centerline instead of half a wall thickness outside it, where they would run parallel to the wall vertex's own guides and fight them.

**Directions.**

- Shape first: `points[0]` to `points[1]`, once there are two points.
- Shape previous: `points[n-2]` to `points[n-1]`, when it differs from the first by more than `guideDirectionToleranceDegrees` (mod 90°).
- Nearby, only when `alignToExisting`: committed edges whose closest point is within `guideRangeMeters` of the cursor (foundation tool: foundation ring edges; wall tool: those plus wall segment centerlines), zero-length edges skipped, walked by that distance, ties by foundation id and ring index, then segment id. An edge within `guideDirectionToleranceDegrees` (mod 90°) of the world axes or of a direction already kept is skipped; the first `nearbyGuideDirectionLimit` are kept, each with the structure that supplied it. Blueprints count; they are what the player is lining up with.
- World: +x.

**Lines, the closed list.** Nothing else generates a line.

1. The shape first pair through every shape point. `DrawnShape`.
2. The shape previous pair through `points.back()`. `DrawnShape`.
3. Each nearby pair through `points.back()` (when there is one) and through the committed corners of the structure that supplied it. `NearbyStructure`.
4. The world pair through every shape point and every committed corner. `WorldAxes`.

Left out on purpose: the shape's directions through committed corners (a building parallel to the shape brings its own direction through its own corners, and no worked case needs more), nearby directions through shape points other than the previous one, and directions from shape edges other than the first and previous.

**Capture.** A line is captured when the cursor is within `guideToleranceMeters` of it. A `DrawnShape` line that passes within 1 mm of `points.back()` or of `points[0]` is also captured when the direction from that point to the cursor is within `angleIncrementDegrees / 2` of the line (mod 180°). The test is geometric, not by node: the first edge's own line is generated through `points[0]` (and through `points[1]`, then dropped as a duplicate by the merge), and it still catches by angle from `points[1]` while that is the previous point. A single line's snapped point is the foot of the perpendicular from the cursor.

The window through the previous point is the angle step's own, so the 0°, 90°, 180°, and 270° turns off the previous edge are caught exactly where `snapAngle` caught them, and now come back as `GuideLine` with a marker. The window through `points[0]` catches the closing corner: on the 30° rectangle below, the cursor (-1.6, 3.8) is 0.51 m off the guide through `points[0]` (outside the 0.3 m tolerance) but 7.2° off it as seen from `points[0]`, so the guide is captured and the crossing forms. Nearby and world lines are distance-only, as world lines are today, so they never crowd out the other 15° steps. `snapAngle` governs only when no line is captured; its result keeps kind `Angle`.

**Merge.** Captured lines are walked in rank order (source, shape point before committed corner, generation order). A line within `guideDirectionToleranceDegrees` of a kept line and less than `guideToleranceMeters` from it at the cursor is dropped. A committed corner's line 0.25 m from the shape's own parallel line no longer takes over when the cursor drifts nearer to it.

**Crossings.** Each pair of remaining captured lines that meet at `angleIncrementDegrees` or more (mod 180°) gives its intersection, kept when it lies within `guideCrossingRadiusMeters` of the cursor and not within 1 mm of any node. The node rule drops every crossing of two lines through a shared point, which would otherwise make each node a magnet out to the crossing radius: with the shape at 30°, a cursor 0.5 m from `points[1]` at 112° is 0.07 m from the 120° line and 0.19 m from the 90° world line through `points[1]`, and their crossing is `points[1]` itself. With the rule it lands on the 120° line. The angle minimum drops near-parallel crossings, which hold the point through a dead zone (a 30° guide and a 31.5° nearby line are both captured over about 23 m of their length).

**Ranking.** One lexicographic key, smaller wins:

1. The best source among the candidate's lines (`DrawnShape` < `NearbyStructure` < `WorldAxes`)
2. Crossing before single line
3. For a crossing, the other line's source
4. The best line's node: shape point before committed corner
5. Distance from the cursor to the snapped point
6. Generation order: list item, then node, then a pair's direction before its perpendicular; crossings by first line, then second

So a `DrawnShape` single line beats a `NearbyStructure` crossing, and the crossing of the shape's own two lines (the rectangle's closing corner) beats everything else in the guide step.

**Squaring on existing edges.** In `snap()`'s edge tier and in `snapWall()`'s wall-segment and foundation-edge tiers, the hit is found as today, then: if a captured `DrawnShape` line meets the hit's line (the foundation edge; in the wall tool the inset edge line; the wall segment's centerline) at `angleIncrementDegrees` or more, at a point on the edge within `edgeSnapRadiusMeters` of the cursor, that point is returned instead of the foot, nearest to the cursor first, generation order on ties. For a wall segment the point must be strictly interior, so it stays a T-junction. Kind and hit ids are unchanged (`Edge`; `WallSegment` with `hitSegment`), and `guides[0]` carries the line with its marker. Freeform skips it. Case: `points` (0, 0), (5.196, 3.000), a foundation edge along y = 6, cursor (3.580, 5.8) on the 120° guide 0.2 m short of the edge. Today's foot is (3.580, 6.000), leaving an 88.3° corner at `points[1]`; the squared point is (3.464, 6.000), 0.23 m from the cursor, on the edge, 90°. The vertex tiers stay ahead and unchanged; a corner beating a guide is intended.

**Landing on the first point.** With three or more points, a guide result within 1 mm of `points[0]` is returned as `Origin` at `points[0]`, so the click closes. Crossings never land there (node rule), but a single line's foot can: a previous-edge line through the previous point that runs through `points[0]`, caught by angle from 10 m away with the cursor 1 m off it. Without this the point would be red (zero-length closing edge) and only `DrawingSystem::handleMouseMove`'s rescue-close (about line 349) would save the click.

**Right-angle marker.** For each returned guide, the edges at its node are tested in order: for a shape point, the shape's edge into it, then out of it; for a committed foundation corner, its two ring edges; for a wall vertex, its segments in ascending id. The first within `guideDirectionToleranceDegrees` of perpendicular to the guide sets `rightAngle` and `rightAngleEdgeDirection` (unit, from the node along that edge). The marker sits at the guide's `from`.

**Where it sits.** The guide step takes `axisAlign`'s slot in both entry points, after the geometry tiers and before `snapAngle`. Real geometry stays ahead (zero-gap adjacency is a hard rule). `freeform` skips the guide step, the edge squaring, and the angle step. `axisAlign` is deleted in the same change (one path); its behaviour is line list item 4 with the same 0.3 m tolerance, except that the wall tool's foundation corners move to their inset points.

#### API

Public `snap()` and `snapWall()` signatures are unchanged.

```cpp
enum class SnapKind {
    None, Angle, Vertex, Edge, Origin, WallEndpoint, WallSegment,
    GuideLine,      // locked onto one guide line
    GuideCrossing,  // snapped to the crossing of two guide lines; point is the crossing
};                  // AxisGuide is removed

enum class GuideSource { DrawnShape, NearbyStructure, WorldAxes };

struct SnapGuide {
    Vec2        from;                     // the guide's node; the marker sits here
    Vec2        to;                       // the snapped point
    GuideSource source = GuideSource::DrawnShape;
    bool        rightAngle = false;       // draw the right-angle marker
    Vec2        rightAngleEdgeDirection;  // unit, from `from` along the perpendicular edge
};

struct SnapResult {
    // point, kind, hitSegment, hitVertex, closesShape() as today
    std::array<SnapGuide, 2> guides{};
    std::size_t              guideCount = 0;  // 2 for GuideCrossing, 1 for GuideLine, Angle, and a squared Edge or WallSegment, else 0
};
```

`guides` replaces `guideFrom`, `guideTo`, `guideFromAlt`, and `guideToAlt`. `Angle` fills one guide, the ray from the previous point, source `DrawnShape` (`WorldAxes` for the first segment), no marker. Private: `axisAlign` is replaced by `alignToGuides(points, cursor, alignToExisting, wallHalfThicknessMm)`; the line, capture, crossing, and marker helpers stay in the .cpp's anonymous namespace.

#### Config

`assets/config/construction/snapping.xml`, `assets::SnappingConfig`, loaded in `ConstructionRegistry.cpp`. `ConfigValidator.cpp` (about lines 374 to 406) checks only `vertexSnapRadiusMeters`, `edgeSnapRadiusMeters`, and `originCloseRadiusMeters` > 0 today; every check in the table is new.

| Key | Default | Change | New validator check |
|-----|---------|--------|---------------------|
| `angleIncrementDegrees` | 15 | also the minimum crossing angle, and half of it the angle window | > 0 and <= 90 |
| `guideRangeMeters` | 8.0 | renamed from `smartGuideRangeMeters`; bounds committed corners and nearby edges | > 0 |
| `guideToleranceMeters` | 0.3 | renamed from `axisGuideToleranceMeters` | > 0 |
| `guideCrossingRadiusMeters` | 1.0 | new | >= `guideToleranceMeters` |
| `guideDirectionToleranceDegrees` | 1.0 | new; nearby de-duplication, merge, marker | > 0 and < `angleIncrementDegrees / 2` |
| `nearbyGuideDirectionLimit` | 4 | new, integer | >= 0 (0 turns nearby directions off) |
| `guideCornerLimit` | 16 | new, integer | >= 0 (0 turns committed corners off) |
| `alignToExistingDefault` | true | none; now also gates nearby directions and wall corners | none |

The renames make every guide key read `guide...`; the old names described the world-axis-only feature. `smartGuideRangeMm` and `axisGuideToleranceMm` are computed and never read; they are deleted, not renamed. The user setting `UserSettings::alignSnapToExistingFoundations` becomes `alignSnapToExistingStructures`, label "Align to existing buildings".

`assets/config/construction/rendering.xml` `<Preview>` gains `rightAngleMarkerPx` (8.0, the marker's leg length) and `guideCrossingDotPx` (3.0, the dot's radius); guides, markers, and dots all use `guideColor` and `guideWidth`. The `guideColor` comment changes from "angle-snap guide line" to "guide lines, right-angle markers, crossing dots".

#### Units, determinism, cost

Positions are float world metres, like every other snap. Float spacing is under 0.5 mm below 4096 m from the world origin and doubles with each power of two beyond it; within that range a guided corner is exact to float rounding (about 0.25 mm), and the millimetre quantization on commit (D1) adds up to 0.5 mm per axis, so a guided right angle over a 3 m edge is square to a few hundredths of a degree. Farther out the error grows with the spacing. Tests compare positions to 1e-3 m. Snapping runs on the local cursor only and the command carries the quantized point, so float differences between machines never reach sim state. On one machine the result is a pure function of the inputs: every walk is over `foundations()`, `segments()`, `vertices()` (ascending id) or `points`, never an unordered container, and every tie falls to the generation order.

The engine is stateless: `DrawingSystem` builds a `SnapEngine` on every move (`handleMouseMove`, `handleWallMove`), and `snap()` and `snapWall()` are const. Per cursor move the line list holds at most 298 lines: 66 from the shape (32 points by one pair, plus the previous pair), 136 nearby (4 directions by the previous point and up to 16 corners, by a pair), and 96 world (48 nodes by a pair). Each is one point-to-line distance. After the merge at most 48 lines stay captured (14 directions, at most three parallel lines per direction inside the band twice the tolerance wide, plus the 6 shape lines through `points[0]` and `points.back()` that catch by angle), and crossings are formed among those; in practice two or three. Finding the corners and nearby edges is a walk over the committed foundations and walls, as `axisAlign` and `snapToEdge` do today. `Perf: none` is the expectation; nothing on the sim or render hot path changes.

#### Rendering

One helper in `DrawingSystem`, `drawSnapGuides(const SnapResult&, ...)`, used by both the foundation preview and the wall preview, replaces the two near-identical guide blocks (`DrawingSystem.cpp` about 1711 to 1737 and 2224 to 2250) and their alt-pair branch. It draws `guideCount` guides whatever the kind (a squared `Edge` or `WallSegment` carries one), each from `from` to `to`, skipping a zero-length one as today. The right-angle marker is drawn in screen space: `from`, `from` + the edge direction, and `from` + the unit direction toward `to` go through `toScreen`, the two screen directions are normalised, and with `s` = `rightAngleMarkerPx` the mark is two short lines from corner + s·e to corner + s·e + s·g to corner + s·g. A `GuideCrossing` adds a filled dot of `guideCrossingDotPx` at `point`. Keys stay in the existing preview key ranges.

#### Tests

`libs/sim/construction/SnapEngine.test.cpp`. The worked rectangle is p0 (0, 0), p1 (5.196, 3.000), p2 (3.196, 6.464), p3 (-2.000, 3.464).

Existing tests whose expectations change:

- `AngleSnapRelativeToPreviousSegment` (line 57) and `WallAngleSnapOffPreviousChainPoint` (line 413): a 90° turn is now a shape line caught by angle, so `Angle` becomes `GuideLine`; the point is unchanged. Rename them `GuideRightAngleOffPreviousSegment` and `WallGuideRightAngleOffPreviousChainPoint`.
- The seven axis tests (lines 165 to 258), ported with the same inputs: `AxisAlignsXToInProgressNode` and `AxisAlignsYToInProgressNode` become `GuideWorldAxisLocksX` and `GuideWorldAxisLocksY` (`GuideLine`); `AxisAlignsBothAxesToTwoNodes` becomes `GuideShapeLinesCrossAtSquareCorner` (`GuideCrossing` of two `DrawnShape` lines at (0, 6), `guideCount == 2` in place of the alt-pair checks); `AxisAlignToleranceBoundary` becomes `GuideToleranceBoundary` (0.35 m falls through to `Angle`); `AxisAlignFreeformSuppresses` becomes `GuideFreeformSuppresses`; `AxisAlignTogglesCommittedFoundationTargets` becomes `GuideCommittedCornersFollowAlignToExisting`; `AxisAlignCommittedRespectsRange` becomes `GuideRangeExcludesFarCorners`.
- `libs/assets/assets/ConstructionRegistry.test.cpp`: `Snapping_Parsed` reads `guideRangeMeters`; the XML in `Validation_RejectsNegativeSnapRadius` uses the new key name.

Checked and unchanged: `NoOriginCloseBeforeThreePoints` and `OriginCloseRadiusDefaultIgnoresOverride` now land on a `GuideLine` instead of `Angle`, still not `Origin`; `FoundationSnapUnaffectedByWalls` (the foundation tool still reads no walls) and the other wall tests (no points placed, or freeform) keep their kinds.

New, foundation tool:

- `GuidePerpendicularOffFirstEdgeAt30Degrees`: p0, p1; cursor 4 m out at 118°; `GuideLine` on the 120° line through p1, marker at p1 along the edge toward p0.
- `GuidePerpendicularCapturedByAngleFarOut`: cursor 10 m out at 115° (0.87 m off the line); still `GuideLine` with marker.
- `GuideClosesRotatedRectangle`: p0, p1, p2; cursor (-1.6, 3.8), 0.51 m off the line through p0; `GuideCrossing` at p3 within 1e-3; two guides, markers at p2 and p0.
- `GuideCrossingNeverOnANode`: p0, p1; cursor 0.5 m from p1 at 112°; `GuideLine` on the 120° line, not p1.
- `GuideCrossingOutsideRadiusIgnored`: two captured lines crossing 1.5 m from the cursor; single `GuideLine`.
- `GuideNearParallelLinesNeverCross`: nearby edge at 31.5° beside a 30° shape; cursor 10 m out near both; `GuideLine` on the shape's line.
- `GuideParallelMergeKeepsShapePoint`: a shape point's world line and a committed corner's 0.25 m away; cursor nearer the corner's; result on the shape point's line.
- `GuideParallelToNearbyRotatedFoundation`: a committed rectangle at 37° in range; p0 placed, cursor 5 m out at 39°; `GuideLine` on the 37° line through p0, `NearbyStructure` (the bare angle step would give 45°).
- `GuideDrawnShapeBeatsCloserWorldAxis`: the reported bug; a 30° shape, cursor 0.05 m from a world line and 0.25 m from the shape's perpendicular; the point lies on the perpendicular and `guides[0].source == DrawnShape`.
- `GuideNearbyStructureBeatsWorldAxes`.
- `GuideNearbyDirectionLimit`: six foundations at distinct angles in range; only the four nearest give lines.
- `GuideCornerLimit`: twenty corners in range; only the sixteen nearest give lines.
- `GuideAlignToExistingOffDropsBuildingGuides`.
- `GuideSquaresPointOnExistingEdge`: the squaring case above; `Edge` at (3.464, 6.000), `guides[0]` the 120° line with its marker. (Replaces the draft's `GuideLosesToExistingEdge`.)
- `GuideOnFirstPointReportsOrigin`: a shape whose previous-edge line through `points.back()` runs through `points[0]`; cursor 1 m off that line on its perpendicular at `points[0]`; `Origin`.
- `GuideNoMarkerOnParallel`: a parallel guide reports `rightAngle == false`.
- `GuideDeterministicTieBreak`: no points placed; two committed foundations with corners 0.4 m apart in y; the cursor midway between their world lines; the line through the earlier foundation's corner wins.

New, wall tool:

- `WallGuideClosesRotatedRectangleNotInset`: the chain's fourth point is the crossing, not offset by half-thickness.
- `WallGuideParallelToHostFoundationEdge`: a rotated host; a mid-floor chain locks parallel to its edge.
- `WallGuidePerpendicularToNearbyWallSegment`.
- `WallGuideUsesInsetCorners`: a perimeter wall vertex at a foundation's inset corner and that raw corner give one line, not two 15 cm apart.
- `WallGuideSquaresTJunction`: a chain guide crossing an existing wall near the cursor; `WallSegment` at the crossing with `hitSegment` set.
- `WallGuideSquaresInsetEdge`.
- `WallEndpointBeatsGuideCrossing`.
- `WallGuideFreeformSuppresses`.

`ConstructionRegistry.test.cpp` also: renamed keys load; new keys load with defaults; the validator rejects each bound in the table. Check for an existing case on the same path before adding one.

Manual regression: add steps to `docs/testing/scenarios/03-build-wood-foundation.md` (draw the 30° rectangle, see the marker at corner 3 and the dot at corner 4, close) and `04-build-walls.md` (a rotated chain squares its corners on guides; Alt drops them).

#### Build steps

One agent, one branch; each step is a commit that builds and passes the tests on its own.

1. **Snapping config.** `assets/config/construction/snapping.xml` (rename two keys, add five), `libs/assets/assets/ConstructionRegistry.h` (`SnappingConfig` fields; delete the two unused Mm copies), `ConstructionRegistry.cpp` (load), `ConfigValidator.cpp` (the new checks), `ConstructionRegistry.test.cpp` (the two changed tests, the new cases). `SnapEngine.cpp`'s `axisAlign` and the comments in `SnapEngine.h` and `SnapEngine.test.cpp` move to the renamed fields in the same commit. Behaviour unchanged.
2. **Setting.** `apps/world-sim/UserSettings.h`, `apps/world-sim/scenes/settings/SettingsScene.cpp`, and the two `DrawingSystem.cpp` call sites (about lines 330 and 822): rename to `alignSnapToExistingStructures`, label "Align to existing buildings".
3. **Engine and its drawing.** One commit, since `SnapResult` changes under `DrawingSystem`. `libs/sim/construction/SnapEngine.h` (`SnapKind`, `GuideSource`, `SnapGuide`, `SnapResult.guides`, `alignToGuides`, and the header comment's priority lists, whose foundation list omits the axis guide today); `SnapEngine.cpp` (lines, capture, merge, crossings, ranking, edge squaring, the first-point rule, the marker; `snapAngle` fills `guides[0]`; `axisAlign` deleted); `SnapEngine.test.cpp` (the changed and new tests above); `rendering.xml` `<Preview>` keys with their `ConstructionRegistry.h` fields and loader; `apps/world-sim/scenes/game/world/construction/DrawingSystem.cpp` and `.h` (`drawSnapGuides`, both old blocks deleted).
4. **Scenarios.** The two scenario files above; run 03 and 04 as the regression pass.

No perf capture is needed; the PR's line is `Perf: none`.

#### Open points

- The crossing radius (1.0 m) and corner limit (16) are guesses; the radius has to stay above the 0.52 m the closing corner needs in the worked example. Tune in play.
- Whether the tolerance should be zoom-stable (pixels, like the origin-close radius the tool already widens with zoom) is left as metres, matching today's axis guides.

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
| **Cities: Skylines II** road tools | The interaction grammar checklist for freeform drawing: angle snap, geometry snap, projected guidelines, length/angle readouts, validity coloring. Matches the design spec's drawing UX. Its several separately toggled snap options ([road tools](https://paradoxinteractive.com/games/cities-skylines-ii/features/road-tools)) are the counter-example for D12, which ranks sources instead of exposing a toggle per source. ([dev diary](https://forum.paradoxplaza.com/forum/developer-diary/development-diary-1-road-tools.1590300/)) |
| **SketchUp** inference engine | Point inferences (endpoint, midpoint, intersection) and linear inferences (axis, "from point" along an axis, parallel/perpendicular to an edge, shown in magenta); Alt cycles all on, all off, and parallel/perpendicular only; Shift or an arrow key locks one. Our D12 frames are its linear inferences, the crossing is its intersection point, and Alt maps to "all off". The lock key is left out. ([drawing basics](https://help.sketchup.com/en/sketchup/introducing-drawing-basics-and-concepts)) |
| **AutoCAD** polar and object snap tracking | Polar tracking shows temporary alignment paths at set angle increments; object snap tracking acquires up to seven points and shows paths through them; the pick goes where two paths cross. The bounded acquired-point set and path-crossing pick are the shape of D12's nodes and crossings. ([polar tracking](https://help.autodesk.com/cloudhelp/2026/ENU/AutoCAD-LT/files/GUID-7EC3C63D-EA4E-4E65-A676-C3A3627E3F19.htm), [object snap tracking](https://help.autodesk.com/cloudhelp/2026/ENU/AutoCAD-LT/files/GUID-665DC37F-8C3E-414A-9369-72A13C0BE07A.htm)) |
| **Figma** snapping | A red line shows alignment to other objects' centres and edges while moving; holding a modifier turns snapping off for the drag. The one-colour thin line and hold-to-disable match D12's look and Alt. ([alignment and position](https://help.figma.com/hc/en-us/articles/360039956914-Adjust-alignment-rotation-position-and-dimensions)) |
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
