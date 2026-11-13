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

### Snapping and guides

Pure freeform produces accidental 87° corners that read as mistakes, so snapping is on by default and escapable. Mechanism, config, and tests: [architecture D12](../../../technical/building-construction-architecture.md#d12-snap-guides-parallel-perpendicular-crossings). There is one polygon tool; a rotated rectangle is drawn with it, corner by corner, and the guides below make each corner square. There is no separate rectangle mode.

**Angle increments.** Segments soft-snap to **15°** steps (so 90°, 45°, 30° are all reachable) relative to the previous segment, or relative to the world east-west axis for the first segment.

**Guides.** A guide is a thin line through a reference point (a placed corner of the shape, or a corner of a nearby building or wall) along a reference direction. Each reference direction comes with its perpendicular, so a guide always means "parallel to" or "perpendicular to" something. Three sources supply directions, in this priority order:

1. **The shape being drawn.** Its first edge and its previous edge. The guides through the corners of the shape run parallel and perpendicular to those two edges.
2. **Nearby buildings.** Edges of committed foundations and walls within 8 m of the cursor, the nearest few directions only (4 by default).
3. **The world.** North-south and east-west.

When the cursor comes within 0.3 m of a guide, it locks onto the guide and the guide draws. A guide through the previous corner (the one the rubber band starts from) that is parallel or perpendicular to the shape's own edges also catches the cursor by angle: within half an angle step (7.5°) of the guide's direction, at any distance. That keeps a perpendicular turn as easy to hit 10 m out as it is next to the corner.

**Crossings.** When the cursor is near two guides with different directions, it snaps to the point where they cross and a dot marks the crossing. This is what squares the last corner of a rectangle: the guide perpendicular to the previous edge crosses the guide perpendicular to the first edge exactly where the fourth corner closes a true rectangle. A crossing more than 1 m from the cursor is ignored.

**Priority among guides.** The source ranks first: a guide from the shape being drawn beats one from a nearby building, which beats a world guide, however close the lower-ranked guide is. Within the same source, a crossing beats a single guide, and a crossing whose second guide is higher-ranked beats one whose second guide is lower. Then the closest to the cursor wins. So an incidental line-up with a world axis never pulls a rotated shape off its own perpendicular.

**Whole priority order, foundation tool** (first that applies wins):

1. Close onto the first point (three or more points placed, cursor within the origin radius)
2. A corner of an existing foundation
3. An edge of an existing foundation
4. Guides, ranked as above (crossings and single guides)
5. Angle increment off the previous segment
6. The raw cursor

Real geometry stays ahead of guides: a guide crossing that lands within snap range of an existing edge loses to the edge, since zero-gap adjacency is what the clearance rule depends on. The wall tool's order is in Walls > Drawing.

**Look.** Every guide is the same thin, faint line the axis guides use today, drawn from its reference point to the snapped cursor. A perpendicular guide adds a small right-angle marker at the corner where it meets the edge it is perpendicular to. A crossing adds a small dot at the crossing point, and both guides draw. Colours and sizes are data in the construction rendering config.

**Alt.** Holding **Alt** is true freeform: no angle increments and no guides of any source. Snaps to existing corners, edges, and walls stay on (they are adjacency, not style), and so does the close onto the first point. Distance and validity constraints still apply.

This Alt rule is a call made while writing the spec, not one the owner has ruled on; it extends today's behaviour, where Alt already drops the axis guides.

**Setting.** "Align to existing buildings" in Settings (today "Align to existing foundations") gates everything that comes from committed structures: the nearby-building directions, and guides through the corners of existing foundations and walls. With it off, the shape's own guides and the world guides still run through the shape's own corners.

#### Worked example: a rectangle at 30°

A 6 m by 4 m rectangle whose long side runs at 30° from east, starting at the origin. Corners land at (0, 0), (5.196, 3.000), (3.196, 6.464), and (-2.000, 3.464).

1. **Point 1.** Click anywhere. No guides yet beyond nearby buildings and the world axes.
2. **Point 2.** Move toward 30°. The angle increment catches the direction (30° is a 15° step from east) and the faint angle ray draws from point 1. Click at 6 m: (5.196, 3.000).
3. **Point 3.** The first and previous edges are the same edge, so the shape gives one frame: 30° and 120°. Move the cursor up and left of point 2. Within 7.5° of 120° as seen from point 2, the cursor locks onto the guide through point 2 perpendicular to the first edge; the guide draws with a right-angle marker at point 2. Length is free. Click at 4 m: (3.196, 6.464). If the cursor also passes within 0.3 m of another guide (say level with a nearby building's corner), the point snaps to that crossing and a dot shows.
4. **Point 4.** Two shape guides now matter: the guide through point 3 perpendicular to the previous edge (running at 210°, back parallel to the first edge) and the guide through point 1 perpendicular to the first edge (at 120°). Near (-2, 3.5) the cursor is on both, so it snaps to their crossing at (-2.000, 3.464): both guides draw, each with a right-angle marker (at point 3 and at point 1), and a dot sits on the crossing. Click.
5. **Close.** Move to point 1; the origin halo lights and the cursor snaps onto it. Click to close. All four corners are right angles to within the 1 mm the committed geometry is rounded to.

Walls draw the same rectangle the same way, with one difference today: a wall chain cannot end on its own first point (the closing segment touches the first one and is refused as self-intersecting), so a closed loop of walls is two chains, the second starting from the first chain's committed end.

#### Not in this design

- A rectangle mode (two clicks for a side, a third for depth). The polygon tool plus guides covers it.
- Length matching (equal-length hints, "same length as that edge"). Guides lock direction and position only. (Left out by a call made while writing the spec; the owner may pull it in.)
- Guides from arcs or curves (there are none in v1), tangent guides, midpoint guides, and equal-spacing guides.
- Locking a guide with a key (SketchUp's Shift lock). Revisit if players fight the priority order.
- Any guide from an edge of the shape other than its first and previous edges.

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
| Terrain | Buildable ground only (no water, and that includes a stream narrow enough for colonists to wade across). Trees, rocks, and loose items don't block placement; they generate clear tasks | — |

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

The same angle increments and guides apply as for foundations (Foundations > Snapping and guides): the chain's own first and previous segments, then nearby foundation edges and walls (the host foundation's edges included), then the world axes. This holds for every chain, whether it runs along the foundation edge or stands in the middle of the floor touching nothing; that a mid-floor chain gets the full guide set is a call made while writing the spec, open to the owner's overrule. Whole priority order, first that applies wins:

1. Wall endpoints (continue or join a wall)
2. Foundation vertices (inset so the wall sits outer-face-flush)
3. Points along existing walls (T-junction anywhere along a wall's length)
4. Foundation edges (slide along the edge, inset outer-face-flush)
5. Guides, ranked by source as for foundations (crossings and single guides)
6. Angle increment off the previous chain point
7. The raw cursor

A guided point is not inset: it lines up with a reference point or edge direction, not a foundation face, so the wall's centerline lands on the guide. There is no close onto the first point; the chain is open.

Snapping is generous but escapable (Alt), which drops guides and angle increments and keeps 1 to 4. Walls may be drawn anywhere on the foundation, not just at edges: a wall in the middle of a foundation touching nothing is valid, subject to clearance constraints.

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

An opening **sits on the wall** as its own blueprint with its own materials and work; it does not cut, consume, or modify the wall (deliberately unlike the real world, where a door needs a hole framed for it). The wall and the opening are independent: placing an opening adds no wall work and costs no wall material, there is no cut-and-patch step, and removing an opening leaves the wall fully intact. Openings can be added to a wall (blueprint or built) at any time, so buildings stay renovatable forever.

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
- Guides (parallel/perpendicular to the shape's own edges, nearby buildings, and the world axes) as thin hint lines, a right-angle marker on a perpendicular guide, and a dot where two guides cross (Foundations > Snapping and guides).
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
| Alt (hold) | Freeform: no angle increments, no guides (snaps to existing corners, edges, and walls stay on) |
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
| Snap behavior | angle increment, snap radii, guide range, guide tolerance, crossing radius, nearby-direction count |
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
