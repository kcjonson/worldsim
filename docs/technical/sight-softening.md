# Sight softening

**Created:** 2026-11-16
**Companions:** [Player Visibility](./player-visibility.md) (the seen record and the cover), [Vision Architecture](./vision-architecture.md) (where sight shapes come from), [Nav Mesh](./nav-mesh-variable-resolution.md) (the collision map sight reads)

Sight is softened by two mechanisms kept apart. Every body in the collision map less movers has a **kind**, fixed by its own size and never by the angle it is seen from. An **opaque** body (a built wall, a big rock) casts its exact shadow in the sim, and its shadow's sides are drawn soft. A **soft** body (a tree, a stub of wall) casts no shadow in the sim; a faint triangle that closes behind it is drawn on the cover, growing and shrinking with its width as seen as a colonist walks. **Forest depth**: the placed things in a direction shorten how far he sees that way, as a smooth density down to a floor, so nobody sees deep into a forest. The sight shape the sim uses is the convex forest shape, cut by the exact shadows of opaque bodies. What is drawn only darkens. Both edges of the cover fade inside the brighter area, and where several colonists see a point its light is the greatest any of them gives it.

Terms are player-visibility.md's: **player visibility**, the **unexplored area**, and the three areas **unseen**, **seen**, and **visible**. A **body** is one placed obstacle's collision ring or one connected wall graph's outline. Quantities used throughout, each with its name:

| Name | What it is |
|---|---|
| `sightRadius` | The observer's sight, 30 m for a colonist |
| `step` | The rebuild step, 0.5 m (`kSightRebuildStepMeters`) |
| `reach` | `sightRadius + step`, the radius a polygon-path shape is built to |
| `eyeCentre` | The observer's position at the build |
| `diameter` | The greatest distance between two vertices of a body's outline |
| `silhouette` | The arc of directions from the eye whose rays meet a body; `bisector` is its middle direction |
| `widthAsSeen` | A body's extent across its bisector |
| `distance` | From the eye to a body's nearest point |
| `cornerDistance` | From the eye to the body's vertex at one end of its silhouette |
| `rangeFromEye` | How far a point on the ground lies from the eye |
| `softRayWidth` | The width of the ray a soft body's triangle is drawn with (`kSoftShadowRayWidthMm`) |
| `edgeWidth` | The width of the ray a shadow's soft edge is drawn with (`kShadowEdgeWidthMm`) |
| `shadowLength` | How far behind a soft body its triangle reaches; `progress` is how far along it a point lies, 0 at the body and 1 at the tip |
| `forestDepth` | How much forest depth takes from the range across one side of the sight polygon |

---

## 1. Rules

The rules are player-visibility.md's 23 to 27 (2026-11-16), which replace its rules 5 and 11. What affects sight is read from the collision map less movers, and a wall is its whole body (23). Each thing has a fixed kind by its size, so nothing changes kind as a colonist walks: small things cast a faint soft shadow that collapses behind them, drawn on the cover only and scaled by their width, and walls and large things are opaque with soft edges (24). Things in the way shorten how far a colonist sees, one thing by a small capped amount and many down to a floor (25). Both cover edges fade inside the brighter area, built as alpha mask layers in salal, and rule 10 stands (26). One sight shape serves colonist knowledge and player visibility, and what is only drawn is never read by the sim (27).

## 2. Bodies

### 2.1 What occludes

The collision map is the nav mesh's blocked rings: wall bands and junctions, placed obstacles, and water. Sight reads two of those kinds, and reads walls through construction rather than through nav:

| Source | Sight |
|---|---|
| A built wall | Its body is the outline of its connected wall graph, as `GeometryIndex` builds it: `geometry::wallPiecesAndOutline(...).outline` (`libs/geometry/offset/WallOffset.h`), the boundary of the union of the trimmed bands and junctions, cut at every opening whose type is `transparentToSight` (`ConstructionRegistry.h`). The whole band is opaque, so an opaque wall's shadow starts at its near face |
| A placed obstacle (`kProvenancePlaced`) | Its body is the ring nav holds for it |
| Water (`kProvenanceWater`) | Never: water has no height |
| A ford (`kProvenanceFord`) | Never: a cover-only ring |
| A mesh limit or the walkable bounds (`kProvenanceBorder`) | Never: bounds rings, `blocked = false` (`NavInputBuilder.h`) |

Movers are never rings, so a colonist never hides another. A door and a window pass sight (vision-architecture.md D7); a closed solid door, when there is one, flips the same flag. The wall outline is the same geometry as nav's wall rings, with windows cut as well as doors.

What counts follows the collision data alone, and no code in vision or nav names a kind of thing. A tree's trunk is a Rect collider, so it is in; a bush, groundcover, and most rocks have no collision and do nothing to sight; a BigRock is in. Nav pads a Rect collider by `kPlacedObstaclePadMm` (50 mm, `PlacedObstacle.h`) on every side before scaling by the entity's scale (`placedObstacleBox`, `NavInputBuilder.cpp`), and sight reads that ring as it is. Trees take no `randomRotation`, so their rings are axis-aligned squares; a BigRock (`randomRotation` 180) is a rectangle at any angle, whose diameter the angle does not change. Wall bands are 0.15 to 0.30 m thick (`materials.xml`) and a wall is at least 0.5 m long (`minSegmentLengthMeters`, `constraints.xml`), so the least wall body is 0.5 by 0.15 m.

| Body | Face on | `diameter` | Kind |
|---|---|---|---|
| The shortest wall, 0.5 by 0.15 to 0.30 m | 0.15 m end on, 0.50 m along | 0.52 to 0.58 m | Soft |
| A pine | 0.28 m | 0.40 m | Soft |
| An oak sapling | 0.35 m | 0.49 m | Soft |
| A mature oak | 0.55 m | 0.78 m | Soft |
| A veteran maple | 0.60 m | 0.85 m | Soft |
| A veteran oak | 0.82 m | 1.16 m | Soft |
| A wall 1 m long or more | its band end on | 1.01 m and up | Opaque |
| A BigRock, 1.40 by 1.07 m | 1.07 m | 1.76 m | Opaque |

### 2.2 Kinds

A body's kind is decided once, from its own outline, when it enters the index that holds it, and never from where it is seen:

- A wall body is opaque when its `diameter` is at least `kWallOpaqueLeastDiameterMm` (600 mm), and soft below it. Every wall the tools allow 0.6 m long or more is opaque; only the shortest stubs are soft.
- A placed body is opaque when its `diameter` is at least `kPlacedOpaqueLeastDiameterMm` (1400 mm), and soft below it. That lies between the widest trunk (a veteran oak, 1.16 m across its diagonal) and a BigRock (1.76 m), so every tree is soft and a BigRock is opaque.

The two values differ because the two sources differ. A wall is a built structure whose whole body is opaque by rule 23, and only a piece too small to read as a wall is let through. A placed thing is opaque only when it is large enough that a hard shadow reads as its own, since trees affect sight "very slightly" (rule 24). Which threshold applies is the body's provenance in the collision map (a wall outline from construction or a `kProvenancePlaced` ring), never a list of asset kinds.

The `diameter` is exact: the greatest squared distance between two outline vertices (`Int128` products, `core/Int128.h`), against the threshold squared. A wall graph the band offsetter rejects is outlined as each wall's untrimmed band on its own (`GeometryIndex.cpp`); its body is still the whole connected graph, its outline the set of those band outlines and its `diameter` taken over all their vertices, so a graph's kind follows its walls, not whether the offsetter took them, but for the little an untrimmed end can add across a junction.

A kind changes only when the world does. Felling, placing, or building replaces a ring, and the new ring takes its kind from its own size. A tree whose collider grows between growth stages leaves nav as one ring and enters as another (`queueRemovedDetail`, then a limit's add), so if a stage ever crossed `kPlacedOpaqueLeastDiameterMm` it would become opaque at that change, as if a rock were placed there.

### 2.3 What is measured

Every quantity is taken from the eye: the build's `eyeCentre` in the sim, the colonist's position that frame in the drawing. A mover's centre is held outside blocked rings (routes keep its clearance and static collision pushes it out), so the eye is never inside a placed ring.

- The `silhouette` runs from the direction of the body's extreme vertex on one side to the other's. A body seen from outside its convex hull meets one arc less than 180 degrees wide, whose ends move continuously as the eye moves. A soft body is at most 1.4 m across and the eye is held outside it, so its silhouette is always such an arc.
- `widthAsSeen` is the greatest less the least of `dot(vertex - eye, across)` over the body's vertices, `across` the unit direction perpendicular to the bisector. A square face on gives its side, at its diagonal its diagonal; it is never more than the body's `diameter`. It is continuous in the eye's position.
- `distance` is from the eye to the body's nearest point, `cornerDistance` to the vertex at that end of the silhouette.

### 2.4 Where vision reads them

**Walls.** Walls come from `GeometryIndex` (`libs/sim/vision/`) in the shape the wall sight occluders work gives it (branch `fix/WOR-931-wall-sight-occluders`), and this design depends on that shape. There, `rebuild()` walks `geometry::connectedWallGraphs` and outlines each graph, but stores flat per-segment `OccluderRecord`s (a `SegmentId` source and one edge) with no body. Grouping them into bodies is this design's work: each record carries its body, the graph it came from, and the index keeps one record a body with its edges, box, `diameter`, and kind, made in the same pass. On that branch `VisionSystem::update` and `rememberArea` (`VisionSystem.cpp`) and the sweep's builder (`PlayerVisibilitySystem.cpp`) call `queryFacingOccluders`, and only `StoreOrDrop.cpp` calls `queryOccluders`. With kinds:

- `queryFacingOccluders` returns the edges of opaque wall bodies only, less their back faces when `backFacesHiddenFrom` holds, and both `backFacesHiddenFrom` and `insideSolid` read opaque bodies only. A soft wall body is left out of every sight query, so the visibility polygon never sees it.
- `queryOccluders` keeps every wall, soft ones included: `StoreOrDrop` asks whether an arm reaches a box, and no wall, however short, lets an arm through.
- The structure pass (`rememberStructures`) reads every wall body, soft ones included, so a stub a colonist sees joins memory and, through `segmentsSeen`, the record (rule 22).

**Placed obstacles.** Vision runs at priority 45 and nav at 51. The patch job nav submits at the end of a tick runs on its worker into the next tick until nav's `settle()` collects it, and `navMesh()` joins it, so a vision read of the mesh would stall the main thread on the job. Vision never reads the mesh. `NavigationSystem` holds a **placed occluder index** on the main thread: each placed ring in the mesh with its outline (stored counter-clockwise), box, perimeter, `diameter`, kind, the sim time it arrived, and a **serial** from a counter that never repeats.

- Adds. Nav tracks every gathered placed obstacle by key in `obstacleInMesh` (`NavigationSystem.h`), and each limit's patch hands back the ids it added in its collect callback (`obstacleInMesh.at(keys[k]) = added[k]`, `NavigationSystem.cpp`). The index takes each added ring there, its outline read with `PatchableMesh::ring(id)`, under a new serial. An add the patch refused comes back as `gnav::kNoRing`, and the callback erases its key from `obstacleInMesh`; it never enters the index, so sight and nav agree that it is not there, and a later limit gathers it again.
- Removals. `queueRemovedDetail` erases a felled thing's key from `obstacleInMesh` and queues its ring id; `gatherRemovals` puts the ids in a patch whose callback carries the list of ids it removes (`edits.removes`), and the index drops each there. A count of them is not enough to update the index.
- Order. `gather` takes removals before limits, so one job can free a ring id and hand it to a new tree (`PatchableMesh` reuses a freed id from `freeRings`). `collect()` runs the job's callbacks in patch order, removal first, so the index drops the old ring before it takes the new one. The index and every cache keyed on it go by serial, never by bare ring id.
- `join()` is const and runs from every query, so the index is never touched there; a ring shows to vision from the tick after nav collects it, and a felled tree occludes for that long.
- Arrival. A mover's limit is taken `kSightBufferMeters` (16 m) past his sight, so rings normally enter the index well beyond anyone's sight. When gathers lag (a fast mover at 10x, the add budget spent), a ring can arrive inside an observer's sight. A soft ring then comes in over `kForestArrivalMs` of sim time: its forest depth and its drawn triangle are multiplied by the fraction of that time since it arrived, and while any ring within an observer's `reach` is still coming in his shape is rebuilt at each of vision's passes. An opaque ring casts its shadow from its first build, as a rock placed in sight would.

The index is bucketed by `kOccluderBucketMm` (16 m) squares of the ground, each ring listed in every bucket its box meets, so a build reads the buckets its reach's box meets and no others.

**The readers.**

| Caller | What it reads |
|---|---|
| `VisionSystem::update` (the build) | Opaque wall edges from `queryFacingOccluders`, every wall body in reach for the path (6.1), and placed rings from the index: this document's whole shape (5) |
| `VisionSystem::rememberArea` (the landing, from `GameScene.cpp`) | Nothing: the plain disc (5.3) |
| The sweep's builder (`PlayerVisibilitySystem.cpp`, `SeenArea::setSightBuilder`) | This document's whole shape at its point, its rim given by the record (6.4) |
| `StoreOrDrop` (`StoreOrDrop.cpp`) | Every wall, through `queryOccluders`; no placed ring |
| The cover (7.3) | Each colonist's bodies from his last build, through `VisionSystem::sightBodies` |

`VisionSystem::sightBodies(observer)` returns the bodies the observer's last build read within its `reach`, each as its outline, kind, serial or wall body, and arrival fraction, valid until vision's next pass; the cover copies what it draws that frame. He is within `step` of the build, so every body within `sightRadius` of where he stands is among them.

**Rebuilds.** An observer's cached shape is rebuilt when he moves more than `step` from its build, when the walls' version moves, when the sorted serials of the placed rings within `reach` change, and at each pass while a ring in reach is still arriving. The mesh's epoch cannot stand for that, since movers patch it every 4 m of travel.

The mesh exists wherever an observer can see: every mover meshes the disc of sight plus 16 m round him, and the landing 150 m. Ground a colonist reaches before nav meshes it (a dev teleport) has no placed occluders until the mesh arrives.

## 3. Shadows

### 3.1 Opaque bodies

In the sim an opaque body casts its exact point-eye shadow: its outline's edges go to `geometry::computeVisibilityPolygon` with the opaque walls' facing edges, and sight ends at its near face. A placed ring's edges go in on both faces; the eye is never inside one, and a few rocks in reach cost nothing worth filtering.

On the cover each opaque body draws, from the colonist's position that frame:

- Its **wedge** at 1: the silhouette from where each ray meets the body out to the reach, widened on each side out to the line tangent to both the body and the circle of radius `step` round the eye, on the outer side. Every eye within a step of him, the build's among them, sees the body's shadow inside that line, so the sim's shadow from the last build always lies in the drawn wedge.
- Two **fins**, one beside each widened side: from it out to the line tangent to both the body and the circle of radius `step + edgeWidth / 2` round the eye, the value falling linearly across from 1 to 0. That is a wide eye's penumbra beside the corner, 0 wide at the corner and widening with range.

The wedge's widening is what keeps a hard shadow still at a rebuild. The sim's shadow sides turn by up to `step / cornerDistance` at each build; the ground between the old side and the new is outside the bright shape before or after the build, which reads 1, and inside the widened wedge both times, which also reads 1, so no pixel changes. The cost is a sliver of ground the sim has in sight, `step * (rangeFromEye - cornerDistance) / cornerDistance` wide at most, drawn at full tint.

### 3.2 Soft bodies

A soft body casts no shadow in the sim: a mover behind a trunk is in sight, and is drawn under the tint the triangle gives that ground. On the cover it draws a **triangle**:

```
footValue    = kSoftShadowStrengthPercent / 100 * widthAsSeen / softRayWidth
shadowLength = distance * widthAsSeen / (softRayWidth - widthAsSeen)
progress     = (rangeFromEye - distance) / shadowLength
```

At a `rangeFromEye` between `distance` and `distance + shadowLength`, the triangle holds the directions of the silhouette shrunk toward the bisector by `progress` (each end moved that fraction of the way to the bisector), and on each such ray the ground from where the ray first meets the body outward, so the body's own footprint is in it. Its value there is `footValue * (1 - progress)`: the body's silhouette at its foot, closing to a point at `shadowLength` behind it and fading to nothing there. `softRayWidth` (3 m) exceeds every soft body's `diameter` (under 1.4 m), so `widthAsSeen` is always below it and every triangle closes; the sliders hold `kSoftShadowRayWidthMm` above both kind thresholds.

Two fins, one beside each side of the triangle on the lit side: at a `rangeFromEye`, a fin's outer edge lies `(edgeWidth / 2) * (rangeFromEye - cornerDistance) / cornerDistance` across the ray from the side, the line from the edge of a ray `edgeWidth` wide through the corner. Its value falls linearly across from the triangle's value at that side to 0, and it ends where the triangle does.

The triangle grows and shrinks with `widthAsSeen` as he walks round the body, so a trunk's shadow turns and lengthens with him; its kind never changes, so nothing flips. With the start values, at the foot and on screen, where the tint is black at `kSeenDimAlpha` (0.5, `VisibilityCoverRenderer.h`) so a drawn value darkens visible ground by half its value:

| Body | `widthAsSeen` | `footValue` | Darkest on screen | `shadowLength` at 5 m | at 10 m |
|---|---|---|---|---|---|
| The shortest wall | 0.15 to 0.52 m | 0.025 to 0.087 | 0.013 to 0.043 | 0.26 to 1.05 m | 0.53 to 2.10 m |
| A pine | 0.28 to 0.40 m | 0.047 to 0.067 | 0.023 to 0.033 | 0.51 to 0.77 m | 1.03 to 1.54 m |
| An oak sapling | 0.35 to 0.49 m | 0.058 to 0.082 | 0.029 to 0.041 | 0.66 to 0.98 m | 1.32 to 1.95 m |
| A mature oak | 0.55 to 0.78 m | 0.092 to 0.130 | 0.046 to 0.065 | 1.12 to 1.76 m | 2.24 to 3.51 m |
| A veteran maple | 0.60 to 0.85 m | 0.100 to 0.142 | 0.050 to 0.071 | 1.25 to 1.98 m | 2.50 to 3.95 m |
| A veteran oak | 0.82 to 1.16 m | 0.137 to 0.193 | 0.068 to 0.097 | 1.88 to 3.15 m | 3.76 to 6.30 m |

Each range is face on to the worst angle. The darkest point is the foot; the value falls to nothing along the triangle and to nothing across each fin, so a trunk's shadow is under a tenth of the tint's darkness at its darkest and fades from there.

### 3.3 The drawing

For each colonist in view, every frame, every body in `sightBodies` is drawn from his position that frame. The drawing is float arithmetic and decides nothing. Pieces are combined by the greatest (7.5). The triangle's sides curve slightly on the ground, so it is cut into slices along its length until its sides are within `kDrawnShadowSliceToleranceMm` (5 mm) of the rule and each slice's linear value within `kDrawnValueTolerancePercent` (2%) of it.

The drawing only darkens, and only ground the sim has in sight: every piece is clipped to the colonist's bright shape (7.5). A mover or thing inside a drawn triangle is in sight in the sim, so it draws, under the tint the triangle gives that ground, and can be selected; a click there is ruled by the sim.

### 3.4 A wide eye's geometry and the drawing rule

`shadowLength` is a segment eye's: the two lines from a ray `softRayWidth` wide through a strip's opposite edges cross that far behind it. A fin's outer line is where a segment eye's penumbra beside a corner ends, and the opaque wedge's widening is the same construction with an eye `2 * step` wide. The rest is a drawing rule. A segment eye's umbra narrows in width along a line, where the triangle shrinks in angle about the eye; past the umbra a segment eye's hidden fraction falls toward the strip's share of the ray and never to 0, where the triangle closes to nothing; and its value is that share scaled by `kSoftShadowStrengthPercent`, so that a trunk's shadow is faint by choice and not by geometry.

## 4. Forest depth

### 4.1 The rule

Range in a direction is shortened by how much collidable placed stuff lies that way, as a smooth density, never as each thing's silhouette. Forest depth is taken at each of the 64 sides of the sight polygon (`kSightSides`, `libs/geometry/visibility/SightPolygon.h`), in the direction of the side's normal, halfway between two rim directions:

```
forestDepth(side) = sum over placed rings with centre within sightRadius of
    kForestCapMm * widthFactor(ring) * angleWeight(side, ring) * distanceWeight(ring) * arrival(ring)
range(side, radius) = radius - min(forestDepth(side), sightRadius - floorRange)
floorRange          = sightRadius * kForestFloorPercent / 100
```

- `widthFactor` is `min(1, perimeter / kForestFullPerimeterMm)`, the ring's perimeter against that of a body 1 m wide on average (3142 mm). The perimeter over pi is a convex body's mean width over every angle, so it is fixed per ring and needs no angle: a pine 0.36, a mature oak 0.70, a veteran oak or a BigRock 1.
- `angleWeight` is a tent: 1 where the side's normal points at the ring's centre, falling linearly to 0 at `kForestWindowSteps` rim steps away (8 steps, 45 degrees).
- `distanceWeight` is `min(1, centreDistance^2 / kForestNearMm^2) * (1 - centreDistance^2 / sightRadius^2)`, `centreDistance` from the eye to the ring's centre. It is 0 at the eye, rises to its greatest, 0.89, at `kForestNearMm` (10 m), and falls to 0 at the sight radius, so a ring coming into sight adds nothing at first, and a ring beside the eye adds little.
- `arrival` is 1 but for a ring still arriving (2.4).

Every placed ring spends forest depth, opaque or soft: a BigRock is a hard shadow and a little forest depth round it, as any thing in the way is. Built walls spend none; they are opaque bodies with exact shadows or stubs a colonist sees past.

### 4.2 The forest shape

The **forest shape** of a radius round a point is the intersection of 64 half-planes, one a side: the half-plane whose edge is parallel to the sight polygon's side and lies `range(side, radius) * cos(pi / kSightSides)` from the point. With every range equal it is the sight polygon of that radius, to 2 mm. It is convex by definition, holds the sight polygon of `floorRange` round the point (every side lies at least that far out), and is the one outdoor sight shape: what colonist knowledge, `isVisible`, the bright shape, and the record read (5, 6).

Convexity takes ground where the ranges dip. Where a side's range is less than its neighbours', its edge cuts across the neighbouring directions, so a dip is widened and flattened into a straight side:

| Case | What convexity takes |
|---|---|
| A lone veteran oak or BigRock at any distance | Nothing: its tent, 0.71 m deep at most and 45 degrees each side, is convex already |
| Random stands, mature oaks 5 m apart or pines 3 m apart | 0.04 m off the mean range; in the worst direction 0.3 m on average, at most 0.6 m |
| Random stands, veteran oaks 4 m apart or mature oaks 3 m apart | 0.1 to 0.2 m off the mean range; in the worst direction 0.7 to 1 m on average, at most 1.7 m |
| The worst case: a clump in one direction pulling one side to the floor, its neighbours untouched | The floor side's edge runs straight across about 50 degrees each side of the dip, taking up to 8.2 m where the tent would have left full sight |

The stand figures are from the author's simulation of uniform random stands with the start values, 40 viewpoints each; the prototype measures the landing's forest (12).

The forest shape is integer arithmetic throughout (4.4), and its vertices are the crossings of consecutive kept edges, rounded toward the point and pulled in a millimetre at a time while any half-plane fails, as `sightPolygon` pulls its vertices in.

### 4.3 What it gives

**A lone tree.** One ring's depth across any side is at most `kForestCapMm * widthFactor * 0.89`: 0.26 m for a pine, 0.50 m for a mature oak, 0.71 m for a veteran oak or a BigRock, straight at it, tapering to nothing 45 degrees either side.

**Stands.** For trunks at `density` to the square metre with mean `widthFactor`, the expected depth is `density * widthFactor * kForestCapMm * windowAngle * 201 m^2`, where `windowAngle` is the tent's half width in radians (0.785) and 201 m^2 is the integral of `centreDistance * distanceWeight` over 0 to 30 m. That is about `158 m^2 * density * widthFactor` times `kForestCapMm`. With the start values, from where he stands:

| Stand | Rim |
|---|---|
| Mature oaks 5 m apart | 26.5 m |
| Pines 3 m apart | 24.9 m |
| Veteran oaks 4 m apart | 22.1 m |
| Mature oaks 3 m apart | 20.2 m |
| Pines 2 m apart, veteran oaks 3 m apart | 20.1 m, the floor |

The floor is `floorRange`, 20.1 m from where he stands. On the polygon path the shape is built to `reach` round the build, so its floor is 20.6 m from the build and at least 20.1 m from where he stands.

**Continuity.** Every term is continuous in the eye's position: a ring's centre direction, since the eye is never at it; its tent, linear in the side's place in the table (4.4), which rises steadily with angle; `distanceWeight`, a polynomial in the squared distance; and a ring entering or leaving sight does so at weight 0. So every side's range is continuous as a colonist walks, and so is the forest shape, whose edges move with them. A ring felled or placed changes it by at most that ring's depth, as any change to the world does.

**The change per step.** Walking `step`, a ring's centre direction turns by at most `step / centreDistance`, and its tent changes by that over the window, while `distanceWeight` changes by at most `2 * step / kForestNearMm`. The sum is greatest at `kForestNearMm`, where it is `step / (windowAngle * kForestNearMm) + 2 * step / kForestNearMm`: 0.064 + 0.100, so one ring changes one side's range by at most 0.164 of `kForestCapMm * widthFactor` a step, 0.13 m for a veteran oak or a BigRock. Several rings near 10 m in one window add their changes. In the author's simulation the greatest change of one side between builds was 0.23 m through mature oaks 5 m apart and 0.43 m through veteran oaks 4 m apart; the visibility state view reports it (8), and the acceptance limits bound it (10).

### 4.4 Integers

Forest depth is integer arithmetic: millimetres, fractions in `kFractionOne` (65536) parts, and a side's place in the table. It is the same on every toolchain, uses no trig, exp, or log, and reads the integer square root only when a ring enters the index, for its perimeter.

- A ring's position is `index * kFractionOne + place`, `index` the table direction at or before the ring's centre (found by the sign of a cross product) and `place` = `cross(direction[index], offset) * kFractionOne / (cross(direction[index], offset) + cross(offset, direction[index + 1]))`, `offset` the centre less the eye. The table is at scale `2^30` and offsets are under `2^16` mm (a static check holds `reach` under it); the offset lies within 5.625 degrees of `direction[index]`, so the first cross product is under `2^30 * 2^16 * sin(5.625 degrees)`, under `2^43`, and the numerator under `2^59`, which fits `int64`. The denominator is positive and under `2^47`.
- Side `side` lies half a step past direction `side`, so its tent reads the ring's position against `side * kFractionOne + kFractionOne / 2`.
- `centreDistance^2` is under `2^31`, and each weight's numerator under `2^47`. A ring's weight, `kForestCapMm` under `2^20` times four fractions, is under `2^20` mm, each product divided by `kFractionOne` before the next and under `2^36`; the tent's product is under `2^37`, and a sum over `2^16` rings under `2^36`.
- A ring whose weight rounds to 0 mm is skipped.
- A side's half-plane is `dot(point - eye, normal[side]) <= range(side) * kSightCosHalfSide / 2^15`, `normal` a compile-time table of the 64 side normals at scale `2^15` (the odd entries of `directionTable<128>`) and `kSightCosHalfSide` the sight polygon's (`SightPolygon.h`). The product `range * kSightCosHalfSide` is under `2^46`, and both sides of the test under `2^33`. A vertex is the crossing of two kept edges by Cramer's rule, numerators under `2^49` and the determinant under `2^31`, in `int64`.

```cpp
const std::int64_t sides = static_cast<std::int64_t>(geometry::kSightSides);
const std::int64_t windowUnits = tunables.forestWindowSteps * kFractionOne;
for (const PlacedOccluder& ring : ringsInSight) {
    const geometry::Vec2i64 offset = ring.centre - eyeCentre;
    const std::int64_t weightMm = tunables.forestCapMm * ring.widthFactor / kFractionOne * distanceWeight(offset, sightMm, tunables.forestNearMm) / kFractionOne * ring.arrival / kFractionOne;
    if (weightMm == 0) {
        continue;
    }
    const std::int64_t position = tablePosition(offset);
    const std::int64_t first = position / kFractionOne - tunables.forestWindowSteps;
    for (std::int64_t side = first; side <= first + 2 * tunables.forestWindowSteps; ++side) {
        const std::int64_t offsetUnits = std::abs(side * kFractionOne + kFractionOne / 2 - position);
        if (offsetUnits < windowUnits) {
            const auto wrapped = static_cast<std::size_t>((side % sides + sides) % sides);
            forestDepthMm[wrapped] += weightMm * (windowUnits - offsetUnits) / windowUnits;
        }
    }
}
for (std::size_t side = 0; side < geometry::kSightSides; ++side) {
    rangeMm[side] = radiusMm - std::min(forestDepthMm[side], sightMm - floorMm);
}
```

## 5. The sight shape

### 5.1 The build

Each build reads the bodies within `reach` and the placed rings within `sightRadius`, and takes one of three paths (6.1):

- With no body in reach, sight is the plain sight polygon and no shape is built, the outdoor fast path.
- With placed rings in sight, all soft, and no wall in reach, the shape is the forest shape of `sightRadius` round the build, carried with him until the next build: his sight at any moment is that polygon placed where he stands.
- With an opaque body or any wall in reach, the shape is `geometry::starClippedToConvex(hardPolygon, eyeCentre, forestShape(eyeCentre, reach))`, the hard polygon being `geometry::computeVisibilityPolygon` over the opaque bodies' edges to `reach`. It stays round the build, and everything vision decides with it is held to `sightRadius` round where he stands (player-visibility.md 3.2), as with walls.

Any wall in reach takes the third path, soft or opaque, because seen walls enter the record from new polygons: `enterStructures` runs only when a new polygon arrives (`PlayerVisibilitySystem.cpp`).

`computeVisibilityPolygon` is star-shaped only up to the millimetre snapping of its hits and is not promised simple (`Visibility.h`); clipped to a convex ring it stays so. Its result is checked with `geometry::simpleAround`, as `PlayerVisibilitySystem` checks vision's polygons before the record queues them: a shape that fails, which only the snapping can cause, serves memory and `isVisible`, which test points against it, and is not queued to the record; the next build's is.

### 5.2 What uses it

`sightShape(observer)` carries, beside the centre, radius, polygon, revision, and the walls and openings seen, the build's path and the 64 ranges of its forest shape, which a leg reads (6.2). `polygon` is set on the polygon path.

Memory, discovery, and `isVisible` test that a thing lies in the shape: on a leg, the forest shape placed where he stands; on the polygon path, the shape round its build and within `sightRadius` of where he stands. The forest shape is convex, so its test is one side test after a binary search by angle; the polygon path's shape is star-shaped round its build and takes the same search.

`VisionOverlay` (V) draws each observer's shape, the forest shape and the hard polygon in two colours, each body coloured by kind, and the soft ray's width at the build.

### 5.3 The landing

`PlayerVisibilitySystem::revealLanding` commits a plain disc of `kLandingSightMeters` (150 m). In this design `VisionSystem::rememberArea`, whose only caller is the landing (`GameScene.cpp`), makes no occluder query and builds no shape: it gives every crew member the plain disc's things. Rules 8 and 17 say the crew saw the landing as they came down, so the seen area and what the crew knows agree.

## 6. The seen record

The record is player-visibility.md's: an exact polygon union in a second `PatchableMesh`, star-shaped shapes in, the covered test, the frontier, the sweep, and the skirt (3.3 there).

### 6.1 Three paths

| At the build | Path |
|---|---|
| Nothing in reach | An open leg: the forest shape is the sight polygon, so this is today's open leg |
| Placed rings in sight, all soft, no wall in reach | A leg (6.2) |
| An opaque body or any wall in reach | The polygon path: each shape queued with its walk, the skirt, frontier pieces, and the sweep (6.4) |

An open leg and a leg are one mechanism. A leg ends when the path changes; the record only grows, so nothing is lost at the change. A BigRock in the landing's forest (`spawnChance` 0.004, `minDistance` 15 m in TemperateDeciduousForest, `BigRock.xml`) puts every build within `reach` of one on the polygon path, so how builds split between legs and the polygon path in forest is a number the prototype measures (12).

### 6.2 Legs

A leg holds the shapes he walked with. Its ring is the convex hull of every shape it has taken, each placed along the leg as an open leg's ends are and each the forest shape of `sightRadius - kSeenSlackMm` (today's open leg's radius) with the ranges of the build it came from. Between builds, the ring is the hull of what it held at the last build and the current shape placed at the leg's end; at a build, the new shape joins at that point. An open leg's ring is today's capsule, the hull of one polygon at two ends.

A leg ends by an open leg's rules (a turn, a turn back, a jump, or a change of path), when a build's range on some side differs from that side's least range over the leg by more than `kSoftLegToleranceMm`, or when its ring would pass `kSoftLegMostVertices` vertices. The next leg starts at the new build with its ranges.

Within those rules the leg keeps both containments of player-visibility.md 3.1:

- Drawn inside seen, exactly. On a leg the bright shape is the forest shape of `drawnVisibleRadiusMm(sightRadius)` with the build's ranges, placed where he stands. By `drawnVisibleRadiusMm`'s argument (`SeenArea.h`), every position he holds lies within `kSeenSlackMm * sqrt(2)` of the stretch of the leg walked with the current shape, so the bright shape lies in that shape of `sightRadius - kSeenSlackMm` placed at the nearest point of the stretch, which the ring holds. The ring is pending from the frame it holds a point, so no bright ground is ever unseen.
- Seen inside swept sight, to the tolerance. Every shape on the leg lies inside the shape whose sides take each side's greatest range over the leg, and that shape lies within the tolerance of every one of them on every side. The hull of shapes along a straight leg therefore lies inside that greatest shape swept along the leg, which is within `kSoftLegToleranceMm / cos(half the sharpest corner of the leg's shapes)` of a shape some position on the leg held: about 1.1 times the tolerance, 0.55 m with the start value. The record can hold ground that far past where sight reached on a leg, as the skirt lets it reach past a shadow edge.

`isVisible` reads his whole sight shape, so ground in the band between the bright shape and his rim, `sightRadius - drawnVisibleRadiusMm(sightRadius)` wide, can be visible to the input rules and not yet in the record, for as long as he holds it in sight, as on an open leg today. That band is under the tint's feather and drawn as seen ground.

The ring only grows: each shape joins the hull, and the hull of a set holds the hull of any part of it. A shape joins only at a position he held, so the ring holds no ground but the shapes he walked with and the hull between them.

**What a forest costs a leg.** Forest depth drifts as he walks among trees: in the author's simulation of random stands with the start values, legs averaged 2.4 m through mature oaks 5 m apart, 2.6 m through pines 3 m apart, and 1.4 to 1.5 m through veteran oaks 4 m apart and mature oaks 3 m apart, against unbounded in the open. Each leg's ring goes through the frontier when it ends, so a short leg costs about a polygon-path shape without the sweep. That record cost is the risk the prototype measures (12), with 6.6's fallbacks.

### 6.3 The record's interface

- `SeenArea::setOpenLeg(leg, start, end, ranges)` takes the build's 64 ranges in place of a radius (`SeenArea.h`), and joins that forest shape to the leg's ring at `end`; `legCapsule` becomes the hull of the leg's held shapes and the shape at its end, merged in one pass over their sides. An open leg passes every range at `sightRadius - kSeenSlackMm`.
- `SeenArea::queueSightPolygon` takes the shape's rim (6.4) with the shape and its walk.
- `SeenArea::appendEdges(low, high, out)` gives the record's edge in a box: every live triangle edge with nothing across it, as `(start, end)` with seen ground on the left, through `PatchableMesh::meshEdgesNear`, since the record's mesh is private. The cover's feather reads it (7.2).
- `SeenArea::inRecord(point)` is whether the committed record alone holds the point, by its mesh's `locate`. `isSeen` counts the pending shapes too, and the feather's cut needs the two apart (7.2). Tests read the committed record through it.
- `SeenArea::takeChangedBoxes(out)` gives the boxes of the rings registered since the last call, from which the feather's pieces are rebuilt (7.2).
- `PlayerVisibilitySystem` holds each observer's path: leg or polygon path.

**The bright shape.** `PlayerVisibilitySystem::drawnVisible` gives:

- on a leg, the forest shape of `drawnVisibleRadiusMm(sightRadius)` with the build's ranges, round his `Position` that frame (with every range equal, today's disc);
- on the polygon path, vision's shape cut by `starClippedToConvex` to the sight polygon of `drawnVisibleRadiusMm` round his current `Position`, as today's code cuts it (`PlayerVisibilitySystem.cpp`), while the observer's path took this revision. After a jump, nothing until vision builds the next.

On the polygon path the bright shape can reach up to a step past the record ahead of him, onto ground nothing holds until the next build; there it is under the unseen fill, which is opaque and drawn over the tint, so what shows bright is seen by the fills' order (player-visibility.md 3.1), as with walls today.

### 6.4 The polygon path's rim, the skirt, and the sweep

On the polygon path a shape's **rim** is the convex ring it is cut to for the record: the forest shape of `sightRadius` round its build. With no placed ring in sight that is the sight polygon, today's rim.

- What the record takes is the shape cut to its rim by `starClippedToConvex`, as today's polygons are cut to sight.
- The skirt is `geometry::skirtedSightShape(shape, centre, rim, kSeenSkirtMm)` (`SightSkirt.h`), which takes any convex ring round the centre that the shape lies in: the sides along the rim, the forest shape's or the sight polygon's, are not grown, and only shadow sides and body faces are, of walls and opaque bodies. Seen ground reaches past a true shadow edge by up to the skirt, as rule 21 allows with walls, and now with opaque bodies too.
- A shape is swept when its frontier pieces reach within `kSeenSweepRimBandMm` of its rim, measured against the rim above.
- A sweep point's rim is the forest shape whose range on each side is the lesser of the point's own forest depth's (the sight builder computes it at the point) and the linear interpolation of the walk's two builds' ranges by where the point lies between them, less the sweep's inset. With every range equal that is the sight polygon of sight less the inset round the point, today's sweep. The inset is today's, `apart * pi / kSightSides + apart^2 / (2 * sightRadius)`, with `apart` the farthest any vertex of the point's rim lies from the same vertex of the nearer build's rim; with every range equal that is the point's distance to the build, today's case. The interpolated rim shares the 64 side normals of both builds' rims and lies inside their hull, so its rim vertices stand past the builds' rims by no more than today's sight polygons do past each other, and the inset holds them in.
- `SeenArea::setSightBuilder` takes a builder of `(centre, rim)` that returns the shape at the point, cut to the rim, with this document's opaque bodies and walls standing now.

### 6.5 Star-shaped shapes

The skirt (`geometry::skirtedSightShape`), the frontier (`sightShapeFrontier`), the covered test's fan, and `simpleAround` all take a shape star-shaped round its centre. Every shape the record takes is: a leg's ring is a convex hull, and the polygon path's shape is a star-shaped polygon cut to a convex rim round its build and checked with `simpleAround` (5.1).

### 6.6 What a forest costs the record

Legs per kilometre and their lengths, the share of builds on each path, rings registered per kilometre and per minute, the sweep's builds on the polygon path (every 100 mm of a walk that opens ground near its rim, `kSeenSweepSpacingMm`), the registry's bytes per kilometre, and the record's tick are what the prototype measures at 1, 10, and 50 colonists (12), against player-visibility.md 6.5 and 6.7.

If the record costs too much in forest, the fallbacks in order:

1. The sweep skipped where every opaque body in reach is placed: the seen edge past a rock's shadow then steps with vision's builds, 0.5 m.
2. A larger `kSoftLegToleranceMm`, so fewer builds end a leg: the record then holds ground up to about 1.1 times that past where sight reached on a leg.
3. The record's legs taking the sight polygon in place of the forest shape, the record holding ground the forest left out of sight: a departure from rule 27.

## 7. The cover

### 7.1 Two masks

The cover's two fills and their order are player-visibility.md 3.4's: the tint (black at `kSeenDimAlpha`) first, the opaque unseen fill over it. Each draws with a mask whose value at a point is how much of the fill shows there. With `fade(depth) = max(0, 1 - depth / kSightFeatherMm)`:

- The unseen fill's mask is 1 outside the seen area (the record and the pending shapes), and inside it `fade` of the distance to the seen area's edge (7.2).
- The tint's mask is the least, over the colonists, of each colonist's **darkness**: 1 outside his bright shape (6.3), and inside it the greatest of `fade` of the distance to his bright shape's rim and his drawn shadows (3). Light, one less the tint's mask, is the greatest light any colonist gives a point (7.4).

The fade lies inside the brighter area on both edges: nothing unseen is ever shown even faintly, and the fully bright core of a 30 m sight in the open is about 28.5 m round him. Sight, the record, and the bright shape are exact vector shapes; the fade is drawn and decides nothing. A click on ground under it is ruled by the record and by `isVisible`, never by the drawing.

### 7.2 The feather

`fade` of the distance to an area's edge is drawn exactly as the greatest of pieces, one per edge of the area's boundary and one per reflex corner: an exact inward offset, combined by the greatest. An edge's piece is the rectangle from the edge `kSightFeatherMm` into the area, 1 on the edge and 0 on its far side. A corner where the area's inside angle exceeds 180 degrees takes a fan of radius `kSightFeatherMm` round it over the gap between its edges' rectangles, 1 at the corner and 0 on the arc, in triangles of at most `kFeatherFanStepDegrees` (15 degrees), whose chords then lie within 1% of the fade. Inside the area the greatest of these is `fade` of the distance to the boundary, because the nearest boundary point to any point inside is a perpendicular foot on an edge or a reflex corner. Where a rectangle runs past the area's boundary it lies where the mask is 1 already. Where the area is narrower than twice the feather the pieces of its two sides overlap and the greatest is the nearer side's, so a beam of sight through a 1 m doorway is never fully bright and is never darkened twice.

This holds only when the pieces are drawn for the area's boundary and for nothing else: an edge two shapes share, or one inside another shape, must draw no piece, or it draws a dark seam inside the area.

**One colonist's bright shape** is one star-shaped polygon, so its boundary is its sides and needs no cut. On a leg every side is rim; it draws a feather piece, and the feather is built at each build and moved with him between builds, as the open path's annulus is. On the polygon path the build tags each side as rim, shadow side, or body face. A rim side draws a feather piece. A shadow side, running out from an opaque body's corner, draws none: its soft edge is the body's fin (3.1). A body face draws none: the darker side is the body, a wall's or a rock's footprint, so no fade darkens the ground in front of it.

**The seen area** is the record's union with the pending shapes, so its boundary is found on the CPU, exactly and in integers: the record's edge (`appendEdges`) and the pending shapes' sides, each cut where it meets the others, a piece kept when the point just off its middle on its outer side lies in no other shape (the record by `inRecord`, a pending shape by its outline; a point on another shape's side counts as inside that shape when the shape lies on the outer side). The record's edge is the edge of its whole union, however many meshes the record is uploaded in, so several record meshes leave no seam.

- The record's pieces are kept in cells of `kSeenFeatherCellMm` (32 m), one uploaded mesh a cell, in as many meshes as 16-bit indices need. When `epoch()` moves, the cells meeting a box from `takeChangedBoxes` grown by the feather are rebuilt from `appendEdges` over the cell grown by the feather, and the rest are untouched: a registered ring changes the union's boundary only inside its own box, and a boundary edge the mesh's patch splits at a point within it draws the same rectangles and no fan, since the split point is straight. Pieces register one a tick (player-visibility.md decisions 13), so an epoch moves at most once a tick. One registered ring's box can be large: a closed leg's ring is as long as the walk, so an epoch can rebuild every cell along a long leg, and the cost of an epoch is the record's edges in those cells. The prototype measures cells rebuilt an epoch (12).
- Inside the feather of a pending shape the record's pieces are cut against it. The uploaded pieces are drawn only outside the pending shapes grown by the feather, a hard mask of their outlines, and inside that region the cut pieces are drawn with the pending shapes' own. A queued shape's cut is made once when it is queued and dropped when it leaves the queue. A leg's ring changes each frame only in the sides that join its held hull to the shape at its end, so its cut is redone each frame within the box of those sides grown by the feather, and where a changed box of the record meets it. With every range equal those sides are fixed lines and the box is the end's; in forest the sides can pivot from far back along the leg, which the leg's tolerance keeps short. The prototype measures the per-frame cut (12).

### 7.3 One colonist's drawn shadows

The pieces of 3, for each colonist whose sight's box meets the view, built each frame from his position over his `sightBodies`: a triangle and two fins per soft body, a widened wedge and two fins per opaque body. Values that are products, a fin's fall across times a triangle's fade along, are evaluated at the pieces' vertices and subdivided until each triangle's linear value is within `kDrawnValueTolerancePercent` of the product. Pieces are never recorded and never read by the sim.

### 7.4 Several colonists

A colonist's darkness is 1 outside his bright shape and inside it the greatest of his feather and his drawn shadows. The tint's mask is the least darkness over the colonists, so light is the greatest light any colonist gives a point, and one colonist's drawn shadows never darken ground another colonist lights: where another lights it fully, the least is 0. It is drawn in a number of passes linear in the colonists in view, with no layer per pair (7.6). The prototype proves it at 50 colonists in view (7.7).

**The lens.** Each colonist's feather fades from his own rim. Where two bright shapes cross, the ground near both rims is lit by the brighter of two fades, each from a rim the other shape covers, and so is darker than a fade from the union's edge would be. A lens where two sights overlap by under twice the feather is never fully bright, and from each point where two rims cross a dimmer wedge runs into the overlap, longest where they cross at a shallow angle. A fade from the union's boundary would need that boundary found on the CPU every frame for every colonist in view, whose shapes move every frame; this design takes the dimmer lens instead (7.8).

### 7.5 Composition

Each fill's mask is hard layers with one layer of partial coverage on top. The unseen fill:

```cpp
std::vector<MaskShape> unseenFeather;
for (const RecordFeatherCell& cell : recordFeatherCellsInView) {
    for (const InstancedMeshHandle& mesh : cell.meshes) {
        unseenFeather.push_back({.mesh = {mesh, camera}, .mask = outsidePendingFeather, .blend = MaskBlend::Lighten});
    }
}
for (const FeatherPiece& piece : pendingEdgePieces) {
    unseenFeather.push_back({.points = piece.screenPoints, .paint = MaskPaint::linear(piece.from, piece.to, 1.0F, 0.0F), .blend = MaskBlend::Lighten});
}
std::vector<MaskLayer> unseenMask = {
    {.image = MaskImage::content(unseenFeather), .mode = MaskMode::Luminance},
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

Layers are listed top first and composited bottom up, as CSS does. Below the top, the hard layers are the view less the union of the record's meshes and the pending shapes, 0 or 1 at each pixel. The top layer is the partial coverage, its children combined by `lighten`, the greatest at each pixel, so the record's pieces and the pending pieces overlapping draw no darker than either. It is added by `MaskComposite::Add`, which is source over: where the layers under it are 1 the result is 1, and where they are 0 it is the partial coverage, so between a binary layer and any other source over is the greater of the two, and nothing is darkened twice.

The tint's mask is one partial layer: the view at 1, then one isolated group a colonist, combined with what is under it by `darken`. A colonist's group holds his bright shape at 0 and over it his feather pieces and drawn shadows, combined among themselves by `lighten`, each piece carrying a hard mask of his bright shape:

```cpp
std::vector<MaskShape> tint = {{.points = viewPoints, .paint = MaskPaint::flat(1.0F)}};
for (const ColonistCover& colonist : colonistsInView) {
    const std::vector<MaskLayer> insideBrightShape = {{.image = MaskImage::polygon(colonist.brightShape)}};
    MaskShape group{.blend = MaskBlend::Darken, .isolation = MaskIsolation::Isolate};
    group.children.push_back({.points = colonist.brightShape, .paint = MaskPaint::flat(0.0F)});
    for (const DarknessPiece& piece : colonist.pieces) {
        group.children.push_back({.points = piece.screenPoints, .paint = piece.paint, .mask = insideBrightShape, .blend = MaskBlend::Lighten});
    }
    tint.push_back(std::move(group));
}
Primitives::drawRect({.bounds = view, .style = {.fill = kTintColor}, .mask = {{.image = MaskImage::content(tint), .mode = MaskMode::Luminance}}, .key = coverKey});
```

In SVG terms each piece is an element inside the group with its own mask, so it paints only inside the bright shape. Without that clip a fin or a triangle reaching past the bright shape would put a value under 1 in the group where it holds nothing else, and `darken` against the view's 1 would take that value, lightening ground the sim has out of sight. With it the group covers only the bright shape: outside, the group is empty and the view's 1 stands; inside, it is the greatest of his pieces over 0, his darkness, and `darken` takes the least of that and what the colonists before him left.

### 7.6 Masks with partial coverage (salal)

salal's masks (`MaskImage.h`, salal `docs/masks.md`) are layers of `rect`, `polygon`, and `mesh` images composited by `MaskComposite::Add` and `Subtract`, each 0 or 1 at a pixel. Partial coverage takes CSS's and SVG's names and semantics:

- `MaskMode`, CSS `mask-mode`, per layer. `Alpha` is the default and what every hard layer uses. `Luminance` reads the layer's content as luminance; a hard layer's content is white where it covers, so the two agree on it.
- `MaskImage::content(children)`: an SVG `<mask>` whose content is the children. A child is a `<polygon>` (`.points`), an uploaded mesh under a camera (`.mesh`), or a group of children (`.children`, an SVG `<g>`). It is filled flat with a value (`MaskPaint::flat`) or by an SVG `linearGradient` in user space with two stops (`MaskPaint::linear(from, to, fromValue, toValue)`). A mesh child's vertices may each carry a value: a field linear over a triangle is one two-stop linear gradient, so each triangle is a `<polygon>` filled by the gradient its three values define. A child may carry its own `mask` of hard layers, as an element inside an SVG `<mask>` may.
- `MaskBlend`, CSS `mix-blend-mode`, per child: `Lighten`, the greater of its value and what is under it, and `Darken`, the lesser. `MaskIsolation::Isolate`, CSS `isolation: isolate`, makes a group composite its children together before it blends with what is under it.
- `MaskComposite::Add` and `Subtract` keep CSS's meanings on partial values: `add` is source over, `subtract` is source out.

Alpha mask layers (rule 26) are masks whose value at a point is a coverage between 0 and 1, which salal carries as alpha. CSS composites alpha only by its Porter-Duff operators, none of which is the greater or the lesser of two values; `lighten` and `darken` are, and they blend colour, so a layer whose content combines values that way is read as luminance, its content painted in grey and the grey the coverage. The coverage it yields masks the draw as an alpha mask's does.

How salal evaluates partial coverage on the GPU is named nowhere in the API. The GL backend evaluates it this way:

- The hard layers are in the stencil's two bits.
- The partial layer is accumulated in the target's destination alpha. salal blends every channel with `GL_ONE, GL_ONE_MINUS_SRC_ALPHA` (salal `GLBackend.cpp`, `applyBlend`), so after the world draws, destination alpha holds whatever the world left, and the cover owns it: before each fill it writes the layer's starting value over the view with only alpha written, combines its children with a max or a min blend on alpha, draws the fill weighted by destination alpha, and when the cover is done writes alpha back to 1 over the view, so nothing drawn after it reads cover values.
- An isolated group's own greatest is kept in the depth buffer. Depth is cleared at the cover's start. For each colonist: his bright shape is drawn to a stencil bit and to depth 0; his pieces are drawn with each piece's value as its depth and the depth test keeping the greater, colour off, inside that bit; then the same pieces and the shape are drawn again with the depth test passing only where a piece's depth equals what is stored, writing their values to destination alpha with a min blend; the shape clears the bit last. Only the piece that holds the greatest passes the equal test at each pixel, so destination alpha takes the least over colonists of each colonist's greatest. The two passes share a vertex shader with an invariant position, so the equal test is exact. Each colonist costs his shape three times and his pieces twice, linear in the colonists.

This needs backend work in salal, all of it step 0's (13):

- Blend states. `BlendState` has only `Alpha` (salal `Backend.h:68`); the cover needs a max and a min blend equation (`GL_MAX`, `GL_MIN`) and the weighted draw against destination alpha.
- Channel writes. Colour writes are all or nothing (`applyColourWrites`, `GLBackend.cpp:132-135`); the cover needs alpha written alone, and colour and alpha both off for the depth pass.
- Depth. `applyFixedState` disables the depth test (`GLBackend.cpp:219`); the cover needs a depth state with depth writes and a greater-or-equal test, and one with an equal test and no writes.
- A depth clear. The backend clears colour (`GLBackend.cpp:239`) and stencil (`:248`) only; the cover needs depth cleared at its start.
- Targets. The window asks GLFW only for stencil bits (`GLFW_STENCIL_BITS` 8, salal `libs/app/platform/GlfwPlatform.cpp:593`); GLFW's defaults already give 8 alpha and 24 depth bits, and step 0 states them and checks at start that they are there. `createRenderTarget(colour, withStencil)` attaches `GL_DEPTH24_STENCIL8` only with stencil, so a target the cover draws into is made with stencil and an alpha-carrying colour texture.

### 7.7 The prototype and its fallbacks

Step 0 is a sandbox scene of the cover's cases over a busy background: two record cells sharing an edge, a pending piece abutting the record along its edge, a pending leg overlapping it, two bright shapes overlapping and two abutting, a 1 m beam, a reflex corner, a soft body's triangle and fins, an opaque body's widened wedge and fins, a piece reaching past its colonist's bright shape, one colonist's shadow inside another's sight, and 50 colonists' shapes overlapping in one view with their shadows. A test reads the mask's value at chosen pixels (cores, mid-fade, overlaps, abutting edges, the shared cell edge, a reflex corner, a shadow under another colonist's light, the clipped piece's far side) against 7.1 to 7.5, and GPU time is taken at 1, 10, and 50 colonists. No golden image.

If the target has no destination alpha or depth, or the backend cannot hold those values, the fallbacks in order:

1. Each colonist's pieces made disjoint on the CPU each frame, an overlay of their outlines with each cell carrying the greatest of the pieces over it, subdivided to within `kDrawnValueTolerancePercent`, so that the min blend on destination alpha alone gives the least over colonists with no depth pass.
2. The partial layers drawn as fill-coloured geometry carrying alpha, source over, under the stencil's hard result. Where two pieces overlap (inside the feather of a convex corner, a narrow beam, a shadow over a feather) it darkens to `first + second - first * second` in place of the greater, and where two colonists see a point one's shadow darkens the other's light.
3. Hard edges, as player-visibility.md 3.4 draws them, with the sight shape and the record of this document.

### 7.8 Consequences taken on purpose

- Narrow seen and visible ground, a doorway's beam or a strip between two shadows, is never fully bright or fully tinted.
- Where two colonists' sights overlap by under twice the feather the overlap is never fully bright, and a dimmer wedge runs in from each point where their rims cross (7.4).
- A body face, a wall's or a rock's, is a hard edge in the tint, and a shadow side is hard at its corner, its fin widening from there.
- Beside every opaque body's shadow a sliver the sim has in sight is drawn at full tint, up to `step / cornerDistance` wide in angle, so the hard shadow does not move at a rebuild (3.1).
- The forest rim is convex, so a deep dip in forest depth is a straight side across it (4.2), and the bright rim moves at each build by the change of its ranges since the last, at most 4.3's change per step, under the feather.
- The draw bound and the terrain scissor are player-visibility.md 3.4's: the fade lies inside the record, so nothing past it shows.

## 8. Input, things, and dev tools

The input rules (player-visibility.md 3.7) read the record and `isVisible`, which reads vision's shape. A mover that is not a colonist shows while `isVisible` holds: behind a BigRock it is out of sight; behind a trunk or a wall stub it is in sight, drawn under the triangle's tint; past the forest rim it is out of sight.

The visibility state view (`/api/state?what=visibility`) reports: builds by path (leg or polygon path); legs held, and ended by a forest-depth spread, by vertices, and by an open leg's rules; placed rings in sight and opaque bodies in reach, summed over the last builds; the greatest change of one side's range between consecutive builds of one observer, and the ring it came from; rings that arrived inside an observer's sight; sweep builds; record epochs a second and feather cells rebuilt; and shapes that failed `simpleAround`. With `&bodies=1` it also lists each colonist's `sightBodies`: kind, `diameter` in millimetres, centre, and for a placed body its def name, so a scenario can find a BigRock. `/api/dev/reveal` is player-visibility.md's.

Every tunable in 9 not marked fixed is a field of one runtime struct, `SightTunables`, whose defaults are the start values and which vision and the cover read each pass and each frame. Each is a slider on the Dev Tools tab, so the prototype tunes them with the owner watching. A change of a sim tunable re-decides every body's kind where a threshold moved and rebuilds every observer's shape at once; the slider for `kSoftShadowRayWidthMm` cannot go below the larger kind threshold.

`/api/dev/spawn` makes an ECS entity (`PlacementSystem::spawnEntity`, rotation 0) that never enters the placement index nav gathers obstacles from, so a spawned BigRock or tree is no body; tests and scenarios use placed trees and rocks, and built walls.

## 9. Tunables

| Name | Start | What it sets |
|---|---|---|
| `kWallOpaqueLeastDiameterMm` | 600 mm | A wall body at least this across is opaque, below it soft. The shortest wall the tools allow is 0.52 to 0.58 m across |
| `kPlacedOpaqueLeastDiameterMm` | 1400 mm | A placed body at least this across is opaque, below it soft: between a veteran oak (1.16 m) and a BigRock (1.76 m) |
| `kSoftShadowRayWidthMm` | 3000 mm | The ray a soft body's triangle is drawn with: wider shortens every triangle. Above both thresholds |
| `kSoftShadowStrengthPercent` | 50 | A soft body's triangle at its foot, as a percentage of its width over the ray: lower makes every trunk fainter |
| `kShadowEdgeWidthMm` | 1200 mm | The ray every fin is drawn with: wider widens every shadow's soft edge |
| `kForestCapMm` | 800 mm | One placed ring's greatest forest depth, before its width and distance weights |
| `kForestWindowSteps` | 8 | The tent's half width in rim steps of 5.625 degrees: 45 degrees. Wider spreads a ring's depth and lowers the change per step |
| `kForestNearMm` | 10000 mm | Where `distanceWeight` peaks. Larger lowers the change per step and the depth near trees |
| `kForestFloorPercent` | 67 | The least range on any side, as a percentage of sight |
| `kForestFullPerimeterMm` | 3142 mm | The perimeter at which `widthFactor` reaches 1: a body 1 m wide on average |
| `kForestArrivalMs` | 2000 ms | How long a ring arriving inside sight takes to come into forest depth and its drawn triangle |
| `kSightFeatherMm` | 1500 mm | The fade on both edges. The bright core of a 30 m sight is sight less this; a shape narrower than twice it is never fully bright |
| `kSoftLegToleranceMm` | 500 mm | How far a side's range may stray from its least over a leg before the leg ends; the record holds ground up to about 1.1 times this past where sight reached on a leg |
| `kSeenFeatherCellMm` | 32000 mm | The record feather's cells: smaller rebuilds less at an epoch and draws more meshes |
| `kOccluderBucketMm` | 16000 mm | The placed occluder index's buckets |
| `kDrawnShadowReachMm` | 30500 mm, the reach | How far from a colonist bodies draw shadows; lowered only by 12's fallback |
| `kDrawnShadowRebuildMm` | 0 mm | How far a colonist moves before his drawn pieces are rebuilt; 0 is every frame, raised only by 12's fallback |
| `kDrawnShadowSliceToleranceMm` | 5 mm | How far a triangle's sliced side may stray from its rule |
| `kDrawnValueTolerancePercent` | 2 | How far a drawn piece's linear value may stray from the value it stands for |
| `kFeatherFanStepDegrees` | 15 degrees | The widest triangle of a reflex corner's fan |
| `kSightRebuildStepMeters` | 0.5 m | Player-visibility.md's |
| `kSeenSlackMm`, `kSeenLeastRadiusMm`, `kSeenSkirtMm`, `kSeenSweepSpacingMm`, `kSeenSweepRimBandMm` | player-visibility.md's | Legs take the slack and the least radius as open legs do |

Fixed, not tunables or sliders: `kFractionOne` (65536) and the sight polygon's 64 sides (`kSightSides`), by 4.4; `kSeenDimAlpha` (0.5, the tint's darkness, `VisibilityCoverRenderer.h`); and `kSoftLegMostVertices` (256), the most vertices a leg's ring holds.

## 10. Acceptance limits

The prototype checks these with the owner watching, and the tunables move until they hold or the owner moves them.

1. A pine 5 m off, from any angle: its triangle darkens visible ground by at most 0.04 on screen at its foot and is gone within 1 m behind it.
2. The shortest wall the tools allow, from any angle: at most 0.05 on screen at its foot, gone within a quarter of its distance behind it.
3. A veteran oak, from any angle at any distance: at most 0.1 on screen at its foot, gone within its own distance behind it; no trunk draws a streak to the rim darker than 0.05.
4. A lone trunk up to a mature oak shortens the rim by at most 0.6 m on any side; a veteran oak or a BigRock by at most 0.8 m, besides a BigRock's hard shadow.
5. Walking past a veteran oak or a BigRock at 2 m and at 10 m, no side's range changes by more than 0.2 m between consecutive builds. Through the landing's forest, no side's range changes by more than 0.6 m between consecutive builds.
6. Forest depth shows in ordinary forest: through mature oaks 5 m apart the rim is 2.5 to 6 m in from full sight, and in the densest stand at the landing it is at the floor or above.
7. In the landing's forest, convexity takes at most 0.5 m off the mean range against the ranges' own star polygon.
8. Walking past a BigRock or a wall end, no pixel of the cover changes at a rebuild by more than 0.02 on screen.
9. The feather about 1.5 m wide on both edges, with no hard line on a rim or a seen edge.

## 11. Prior art

| Source | What it does | What carried over |
|---|---|---|
| Orangy Tang, "Dynamic 2D Soft Shadows" | A light with a radius casts a hard umbra and a penumbra beside it, the penumbra drawn as geometry at each shadow edge | The ray's width for a soft body's triangle and every fin's outer line, drawn as geometry. The sim has a point eye |
| Scott Lembcke, "Super Fast Soft Shadows" | Shadow geometry per light, the penumbra of a light of given radius worked out per pixel | The same geometry. Its per-pixel shading is raster work this game does not do |
| Company of Heroes 2 | Sight blockers are an authored class: a thing blocks sight or does not | Kinds that never change, but decided by each body's size in the collision map, not authored per asset |
| Combat Mission, tabletop wargame rules | Line of sight through woods falls off with the depth of woods crossed | Forest depth: the placed things in a direction shorten sight there, as a density |
| League of Legends | A coarse grid upscaled and blurred (player-visibility.md 5) | Set aside: raster |
| Godot 2D lights, Unity URP Light 2D | Shadow edges softened by a filter and softness settings on the light, in the renderer | Set aside: raster. The fade is vector geometry with partial coverage |
| Order-independent min and max with the depth buffer (depth peeling, "depth equal" resolves) | The depth test as a per-pixel greatest or least, and an equal test to pick the fragment that holds it | The per-colonist greatest in 7.6 |

## 12. Performance plan

Measured, not assumed, under the machine-load rule in [testing](../testing/README.md): times count only with `idle` checks both sides, and counts do not depend on load. Forest is the common case at the quickstart landing, so forest is what is measured.

What could move:

- Vision's tick. Every observer in forest builds a shape each rebuild step: the rings in sight from the index, their weights over 16 sides each, the 64 ranges, the forest shape's half-plane intersection, and with an opaque body or a wall the hard polygon and its clip. Memory tests candidates against a convex 64-gon.
- The record's tick. Legs, their spread tests and hull merges, and the rings their ends register; on the polygon path, the sweep's builds.
- The cover. Each frame, the drawn pieces of every body in reach of each colonist in view, a few hundred bodies a colonist in forest; the record feather's cells at each epoch, and the epoch rate; the pending cuts at each moving leg's end; the per-colonist passes of 7.6; the partial layers' GPU cost against the hard cover's.
- Views with reveal on draw no cover and build no pieces; captures should come out even with `main` apart from vision's tick.

To measure:

- Step 0, the sandbox: GPU time of the cover with its partial layers against the hard one in `perf_mask` at the same size, at 1, 10, and 50 colonists, and whether every case of 7.7 holds.
- Step 1, the prototype: "Prototype: sight ray casting across the nav mesh with tree attenuation, measure perf" on Specboard. A bench of a build at viewpoints in the quickstart landing's forest and in synthetic stands of known density: microseconds a build, rings in sight and opaque bodies in reach, vertices out, and the range convexity takes. The sim bench with 1, 10, and 50 colonists walking the landing's forest at 1x and 10x: vision's tick; the record's tick; the share of builds on each path; legs per kilometre and their lengths; rings registered per kilometre and per minute; record epochs a second and cells rebuilt an epoch; the per-frame pending cut; the greatest change of a side's range between builds against 10's limits; rings arriving inside sight; and how the landing's tree and BigRock density read from placement compare with 4.3's stands. The cover's CPU time building drawn pieces a frame at 1, 10, and 50 colonists in view. The acceptance limits of 10, checked on screen.
- Step 5, the cover: a capture near the landing with `--no-reveal` at each zoom with the crew, 10, and 50 colonists: GPU and CPU time of the cover, pieces drawn a frame, the per-frame pending cuts, and the record's cells at an epoch.

No-go at step 1 takes the fallbacks in order and measures again; a no-go after them is a change to this design. For vision's tick: builds budgeted per tick, oldest first, an observer past the budget keeping his last shape a tick longer (on a leg it moves with him and only its ranges are stale; on the polygon path the far side of his sight shortens by how far past a step he has gone); then `kForestWindowSteps` 4 with `kForestCapMm` raised to keep a stand's depth, halving the sides each ring adds to, with 10's limits checked again. For the drawn pieces: `kDrawnShadowRebuildMm` 100 mm in place of every frame; then a lowered `kDrawnShadowReachMm`. For the record, 6.6's fallbacks in order. For the cover, 7.7's.

## 13. Build steps

Each step is its own PR with its own done line and a `Perf:` line. Step 2 builds on the wall sight occluders work (branch `fix/WOR-931-wall-sight-occluders`) once it is on `main`.

0. Partial coverage in salal. The backend work of 7.6 (min and max blend equations, alpha-only and no-colour writes, the two depth states, a depth clear, the targets' alpha and depth checked at start), the sandbox scene and test of 7.7, then the API of 7.6. Done: a tagged salal whose `mask` scene shows every case of 7.7 with no seam, nothing darkened twice, no piece lightening outside its colonist's shape, and no colonist's shadow over another's light at 50 colonists, the value test passing on the GL backend, `docs/masks.md` brought up to date (`mask-mode`, `mix-blend-mode`, `isolation`, per-child masks, and the partial layer), and worldsim's pin on that tag; or the fallback of 7.7 that holds, with the test of its values.
1. Prototype. As 12, on a branch that builds 5's shape from the wall bodies and nav's placed rings, draws 3's shadows, and runs legs and the polygon path in the benches. Done: numbers in this document; 10's limits checked with the owner, and the tunables set from it; a go, or the fallback taken for each of vision, the record, and the cover.
2. Bodies. Wall bodies in `GeometryIndex`: each record's body, one record a body with its edges, box, `diameter`, and kind, made in `rebuild()`, with the rejected graph's bands as one body; `queryFacingOccluders`, `backFacesHiddenFrom`, and `insideSolid` reading opaque bodies only. The placed occluder index in `NavigationSystem` with serials, perimeters, diameters, kinds, arrival times, and buckets, updated in `collect()` in patch order, the removal callback carrying its ids. Silhouettes, widths, distances; the cache keyed on serials; `sightBodies`; `SightTunables` and its sliders; the state view's body list. Done: a placed obstacle occludes by its kind and a mover, water, a ford, and a bounds ring do not; a refused add never enters the index; a wall stub is left out of the visibility polygon and still joins memory and `StoreOrDrop`'s walls; a rejected graph takes the kind the same walls take when accepted, away from the threshold; felling a tree rebuilds an observer's shape and walking over new ground with no obstacle does not; a tree placed on a ring id freed in the same job rebuilds it and leaves the index with one entry; vision never joins the patch job (a test with the job held running); the state view lists a BigRock with kind opaque.
3. The sight shape. Forest depth and the forest shape in `libs/geometry/visibility` (the side normals' table among them), the polygon path's clip, `SightShape`'s fields, the overlay, and the landing of 5.3. Done: unit tests for a wall stub (no change to the shape), a lone tree (depth at most the cap times its weights), a stand of known density (depth within 10% of 4.3's), a veteran oak walked round at 10 m in 1 cm steps with no side changing by more than 4.3's bound for the step plus 1 mm, a ring coming into sight, a ring arriving inside sight over `kForestArrivalMs`, equal ranges giving the sight polygon to 2 mm, the forest shape convex and holding the floor's sight polygon over random stands, a BigRock's shape equal to `computeVisibilityPolygon`'s for its ring clipped to the forest shape, a wall run cutting its exact shadow, and a clear direction untouched; forest depth and the forest shape pinned and identical on MSVC, Clang, and GCC.
4. The record. Legs as hulls with the spread and vertex ends, `setOpenLeg` with ranges, the rim passed to `queueSightPolygon`, the skirt and the sweep's band on that rim, the sweep point's interpolated rim and its builder, `appendEdges`, `inRecord`, `takeChangedBoxes`, and the bright shape's rule. Done: tests for a straight walk through a stand (one leg while the spread holds), the spread ending a leg, a walk from open ground into forest and to a BigRock, the bright shape inside the record or pending on every frame of a walk through a stand, the record never more than 1.1 times the tolerance past the shapes a leg held, and the record oracle (no point on the record outside a true shape grown by the leg tolerance and the skirt); the sim test that every seen point is on the mesh and the record never lost ground, run in forest.
5. The cover. The feather cells, the pending cuts, each colonist's feather and drawn shadows each frame, and the masks of 7.5. Done: soft edges on both edges in the game, faint triangles collapsing behind trunks and wall stubs, widened wedges and fins beside hard shadows, scenario 14's new steps passing, the capture of 12.
6. Tests and docs. Scenario 14's steps (14) written in, player-visibility.md's tunables and measured sections brought up to date. Done: scenario 14 run.

## 14. Tests and scenario steps

Unit and sim tests are named in each step's done line. Before they are added, check the existing vision and record suites for the same paths and extend those.

Scenario 14 (`docs/testing/scenarios/14-player-visibility.md`) takes these steps after its step 2, all at the quickstart landing, written in at step 6. Spawned things are not bodies (8), so the steps use the landing's placed trees and rocks, and built walls:

1. Zoom 3 on Bob among trees: the bright area's edge is a fade about 1.5 m wide with no hard line, and the tinted area's edge against the black fades on the tinted side; past it the black is solid.
2. Press V: Bob's rim is drawn in toward denser trees, never closer than about 20 m, and is whole where the trees thin out.
3. Look behind a trunk 5 to 10 m from Bob: a barely visible darker triangle starts at its foot and closes behind it, shorter behind a thin trunk and a little longer behind a thick one.
4. `curl.exe "http://127.0.0.1:8081/api/state?what=visibility&bodies=1"` and find a BigRock among Bob's bodies (walk Bob through the forest until one is listed), then stand Bob 8 m from it: a shadow runs from its near face to the rim, and beside each side a gradient widens toward the rim, hard at the rock's corner.
5. Walk Bob slowly past a trunk and past the BigRock: the triangle and the shadow turn and grow or shrink with him, with no jump, and the seen area behind him does not shrink.
6. Build the shortest wall the tools allow 5 m from Bob: a barely visible triangle behind it, gone within about a metre. Build a 2 m wall beside it: a full shadow with soft edges.
7. Draw a room with a door and a window and stand Bob inside: sight passes the doorway and the window and ends at the walls' near faces, and both sides of the light through the doorway fade.
8. Stand a second colonist beside Bob among trees: where either sees a trunk's triangle, the ground is as bright as the brighter of the two has it, and no triangle darkens ground the other colonist lights.

## 15. Decisions log

### Kevin's words

1. 2026-11-16, the brief: "we currently have hard edges on the sight cones, and visibilty edges, which we need to soften greatly ... to be less ... precise. for instance if there was just a single wall block a 0.1m in diameter, it really shouldn't effect sight. and right now trees have no effect, but they very slightly should and the cones/rays should kinda ... collapse behind things. and visual softening around the edges as well"
2. 2026-11-16: "lets go with C + 2"
3. 2026-11-16, on occluders: "collidable objects. this needs to use the true collision map (minus movers) not bound to an arbitrary list."
4. 2026-11-16, to another session, on walls: "Note, the shadows start at the wall center, not the wall edge, which is wrong." and "the wall fill is essentially always unseeable, its opaque."
5. 2026-11-16, on classes: "shouldn't things be on a scale, not on/off so they never "flip" but would grow as they walk?"
6. 2026-11-16, on walls: "Need to fix the wall. The whole wall body is opaque. It's a rectangle, not a line."
7. 2026-11-16, on a tiny wall: "The tiny wall should have a shadow that collapses behind it. That's why we need to soften the ray. It would be triangular, softened. Will be barely noticeable really"
8. 2026-11-16, on keeping things in the way shortening how far a colonist sees: "Keep it. I like the idea what you can't see deep into the forest."
9. 2026-11-16, shown three pictures of how a thing's kind is decided: "we've come away from where the trees and other smaller objects just pull the circle edge in and don't cast themselves." and "but the recommended approach is close to what we want, and by the time we edge blur that back, the trunks will barely have any shadow"

### Options Kevin picked

The wording of each option is the author's or the orchestrator's.

10. Of the design canvas's options, C, a wide eye plus a sight budget, and 2, feathered edges as vector geometry.
11. The feather inside the brighter area.
12. One sight shape for colonist knowledge and player visibility.
13. The feather built as alpha mask layers in salal.
14. The tapering shadows and fins drawn on the cover while the sim shape stays simple, the drawing able only to show ground darker than its true state.
15. One object's effect on range capped at a small amount, several adding up to a floor.
16. A drawn shadow's strength scaled by the thing's width.
17. Size decides, once per thing: small things always cast the soft triangle, real walls and big rocks are always opaque with soft edges, and nothing ever changes kind.

### Author's proposals

Each is unconfirmed by Kevin.

18. The orchestrator's reading of 9: he accepts 17; small things' own shadows should be so faint after the edge softening that they barely read; and the rim pulling in (forest depth) remains the main way trees and small things affect sight.
19. The kind measure, a body's `diameter`, and its two thresholds: a wall body opaque from 0.6 m across, a placed body from 1.4 m (2.2). Kevin's "0.1 m" block cannot be built (a wall is at least 0.5 by 0.15 m), so 0.6 m is set to make the shortest walls the tools allow soft, above the 0.3 to 0.5 m first suggested; every tree is soft and a BigRock opaque. A rejected wall graph's kind taken over the whole graph; a growth stage crossing a threshold changing kind as a change in the world.
20. A soft body's triangle drawn with a 3 m ray and its foot at 50% of its width over the ray, so a trunk darkens visible ground by under a tenth of the tint at its foot (3.2); fins from a separate 1.2 m edge ray. Departure from C as drawn: a soft body casts no shadow in the sim.
21. An opaque body's drawn wedge widened by the line tangent to it and a circle of a rebuild step round the eye, so a hard shadow does not move on screen at a rebuild, at the cost of a sliver the sim has in sight drawn at full tint (3.1).
22. Departure from C as drawn: forest depth as a density over the placed rings in sight, evaluated at the 64 sides of the sight polygon, with a tent over 45 degrees, a distance weight peaking at 10 m and falling to 0 at the sight radius, a fixed `widthFactor` from the perimeter, an 0.8 m cap, and a floor at 67% of sight, in place of a budget spent along each ray (4). Opaque placed bodies spend it as soft ones do; walls spend none.
23. The outdoor sight shape convex by definition, the intersection of one half-plane a side (4.2), taking up to about 1 m in the worst direction of a dense random stand and, in the worst case of a clump, a straight side across about 100 degrees.
24. Legs as the hull of every shape walked with, ended by a 0.5 m spread of any side's range or 256 vertices, so bright ground is always seen and the record holds ground up to about 1.1 times the tolerance past where sight reached on a leg, an allowance like the skirt's (6.2). This may overshoot "seen" as Kevin means it.
25. The skirt grown off opaque bodies' sides as well as walls' (6.4), which extends rule 21 from walls to opaque bodies.
26. The polygon path's rim, the sweep point's interpolated rim, and the inset measured between corresponding rim vertices (6.4).
27. A ring arriving inside sight coming into forest depth and its drawn triangle over 2 s of sim time; an opaque one casting its shadow at once (2.4).
28. The placed occluder index in `NavigationSystem`: serials, buckets, kinds, the removal callback carrying its ids, updated in `collect()` in patch order (2.4); wall bodies and kinds in `GeometryIndex` on the wall sight occluders work's shape.
29. Departure from C as drawn: `rememberArea` at the landing reading no occluders, so the crew's memory agrees with the plain disc the record commits (5.3).
30. Rule 10 read against three caches: the placed occluder index's 16 m buckets, the record feather's 32 m cells, and player-visibility.md 3.4's seen boxes are lookups over vector rings and edges and hold nothing sampled, so they are no raster or grid of game data.
31. Rules 10 and 19 read against 7.6: destination alpha, depth, and the stencil are per-pixel state of the frame being drawn, written and read inside the cover's draws, holding no game data between frames, with no texture made, so rule 10's "no raster, grid, bitmap, or mask texture anywhere in this system" holds; rule 19's "drawn with the stencil buffer" covers the hard layers, and the partial layer uses the frame's alpha and depth beside it.
32. The seen area's feather drawn for the union's boundary only, kept in cells rebuilt from changed boxes, with pending cuts held and redone where a leg's ring moves; a body face left hard in the tint (7.2).
33. Departure from C as drawn: several colonists composed as the least darkness, each colonist's darkness the greatest of his pieces, each piece clipped to his bright shape; and the lens, where two sights overlap by under twice the feather, left dimmer than a fade from the union's edge (7.4).
34. The salal surface: `MaskMode::Luminance`, `MaskImage::content`, `MaskBlend::Lighten` and `Darken`, `MaskIsolation::Isolate`, `MaskPaint::linear`, mesh children with a value at each vertex, and children carrying their own hard mask (7.6).
35. Every start value in 9, every acceptance limit in 10, and 6.6's third fallback, a departure from rule 27.

## Related documents

- [Player Visibility](./player-visibility.md): the rules, the seen record, the cover, and the input rules this builds on
- [Vision Architecture](./vision-architecture.md): sight shapes, discovery, doors and windows
- [Nav Mesh: One Mesh, Variable Resolution](./nav-mesh-variable-resolution.md): the collision map and where it exists
- salal `docs/masks.md`: the mask API this extends
- [Testing](../testing/README.md): scenarios and the perf rule
