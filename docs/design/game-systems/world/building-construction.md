# Building & Construction

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

Drawing comes in three levels, and a new player meets the first. The point is onboarding: a first colony shouldn't die, and a first-time player shouldn't leave, because the drawing tools asked too much of them at once. So the default draws a good square building in two clicks, freeform drawing is one choice away, and the precision helpers wait in Drawing Settings for whoever wants them.

1. **Basic: the Rectangle tool.** The default foundation tool. Two clicks, or a drag, make an axis-aligned rectangle with its corners on a 1 m grid.
2. **Freeform: the Freeform tool, helpers off.** Any polygon, point by point. It still lands on existing corners and edges, closes on its first point, and rounds segment lengths to 0.1 m; nothing else pulls the cursor.
3. **Freeform with helpers.** Each helper in [Drawing Settings](#drawing-settings) switches on one kind of snapping together with what it draws. They are independent: a player can take one, several, or all of them.

The wall tool has no rectangle; it follows the same helper settings (Walls > Drawing).

Select Foundation in the Build menu: it opens the foundation tool last picked this session, Rectangle the first time. A config strip appears with the tool picker, which switches between Rectangle and Freeform, material choices, and a live cost readout ([Config strip](#config-strip)). Material is picked **before** drawing; the ghost renders in that material's blueprint style. While the tool is active a small [key guide](#key-guide) panel lists its keys.

Every drawing tool pauses while the pointer is outside the game's window or the game is in the background: nothing follows the pointer and nothing reads refused, the points already placed stay, and the tool picks up from the pointer when it comes back.

#### Rectangle tool

1. Click one corner, then the opposite corner. Or press on one corner, drag, and release on the other: a release more than 4 screen pixels (logical pixels, before any HiDPI scaling) from the press finishes the rectangle, and a shorter one counts as a click, so the next click finishes it.
2. Each corner snaps, first that applies wins:
   1. A corner of an existing foundation within 0.4 m (the vertex snap radius).
   2. An edge of an existing foundation within 0.3 m (the edge snap radius), at the nearest point on the edge. When that edge runs along a world axis, the point also rounds to the grid along it, as long as the rounded point stays on the edge.
   3. The nearest grid point. The grid is square, aligned to the world axes, anchored at the world origin, and spaced 1 m (0.5 m is the other choice in Drawing Settings).
3. The rectangle spans the two corners, its edges on the world axes. While the second corner is pending it previews as a faint fill and outline, and a label beside the cursor reads its size, `4 m x 3 m` (meters, to the millimetre, trailing zeros dropped). When the grid places the cursor a small cross marks the grid point.
4. A second corner level with the first in either axis (no width or no depth, under 1 mm) makes no rectangle: the click places nothing, and the first corner waits for another click.
5. Finishing creates the foundation blueprint. The shape constraints apply as to any drawn shape: an invalid rectangle previews red with its reason and the click is refused.

**Esc**, **Backspace**, or right-click drops the first corner; a second Esc exits the tool. Holding **Alt** places the raw cursor: no grid and no adjacency. The rectangle tool has no guides, angle steps, common lengths, or length rounding (the grid does that job), and none of the helpers' drawings appear. It cannot be rotated; a rotated building is drawn with the Freeform tool.

#### Freeform tool

The flow is polygon drawing as in design software:

1. First click places the origin point at the cursor.
2. A line rubber-bands from the last point to the cursor. The line colorizes for validity: blueprint-blue when the placement is valid, red when not, with a short reason near the cursor and in the config strip ("corner too tight", "edges too close").
3. Each click commits a point.
4. Once three points exist, a faint fill previews the implied closed polygon.
5. The user must click the origin point to close the shape. Once three points exist, a faint dotted ring the size of the close radius (0.5 m, and never under 8 screen pixels, so it stays reachable zoomed far out) circles the origin; inside it the cursor snaps to the origin and a solid halo marks it.
6. Closing the shape creates the foundation blueprint.

While drawing:

- **Backspace** removes the last committed point.
- **Esc** (or right-click) cancels the in-progress shape. Pressing Esc again, with nothing in progress, exits the tool.
- A label beside the segment being drawn shows its length in meters, to two decimals (`4.00 m`); the config strip shows running area (m²), the point count, material cost, and work estimate.

With every helper off a point goes, first that applies wins: onto the first point (the close), onto an existing corner, onto an existing edge, or else where the pointer is, with the segment's length rounded to 0.1 m along the line from the previous point toward the pointer. Snapping and guides below has the full order once helpers are on.

#### Helpers

Each helper is a switch in Drawing Settings, off until the player turns it on, and turns on both its snapping and its drawing.

| Helper | What it snaps to | What it draws |
|--------|------------------|---------------|
| Angle steps | A segment turns in 15 degree steps off the previous segment (off east for the first), whenever no guide holds the cursor | A guide from the previous point along the step |
| Guides from the shape | Lines parallel and perpendicular to the shape's first edge through every placed point, and to its previous edge through the previous point; also squares a point that lands on an existing edge | Guides; a right-angle marker where a guide is square to an edge at its point |
| Align to existing buildings | The edge directions of nearby buildings, through the previous point and through those buildings' corners | Guides |
| Guides from the world axes | North-south and east-west through every placed point, and through nearby buildings' corners when Align to existing buildings is on | Guides |
| Crossings | The point where two caught guides meet (needs at least one guide source on) | A dot on the crossing; both guides draw |
| Common lengths | A segment length equal to an edge already built, or to an earlier edge of the shape | `=` on the length label, a tick across the snapped end, equal-length ticks on the matching edges |
| Guide distances | Nothing; display only | Each guide's distance from its own point |
| Angle readout | Nothing; display only | An arc and the angle in degrees at a corner that is not square |

Length rounding is not a helper: it is basic behaviour, on by default, with its own switch ("Round lengths to 0.1 m").

### Snapping and guides

Pure freeform produces accidental 87-degree corners and 4.97 m walls that read as mistakes. The basic tools avoid them by construction (the grid, the length rounding); the helpers are the precise version, and each is escapable with Alt. Mechanism, config, and tests: [architecture D12](../../../technical/building-construction-architecture.md#d12-snap-engine-helpers-guides-lengths).

**Whole priority order, Freeform tool** (first that applies wins; the helper a step needs is in brackets):

1. Close onto the first point (three or more points placed, cursor within the origin radius, 0.5 m)
2. A corner of an existing foundation (within 0.4 m)
3. An edge of an existing foundation (within 0.3 m); where a caught guide of the shape crosses that edge within 0.3 m of the cursor, the point lands on the crossing [Guides from the shape]
4. Guides, ranked as below; a guide result within the origin radius of the first point closes there, as step 1 [Guides from the shape, Align to existing buildings, or Guides from the world axes; a crossing also needs Crossings]
5. Angle step off the previous segment [Angle steps]
6. The raw cursor

Then the length step, wherever the point can still slide along its direction (a single guide at step 4, step 5, or step 6): the segment's length snaps to a common length within its catch [Common lengths], or else rounds to 0.1 m [Round lengths]. Steps 1 to 3 and a crossing fix the point, so its length is whatever it comes out.

An existing corner beating a guide is intended: landing on it is adjacency. Landing on an edge is adjacency too, and taking the shape's guide where it crosses the edge keeps the corner square; without it, a corner drawn 0.2 m short of a neighbour's edge along its perpendicular would slide to the foot on the edge and come out at 91.69 degrees inside the shape. The wall tool's order is in Walls > Drawing.

**Angle steps.** Segments snap to **15 degree** steps (so 90, 45, and 30 degrees are all reachable) relative to the previous segment, or to east for the first segment. The step has no catch window: when it's on and no guide caught the cursor, it holds the direction and keeps the cursor's distance from the previous point, which the length step then rounds.

**Guides.** A guide is a thin line through a point along a direction, and every direction comes with its perpendicular, so a guide means "parallel to" or "perpendicular to" something. The guides are exactly these, each from its helper, in priority order:

1. **The shape being drawn** (Guides from the shape). Its first edge's direction through every point placed so far. When the previous edge runs at another angle (more than 1 degree off, quarter turns equivalent), also the previous edge's direction through the previous point.
2. **Nearby buildings** (Align to existing buildings). The edge directions of the buildings nearest the cursor (within 8 m, at most 4 distinct directions; axis-aligned edges are left to the world guides). Each runs through the previous point, so the new segment can run parallel or perpendicular to that building, and through the corners of every building in range with an edge in that direction (within 1 degree, quarter turns equivalent), so a new corner can line up with any of their faces, even when two buildings share an angle and only one supplied it. Building corners count only within 8 m of the cursor, the 16 nearest.
3. **The world** (Guides from the world axes). North-south and east-west, through every point placed so far and, with Align to existing buildings on, through those 16 nearest building corners.

The Freeform tool reads corners and edges from foundations. The wall tool reads walls too, and takes each foundation corner at its outer-face-flush inset (where a perimeter wall's centerline turns), so a guide through a corner lines up with the walls there.

When the cursor comes within 0.3 m of a guide it locks on and the guide draws. Guides of the shape through the previous point or the first point also catch by angle: seen from that point, a cursor within half an angle step (7.5 degrees) of the guide, on either side of the point, locks on at any distance. So a right-angle turn is as easy to hit 10 m out as next to the corner, and so is the closing corner of a rectangle.

**Crossings.** With Crossings on, when the cursor is on two guides that cross at 15 degrees or more, it snaps to the crossing and a dot marks it. This squares the last corner of a rectangle: the guide through the previous point parallel to the first edge crosses the guide through the first point perpendicular to it exactly at the fourth corner. A crossing more than 1 m from the cursor is ignored. So is a crossing on a corner that one of the two guides was drawn through, when both guides pass through that corner (placed or existing), so a corner never pulls the cursor back onto itself. A crossing of guides drawn through different corners can land on a third corner: the last corner of a rectangle may fall on a neighbour's corner and still snaps there.

**Priority among guides.** Source first: the shape beats a nearby building, which beats the world, however close the lower-ranked guide is. Then a crossing beats a single guide, a guide through one of the shape's own points beats one through an existing corner, and the closest wins. Two near-parallel guides (within 1 degree) less than 0.3 m apart at the cursor count as one, the higher-ranked. So an incidental line-up with the world axes never pulls a rotated shape off its own perpendicular.

**Squaring on existing edges.** With Guides from the shape on, a point that lands on an existing edge (step 3) moves along the edge to where a caught guide of the shape crosses it at 15 degrees or more, when that crossing is within 0.3 m of the cursor; the nearest such crossing wins. The guide draws with its marker. Case: the shape at 30 degrees, a foundation edge along y = 6, and the cursor 0.2 m short of it on the perpendicular from the second point: the point lands on (3.464, 6.000), square, rather than the foot at (3.580, 6.000), which would leave a 91.69 degree corner.

**Landing on the first point.** With three or more points placed, a guide result (a single guide's point or a crossing) within the origin radius of the first point lands on the first point and closes, so the closing guide of a rectangle closes the shape even when the cursor is just outside the radius.

**Common lengths.** With Common lengths on, a segment's length snaps to a length the player has already used. The use case is a row of shacks: after the first, its two side lengths become lengths the next ones catch.

- *Sources.* The shape's own earlier edges, and every edge of each foundation (blueprints included) that has any edge within 30 m of the cursor. In the wall tool, also walls, a drawn chain counting as one building, and a foundation's edges at their inset lengths (the length between the inset corners a wall's points land on; a 4 m by 3 m foundation offers 3.796 m and 2.796 m under a 0.2 m wall). Edges under 0.5 m don't count.
- *Grouping.* Lengths within 5 cm of each other are one length, the length of the highest-priority edge among them (the shape's own edges first, then the most recently placed building, then the nearest), so 4.03 m joins 4.00 m and the length is 4.00 m. A length within 5 cm of two groups joins the one formed first.
- *Ranking.* Lengths from the shape being drawn rank first, then those of the most recently placed building, then the length more edges share, then the nearer building's, then the shorter. The first 6 are kept.
- *Catch.* The point slides along its locked direction (the guide, the angle step, or the raw direction) to where the segment has a kept length. A length catches when that slide is 0.3 m or less, and of those the shortest slide wins. On a guide through the previous point the slide is the length difference; on a guide through another point it is measured along that guide.
- *Where it applies.* Only where the point can still slide: a single guide, the angle step, or the raw cursor, and only once a point is placed. Never on the close, an existing corner or edge, a wall endpoint or junction, or a crossing, and never in the rectangle tool. A crossing keeps its point even when a common length would catch along one of its guides; a config switch lets the length win (the crossing then gives way to that guide alone), off by default.
- *Look.* The segment's length label reads `4.00 m =`, bold in the accent colour. A short tick crosses the segment at the snapped end, and every edge that shares the length gets a faint highlight and the drafting double tick at its middle. A label also reads `=` when another rule (a crossing, say) happens to land on a kept length.

**Length rounding.** With "Round lengths to 0.1 m" on (the default), a segment's length rounds to the nearest 0.1 m wherever the length step applies, unless a common length caught. On a single guide through a point other than the previous one (a building corner, an inset corner, the first point), the guide's own distance and the segment's length can't both be round, and the distance along the guide from its own point rounds instead, the number its label shows; before any point is placed that is the only one there is. The config can switch this to rounding the segment's length. The increment is in meters and the same at every zoom. Rounding never moves a fixed point, never applies under Alt, and never in the rectangle tool, whose grid does the job.

**Look.** One rule: solid means geometry, dotted and part transparent means alignment, small solid markers state a fact.

- Geometry, solid: placed edges, the segment being drawn, buildings, and walls.
- Alignment, dotted and part transparent: every guide, drawn from its point to the snapped cursor; the close radius ring, which gives way to the halo once the click will close.
- Facts, small and solid: the right-angle marker (where a guide is perpendicular to an edge at its point, the shape's edge or that of the building the corner belongs to), the crossing dot, the common-length ticks, and the close halo (a ring round the first point whose radius on screen is half the close radius, never so small that it falls inside the snapped-point marker's ticks, drawn when a click would close the shape and commit it; a close the click would refuse draws none, and the shape reads red).
- The cursor: the operating system's pointer stays at the raw position, and where the point will go the game draws the snapped-point marker, a white dot with a dark outline and four short ticks. The game draws no raw-pointer ring and no pull line from the raw pointer to the snapped point; the system pointer already shows where the hand is.
- Guides are one muted teal; colour by source (shape, nearby building, world, common length) is the config alternative. Colours are written with an alpha of FF; their opacity comes from the guide and marker opacity keys. Sizes are screen pixels and never scale with zoom; label text follows the Interface size. Values: [architecture D16](../../../technical/building-construction-architecture.md#d16-guide-rendering-look).
- **Guide distances** (helper). Each guide drawn carries its distance from its own point to the snapped cursor, to two decimals, beside the guide's middle on the side away from the segment, nudged along so labels don't overlap. A guide from the previous point is not labelled, since that distance is the segment's own length and its label already says it; at a crossing both guides are labelled (unless one runs from the previous point). A label reads `=` when the distance matches a kept common length. A guide too short on screen for a label (under 40 px) goes without.
- **Angle readout** (helper). While drawing, the corner at the previous point (and, once three points are placed and the cursor is within twice the origin radius of the first point, the closing corner there) shows its angle: a foundation reads the interior angle of the shape so far, an open wall chain the angle between its two segments. A corner within 0.05 degrees of square gets the right-angle marker; any other gets an arc and its angle in degrees to one decimal (`75.0` and a degree sign). Nothing shows on the first segment, or under Alt.

**Alt.** Holding **Alt** is true freeform: no angle steps, no guides, no squaring, no common lengths, no rounding. Snaps to existing corners, edges, and walls stay on (they are adjacency, not style), and so does the close onto the first point. Distance and validity constraints still apply.

**Settings.** Every helper, and Round lengths, is a player setting in [Drawing Settings](#drawing-settings). "Align to existing buildings" gates everything that comes from existing buildings: their directions, the guides through their corners, and the world guides through those corners. With it off, the shape's own guides and the world guides through the shape's own points remain.

#### Worked example: a rectangle at 30 degrees

Needs Angle steps, Guides from the shape, Guides from the world axes, and Crossings on (Round lengths is on by default). A 6 m by 4 m rectangle whose long side runs at 30 degrees from east, starting at the origin, with no buildings nearby. Corners land at (0, 0), (5.196, 3.000), (3.196, 6.464), and (-2.000, 3.464).

1. **Point 1.** Click anywhere.
2. **Point 2.** Move toward 30 degrees. The angle step catches the direction (30 degrees is a 15 degree step from east) and the guide draws from point 1. Click near 6 m; the length rounds to 6.0 m: (5.196, 3.000).
3. **Point 3.** The shape's direction is now 30 degrees, with its perpendicular at 120 degrees. Move the cursor up and left of point 2. Within 7.5 degrees of 120 degrees as seen from point 2, the cursor locks onto the guide through point 2 perpendicular to the first edge; it draws with a right-angle marker at point 2. A cursor half a metre off the corner in any direction still lands on the guide, and the length rounds along it. Click near 4 m: (3.196, 6.464).
4. **Point 4.** The guide through point 3 parallel to the first edge and the guide through point 1 perpendicular to it both catch by angle, so from above the corner and to its right, at (-1.6, 3.8) say, the cursor snaps to their crossing at (-2.000, 3.464). Both guides draw, with right-angle markers at point 3 and point 1, and a dot sits on the crossing. Click.
5. **Close.** Move to point 1; the origin halo lights and the cursor snaps onto it. Click to close. The corners are square to within a few hundredths of a degree (committed points are rounded to the millimetre); with the corners exactly as listed they read 90.00.

Walls square a rotated chain the same way, and a closed loop of walls is one chain: points 1 to 4, then point 1 again. With three or more points placed the cursor snaps onto the first point and the click closes the chain and ends it. Point 4 lands on the crossing of the two guides, as for the foundation.

#### Worked example: four shacks

The designer's case: four small shacks for the colonists, each the size of the first.

*With the Rectangle tool* (basic). The first shack is built from (0, 0) to (4, 3). A click near (6.3, 0.8) puts a corner on the grid at (6, 1); a click near (9.6, 3.7) puts the opposite corner at (10, 4), and the label reads `4 m x 3 m`. The grid makes every shack the same size by itself. A shack meant to sit against the first starts on its corner: a click near (4.2, 0.25) lands on (4, 0), the existing corner, not the grid.

*With the Freeform tool and helpers* (Common lengths, Align to existing buildings, Guides from the shape, Guides from the world axes, Crossings, and Angle steps on). The first shack is 4 m by 3 m turned 20 degrees, corners at (0, 0), (3.759, 1.368), (2.733, 4.187), and (-1.026, 2.819). It offers exactly two common lengths, 4.00 m and 3.00 m, two edges each.

1. Click the second shack's first corner at (7.177, -2.177).
2. Move out along 20 degrees; the first shack's direction holds the cursor as a guide through point 1. At 3.85 m or 4.2 m out the length catches 4.00 m (a slide of 0.3 m or less) and the label reads `4.00 m =`, with double ticks on the first shack's two 4 m edges. At 4.5 m it doesn't catch and the length rounds to 4.5 m. Click at 4.00 m.
3. Move up the shape's perpendicular (110 degrees); at 2.85 m it catches 3.00 m. Click.
4. Move back along 200 degrees; near the fourth corner the shape's two guides cross and the crossing fixes the point, 4.00 m from point 3. Click.
5. Close on point 1. The edges are 4.000, 3.000, 4.000, and 3.000 m and the corners 90.00 degrees.

A third shack turned to some other angle takes its directions from the angle step and its lengths still catch 4.00 m and 3.00 m.

#### Not in this design

- Guides from arcs or curves, tangent guides, midpoint guides, and equal-spacing guides.
- Locking a guide with a key (SketchUp's Shift lock).
- Guides from shape edges other than the first and the previous.

### Shape Constraints

**Why these exist:** pathfinding. Without them, freeform drawing produces slivers, knife-edge corners, and near-touching edges, exactly the pockets that trap wandering entities and enable bait exploits. The constraints guarantee that everything the tools produce is either solid and closed, or open by at least the pathing clearance.

All values live in the external construction config (see Config surface), so tuning never touches code. The pathing clearance constant (proposed **0.7 m**, a colonist body plus working room) is the root from which the distance constraints derive. Clearance assumes colonists; larger animals exist in the future but are not sized for here. Violations colorize the preview red and block the click. Defaults below are placeholders pending playtesting.

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
| Terrain | Buildable ground only (no water, and that includes a stream narrow enough for colonists to wade across: nothing is built in a stream, so walls, stations, and storage refuse it too). Trees, rocks, and loose items don't block placement; they generate clear tasks | — |

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

Walls draw as a polyline chain: click to commit each point, the chain continues from the last point. The chain does not need to close, but it may: clicking the chain's first point closes the loop and ends the chain. Once the chain has three points, the cursor snaps onto the first point when near it, as it does when closing a foundation; holding Alt skips that snap, so a point near the start is placed as an ordinary one. **Right-click, Esc, Enter, or a double-click** ends the chain: a double-click's first click places the final point, the second places nothing and ends the chain. A second Esc, with no chain in progress, exits the tool.

The wall tool has no rectangle and no grid. It follows the same Drawing Settings as the Freeform tool (Foundations > Snapping and guides): with every helper off it keeps its adjacency snaps (steps 1 to 5 below) and length rounding, which is the basic level for walls, since walls mostly go onto foundations whose corners and edges are already snaps and a grid would fight a rotated building. With helpers on, the same angle steps and guides apply for every chain, along the foundation edge or standing mid-floor; the wall tool also reads walls' corners and directions, and takes foundation corners at their outer-face-flush inset. Whole priority order, first that applies wins (the helper a step needs is in brackets):

1. The chain's first point (closes the loop; once the chain has three points, and skipped while Alt is held)
2. Wall endpoints (continue or join a wall)
3. Foundation vertices (inset so the wall sits outer-face-flush)
4. Points along existing walls (T-junction anywhere along a wall's length); where a caught guide of the chain crosses that wall near the cursor, the point lands on the crossing [Guides from the shape]
5. Foundation edges (slide along the edge, inset outer-face-flush); likewise squared by a guide of the chain [Guides from the shape]
6. Guides, ranked as for foundations; a guide result within the close radius of the chain's first point closes there [the guide helpers]
7. Angle step off the previous chain point [Angle steps]
8. The raw cursor

The length step follows as for foundations, at steps 6 (a single guide), 7, and 8. Common lengths in the wall tool read walls as well as foundations, a drawn chain counting as one building, and read a foundation's edges at their inset lengths, the length between the inset corners a wall's points land on, so a wall drawn to match an outer-face-flush perimeter wall comes out the same length.

A guided point is not offset afterwards: the wall's centerline lands on the guide, which already runs through the inset corners.

Snapping is generous but escapable (Alt), which drops guides, angle steps, squaring, and the length step and keeps 2 to 5 (the close onto the chain's first point is skipped too). Walls may be drawn anywhere on the foundation, not just at edges: a wall in the middle of a foundation touching nothing is valid, subject to clearance constraints.

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

A room built inside another room, its walls standing free of the outer room's, is a room of its own. The outer room's area no longer counts the floor inside it, and the outer room keeps its name; clicking inside the inner room selects the inner room.

Rooms are an abstract concept, not clickable world objects. They are inspected through a **rooms overlay**, like RimWorld or Going Medieval. The overlay exists and is toggled from the game scene; it tints, outlines, and names each room.

- Toggled from the HUD (and a hotkey). While active, room interiors tint and show name labels ("Room 1", renamable).
- Clicking a room while the overlay is active opens its **room info panel**: area (m²), bounding wall materials, opening count, average insulation, floor material, name.
- With the overlay off, rooms have no click presence; clicks fall through to whatever structure or entity is under the cursor.

Beyond the overlay, a room darkens its area ([Enclosed Look](#enclosed-look)) and has no other effect. Future: room types from contents, ownership, adjacency bonuses, material effects. See [Rooms](./rooms.md).

### Enclosed Look

A room reads as indoors at a glance, overlay on or off. One flat, hard-edged darkening lies over the room's whole area, and everything in the room darkens with it, the floor and all that stands on it. It is one layer over the room, not a tint on each thing, so whatever is drawn over the room's area is darker and whatever is drawn outside it is not. The step is small, a black fill at about 5%, and the same for every room whatever its size, material, or openings; it is a named value in the construction config, so tuning it is a data change. Mechanism and draw order: [architecture D6, Enclosed shade](../../../technical/building-construction-architecture.md#enclosed-shade-wor-743).

Every closed room is shaded, doors and windows included; a closed room is exactly what room detection calls a room. Roofs add the open-air distinction: a walled space with no roof is open-air ([Roofs](./roofs.md)).

The shade is the same whether the room is in a colonist's sight or not; it knows nothing of player visibility. The out-of-sight darkening draws over it as over any other ground, so a room out of sight reads darker than the out-of-sight open ground beside it by the same small step, and unexplored ground stays hidden, room or not.

The shade appears the moment the closing wall segment completes, with the "Room formed" notification, and goes the moment the loop breaks. It does not wait for the formation pulse: the pulse is a notification, the shade is the room's light.

Cases:

- **Under construction.** While any wall of the loop is a blueprint or under construction there is no room and no shade; the floor looks like any other foundation.
- **A wall removed.** When a wall in the loop is demolished (the segment leaves the building when its demolition work completes), the room is gone and its shade goes with it in the same update. A wall that splits a room gives two shaded rooms; removing a wall between two rooms gives one, still shaded.
- **A room inside a room.** Each floor is shaded once: the outer room's shade has the inner room cut out of it and the inner room carries its own, so the inner room is no darker than the outer one.
- **The rooms overlay.** Its tint, outline, and name draw over the shade. Turning the overlay off leaves the shade.
- **The walls.** The room's edge is the wall centrelines, so the inner half of each wall, door, and window darkens with the room and the outer half does not.
- **Across the edge.** Something straddling the edge, a colonist in a doorway, is darker on the part drawn over the room and as bright as outside on the rest. A tree or other tall thing outside whose drawing reaches over the room is darker where it does; a thing inside drawn out past the wall line is bright beyond it.
- **Building inside a room.** A blueprint, a foundation edit, or anything else placed inside a room darkens with it, and so does a selected thing's outline, which draws with the things themselves. The drawing preview, the placement ghost, the outline of a selected foundation, wall, or opening, and the rooms overlay's selected-room highlight draw over the shade and stay bright. An outline drawn with the things themselves darkens with them; one drawn with the construction overlays does not.

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
| Distance from wall end / corner / junction | Openings keep clear of structural points, measured from where the wall's band ends: the vertex at a free end or a straight join, the edge of the corner or junction piece otherwise (that piece reaches further along the wall the sharper the corner) | 0.3 m |
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

### Drawing feedback

- Rubber-band segment and validity colorization; on invalid, the reason beside the cursor and in the config strip.
- A pointer that has not visibly moved since a click proposes nothing about what that click placed: no "points too close", "wall too short", or "openings overlap", nothing red, and the strip shows the tool's hint. A click there places nothing and says nothing. The first visible move, of the mouse or of the camera under it, brings back every refusal that applies.
- Faint polygon fill after the third point; the rectangle tool's fill and outline while its second corner is pending.
- Origin-point halo + snap when the shape can close.
- Length readout (meters, two decimals) beside the segment being drawn, `=` when it matches a common length; the rectangle tool's size label at the cursor instead (`4 m x 3 m`). Area/cost/work in the config strip.
- The snapped-point marker (a white dot with a dark outline and four short ticks) where the point will go; the system pointer marks the raw position, and nothing joins the two.
- With helpers on: guides as dotted, part-transparent lines, a right-angle marker on a perpendicular guide, a dot where two guides cross, common-length ticks, and, if switched on, guide distances and the angle readout (Foundations > Snapping and guides, under Look).

---

## UI & Selection

### Build menu

The gameplay bar's [Build▾] menu gets a **Structure** category: Foundation, Wall, Door, Window. Selecting one activates its tool and opens the config strip. Foundation opens the foundation tool last picked this session, Rectangle the first time; the strip's tool picker switches between Rectangle and Freeform.

### Config strip

A horizontal strip docked above the gameplay bar while a structure tool is active:

- **Tool picker** (Foundation): switches between Rectangle ("Two corners") and Freeform ("Point by point"). The Build menu's Foundation entry opens the one last picked this session, Rectangle the first time.
- **Material cards:** icon, name, per-unit cost (per m² or per m). One selected at a time; each tool keeps its own selection for the session. Add / Subtract shows none, since it keeps the edited foundation's material.
- **Thickness presets** (Wall tool): the selected material's presets, each its name over its thickness in meters, with their stat summary (HP, insulation, cost) on hover.
- **Type picker** (Door/Window tool): the opening types.
- **Live readouts:** running area or length, total material cost, work estimate. Updates as the cursor moves. The Freeform tool also shows its point count (`Points`), which the Rectangle tool leaves out. Add / Subtract leads with the mode and the foundation's material ("Add to foundation (wood)") and reads the area of the outline the edit would leave and how much it changes by. The wall tool's length is the chain's with the segment being drawn, beside that segment's own length (`Len: 7.5 m  (seg 2.5 m)`). The door and window tools show the type and its width.
- **Validity message area:** a hint for the next step ("Click or drag from a corner", "Click origin to close", "Right-click / Enter to finish"), or, when the cursor position is invalid, the reason, which also shows beside the cursor.

Changing material or thickness mid-session applies to the next shape, not shapes already drawn.

### Drawing Settings

A "Drawing Settings" pane in the game's Settings holds the player's choices about the drawing tools. Everything else about snapping (every radius, range, tolerance, count, the priority rule, the length increment's value, and the look) is tuning in the construction config file, not a player setting. Defaults are what a new player meets: the rectangle tool and no helpers.

| Setting | Default | What the player reads |
|---------|---------|-----------------------|
| Angle steps | off | Turn segments in 15 degree steps. |
| Guides from the shape | off | Line up with the shape's own edges, parallel or square. |
| Align to existing buildings | off | Line up with nearby buildings' edges and corners. |
| Guides from the world axes | off | Line up north-south and east-west with points and corners. |
| Crossings | off | Snap to where two guides meet. |
| Common lengths | off | Match the lengths of edges already built. |
| Guide distances | off | Show how far the cursor is along each guide. |
| Angle readout | off | Show the angle of a corner that is not square. |
| Round lengths to 0.1 m | on | Segment lengths come out round. |
| Show key guide while drawing | on | A small panel with the keys and what the lines mean. |
| Grid spacing (rectangle tool) | 1 m (or 0.5 m) | Rectangle corners snap to this grid. |

The first eight are the helpers (Foundations > Drawing > Helpers); each turns on its snapping and its drawing together, in the Freeform and wall tools. The "15 degree" and "0.1 m" in the labels come from the config values.

### Key guide

While a drawing tool is active, a compact panel directly above the construction config strip, centred like it and stacked with it so the two never overlap, lists, in a player's words, the keys for that tool, then one legend line for each helper that is on. It is for new players: "Show key guide while drawing" in Drawing Settings, on by default, turns it off. **?** or **H** collapses it to a small "Keys ?" chip and reopens it, and clicking the chip reopens it too; a foot line says so ("? or H hides this"). `?` is the typed `?` character, whatever keys make it on the player's keyboard, and H toggles it as well. It starts open each time a game starts; collapsing it holds for the rest of that game, across tools, and is not saved. It takes clicks only inside its own box, never keyboard focus.

Keys per tool:

| Tool | Keys |
|------|------|
| Rectangle | Click: place two opposite corners (or drag). Backspace / Esc / Right-click: drop the first corner. Esc again: leave the tool. Alt: hold to place the raw cursor (no grid, no snapping). |
| Freeform | Click: place a point. Click point 1: close the shape. Backspace: undo a point. Esc / Right-click: cancel the shape. Esc again: leave the tool. Alt: hold to turn snapping off. |
| Wall | Click: place a point. Click point 1: close the loop. Double-click / Enter / Right-click: end the wall. Backspace: undo a point. Esc: end the wall. Esc again: leave the tool. Ctrl+click a foundation edge: wall the whole edge. Alt: hold to turn snapping off. |
| Door or window | Click a wall: place it. Esc / Right-click: leave the tool. |
| Add to or subtract from a foundation | Click: place a point. Click point 1: close the shape and apply it (the tool stays on for the next). Backspace: undo a point. Esc / Right-click: cancel the shape. Esc again: leave the tool. Alt: hold to turn snapping off. |

The close is taught by the "Click point 1" row, and in the Freeform and Add / Subtract tools by the config strip's "Click origin to close" once three points are placed and the shape is valid; on the canvas the close ring and halo are all that mark it.

Legend lines, in the Freeform, wall, and Add / Subtract tools only (the rectangle and opening tools draw no helpers), each with a small glyph painted the way the canvas paints it, so it follows the look config:

| Shown when | Glyph | Line |
|------------|-------|------|
| Any of Angle steps, Guides from the shape, Align to existing buildings, Guides from the world axes is on | A dotted guide | Lined up with something |
| Any of those, or Angle readout, is on | A right-angle marker | A square corner |
| A guide helper and Crossings are on | Two dotted guides and the crossing dot | Two guides cross |
| Common lengths is on | An edge with the double tick | = and ticks: same length as an edge already built |
| Angle readout is on | An arc between two edges | Arc and degrees: the corner's angle |

The selection keys (Shift+click adds to the selection; double-click a wall selects its run) work only with no tool active, so they are in the reference below but not on the panel.

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

Wall blueprints have no vertex editing; segments are cheap, cancel and redraw.

### Keyboard and mouse reference

| Input | Effect |
|-------|--------|
| Click | Place point / select; in the rectangle tool, place a corner (two opposite corners make the rectangle) |
| Press, drag, release | Rectangle tool: draw the rectangle from the press to the release |
| Click origin point | Close foundation polygon; close a wall loop |
| Double-click | End wall chain (the first click places the final point, the second places nothing); expand wall selection to connected run |
| Enter | End wall chain |
| Right-click / Esc | End wall chain; cancel in-progress shape or the rectangle's first corner; second Esc exits tool |
| Backspace | Undo last point (drawing); drop the rectangle's first corner; delete vertex (shape editing) |
| Alt (hold) | Freeform: no grid, angle steps, guides, or length snapping (snaps to existing corners, edges, and walls stay on, except in the rectangle tool, which takes the raw cursor) |
| Ctrl+click foundation edge | Fill edge with wall |
| Shift+click | Add to selection |
| ? or H | Collapse or reopen the key guide (while a drawing tool is active) |

---

## Config surface

In the spirit of everything else in the game, no magic numbers in code. Constraints, snap behavior, and construction tuning live in an external config file (`assets/config/construction/`, following the `assets/config/work/` pattern); material definitions live in their material asset XML. The player's own choices about the drawing tools are not config: they are Drawing Settings (UI & Selection > Drawing Settings), whose defaults the config file supplies.

| What | Examples |
|------|----------|
| Material definitions (per structure type) | cost rate, work rate, HP, insulation, flammability, beauty, movement speed modifier, visual pattern |
| Wall thickness presets | per material: thickness values (meters) and their stat multipliers |
| Pathing clearance | the root constant the distance constraints derive from |
| Shape constraints | min corner angle, min point spacing, segment clearance, min/max area, max points |
| Snap behavior | angle step, snap radii, guide range, catch distance, angle catch window, merge distance, crossing minimum angle and radius, direction tolerance, nearby-direction and corner counts, the priority rule, the rectangle tool's adjacency switch |
| Common lengths and rounding | sources, range, catch, merge tolerance, count, minimum, whether a length beats a crossing, the length increment (0.1 m) and what rounds on a guide through another point |
| Drawing Settings defaults | each setting's default (helpers off, Round lengths on, key guide on, 1 m grid) |
| Drawing look | guide dot pattern, width, opacity, colour (one, or by source), marker opacity and sizes, label size and decimals |
| Construction | refund percentage, max concurrent builders per size, work formulas' factors |
| Opening types | widths (meters), margins, materials allowed |

Every key, its default, and its meaning: [architecture D12 config](../../../technical/building-construction-architecture.md#config) and [D16](../../../technical/building-construction-architecture.md#d16-guide-rendering-look).

---

## Out of Scope (Future)

- Curved walls and foundation edges
- Freestanding walls / fences / defensive perimeters (separate tool)
- Flooring overlays distinct from foundation material
- [Roofs](./roofs.md), and with them open-air enclosures and temperature/insulation actually mattering per room
- Lighting
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

---

## Addendum: drawing tool decisions

A supplement, not part of the spec. Where it and the spec disagree, the spec is right. What the drawing tools settle, over what, and why, where the sources give a reason.

- **"The perpendicular from the first point."** The owner's words, read as the guide perpendicular to the first edge: through point 2 it places point 3, and through point 1 point 4.
- **A rectangle tool, as the default.** Chosen over freeform-only drawing, which an earlier draft had: a first-time player draws a square building in two clicks, and freeform is one choice away.
- **Common lengths.** Chosen over an earlier draft in which lengths did not match: a row of shacks needs the first one's sides again.
- **Walls take the same guides as foundations**, mid-floor chains included, and wall corners are guide points: walls square a rotated chain as a foundation does.
- **Alt drops every guide**: one key is true freeform; the adjacency snaps stay, since they are adjacency, not style.
- **"Align to existing buildings"**, over "Align to existing foundations": the one switch gates everything that comes from existing structures, walls included, nearby directions as well as corners.
- **The shape's guides through the first point catch by angle**, as those through the previous point do, over distance only: it is what makes the closing corner of a rotated rectangle as easy to hit as a right-angle turn.
- **A crossing beats a common length**, over letting the length win (the config switch `commonLengthBeatsCrossing`, off): a crossing fixes the point.
- **Angle steps are a helper, off by default**, over always-on steps in plain freeform: the basic level stays plain, and the rectangle tool covers the square case.
- **The rectangle tool cannot rotate**, over a rotated rectangle: rotated buildings are Freeform work.
- **The length increment is in meters, the same at every zoom**, over a screen-pixel increment that scales with zoom as the close radius does.
- **No grid for walls**, over a wall grid: walls go onto foundations whose corners and edges are already snaps, and a grid would fight a rotated building.
- **The close hint is the key guide's "Click point 1" row and the strip's "Click origin to close"**, over a hint drawn on the canvas.
- **The key guide starts open in each game, and collapsing it is not saved**, over remembering the fold: the panel is for new players.
- **Walls match a foundation edge's inset length** (4.796 m off a 5 m edge under a 0.2 m wall), over the edge's own length: a wall drawn to match an outer-face-flush perimeter wall comes out the same length, at the cost of an odd-looking number.
- **No raw-pointer ring or pull line**, over the prototype's: the operating system's pointer already shows the raw position, so the game draws only the snapped-point marker and the close halo.

## Addendum: enclosed look decisions

A supplement, not part of the spec. Where it and the spec disagree, the spec is right. What the enclosed look settles, over what, and why, where the sources give a reason.

- **Everything in the room darkens**, over the floor alone with colonists and furniture left bright: "everything. we'll do lighting later."
- **Every closed room, doors and windows included**, over only rooms with no windows.
- **About 5%, one layer independent of sight**, over a stronger floor shade and over a look that differs from the out-of-sight darkening: a small tunable darkening that adds with player visibility as its own layer.
- **At once when the room forms, the pulse separate**, over appearing with the formation pulse: the shade is the lighting changing, the pulse a notification, and they have different reasons.
- **Open-air enclosures wait for roofs**, over leaving courtyards and pens out by some other rule: walled courtyards, animal pens, a courtyard inside a building, and roofs over spaces walls do not enclose all come with the roof tool, WOR-1112 (Roofs: the roof tool and open-air enclosures).
- **A flat fill over the room**, over a dark band along the inside of the walls (needs a general polygon inset the geometry library does not have) and over a shadow cast outside the walls (reads as wall height more than enclosure).
