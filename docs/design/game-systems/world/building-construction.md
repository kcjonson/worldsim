# Building & Construction

**Status:** Design
**Created:** 2026-06-12
**MVP Status:** See [MVP Scope](../../mvp-scope.md) — This feature: Phase 3 (Construction basics)

---

## Design Pillars

1. **Freeform, not grid-bound.** Buildings are drawn polygons on the map with no relation to terrain tiles. Straight lines only in v1; curves may come later.
2. **Real-world construction order.** Foundation first, then walls, then openings. Each step is a blueprint that colonists supply with materials and then build with work.
3. **Materials matter.** Every structure is made *of* something, and the material drives appearance, insulation, flammability, beauty, hit points, movement speed (floors), cost, and work. All material data is config-driven.
4. **Work builds things, not materials.** Materials are a prerequisite. Construction progress comes from colonist work at the site, and the visuals literally render work invested.
5. **Procedural vector visuals.** Foundations and walls are procedurally drawn from material definitions. Doors and windows are parameterized vector assets that adapt to wall thickness and material.
6. **Pathfinding-sound geometry.** The shape constraints exist for pathfinding. World entities must never get stuck in odd little pockets (the classic bait-an-enemy-into-a-gap exploit). Every gap the construction tools can produce is either fully sealed or wide enough to path through. Nothing in between.

## Units

All user-facing and config distances are real-world units: meters and square meters. Tiles are a rendering concept for terrain backgrounds, not a gameplay unit. No config value, readout, or constraint is expressed in tiles.

## Concepts

| Term | Meaning |
|------|---------|
| **Foundation** | A drawn polygon that becomes the buildable surface for a building. Required before any walls. |
| **Wall** | A drawn line (chain of segments) on a foundation. Has material and thickness. |
| **Opening** | A door or window placed on a wall. |
| **Room** | An enclosed region bounded entirely by built walls. Detected automatically, inspected via the rooms overlay. |
| **Blueprint** | Any structure that has been drawn but not yet built. Shows as a ghost, generates hauling and build tasks. |
| **Segment** | The span of wall between two junctions or endpoints. The unit of wall selection, demolition, and retrofit. |
| **Pathing clearance** | The config constant defining the minimum gap a world entity needs to pass through. Every distance constraint derives from it. |

---

## Foundations

### Purpose

A foundation is required to build a building, as in the real world. Terrain is flat (no height shown during gameplay, like RimWorld), so foundations are not about leveling; they are the structural prerequisite and the floor. The foundation material is the visible floor surface and contributes look, insulation, flammability, and beauty to the building and its rooms.

Floor surfaces also affect movement speed: a built foundation is faster to walk on than rough ground. Eventually every surface in the game carries a speed modifier; foundations adopt this first.

Optional flooring overlays (a different surface laid on top of the foundation) are a future feature. Until then, foundation material is what you walk on.

### Materials

V1: **wood** and **stone**. A foundation is one material throughout; no mixed-material foundations. Each material defines, in config:

- Material cost per m²
- Work per m²
- Hit points per m² (structure HP)
- Insulation value
- Flammability
- Beauty contribution
- Movement speed modifier
- Visual pattern (procedural fill: plank direction and joints for wood, slab/course layout for stone)

### Drawing

Select Foundation in the Build menu. A config strip appears with material choices and a live cost readout. Material is picked **before** drawing; the ghost renders in that material's blueprint style.

The flow is polygon drawing as in design software:

1. First click places the origin point at the cursor.
2. A line rubber-bands from the last point to the cursor. The line colorizes for validity: blueprint-blue when the placement is valid, red when not, with a short reason near the cursor ("corner too tight", "too close to existing wall").
3. Each click commits a point.
4. Once three points exist, a faint fill previews the implied closed polygon.
5. The user must click the origin point to close the shape. The origin shows a visual hint when closable: it grows a halo and the cursor snaps to it within a generous radius.
6. Closing the shape creates the foundation blueprint.

While drawing:

- **Backspace** removes the last committed point.
- **Esc** (or right-click) cancels the in-progress shape. Pressing Esc again, with nothing in progress, exits the tool.
- A readout near the cursor shows current segment length in meters; the config strip shows running area (m²), material cost, and work estimate.

### Angle Snapping

Pure freeform produces accidental 87° corners that read as mistakes, so snapping is on by default and escapable:

- Segments soft-snap to **90° / 45° / 15°** increments relative to the previous segment (and to world axes for the first segment).
- Smart guides: the cursor also snaps when the segment would be parallel or perpendicular to a nearby existing edge (foundation edges, walls), with a faint guide line shown, like design-software smart guides.
- Hold **Alt** for true freeform (no angle snapping). Distance/validity constraints still apply.

### Shape Constraints

**Why these exist:** pathfinding. Without them, freeform drawing produces slivers, knife-edge corners, and near-touching edges, exactly the pockets that trap wandering entities and enable bait exploits. The constraints guarantee that everything the tools produce is either solid and closed, or open by at least the pathing clearance.

All values live in the external construction config (see Config Surface), so tuning never touches code. The pathing clearance constant (proposed **0.7 m**, a colonist body plus working room) is the root from which the distance constraints derive. Clearance assumes colonists; larger animals exist in the future but are not sized for here. Violations colorize the preview red and block the click. Defaults below are placeholders pending playtesting.

| Constraint | Rule | Proposed default |
|------------|------|------------------|
| Minimum corner angle | No knife-edge corners | 30° |
| Minimum point spacing | Adjacent vertices can't be too close | 0.5 m |
| Minimum segment clearance | Non-adjacent edges of the same polygon can't pass closer than pathing clearance (no slivers) | 1.0 m |
| Self-intersection | Forbidden | — |
| Minimum points | A volume needs three | 3 |
| Maximum points | Soft cap to keep shapes sane | 32 |
| Minimum area | No micro-foundations | 4 m² |
| Clearance to other foundations | Either snapped exactly edge-to-edge (zero gap) or at least pathing clearance apart. Never a sliver between buildings | 1.0 m |
| Holes | Donut foundations not supported in v1 | — |
| Terrain | Buildable ground only (no water). Trees, rocks, and loose items don't block placement; they generate clear tasks | — |

Concave shapes are fine. L-shapes, U-shapes, long halls: all valid as long as the constraints hold.

### Obstacles: Auto-Clear

Drawing over trees, rocks, or loose items is allowed. The blueprint spawns prerequisite tasks: chop, mine, and haul-away, using the existing task system. Foundation hauling and building begin only after the footprint is clear. The blueprint shows badges for outstanding clear work.

### Lifecycle

```
Drawn → Blueprint (editable) → Materials delivered → Under construction → Built
```

1. **Blueprint.** Selectable, has an info panel (material, area, cost, work estimate, clear-task status). Fully editable until the first material is delivered: drag vertices, add a vertex on an edge, delete vertices, all constraint-checked. Cancel at any time; any delivered materials drop at the site.
2. **Materials delivered.** The blueprint advertises material needs; colonists haul using standard haul chains. Delivered materials appear visually staged at the site (procedural piles along the perimeter). All materials must be delivered before build work starts.
3. **Under construction.** Builders (Construction work type) work the site. Total work is computed from area × material work factor. Building skill affects speed only in v1 (no quality tiers). Multiple colonists can build simultaneously, spaced along the footprint (max concurrent builders scales with perimeter, config).
4. **Built.** The foundation is a walkable floor surface and a valid canvas for walls.

### Editing After Build: Add / Subtract

Foundations are reshaped with a zone-style add/subtract tool, like a RimWorld storage zone:

- **Add:** draw a polygon overlapping or sharing an edge with an existing foundation (cursor snaps to its edges and vertices). The new region becomes a blueprint attached to the foundation; when built, the two merge into one foundation. The merged outline must satisfy all shape constraints. **Add requires the same material.**
- **Different material?** Draw a separate foundation snapped exactly edge-to-edge (the zero-gap case the clearance rule permits). It remains a distinct foundation: its own entity, own info panel, own demolition. A stone wing on a wood house is two snapped foundations, not one mixed one. Each foundation renders its own floor pattern up to the shared edge; the hairline material seam is the honest visual. A wall along the shared edge belongs to one side (see Host Foundation under Walls).
- **Subtract:** blueprint-only. Carve a region off an unbuilt foundation blueprint. The subtraction must cross the boundary (carving an interior hole is invalid) and the remainder must still satisfy constraints.
- **Built foundations never shrink.** Removal is whole-foundation demolition only.

When an Add extends past an exterior wall, that wall simply becomes an interior wall. Nothing moves.

### Demolition

Foundations demolish only as a whole, and only when no walls or openings stand on them. Two paths, both from the foundation's info panel:

- **Demolish foundation:** enabled only when the foundation is already clear of structures.
- **Demolish building:** the cascade. Queues demolition of all openings, then all walls, then the foundation itself, as ordered work.

Demolition is work, performed by builders, and refunds a configurable percentage of materials (proposed 50%).

---

## Walls

### Prerequisites

Walls require a foundation. They can be **drawn** on a foundation blueprint or a built foundation, but colonists only **build** walls once the foundation beneath them is complete. Wall blueprints on an unbuilt foundation wait, and their hauling tasks activate when the foundation finishes.

Freestanding walls, fences, and defensive perimeters on bare ground are a future, separate tool.

### Materials and Thickness

Material is picked before drawing, same config-strip pattern as foundations. Each material offers discrete **thickness presets** (e.g., light / standard / heavy), and each material+thickness combination defines:

- Material cost per meter
- Work per meter
- Hit points
- Insulation value
- Flammability
- Beauty contribution
- Visual pattern for the wall top

### Alignment

A wall is drawn as a mathematical line; its thickness has to sit somewhere relative to that line. Design tools offer inner/outer/center alignment. We don't expose the choice:

- **Walls center on their line** everywhere in the interior.
- **Exception: walls snapped to a foundation edge align outer-face-flush** with the edge, so the wall's full thickness sits on the foundation and the foundation never lips outside the building.

### Host Foundation

Every wall has exactly one **host foundation**: the foundation containing its full thickness footprint. The host gates the wall's build order, counts it in the foundation's panel summary, blocks foundation demolition while the wall stands, and includes it in the Demolish building cascade.

On a shared edge between two snapped foundations there is no co-ownership. The wall snaps flush to the side the cursor approaches from and hosts there. Room detection and pathing operate on geometry alone and don't care about ownership, so the neighboring building still gets enclosure from a wall it doesn't own. (Co-owned walls would make build gating and demolition cascades ambiguous for no player-visible benefit.)

### Drawing

Walls draw as a polyline chain: click to commit each point, the chain continues from the last point. The chain does not need to close. **Right-click or Esc** ends the chain; double-click commits a final point and ends.

The same angle snapping and smart guides apply as for foundations. Additional snap targets, in priority order:

1. Wall endpoints (continue or join a wall)
2. Foundation vertices
3. Points along existing walls (T-junction anywhere along a wall's length)
4. Foundation edges (slide along the edge)

Snapping is generous but escapable (Alt). Walls may be drawn anywhere on the foundation, not just at edges: a wall in the middle of a foundation touching nothing is valid, subject to clearance constraints.

### Edge Fill

**Ctrl+click on a foundation edge** instantly places a wall blueprint along that entire edge, no dragging. If the edge is partially walled already, the fill covers the remaining gaps. Right-click is reserved for ending/canceling the chain, which is why Ctrl+click gets the job.

### Wall Constraints

Same pathfinding rationale as foundations: a gap between walls is either zero (joined) or at least pathing clearance. No pockets.

| Constraint | Rule | Proposed default |
|------------|------|------------------|
| Minimum segment length | No stub walls | 0.5 m |
| Minimum junction angle | Walls meeting at a point can't form knife-edges | 30° |
| Minimum parallel clearance | Wall faces (thickness included) can't pass closer than pathing clearance unless they join | 0.8 m face-to-face |
| Overlap | Walls can't overlap other walls; they may only meet at junction points | — |
| Containment | The wall's full thickness footprint must lie within the foundation | — |
| Crossing | Drawing across an existing wall snaps to a junction on it rather than crossing (no X-crossings in v1; make two T-junctions) | — |

### Lifecycle

Same as foundations: blueprint → haul → build → built. Work is length × thickness × material work factor. Wall blueprints do not block colonist pathing; built walls do. A wall segment will not complete construction while a colonist stands in its footprint (builders stand adjacent; pawns in the way cause the builder to wait).

### Segments and Demolition

A drawn chain is split into **segments** at junctions. Segments are the unit of selection, demolition, and retrofit. Demolishing a segment is work and refunds a percentage of its materials (same config as foundations). Demolition can split a room or unmake it; room detection updates automatically.

---

## Rooms

When built walls (with the foundation boundary playing no part: enclosure must be walls) form a closed polygon, the enclosed region becomes a **room**. Doors and windows count as part of the wall for enclosure: a room with a doorway is still a room.

Only **built** walls count. The room appears at the moment the closing wall segment completes, with a small notification ("Room formed") and a brief highlight pulse on the new room.

Rooms are an abstract concept, not clickable world objects. They are inspected through a **rooms overlay**, like RimWorld or Going Medieval. The overlay is designed here but ships with the game's broader overlay system (zones, beauty, etc.), not with this epic; this epic ships detection and the room-formed notification only.

- Toggled from the HUD (and a hotkey). While active, room interiors tint and show name labels ("Room 1", renamable).
- Clicking a room while the overlay is active opens its **room info panel**: area (m²), bounding wall materials, opening count, average insulation, floor material, name.
- With the overlay off, rooms have no click presence; clicks fall through to whatever structure or entity is under the cursor.

V1 rooms don't do anything beyond this. Future: room types from contents, ownership, adjacency bonuses, material effects. See [Rooms](./rooms.md).

---

## Doors & Windows

### Placement

Doors and windows place onto walls, blueprint or built. The cursor slides along the wall, showing a ghost of the opening at the snapped position, colorized for validity.

- On a **wall blueprint**: the opening is reserved in the wall; the wall builds around it (framing the gap), then the door/window builds as its own blueprint with its own materials and work.
- On a **built wall** (retrofit): placing the opening creates a **cut opening** task. A builder cuts away that wall section (work, no material refund for the cut section), then the opening builds normally. Buildings stay renovatable forever.

### Sizes

Fixed widths per type. V1 types:

| Type | Width (proposed) | Notes |
|------|------------------|-------|
| Door | 0.9 m | Pathable by colonists |
| Window | 0.6 m | Not pathable |

Double doors and other widths can be added later as new types, not as stretchable assets.

### Constraints

| Constraint | Rule | Proposed default |
|------------|------|------------------|
| Distance from wall end / corner / junction | Openings keep clear of structural points | 0.3 m |
| Distance from other openings | Openings can't touch | 0.3 m |
| Wall length | The segment must be long enough to honor the margins | derived |

### Materials and Properties

Doors and windows have their own material choice (wood, stone... glass later for windows) and properties: hit points, insulation, flammability, beauty. A door in a wall affects the wall's effective insulation; a cheap door in a thick stone wall is the thermal hole you'd expect.

Doors are pathable in v1 (no open/close simulation yet; open/close states, speed, and locking are future). Windows block movement.

### Visuals

Doors and windows are vector assets in the asset management system, but **parameterized**: one asset per type adapts to the wall it sits in. The asset stretches along the wall-thickness axis and recolors/retextures from the material definition. There are explicitly not separate assets per thickness and material combination. (The asset system needs parameter slots for this: a thickness dimension and a material palette. Design requirement here; mechanics in the technical docs.)

---

## Construction Loop (Shared)

Every structure follows the same loop:

```
Blueprint placed
  → prerequisite clear tasks (foundations only)
  → haul tasks: deliver full material list to the site
  → build tasks: colonists with Construction work type perform work
  → built
```

- **Deliver all, then build.** Build work cannot start until the structure's full material requirement is on site. Staged materials are visible at the site.
- **Work score.** Each structure's total work is computed from its size (area for foundations, length × thickness for walls, constants for openings) times the material's work factor. All config.
- **Skill: speed only (v1).** Building skill scales work rate. Outcome is identical regardless of who builds. Quality tiers may layer in later.
- **Multiple builders.** Concurrent builders allowed, capped by structure size (config), positioned around the work.
- **Cancellation.** Cancel a blueprint at any stage before completion: delivered materials drop at the site, partial build work is lost.
- **Task integration.** Hauling uses the standard haul chains ([Task Chains](../colonists/task-chains.md)); build tasks flow through the global task registry and work priorities like any other work.

---

## Visuals

### Top-Down Representation

This is a top-down game. For the first prototype, walls render as **wall tops** only (the plan-view slab of the wall, in material pattern, at true thickness). A short south-face hint, RimWorld-style, may be added later; the design should not preclude it.

### Procedural Construction

Foundations and walls are procedurally drawn from material definitions, not hand-authored sprites. This is where the vector pipeline gets to show off:

- **Blueprint state:** desaturated blue dashed outline, faint fill, material icon badges showing delivered vs. required counts.
- **Staged materials:** delivered materials render as procedural piles at the site.
- **Build progress renders work invested.** The construction visuals advance with build work, deterministically (seeded per structure, so progress never reshuffles between frames or saves). Material-specific progressions, for example:
  - *Wood foundation:* perimeter sill beams appear → joists span the interior → deck planks lay in rows, sweeping across the footprint as work completes.
  - *Stone foundation:* corner stones set → perimeter courses build up → slab infill spirals inward.
  - *Wood wall:* studs appear along the segment → planking fills between them.
  - *Stone wall:* stones place one by one in courses along the length.
- **Built state:** the completed procedural pattern (plank runs with seeded joint offsets, stone courses with varied block sizes), continuous across junctions and mitered at corners so chains read as one wall, not overlapping pieces.

### Drawing Feedback

- Rubber-band segment, validity colorization, reason text on invalid.
- Faint polygon fill after the third point.
- Origin-point halo + snap when the shape can close.
- Smart guides (parallel/perpendicular) as thin hint lines.
- Length readout (meters) at cursor; area/cost/work in the config strip.

---

## UI & Selection

### Build Menu

The gameplay bar's [Build▾] menu gets a **Structure** category: Foundation, Wall, Door, Window. Selecting one activates its tool and opens the config strip.

### Config Strip

A horizontal strip docked above the gameplay bar while a structure tool is active:

- **Material cards:** icon, name, per-unit cost (per m² or per m). One selected at a time; selection persists per tool.
- **Thickness presets** (Wall tool): the selected material's presets with their stat summary (HP, insulation, cost) on hover.
- **Type picker** (Door/Window tool): the opening types.
- **Live readouts:** running area or length, total material cost, work estimate. Updates as the cursor moves.
- **Validity message area:** when the cursor position is invalid, the reason shows here as well as at the cursor.

Changing material or thickness mid-session applies to the next shape, not shapes already drawn.

### What Is Selectable

Foundations, wall segments, and openings are first-class selectable entities, in both blueprint and built states, through the existing selection system. Rooms are not (overlay only, above).

**Click priority** when overlapping things sit under the cursor, most specific first:

1. Colonists and creatures
2. Loose items
3. Openings (doors/windows)
4. Wall segments
5. Furniture and placed objects
6. Foundation
7. Terrain

Clicking again in the same spot cycles down the stack (click a door, click again to get the wall, again for the foundation). Standard colony-game behavior, and it resolves the "everything is stacked on a foundation" problem without modifier gymnastics.

**Multi-select:** Shift-click adds to the selection. Mixed selections reduce the action set to what's shared (e.g., Demolish). Double-clicking a wall segment selects the whole connected run of the same material and thickness, the common case for "demolish this wall, all of it."

### Selection Visuals

Selected structures get an outline highlight. Blueprints are already ghosted; selection brightens them and shows their outline dashed. The info panel opens on selection as with any other entity.

### Info Panels

Per structure and state. All panels show name/type, material, and dimensions in real units.

| Selection | Shows | Actions |
|-----------|-------|---------|
| Foundation blueprint | material, area, delivered/required materials, clear-task status, work remaining | Edit shape, Cancel |
| Foundation (built) | material, area, HP, insulation, beauty, speed modifier, walls/rooms summary | Add, Demolish building (cascade), Demolish foundation (enabled when clear) |
| Wall blueprint segment | material, thickness, length, required materials | Cancel |
| Wall segment (built) | material, thickness, length, HP, insulation, flammability | Demolish segment |
| Opening blueprint | type, material, host wall | Cancel |
| Opening (built) | type, material, HP, insulation, flammability | Demolish |

**Edit shape** (foundation blueprint) re-enters drawing-like editing: vertex handles appear, drag to move, click an edge midpoint to add a vertex, select a vertex and Backspace to delete, all constraint-checked live. Available until the first material is delivered.

Wall blueprints have no vertex editing in v1; segments are cheap, cancel and redraw.

### Keyboard / Mouse Reference

| Input | Effect |
|-------|--------|
| Click | Place point / select |
| Click origin point | Close foundation polygon |
| Double-click | End wall chain (committing final point); expand wall selection to connected run |
| Right-click / Esc | End wall chain; cancel in-progress shape; second Esc exits tool |
| Backspace | Undo last point (drawing); delete vertex (shape editing) |
| Alt (hold) | Disable angle snapping |
| Ctrl+click foundation edge | Fill edge with wall |
| Shift+click | Add to selection |

---

## Config Surface

In the spirit of everything else in the game, no magic numbers in code. Constraints, snap behavior, and construction tuning live in an external config file (`assets/config/construction/`, following the `assets/config/work/` pattern); material definitions live in their material asset XML.

| What | Examples |
|------|----------|
| Material definitions (per structure type) | cost rate, work rate, HP, insulation, flammability, beauty, movement speed modifier, visual pattern |
| Wall thickness presets | per material: thickness values (meters) and their stat multipliers |
| Pathing clearance | the root constant the distance constraints derive from |
| Shape constraints | min corner angle, min point spacing, segment clearance, min/max area, max points |
| Snap behavior | angle increments, snap radii, smart-guide range |
| Construction | refund percentage, max concurrent builders per size, work formulas' factors |
| Opening types | widths (meters), margins, materials allowed |

---

## Out of Scope (Future)

- Curved walls and foundation edges
- Freestanding walls / fences / defensive perimeters (separate tool)
- Flooring overlays distinct from foundation material
- Roofs, and with them temperature/insulation actually mattering per room
- Wall south-face rendering (2.5D hint)
- Door open/close simulation, locks, ownership
- Build quality tiers from skill
- Multi-story
- Room functions (types, ownership, bonuses): designed in [Rooms](./rooms.md), activates after v1
- Surface speed modifiers as a global system (foundations carry one from day one; terrain surfaces join later)
- Rooms overlay (designed above, ships with the overlay system)
- Larger-creature pathing clearance class (clearance assumes colonists for now)
- Structure damage and repair (everything has HP; the damage dealers, repair tasks, and damaged-state visuals arrive with threats/combat — but raiders breaking doors depends on it)

---

## Open Questions

1. **Constraint values.** All defaults in this doc are placeholders pending playtesting. They live in the external construction config, so tuning is data-only.
2. **Bulk material logistics.** Hauling a foundation's worth of material with the two-hand carry system means dozens-to-hundreds of trips. Needs a decision: stack hauling, bulk containers, hauling tools (wheelbarrow fits the tech progression), or material units sized so counts stay low. Affects material cost rates in config and how staged-material piles read visually.

---

## Related Documents

- [Rooms](./rooms.md) — room types, ownership, bonuses (future activation)
- [Crafting](./crafting.md) — material production chains
- [Entity Capabilities](./entity-capabilities.md) — capability advertising
- [Task Chains](../colonists/task-chains.md) — hauling and build task flow
- [Work Priorities](../colonists/work-priorities.md) — Construction work type
- [MVP Scope](../../mvp-scope.md) — phase placement
- [Vector Graphics](../../features/vector-graphics/README.md) — procedural vector pipeline
