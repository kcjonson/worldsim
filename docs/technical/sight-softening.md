# Sight softening

**Created:** 2026-11-16
**Companions:** [Player Visibility](./player-visibility.md) (the seen record and the cover), [Vision Architecture](./vision-architecture.md) (where sight shapes come from), [Nav Mesh](./nav-mesh-variable-resolution.md) (the collision map sight reads)

Sight is two layers kept strictly apart. The **sight shape** is what the simulation uses for colonist memory, discovery, `isVisible`, and the seen record: one star-shaped polygon round the eye's centre, in which a thing wider than the eye casts a full shadow, a narrower thing casts none, and narrow things pull the rim in by a capped amount each. The **drawn softening** is cover only: from a true disc eye, the short shadows that close behind narrow things and the partly hidden fins beside full shadows, drawn each frame as ground not in sight right now, and able only to show ground darker than the sim has it. Both edges of the cover fade over the feather width inside the brighter area. What occludes is read from the collision map, never from a list of kinds.

Terms are player-visibility.md's: **player visibility**, the **unexplored area**, and the three areas **unseen**, **seen**, and **visible**. An **occluder** is a group of touching collision outlines (2.2). It is **hard** when it holds a wall or its apparent width from the eye's centre is at least the eye width `E`, and **narrow** otherwise.

---

## 1. Rules

The rules are player-visibility.md's 23 to 27 (2026-11-16), which replace its rules 5 and 11. In short: collidable objects from the collision map less movers affect sight, walls by their band outline (23); sight is less precise, with full shadows only from things wider than the eye, tapering shadows and fins drawn on the cover only, and a capped dent per object down to a floor (24); both cover edges fade inside the brighter area, and rule 10 stands (25); one sight shape serves colonist knowledge and player visibility, and the drawing is never read by the sim (26); the fade is drawn with mask layers in salal that carry partial coverage (27).

## 2. Occluders

### 2.1 What occludes

The collision map is the nav mesh's blocked rings: wall bands and junctions, placed obstacles, and water. Sight reads two of those kinds, and reads walls through construction rather than through nav:

| Source | Sight |
|---|---|
| A built wall | Always hard. Occludes by its band outline: `geometry::wallPiecesAndOutline(...).outline` (`libs/geometry/offset/WallOffset.h`), the boundary of the union of the trimmed bands and junctions, cut at every opening whose type is `transparentToSight` (`ConstructionRegistry.h`), as `GeometryIndex` keys its gaps on that flag. Sight stops at the wall's near face, and the wall's fill is out of sight from every side |
| A placed obstacle (`kProvenancePlaced`) | Occludes by the ring nav holds for it |
| Water (`kProvenanceWater`) | Never occludes: water has no height |
| A ford (`kProvenanceFord`) | Never occludes: a cover-only ring |
| A mesh limit or the walkable bounds (`kProvenanceBorder`) | Never: bounds rings, `blocked = false` (`NavInputBuilder.h`), which a blocked-ring query never returns |

Movers are never rings, so a colonist never hides another. A door and a window pass sight (vision-architecture.md D7); a closed solid door, when there is one, flips the same flag. The wall outline is the same geometry as nav's wall rings, the bands and junctions `resolveWallBands` gives, with windows cut as well as doors, so walls and placed obstacles together are every blocking thing in the collision map that stands above the ground.

What lands in the set follows the collision data alone, and no code in vision names a kind of thing. A tree's trunk is a Rect collider, so it is in; a bush, groundcover, and most rocks have no collision, so they do nothing to sight; a BigRock is in. Nav pads a Rect collider by `kPlacedObstaclePadMm` (50 mm, `PlacedObstacle.h`) on every side before scaling by the entity's scale (`NavInputBuilder.cpp`), and sight reads that ring as it is. Trees take no `randomRotation` and no placement scale, so their rings are axis-aligned squares of the trunk width plus 0.1 m: a pine 0.28 m, an oak sapling 0.35 m, a mature oak 0.55 m, a veteran oak 0.82 m (1.16 m across its diagonal), a maple 0.29, 0.44, or 0.6 m by age. A BigRock's ring is 1.5 m by 1.17 m, turned at random. A 0.1 m post is a 0.2 m ring.

### 2.2 Groups, width, and distance

Rings that touch or overlap are one occluder, and so is a ring that touches a wall outline with that wall: two trees whose padded rings meet are one, and a trunk against a wall is part of the wall. All of a build's quantities are taken from the eye's centre `c`, the observer's position at the build:

- The **apparent width** `w` is the occluder's extent across the line from `c` to the mean of its vertices: the largest less the smallest cross product of that line with the vertices, over the line's length, one integer square root.
- The **distance** `D` is from `c` to the occluder's nearest point, exact in integers and one square root.
- The **silhouette** is the pair of its vertices at the extreme directions from `c`.

All three are continuous in `c`. A mover's centre stays outside blocked rings (routes keep its clearance and static collision pushes it out); an occluder whose ring holds `c` is skipped for that build.

### 2.3 Where vision reads them

`GeometryIndex` (`libs/sim/vision/`) gives the wall outline, from the change "wall sight occluders" on Specboard. Placed obstacles come from nav.

Vision runs at priority 45 and nav at 51. The patch job nav submits at the end of a tick runs on its worker into the next tick until nav's `settle()` joins it, and `navMesh()` joins it too, so a vision read of the mesh would stall the main thread on the job. Vision never reads the mesh. `NavigationSystem` keeps a **placed occluder index** on the main thread, refreshed at its join when the job added, removed, or moved a placed ring: a box grid of those rings, each with its outline and a **serial** from a counter that never repeats, assigned when a ring is added or moved. It is filled from a public query on `PatchableMesh`, `blockedRingsInBox`, the public form of the private `ringsInBox`, returning the blocked rings whose boxes meet a box with their provenance. Vision reads the index read-only. A ring the last job patched shows to vision from the tick after nav's join publishes it; a felled tree occludes for that long.

An observer's cached shape is rebuilt when he moves more than the rebuild step from its build, when the walls' version moves, or when the sorted serials of the placed occluders in reach change. The mesh's epoch cannot stand for that, since movers patch it every 4 m of travel. Ring ids cannot either: `PatchableMesh` recycles a freed ring's id through its free list (`PatchableMesh.cpp`, `allocateRing` and the commit that frees it), so a felled tree's id can return as a new tree's, and a move keeps the id while the outline changes. A serial is never reused and changes on a move.

The mesh exists wherever an observer can see: every mover meshes the disc of sight plus 16 m round him, and the landing 150 m. Ground a colonist reaches before nav meshes it (a dev teleport) has no placed occluders until the mesh arrives.

## 3. The sight shape

### 3.1 Hard shadows

Each hard occluder's outline edges go to `geometry::computeVisibilityPolygon` as occluder segments: its shadow is the region past it on rays from `c`, out to the reach. A wall seen face on, a building, and a BigRock (at least 1.17 m from any side) are hard; a mature oak (0.55 m, 0.78 m across its diagonal) is narrow from every side. A veteran oak is narrow within about 15 degrees of face on and hard elsewhere. A wall is hard whatever its apparent width, end on included, since its fill is opaque (rule 23).

### 3.2 Dents

A narrow occluder casts no shadow in the sim. It **dents** the rim: it pulls the range in its directions in by an amount of at most the cap. A dent is a shortening of the rim, never a shadow starting at the object, so the ground between the object and the dented rim stays in sight and nothing hidden in the sim is ever counted as in sight.

**The cost.** An occluder of apparent width `w < E` costs

```
c = kSightDentCap * w * w / (E * E)
```

in millimetres, with the cap a tenth of the sight radius (3 m at 30 m). It is continuous in `w`, does not depend on `D`, never reaches the cap below `E`, and nears it only as the occluder nears turning hard. A 0.2 m ring costs 0.12 m, a pine 0.24 m, a mature oak 0.91 m, a veteran maple 1.08 m, a veteran oak face on 2.02 m. This is a rule with a cap, not a model of the eye: a disc eye's partial shadow spreads a narrow thing's share over about `(E + w) / 2D` either side of it and the shares of trunks in line overlap, and neither is modelled.

**Directions.** Ranges are kept for `kSightDentDirections` (288) directions from a table made at compile time with `directionTable` (`core/DirectionTable.h`), as `geometry::kSightDirections` is. Each stands for the wedge of 1.25 degrees between its two neighbours' bisectors, which are the odd entries of the 576-direction table. An occluder's silhouette covers part of a wedge: for a direction `u` inside a wedge bounded by `a` and `b`, its place across the wedge is `cross(a, u) / (cross(a, u) + cross(u, b))`, a ratio of integers that rises steadily from 0 to 1, and the occluder's **coverage** of the wedge is the difference of that place at its two silhouette directions clamped to the wedge. Coverage is continuous in `c`.

**Accumulation.** In each wedge the range starts at the reach (sight plus the rebuild step), and the narrow occluders covering the wedge are taken nearest first by `D`. Each pulls the range in:

```
if D < range:  range = max(D, range - c * coverage * gate)
```

never in front of itself, so an object near the rim costs at most the distance from it to the rim and its cost turns off continuously as the rim reaches it. The **gate** keeps an occluder hidden behind a hard one from costing: it is 1 while `D` is at least `E` short of the hard range along the wedge's direction, 0 at or past it, and linear between. Then the range is held at no less than the **floor**, two thirds of the sight radius (20 m at 30 m), so costs add along a direction down to the floor and no further.

```cpp
for (std::size_t direction = 0; direction < kSightDentDirections; ++direction) {
    std::int64_t rangeMm = reachMm;
    for (const DentCrossing& crossing : crossingsNearestFirst[direction]) {
        if (crossing.distanceMm >= rangeMm) {
            break;
        }
        const std::int64_t pullMm = crossing.costMm * crossing.coverage / kFractionOne * crossing.gate / kFractionOne;
        rangeMm = std::max(crossing.distanceMm, rangeMm - pullMm);
    }
    dentedRangeMm[direction] = std::max(rangeMm, floorMm);
}
smoothWithTent(dentedRangeMm, kSightDentSmoothingBins);
```

Every quantity is an integer: millimetres, and fractions in `kFractionOne` parts. The result is the same on every toolchain.

### 3.3 Smoothing and the shape

The dented ranges are smoothed across directions with a tent of `kSightDentSmoothingBins` (4) wedges either side, plus or minus 5 degrees, weights 1, 2, 3, 4, 5, 4, 3, 2, 1 over 25, in integers. A dent narrower than the window becomes a shallow scallop and never a slit. Smoothing reads only the dented ranges, which no hard shadow enters, so it can neither raise a range past a hard shadow nor lower one.

```
range(direction) = min(hard range, smoothed dented range)
```

The **dent ring** is the ring through the smoothed ranges at the 288 directions, cut to the sight polygon of the reach (`starClippedToConvex`), so an undented stretch has the sight polygon's vertices and no others of its own. The shape is the nearer of the hard polygon and the dent ring along every ray from `c`, `geometry::starNearerOf(first, second, centre)`, an angular sweep of the kind `starClippedToConvex` runs. Both are star-shaped round `c`, so the shape is too, always.

With no occluder in reach, sight is the plain disc and no shape is built, the outdoor fast path. Everything vision decides with a shape is held to the sight radius round where he stands (player-visibility.md 3.2).

### 3.4 What the sim does

Ground is out of sight in the sim when the ray to it from the eye's centre crosses a hard occluder, or when it lies past the dented rim in its direction. Everything else within sight is in sight, ground directly behind a narrow trunk included. Ground a disc eye would partly see beside a hard shadow edge on the shadow side is out of sight, and ground a disc eye would not see at all in the closed umbra behind a narrow thing is in sight. The drawn softening (5.3) shows the second as not in sight right now; the sim does not.

**Flips.** As a colonist walks round a veteran oak, or round two touching trunks, its apparent width crosses `E` and the occluder turns between narrow and hard: in its directions the range goes from the dented rim to its own distance and back. What the sim knows can flip with it: a deer behind the oak is visible on one build and not on the next. The seen record only grows, so ground it took while the oak was narrow stays seen. The cover does not flip (5.3).

### 3.5 Rim change between builds

Builds are a rebuild step (0.5 m) apart. Between two builds the range in one direction changes by at most the costs of the occluders whose dents move across it, and for one occluder by at most `c`, since coverage is at most 1 and smoothing is an average. For an occluder `D` away the change is also at most about `c * 9.2 m / D`: a 0.5 m step turns its silhouette edges by at most `0.5 / D` radians, `22.9 / D` wedges, and the tent passes at most a fifth of a wedge's change in coverage for each of the two edges. So the worst cases are near things passed at arm's length, and only in the directions their dents' edges sweep: 2.02 m for a veteran oak face on, 1.08 m for a veteran maple, 0.91 m for a mature oak, 0.24 m for a pine, 0.12 m for a post; a mature oak 20 m off moves the rim by at most 0.42 m. The drawn rim carries the 1.5 m feather over these steps.

### 3.6 Worked examples

One occluder, a 30 m sight, `E` of 1 m, cap 3 m, floor 20 m. Widths are rings as nav holds them, face on. "Rim" is the smoothed range in the direction through the occluder's middle; "drawn" is the length behind it of the drawn shadow before its taper starts and where it ends (5.3).

| Occluder | `w` | `D` | Cost `c` | Wedges covered | Rim | Drawn |
|---|---|---|---|---|---|---|
| A 0.1 m post | 0.2 m | 12.5 m | 0.12 m | 0.7 | 29.98 m | 3.1 m, gone by 6.3 m |
| A pine | 0.28 m | 10 m | 0.24 m | 1.3 | 29.94 m | 3.9 m, gone by 7.8 m |
| A pine | 0.28 m | 1 m | 0.24 m | 12.7 | 29.76 m over about 16 degrees | 0.4 m, gone by 0.8 m |
| A mature oak | 0.55 m | 10 m | 0.91 m | 2.5 | 29.6 m | 12.2 m, still 36% at the rim |
| A mature oak | 0.55 m | 2 m | 0.91 m | 12.5 | 29.1 m over about 16 degrees | 2.4 m, gone by 4.9 m |
| A veteran maple | 0.6 m | 5 m | 1.08 m | 5.5 | 29.2 m | 7.5 m, gone by 15 m |
| A veteran oak, face on | 0.82 m | 10 m | 2.02 m | 3.8 | 28.8 m | 45.6 m: full to the rim |
| A veteran oak, 20 degrees off face on | above 1 m | 10 m | | | a hard shadow | the wedge, with fins |
| A BigRock | 1.17 m or more | 10 m | | | a hard shadow | the wedge, with fins |

**A stand.** Trunks of width `w` at `n` to the square metre cover, in expectation, `n * w` of a wedge for each metre along it, so before the clamps a direction's dent is about `n * w * c * S`. Mature oaks 2 m apart (`n` 0.25) dent the rim by about 3.7 m, to about 26 m; 5 m apart, about 0.6 m; at about one to every 1.5 m2 the rim reaches the floor. Pines 2 m apart dent it by about 0.5 m. The quickstart landing's density is read by the prototype (9).

### 3.7 What uses the shape

`VisionSystem::update` builds the shape for every observer with an occluder in reach, on the rebuild rule of 2.3. `sightShape(observer)` carries, beside the centre, radius, polygon, revision, and the walls and openings seen, whether a hard occluder was in reach, which decides the record's path (4.1), and the 288 final ranges when none was, which a leg reads (4.2). `polygon` is set whenever a shape was built, narrow occluders alone included.

Memory, discovery, and `isVisible` keep their test: a thing is seen when it lies within sight of where he stands and in the shape (player-visibility.md 3.2). In a forest the shape has up to a few hundred vertices and the test runs for every candidate; the shape is star-shaped, so a test by angle (one binary search and one side test) is its cheaper form, and the prototype measures which is needed.

`VisionOverlay` (V) draws each observer's shape, hard occluders in one colour, narrow ones in another with their cost, and the eye's disc.

**The landing.** `PlayerVisibilitySystem::revealLanding` commits a plain disc of `kLandingSightMeters`, while `VisionSystem::rememberArea` queries occluders for the crew's memory. Rules 8 and 17 say the crew saw the landing as they came down, so both take no occluders: `rememberArea` at the landing builds no shape and gives every crew member the plain disc's things, and the seen area and what the crew knows agree.

## 4. The seen record

The record keeps its form: an exact polygon union in a second `PatchableMesh`, star-shaped shapes in, the covered test, the frontier, and the skirt (player-visibility.md 3.3).

### 4.1 Three paths

| At the build | Path |
|---|---|
| No occluder in reach | An open leg: one capsule a straight walk |
| Narrow occluders only | A leg with a polygon (4.2) |
| Any hard occluder | The walls path: each shape queued with its walk, the skirt, frontier pieces, and the sweep, its shapes this document's |

A leg ends when the path changes, as an open leg ends when walls come into sight. The sweep's builder (`SeenArea::setSightBuilder`) builds this document's shape at its point, hard shadows and dents together, so a sweep point sees what vision's own build there would. In the quickstart forest most builds have a hard occluder in reach (a veteran oak seen off its face, a BigRock), so the walls path is the common case there and legs with a polygon are the case in thinner stands.

### 4.2 Legs with a polygon

An open leg is one ring however long the walk, and rests on the outdoor shape being convex: the hull of a convex polygon at two ends holds that polygon at every point between, so the ring only grows. A dented shape is not convex. A leg with a polygon holds a convex polygon `Q` fixed at its first build, and its ring is `Q` swept along the leg.

**The leg polygon.** At the first build, at position `p0`, `Q` is the intersection of one half-plane per direction `d`, `{y : (y - p0) . d <= R(d) - kSoftLegToleranceMm - chord(R(d))}`, `R` being that build's final ranges, cut to the sight polygon of sight less `kSeenSlackMm` round `p0`. `chord(R)` is `R * (1 / cos(delta / 2) - cos(delta / 2))` for the angle `delta` between two directions, about 3.6 mm at 30 m and growing with range, from a compile-time ratio: where two half-planes meet, their corner can stand that much past the chord the shape's ring takes between the same two directions. `Q` is found exactly as the dual of the convex hull of the points `d / h`, in integers, its vertices rounded toward `p0`.

**Confirmation.** A build at `p` confirms the leg when its shape holds `Q` moved to `p` and grown by the **margin** `r`, half the distance from the build before it plus `kSeenSlackMm`, its sides moved out by `r` and mitred, tested exactly: every vertex inside the shape and no side crossing it. The build before it must hold the same grown polygon too, which the leg checks against that build's shape, kept with the leg. The first build checks itself by the same test, so the chord allowance places `Q` and does not keep it inside. The leg's ring is the hull of `Q` at `p0` and at the last confirmed build, `setOpenLeg` passing `Q` (4.3). Every point of the leg's segment lies within the margin of a confirmed build, so every translate of `Q` along it lies in a confirmed build's shape.

The ring advances only to positions a build has confirmed. Between builds it stays at the last one, so it never holds ground no build has confirmed; it moves forward only along the leg, so it never shrinks (rule 16). The leg ends at the last confirmed build when a build does not confirm (a new dent deeper than the tolerance where `Q` reaches, or builds farther apart than the tolerance can bridge), and by an open leg's rules: a turn, a turn back, a jump, or a change of path. The next leg starts at the new build with its ranges. The tolerance must exceed the largest margin a leg bridges.

**The open leg** is the same mechanism with `Q` the sight polygon of sight less the slack: with nothing in reach every position confirms it, so its ring's end is where he stands each frame, as built.

A leg's ring goes through the frontier unskirted, as an open leg's does. A build that does not hold its own `Q`, which only rounding can cause, goes in as the walls path's shapes do, skirted and swept.

What it gives up: ground between `Q` and the shape, the tolerance band and a scallop's outer part, enters the record only from another leg or a later build. At the exploring edge in a thin stand the record trails sight by that much, under the unseen fill.

### 4.3 The record's interface

- `SeenArea::setOpenLeg(leg, start, end, polygon)` takes the leg's convex polygon round its start in place of a radius (`SeenArea.h`): an open leg passes the sight polygon of sight less the slack, a leg with a polygon its `Q`. `legCapsule` is the hull of that polygon at the two ends, built in one pass over its sides.
- `SeenArea::appendEdges(low, high, out)` gives the record's edge in a box: every live triangle edge with nothing across it, as `(start, end)` with seen ground on the left, through `PatchableMesh::meshEdgesNear`, since the record's mesh is private. The cover's feather reads it (5.2).
- `PlayerVisibilitySystem` keeps each observer's path: open, leg with a polygon, or walls.

**The bright shape.** `drawnVisibleRadiusMm`'s guarantee, that the disc lies in the leg's capsule, rests on the open leg (`SeenArea.h`). `PlayerVisibilitySystem::drawnVisible` gives:

- on the open path, the disc of `drawnVisibleRadiusMm` round his `Position` that frame;
- on the other two paths, vision's shape cut to that disc round its build (`starClippedToConvex`), while the observer's path took this revision. After a jump, nothing until vision builds the next.

`drawnVisible` gives the shape only while the observer's path is not open and the revision matches, the walls path's gate (`PlayerVisibilitySystem.cpp`) taken for both paths that build a shape. On a leg with a polygon the bright shape reaches past the leg's ring, by the tolerance and up to a build's spacing ahead of him; there it is under the unseen fill, which is opaque and drawn over the tint, so what shows bright is seen by the fills' order (player-visibility.md 3.1), with walls in sight or among trees alike.

### 4.4 Star-shaped shapes

The skirt (`geometry::skirtedSightShape`), the frontier (`sightShapeFrontier`), the covered test's fan, and `simpleAround` all take a shape star-shaped round its centre. Every shape the record takes is: a leg's ring is a convex hull, and this document's shape is star-shaped round its build by construction (3.3). The skirt grows a shape's sides off the rim, the dented stretches among them, by `kSeenSkirtMm`, so seen ground can reach that far past a dented rim on the walls path, as rule 21 allows past a shadow edge.

### 4.5 What a forest costs the record

In the quickstart forest the walls path is the common case, and the sweep's builder there builds a dented shape every 100 mm of a walk that opens ground near its rim (`kSeenSweepSpacingMm`), with every narrow trunk in reach. That, legs per kilometre in thinner stands, the registry's bytes per kilometre, and the record's tick are what the prototype measures (9), against player-visibility.md 6.5 and 6.7.

If the walls path costs too much in forest, the fallbacks in order:

1. The sweep skipped where every hard occluder in reach is a placed obstacle: the seen edge past a rock's or a wide trunk's shadow then steps with vision's builds, 0.5 m.
2. A leg polygon of one radius, the floor less the tolerance: a capsule that every build confirms, since no range falls below the floor without a hard occluder, so legs end only by an open leg's rules. The record trails sight by up to the sight less the floor at the exploring edge.
3. 144 directions in place of 288.

## 5. The cover

### 5.1 Two fields

The cover keeps its two fills and their order (player-visibility.md 3.4): the tint (black at `kSeenDimAlpha`) first, the opaque unseen fill over it. Each draws with a mask whose value at a point is how much of the fill shows there. With `fade(d) = max(0, 1 - d / kSightFeatherMm)`:

- The unseen fill's mask is 1 outside the seen area `S` (the record and the pending shapes), and `fade` of the distance to `S`'s edge inside it.
- The tint's mask is 1 outside the visible area `V` (every colonist's bright shape, 4.3), and inside it the greatest of `fade` of the distance to `V`'s feathered edge (5.2) and the drawn shadows (5.3).

The fade lies inside the brighter area on both edges: nothing unseen is ever shown even faintly, and the fully bright core of a 30 m sight is about 28.5 m round him. Sight, the record, and the bright shape are exact vector shapes; the fade is drawn and decides nothing. A click on ground under it is ruled by the record and by `isVisible`, never by the drawing.

### 5.2 The feather

`fade` of the distance to an area's edge is drawn exactly as the greatest of pieces, one per edge of the area's boundary and one per reflex corner. An edge's piece is the rectangle from the edge `kSightFeatherMm` into the area, 1 on the edge and 0 on its far side. A corner where the area's inside angle exceeds 180 degrees takes a fan of radius `kSightFeatherMm` round it over the gap between its edges' rectangles, 1 at the corner and 0 on the arc, in triangles of at most 15 degrees, within 1% of the fade on their chords. Inside the area the greatest of these is `fade` of the distance to the boundary, because the nearest boundary point to any point inside is a perpendicular foot on an edge or a reflex corner. The level where it reaches 0 is the true inward offset of the area, rounded at reflex corners and sharp at convex ones. Where a rectangle runs past the area's boundary it lies where the mask is 1 already. Where the area is narrower than twice the feather the pieces of its two sides overlap and the greatest is the nearer side's, so a beam of sight through a 1 m doorway is never fully bright and is never darkened twice.

This holds only when the pieces are drawn for the boundary of the union and for nothing else: an edge two shapes share, or one inside another shape, must draw no piece, or it draws a dark seam inside the area. So the boundary is found on the CPU, exactly and in integers:

- The seen area's edge is the record's edge (`appendEdges`) and the pending shapes' sides, each cut where it meets the others, a piece kept when the point just off its middle on its outer side lies in no other shape (the record by `locate`, a pending shape by its outline; a point on another shape's side counts as inside that shape when the shape lies on the outer side). The record's own edge is the edge of its whole union, however many meshes the record is uploaded in, so several record meshes leave no seam. The record's pieces are built when `epoch()` moves, with the mesh upload, as a world-space mesh in as many meshes as 16-bit indices need. Each frame, for the pending shapes whose boxes meet the view, the record's edges within the feather of a pending shape's box are cut again and their pieces drawn with the pending shapes' own; the uploaded record pieces are drawn only outside those boxes. Inside a box the frame's pieces stand alone; outside, both may draw the same edge's piece, and the greatest of two equal values is that value.
- The visible area's edge is that of the bright shapes of colonists in view, each side cut where it meets another colonist's shape and kept by the same test. A side lying on an occluder's outline is left out: the darker side is the occluder itself, a wall's or a rock's footprint, and the edge stays hard there, so no fade darkens the ground in front of a wall or a rock. The build tags each side of a shape as rim, shadow edge, or occluder face for this.

### 5.3 The drawn shadows

For each colonist whose sight's box meets the view, the drawn shadows are built from his build's shape and position when vision builds it, kept until the next, and drawn each frame. They are never recorded and never read by the sim. With `c` his build position, `E` the eye width, and `D` held at no less than `E / 2`:

- Fins. Beside each shadow edge of the shape, at a silhouette corner of a hard occluder, a disc eye still partly sees the ground on the lit side, out to the line from the far edge of the eye's disc through the corner. The fin is that wedge round the corner, its value falling linearly in angle round the corner from 1 at the shadow edge to 0 at that line. It widens with range: `(E / 2) * (R - D) / D` at range `R` behind a corner `D` off, 1 m at a 30 m rim behind a corner 10 m off.
- A narrow occluder is drawn as it would be drawn were it hard: the wedge between the rays from `c` past its two silhouette corners, from its near face, at 1; beside each of that wedge's two edges a feather piece `kSightFeatherMm * (w / E)^2` wide; and its two fins. All of it is multiplied by the **taper**, 1 out to `D + L` from `c` and falling linearly to 0 at `D + kShadowTaperPercent / 100 * L`, where `L = D * w / (E - w)` is the length behind it at which a disc eye's umbra closes. So the shadow closes behind narrow things, and its foot behind the near face is drawn not in sight as a wall's or a rock's is.

The drawing is continuous when an occluder turns hard. As `w` rises toward `E`, `L` grows without bound, the taper is 1 out to the rim once `L` reaches the rim, and the narrow drawing is the hard one: a wedge at 1, feather pieces `kSightFeatherMm` wide, and fins. At `w = E` the sim takes the wedge out of the bright shape, where the drawing already showed it at 1, and the feather beside its edges passes from the drawing to the visible area's edge at the same width. A veteran oak 10 m off draws its full wedge to the rim from face on, before it turns hard.

The drawn shadows only darken, and only ground the sim has in sight. A mover standing in one is visible to the sim, so it draws, under the tint, and can be selected, and a click there is ruled by the sim. Products such as the taper times a fin are evaluated on the CPU at the vertices of the pieces, which are cut at `D + L` and at the taper's end and subdivided until each triangle's linear field is within 2% of the product.

**Several colonists.** A pixel is in sight if any colonist sees it, so the honest combination is, per pixel, the least over colonists of each one's greatest shadow, and one channel of partial coverage cannot hold both. Each colonist's drawn shadows are drawn only on ground no other colonist's bright shape covers, cut there by a hard mask. Where one colonist alone sees, his shadows are exact; where two see, the ground shows as the sim has it, in sight. The cut is a hard line inside one colonist's shadow where another colonist's sight begins, the one place in the cover with no fade across it.

### 5.4 Composition

Each fill's mask is hard layers, as built, with one layer of partial coverage added on top:

```cpp
std::vector<MaskShape> unseenFeather;
unseenFeather.push_back({.mesh = {seenEdgePieces, camera}, .mask = outsidePendingBoxes});
for (const FeatherPiece& piece : pendingEdgePieces) {
    unseenFeather.push_back({.points = piece.screenPoints, .paint = MaskPaint::linear(piece.from, piece.to, 1.0F, 0.0F)});
}
std::vector<MaskLayer> unseenMask = {
    {.image = MaskImage::content(unseenFeather, MaskBlend::Lighten), .mode = MaskMode::Luminance},
    {.image = MaskImage::rect(view), .composite = MaskComposite::Subtract},
};
for (const InstancedMeshHandle& mesh : seenMeshes) {
    unseenMask.push_back({.image = MaskImage::mesh(mesh, camera)});
}
for (const PendingShape& pending : pendingShapes) {
    unseenMask.push_back({.image = MaskImage::polygon(pending.screenPoints)});
}
Primitives::drawRect({.bounds = view, .style = {.fill = kUnseenColor}, .mask = unseenMask, .key = coverKey + 1});
```

Layers are listed top first and composited bottom up, as CSS does. Below the top, the hard layers are the view less the union of the record's meshes and the pending shapes, 0 or 1 at each pixel, as built. The top layer is the partial coverage, its children combined by `lighten`, the greatest at each pixel, so the record's pieces and the pending pieces overlapping draw no darker than either. It is added by `MaskComposite::Add`, which is source over: where the layers under it are 1 the result is 1, and where they are 0 it is the partial coverage, so between a binary layer and any other source over is the greater of the two, and nothing is darkened twice.

The tint is built the same way: the hard layers are the view less the union of the colonists' bright shapes; the partial layer's children are the visible area's feather pieces and, for each colonist, his drawn shadows in a child whose own hard mask is his bright shape less the other colonists' (5.3), all combined by `lighten`. Overlapping sight shapes leave no seam, because the feather is drawn for the union's boundary only and the hard layers are binary.

### 5.5 Masks with partial coverage (salal)

salal's masks gain partial coverage under CSS's and SVG's names (salal `docs/masks.md`):

- `MaskMode`, CSS `mask-mode`, per layer: `Luminance` reads the layer's content as luminance. A hard layer's content is white where it covers, so `Alpha` and `Luminance` agree on it, and existing masks draw as they do.
- `MaskImage::content(children, MaskBlend::Lighten)`: an SVG `<mask>` whose content is the children, each painted with CSS `mix-blend-mode: lighten`, the greater of its value and what is under it. A child is a `<polygon>` (`.points`) or an uploaded mesh under a camera (`.mesh`). It is filled flat with a value, or by an SVG `linearGradient` in user space with two stops (`MaskPaint::linear(from, to, fromValue, toValue)`). A mesh child's vertices may each carry a value: a field linear over a triangle is exactly one two-stop linear gradient, so each triangle is a `<polygon>` filled by the gradient its three values define. A child may carry its own `mask` of hard layers, as an element inside an SVG `<mask>` may.
- `MaskComposite::Add` and `Subtract` keep CSS's meanings on partial values: `add` is source over, `subtract` is source out.

How salal evaluates partial coverage on the GPU is the prototype's to choose and is named nowhere in the API. The expected build keeps the hard layers in the stencil's two bits, as now, and takes the partial layer's greatest value in the target's destination alpha with a max blend, each child under a stencil test for its own mask, then draws the fill weighted by that alpha. Destination alpha is a single channel of transient per-pixel state, as the stencil is; it is not a texture and no data lives in it past the draw, so rule 10 holds.

The prototype (step 0) is a sandbox scene of the cover's cases over a busy background: two record meshes sharing an edge, a pending piece abutting the record along its edge, a pending leg overlapping it, overlapping and abutting sight shapes, a 1 m beam, a reflex corner, a fin and a tapered shadow, and one colonist's shadow cut by another's sight. A test reads the mask's value at chosen pixels (cores, mid-fade, overlaps, abutting edges, the shared record edge, a reflex corner) against 5.1 to 5.3. No golden image. If the target has no destination alpha, or the build cannot hold those values, the fallbacks in order:

1. The partial layer drawn as fill-coloured geometry carrying alpha, source over, under the stencil's hard result. Where two pieces overlap (inside the feather of a convex corner, a narrow beam, a shadow over a feather) it darkens to `a + b - a * b` in place of the greater, at most a quarter more.
2. Hard edges, as player-visibility.md 3.4 draws them, with the sight shape and the record of this document.

### 5.6 Consequences taken on purpose

- Narrow seen and visible ground, a doorway's beam or a strip between two shadows, is never fully bright or fully tinted.
- A wall's or a rock's face keeps a hard edge in the tint (5.2).
- Each tree's foot behind its near face is drawn not in sight, as a wall's fill is.
- The draw bound and the terrain scissor (player-visibility.md 3.4) are as built: the fade lies inside the record, so nothing past it shows.

## 6. Input, things, and dev tools

The input rules (player-visibility.md 3.7) read the record and `isVisible`, which reads vision's shape. Among trees a colonist's bright shape can reach past the record by the tolerance at the exploring edge, under the black, where placing and ordering read unseen. A mover that is not a colonist shows while `isVisible` holds against this document's shape: behind a BigRock it is out of sight, behind a trunk it is in sight and drawn under any drawn shadow.

The visibility state view (`/api/state?what=visibility`) gains: builds by path (open, leg with a polygon, walls); legs with a polygon held, ended by a dent, and ended by a gap between builds; narrow and hard occluders in reach summed over the last builds; the largest change of one direction's range between consecutive builds of one observer, and the occluder distance it came from; and sweep builds with narrow occluders in reach. `/api/dev/reveal` is as built.

## 7. Tunables

The starting values are placeholders, set by the prototype's screenshots and numbers.

| Name | Start | What it must cover |
|---|---|---|
| `kEyeWidthMm` | 1000 mm | The eye's width `E`. Sets which occluders are hard (a wall, a building, a BigRock, a veteran oak off its face) and which narrow, every dent's cost, and every drawn umbra's length |
| `kSightDentCapPerMille` | 100 | The most one object pulls the rim in, a tenth of sight (3 m at 30 m), reached only as it nears `E` |
| `kSightDentFloorPerMille` | 667 | The floor dents add down to, two thirds of sight (20 m at 30 m) |
| `kSightDentDirections` | 288 | Wedges of 1.25 degrees, from a compile-time table. More adds vertices to every dented shape |
| `kSightDentSmoothingBins` | 4 | Wedges either side the tent reaches, plus or minus 5 degrees. A dent narrower than this is a scallop, never a slit, and the rim's change between builds falls with it |
| `kSightFeatherMm` | 1500 mm | The fade on both edges. The bright core of a 30 m sight is sight less this; a shape narrower than twice it is never fully bright |
| `kShadowTaperPercent` | 200 | Where a narrow occluder's drawn shadow ends, as a percentage of its umbra's length `L`; it fades from `L` to there |
| `kSoftLegToleranceMm` | 1000 mm | How far a leg's polygon sits inside its first build's ranges: how deep a new dent ends the leg, and the largest margin it bridges. Larger means fewer rings and a record that trails sight farther among trees |
| `kSightRebuildStepMeters` | 0.5 m | As built. The rim's change between builds is bounded per step (3.5) |
| `kSeenSlackMm`, `kSeenLeastRadiusMm`, `kSeenSkirtMm`, `kSeenSweepSpacingMm` | as built | As player-visibility.md 4; legs with a polygon take the slack and the least radius as open legs do |

## 8. Prior art

| Source | What it does | What carried over |
|---|---|---|
| Orangy Tang, "Dynamic 2D Soft Shadows" | A light with a radius casts a hard umbra and a penumbra beside it, the penumbra drawn as geometry at each shadow edge | The eye as a disc for drawing: the umbra behind narrow things and the fins beside hard edges, drawn as geometry. The sim keeps a point eye |
| Scott Lembcke, "Super Fast Soft Shadows" | Shadow geometry per light, the penumbra of a light of given radius worked out per pixel | The same geometry. Its per-pixel shading is raster work this game does not do |
| Company of Heroes 2 | Sight blockers are an authored class: a thing blocks sight or does not | Set aside: what occludes is read from the collision map, and by how much from its width |
| Combat Mission, tabletop wargame rules | Line of sight through woods falls off with the depth of woods crossed | The dent: trees in a direction shorten sight there, down to a floor |
| League of Legends | A coarse grid upscaled and blurred (player-visibility.md 5) | Set aside: raster |
| Godot 2D lights, Unity URP Light 2D | Shadow edges softened by a filter and softness settings on the light, in the renderer | Set aside: raster. The fade is vector geometry with partial coverage |

## 9. Performance plan

Measured, not assumed, under the machine-load rule in [testing](../testing/README.md): times count only with `idle` checks both sides, and counts do not depend on load.

What could move:

- Vision's tick. Every observer among trees builds a shape each rebuild step: the occluders in reach from the index, grouping, widths, silhouettes into wedges, a sort a wedge, the smoothing, the hard polygon, and the nearer-of sweep. Memory tests candidates against the shape.
- The record's tick. In forest the walls path is the common case, and the sweep's builder builds the whole dented shape every 100 mm of a walk opening ground near its rim, with every trunk in reach. Legs with a polygon, their confirmation tests and ring counts, in thinner stands.
- The cover. The union boundaries, cut each frame; the record's pieces at each epoch; the drawn shadows per build for colonists in view, hundreds of tapered pieces a colonist in forest; the partial layer's GPU cost against the hard cover's.
- Views with reveal on draw no cover and build no pieces; captures should come out even with `main` apart from vision's tick.

To measure:

- Step 0, the sandbox: GPU time of the cover with a partial layer against the hard one in `perf_mask` at the same size, and whether every case of 5.5 holds.
- Step 1, the prototype: "Prototype: sight ray casting across the nav mesh with tree attenuation, measure perf" on Specboard. A bench of a build at viewpoints in the quickstart landing's forest and in synthetic stands of known density: us a build, occluders in reach, hard and narrow, vertices out. The sim bench with 1, 10, and 50 colonists walking the landing's forest at 1x and 10x: vision's tick; the record's tick; the share of builds on each path; sweep builds and their cost; rings registered per kilometre and per minute; legs per kilometre in a thinner stand; the largest rim change between builds; and how far the record trails sight at the exploring edge. The landing's tree density, read from placement. Overlay and cover screenshots among trees for Kevin to judge `E`, the cap, the floor, the window, the taper, and the feather.
- Step 5, the cover: A capture near the landing with `--no-reveal` at each zoom with the crew, 10, and 50 colonists: GPU and CPU time of the cover, pieces drawn a frame, and the record's pieces at an epoch.

No-go at step 1 goes back to Kevin with the numbers. For vision's build, 144 directions, then a cached silhouette per occluder per observer. For the record, 4.5's fallbacks in order. For the cover, 5.5's.

## 10. Build steps

Each step is its own PR with its own done line and a `Perf:` line.

0. Partial coverage in salal. The sandbox scene and test of 5.5, then the API. Done: a tagged salal whose `mask` scene shows every case of 5.5 with no seam and nothing darkened twice, the value test passing on the GL backend, `docs/masks.md` brought up to date (the `mask-mode` row and the partial layer), and worldsim's pin on that tag; or the fallback of 5.5 that holds, with the test of its values.
1. Prototype the sight shape. As 9, on a branch that builds 3's shape from the walls' outline and nav's placed rings and runs legs and the walls path in the benches only. Done: numbers in this document; Kevin's call on the tunables from the screenshots; a go, or the fallback taken for each of vision, the record, and the cover.
2. Occluders. The placed occluder index in `NavigationSystem` with serials, published at its join; `PatchableMesh::blockedRingsInBox`; grouping, apparent width, distance, and silhouettes; the cache keyed on serials. Walls by their outline come with "wall sight occluders". Done: a placed obstacle occludes and a mover, water, a ford, and a bounds ring do not; felling a tree rebuilds an observer's shape and walking over new ground with no obstacle does not; a tree placed on a recycled ring id rebuilds it; vision never joins the patch job (a test with the job held running).
3. The sight shape. 3.2 and 3.3 in `libs/geometry/visibility` (`starNearerOf` among them), `SightShape`'s new fields, the overlay, and the landing pass of 3.7. Done: unit tests for a post, a pine, a stand, a ring just under `E` and one over it, a veteran oak turning hard as the eye circles it, an occluder behind a wall costing nothing, a wall seen face on; the rim's change between builds 0.5 m apart walking past a trunk at arm's length within 3.5's bound; rings pinned and identical on MSVC, Clang, and GCC.
4. The record. Legs with a polygon, the three paths, `setOpenLeg` with a polygon, `appendEdges`, the bright shape's rule, and the sweep's builder on this document's shape. Done: tests for a straight walk through a stand (one leg while builds confirm), a dent ending a leg, builds too far apart ending one, a walk from open ground into forest and to a BigRock; the record oracle (no point on the record outside a true shape) and the sim test that every seen point is on the mesh and the record never lost ground, run in forest.
5. The cover. The union boundaries and their pieces, the record's pieces at each epoch, the drawn shadows, and the masks of 5.4. Done: soft edges on both edges in the game, the drawn shadows closing behind trunks, scenario 14's new steps passing, the capture of 9.
6. Tests and docs. Scenario 14's steps (11) written in, player-visibility.md's tunables and measured sections brought up to date. Done: scenario 14 run.

## 11. Tests and scenario steps

Unit and sim tests are named in each step's done line. Before they are added, check the existing vision and record suites for the same paths and extend those.

Scenario 14 (`docs/testing/scenarios/14-player-visibility.md`) gains these steps after its step 2, all at the quickstart landing, written in at step 6:

1. Zoom 3 on Bob among trees: the bright area's edge is a fade about 1.5 m wide with no hard line, and the tinted area's edge against the black fades on the tinted side; past it the black is solid.
2. Press V: Bob's rim has shallow scallops toward nearby trunks, no slits, and trunks show as narrow occluders, any BigRock as hard.
3. Look behind a trunk 5 to 10 m from Bob: a short darker taper starts behind it and fades out well before the rim.
4. `/api/dev/spawn` a BigRock 8 m from Bob: a shadow wedge runs from it to the rim with straight edges, and beside each edge a gradient widens toward the rim.
5. Walk Bob past a trunk at arm's length (a move order beside one): his rim dents on that side as he passes and recovers behind him, with no jump of more than a couple of metres; the seen area behind him does not shrink.
6. Draw a room with a door and a window and stand Bob inside: sight passes the doorway and the window and stops at the walls' near faces, and both sides of the light through the doorway fade.
7. Stand a second colonist beside Bob among trees: the tapers behind trunks show only where one of them alone sees.

## 12. Decisions log

### Kevin's words

1. 2026-11-16, the brief: "we currently have hard edges on the sight cones, and visibilty edges, which we need to soften greatly ... to be less ... precise. for instance if there was just a single wall block a 0.1m in diameter, it really shouldn't effect sight. and right now trees have no effect, but they very slightly should and the cones/rays should kinda ... collapse behind things. and visual softening around the edges as well".
2. 2026-11-16, after a design canvas comparing sight models and edge treatments: "lets go with C + 2".
3. 2026-11-16, first question round, offered a list of what blocks or thins sight: "collidable objects. this needs to use the true collision map (minus movers) not bound to an arbitrary list."
4. 2026-11-16, to the building construction session, relayed: "Note, the shadows start at the wall center, not the wall edge, which is wrong." and "the wall fill is essentially always unseeable, its opaque." Walls occlude by their band outline (2.1).

### Options Kevin picked (the wording is the author's)

5. C, a wide eye plus a sight budget; 2, feathered edges as vector geometry.
6. First round: the feather inside the brighter area; softened sight changes both colonist knowledge and player visibility, one sight shape; the feather built as alpha mask layers in salal.
7. Second round, after three reviews of the first draft. (a) "Drawn, sim stays simple": colonist knowledge and the seen record use a plain shape in which things wider than the eye cast full shadows and narrower things cast none; the tapering shadows behind narrow things and the fins beside wall shadows are drawn on the cover each frame as not in sight right now, smooth, and able only to show ground darker than its true state. (b) "Capped per object": one object can pull the rim in by at most a small tunable amount, however near or wide; several in a row add up, to a floor near 20 m in dense forest.
8. The values shown with the options, a 1 m eye, plus or minus 5 degrees of smoothing, a 1.5 m feather, a 3 m cap, and a 20 m floor, were the author's placeholders. They are tunables (7).

### Departures from option C as drawn on the canvas

9. In the sim a thing narrower than the eye casts no shadow, and its closed umbra counts as in sight; the umbra is only drawn (from 7a).
10. The budget is a capped dent per object, added along a direction to a floor, a rule and not a model of the eye: C's budget spent the share of the eye an occluder hid, which double-counted trunks in line and moved the rim 10 m between two builds at arm's length (from 7b).
11. Hard shadows are cast from the eye's centre. Ground a disc eye would partly see on the shadow side of a hard edge is out of sight in the sim, and the fin on the lit side is only drawn (the author's, following 7a).
12. A narrow thing's drawn shadow reaches past its true umbra, fading out by twice its length, and is drawn as a hard one would be, so the cover does not change when it turns hard (the author's).
13. One colonist's drawn shadows show only where no other colonist sees (the author's).

### The author's proposals awaiting Kevin

14. The cost `c = cap * (w / E)^2`, coverage across wedges, nearest first with no pull in front of an object, the gate behind hard occluders, and the floor as two thirds of sight (3.2).
15. Walls hard whatever their apparent width, reading "the wall fill is essentially always unseeable" as later than the brief's "a single wall block a 0.1m in diameter", which is read as a small placed thing (3.1). Touching rings as one occluder, a trunk against a wall part of it, apparent width across the line to the mean of the vertices (2.2).
16. Placed occluders from an index nav publishes at its join, keyed by serials that never repeat (2.3).
17. The landing's memory pass taking no occluders, so it agrees with the plain disc the record commits (3.7).
18. Legs with a polygon: `Q`, confirmation with a margin, the open leg as the same mechanism, and `setOpenLeg` taking a polygon (4.2, 4.3); the bright shape's rule (4.3); 4.5's fallbacks.
19. The feather drawn for each union's boundary only; occluder faces left hard in the tint; a narrow drawing's feather `(w / E)^2` of the full width (5.2, 5.3).
20. The salal surface, `MaskMode::Luminance`, `MaskImage::content` with `MaskBlend::Lighten`, `MaskPaint::linear`, mesh children with a value at each vertex, and children carrying their own hard mask, for Kevin to judge against CSS and SVG (5.5). The author reads rule 27 as replacing rule 19's "drawn with the stencil buffer"; rule 19's API rule stands.

## Related documents

- [Player Visibility](./player-visibility.md): the rules, the seen record, the cover, and the input rules this builds on
- [Vision Architecture](./vision-architecture.md): sight shapes, discovery, doors and windows
- [Nav Mesh: One Mesh, Variable Resolution](./nav-mesh-variable-resolution.md): the collision map and where it exists
- salal `docs/masks.md`: the mask API this extends
- [Testing](../testing/README.md): scenarios and the perf rule
