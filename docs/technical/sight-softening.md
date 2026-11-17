# Sight softening

**Created:** 2026-11-16
**Companions:** [Player Visibility](./player-visibility.md) (the seen record and the cover), [Vision Architecture](./vision-architecture.md) (where sight shapes come from), [Nav Mesh](./nav-mesh-variable-resolution.md) (the collision map sight reads)

Sight is softened by two mechanisms kept apart. The **soft ray**: every collidable body is opaque, and the ray from a colonist's eye has a width, so a body narrower than the ray casts a soft triangular shadow that closes behind it and a wider one casts a full shadow with soft edges. **Forest depth**: the placed things in a direction shorten how far he sees that way, as a smooth density, so nobody sees deep into a forest. The sight shape the sim uses is star-shaped round the eye and simple: forest depth sets its rim, and a body at least as wide as the ray cuts its exact shadow out of it. The collapsing triangles and the soft edges of shadows are drawn on the cover, each colonist each frame, and only darken. Both edges of the cover fade inside the brighter area, and where several colonists see a point its light is the greatest any of them gives it.

Terms are player-visibility.md's: **player visibility**, the **unexplored area**, and the three areas **unseen**, **seen**, and **visible**. A **body** is one placed obstacle's collision ring or one connected wall graph's outline. Quantities used throughout, each with its name:

| Name | What it is |
|---|---|
| `sightRadius` | The observer's sight, 30 m for a colonist |
| `step` | The rebuild step, 0.5 m (`kSightRebuildStepMeters`) |
| `reach` | `sightRadius + step`, the radius a shape is built to |
| `eyeCentre` | The observer's position at the build |
| `eyeWidth` | The ray's width (`kEyeWidthMm`) |
| `silhouette` | The arc of directions from the eye whose rays meet a body; `bisector` is its middle direction |
| `widthAsSeen` | A body's extent across its bisector |
| `distance` | From the eye to a body's nearest point |
| `rangeFromEye` | How far a point on the ground lies from the eye |
| `strength` | `widthAsSeen / eyeWidth` for a body narrower than the ray |
| `shadowLength` | How far behind a narrow body its drawn triangle reaches; `progress` is how far along it a point lies, 0 at the body and 1 at the tip |
| `forestDepth` | How much forest depth takes from the range in one rim direction |

---

## 1. Rules

The rules are player-visibility.md's 23 to 27 (2026-11-16), which replace its rules 5 and 11. Every collidable body in the collision map less movers is opaque, and a wall is its whole body (23). The ray is soft: a small thing's shadow collapses behind it, a wide thing's has soft edges, nothing flips as a colonist walks, the shape colonists know is simple, and the soft shadows are drawn on the cover, only darkening, their strength scaled by the thing's width (24). Things in the way shorten how far a colonist sees, one thing by a small capped amount and many down to a floor (25). Both cover edges fade inside the brighter area, built as alpha mask layers in salal, and rule 10 stands (26). One sight shape serves colonist knowledge and player visibility, and what is only drawn is never read by the sim (27).

## 2. Bodies

### 2.1 What occludes

The collision map is the nav mesh's blocked rings: wall bands and junctions, placed obstacles, and water. Sight reads two of those kinds, and reads walls through construction rather than through nav:

| Source | Sight |
|---|---|
| A built wall | Its body is the outline of its connected wall graph: `geometry::wallPiecesAndOutline(...).outline` (`libs/geometry/offset/WallOffset.h`), the boundary of the union of the trimmed bands and junctions, cut at every opening whose type is `transparentToSight` (`ConstructionRegistry.h`). A wall graph the band offsetter rejects is each wall's untrimmed band, outlined on its own and cut at its transparent openings. The whole band is opaque, so a wall's shadow starts at its near face |
| A placed obstacle (`kProvenancePlaced`) | Its body is the ring nav holds for it |
| Water (`kProvenanceWater`) | Never occludes: water has no height |
| A ford (`kProvenanceFord`) | Never occludes: a cover-only ring |
| A mesh limit or the walkable bounds (`kProvenanceBorder`) | Never: bounds rings, `blocked = false` (`NavInputBuilder.h`) |

Movers are never rings, so a colonist never hides another. A door and a window pass sight (vision-architecture.md D7); a closed solid door, when there is one, flips the same flag. The wall outline is the same geometry as nav's wall rings, with windows cut as well as doors, so walls and placed obstacles together are every blocking thing in the collision map that stands above the ground.

What counts follows the collision data alone, and no code in vision or nav names a kind of thing. A tree's trunk is a Rect collider, so it is in; a bush, groundcover, and most rocks have no collision and do nothing to sight; a BigRock is in. Nav pads a Rect collider by `kPlacedObstaclePadMm` (50 mm, `PlacedObstacle.h`) on every side before scaling by the entity's scale (`placedObstacleBox`, `NavInputBuilder.cpp`), and sight reads that ring as it is. Trees take no `randomRotation`, so their rings are axis-aligned squares; a BigRock (`randomRotation` 180) is a rectangle at any angle. A square's `widthAsSeen` runs from its side face on to its diagonal; a rectangle's from its shorter side to its diagonal. Wall bands are 0.15 to 0.30 m thick (`materials.xml`).

| Body | Face on | Worst angle |
|---|---|---|
| A 0.1 m wall block (an outline, unpadded) | 0.10 m | 0.14 m |
| A wall end on, 0.15 to 0.30 m band | 0.15 to 0.30 m | the band's diagonal |
| A pine | 0.28 m | 0.40 m |
| An oak sapling | 0.35 m | 0.49 m |
| A mature oak | 0.55 m | 0.78 m |
| A veteran maple | 0.60 m | 0.85 m |
| A veteran oak | 0.82 m | 1.16 m |
| A BigRock, 1.40 by 1.07 m | 1.07 m | 1.76 m |

### 2.2 What is measured

Every quantity is taken from `eyeCentre`. A mover's centre is held outside blocked rings (routes keep its clearance and static collision pushes it out), so the eye is never inside a placed ring.

- The `silhouette` runs from the direction of the body's extreme vertex on one side to the other's. A body seen from outside its convex hull meets one arc less than 180 degrees wide, whose ends move continuously as the eye moves, since each is the extreme of continuous angles. A body whose hull holds the eye (a room seen from inside, the eye in the crook of an L) is wide, whatever its other measures.
- `widthAsSeen` is the greatest less the least of `dot(vertex - eyeCentre, across)` over the body's vertices, `across` the unit direction perpendicular to the bisector. A square face on gives its side, at its diagonal its diagonal. It is continuous in the eye's position: the bisector turns continuously, and a fixed set of points' extent across a turning direction changes by at most the set's diameter times the turn.
- `distance` is from the eye to the body's nearest point.

### 2.3 Where vision reads them

**Walls.** `GeometryIndex` (`libs/sim/vision/`) builds the wall outline and its edges, as the Specboard item on wall sight occluders gives them, each connected wall graph's outline held together as one body.

**Placed obstacles.** Vision runs at priority 45 and nav at 51. The patch job nav submits at the end of a tick runs on its worker into the next tick until nav's `settle()` collects it, and `navMesh()` joins it, so a vision read of the mesh would stall the main thread on the job. Vision never reads the mesh. `NavigationSystem` holds a **placed occluder index** on the main thread: each placed ring in the mesh with its outline, box, perimeter, and a **serial** from a counter that never repeats.

- Adds. Nav tracks every gathered placed obstacle by key in `obstacleInMesh` (`NavigationSystem.h`), and each limit's patch hands back the ids it added in its collect callback (`obstacleInMesh.at(keys[k]) = added[k]`, `NavigationSystem.cpp`). The index takes each added ring there, its outline read with `PatchableMesh::ring(id)`, under a new serial.
- Removals. `queueRemovedDetail` erases a felled thing's key from `obstacleInMesh` and queues its ring id; `gatherRemovals` puts the ids in a patch whose callback carries the list of ids it removes (`edits.removes`), and the index drops each there. A count of them is not enough to update the index.
- Order. `gather` takes removals before limits, so one job can free a ring id and hand it to a new tree (`PatchableMesh` reuses a freed id from `freeRings`). `collect()` runs the job's callbacks in patch order, removal first, so the index drops the old ring before it takes the new one. The index and every cache keyed on it go by serial, never by bare ring id.
- `join()` is const and runs from every query, so the index is never touched there; a ring shows to vision from the tick after nav collects it, and a felled tree occludes for that long.

The index is bucketed by `kOccluderBucketMm` (16 m) squares of the ground, each ring listed in every bucket its box meets, so a build reads the buckets its reach's box meets and no others. The buckets are a lookup over vector rings and hold nothing sampled: like player-visibility.md 3.4's seen boxes, they are no raster or grid of game data under rule 10.

**The four readers of `queryOccluders`.**

| Caller | What it reads |
|---|---|
| `VisionSystem::update` (`VisionSystem.cpp`, the build) | Walls, and placed rings from the index: this document's whole shape (5) |
| `VisionSystem::rememberArea` (`VisionSystem.cpp`, the landing) | Nothing: the plain disc (5.2) |
| The sweep's builder (`PlayerVisibilitySystem.cpp`, `setSightBuilder`) | This document's whole shape at its point, walls and placed rings, so a sweep point sees what vision's own build there would |
| `StoreOrDrop` (`StoreOrDrop.cpp`) | Walls only. It asks whether an arm reaches a box, which a tree beside the box does not stop |

**Rebuilds.** An observer's cached shape is rebuilt when he moves more than `step` from its build, when the walls' version moves, or when the sorted serials of the placed rings within `reach` change. The mesh's epoch cannot stand for that, since movers patch it every 4 m of travel.

The mesh exists wherever an observer can see: every mover meshes the disc of sight plus 16 m round him, and the landing 150 m. Ground a colonist reaches before nav meshes it (a dev teleport) has no placed occluders until the mesh arrives.

## 3. The soft ray

### 3.1 The eye width and the sim

The ray from the eye is `eyeWidth` wide, 1.2 m to start. In the sim a body casts its exact point-eye shadow when its `widthAsSeen` is at least `eyeWidth`, or when its hull holds the eye: its outline's edges go to `geometry::computeVisibilityPolygon` with the other wide bodies', and sight ends at its near face. A narrower body casts no shadow in the sim; its collapsing shadow is drawn (3.2).

With `eyeWidth` 1.2 m every tree ring at every angle is narrower than the ray, so no tree casts a shadow the sim knows of, and a stand of trees shortens sight only through forest depth (4). A wall run 3 m long is wider than the ray seen at more than about 22 degrees off its line, a 2 m run at more than about 34 degrees, and a room from inside is wide in every direction. A BigRock is wide from every angle but the 11 degrees round looking along its long side. A 0.1 m block, a wall end on, and a short stub seen nearly end on are narrow.

| Body | `strength` | `shadowLength` at 5 m | at 10 m | In the sim |
|---|---|---|---|---|
| A 0.1 m wall block | 0.08 to 0.12 | 0.5 to 0.7 m | 0.9 to 1.3 m | No shadow |
| A pine | 0.23 to 0.33 | 1.5 to 2.5 m | 3.0 to 5.0 m | No shadow |
| An oak sapling | 0.29 to 0.41 | 2.1 to 3.5 m | 4.1 to 6.9 m | No shadow |
| A mature oak | 0.46 to 0.65 | 4.2 to 9.3 m | 8.5 to 18.6 m | No shadow |
| A veteran maple | 0.50 to 0.71 | 5.0 to 12.1 m | 10 to 24 m | No shadow |
| A veteran oak | 0.68 to 0.97 | 10.8 m to the rim | 21.6 m to the rim | No shadow |
| A BigRock | 0.89 end on, wide otherwise | to the rim | to the rim | A full shadow but end on |

Each range is face on to the worst angle. The tint is black at `kSeenDimAlpha` (0.5, `VisibilityCoverRenderer.h`), so on screen a drawn shadow darkens visible ground by at most half its value: a 0.1 m block by 0.06 at its foot, a pine by up to 0.17, a mature oak by up to 0.33.

### 3.2 The drawn shadow

For each colonist in view, every frame, every body within his `reach` is drawn from his position that frame. The drawing is float arithmetic and decides nothing.

**A narrow body** (`widthAsSeen < eyeWidth`) draws a **triangle**:

```
strength     = widthAsSeen / eyeWidth
shadowLength = distance * widthAsSeen / (eyeWidth - widthAsSeen)
progress     = (rangeFromEye - distance) / shadowLength
```

At a `rangeFromEye` between `distance` and `distance + shadowLength`, the triangle holds the directions of the silhouette shrunk toward the bisector by `progress` (each end moved that fraction of the way to the bisector), and on each such ray the ground from where the ray first meets the body outward, so the body's own footprint is in it. Its value there is `strength * (1 - progress)`. It is the body's silhouette at its foot and closes to a point at `shadowLength` behind it, fading along its length to nothing at the point.

**A wide body** draws its **wedge**, the whole silhouette from where each ray meets the body out to the reach, at 1.

**Every body** draws two **fins**, one beside each side of its triangle or wedge, on the lit side. At a `rangeFromEye`, the fin's outer edge lies `(eyeWidth / 2) * (rangeFromEye - cornerDistance) / cornerDistance` across the ray from the side, `cornerDistance` the distance to the body's vertex at that end of the silhouette: the line from the edge of the ray through the corner. Its value falls linearly across, from the side's value to 0. A wide body's fins run to the reach with 1 at the side; a narrow body's end where its triangle does, their value at the side falling with the triangle's. Fins are the soft edge of every shadow side, and a shadow side of the bright shape takes no feather of its own (7.2).

Pieces are combined by the greatest (7.5). The triangle's sides curve slightly on the ground, so it is cut into slices along its length until its sides are within 5 mm of the rule and each slice's linear value within 2% of it.

The drawing only darkens, and only ground the sim has in sight: every piece is clipped to the colonist's bright shape (7.5). A mover or thing inside a drawn triangle is in sight in the sim, so it draws, under the tint the triangle gives that ground, and can be selected; a click there is ruled by the sim.

### 3.3 The threshold

As `widthAsSeen` rises to `eyeWidth`, `strength` goes to 1 and `shadowLength` grows without bound, so `progress` goes to 0 at every range up to the reach, uniformly. The triangle's directions at every range go to the whole silhouette, its value to 1, and its fins to a wide body's fins. Just below the threshold the drawn picture is the wedge at 1 from where the rays meet the body out to the reach, with its fins; just above it, it is the same wedge and fins, drawn by 3.2 and cut out of the bright shape by the sim. Shape, reach, and strength agree, and the drawing is continuous in the colonist's position across the threshold.

The sim decides at builds, from the build's position, and the drawing follows the colonist every frame. So:

- At a build where a body reaches the threshold, the sim takes its point-eye wedge out of sight, and the drawing at that frame, from the same position, already shows that wedge at 1. Nothing on screen changes.
- At a build where a body falls below it, the sim's wedge from the build before goes, and the screen there shows the drawn triangle. Two things change: the stale wedge's step, which every hard shadow takes at a rebuild (player-visibility.md 8), and the triangle's shortfall from a full shadow at a width at most one step's change below `eyeWidth`. `widthAsSeen` changes by at most `diameter * step / distance` a step, so for a BigRock (diameter 1.76 m) the shortfall at the reach is:

| BigRock at | `widthAsSeen` at least | `progress` at the reach | Darkest change on screen |
|---|---|---|---|
| 20 m | 1.16 m | 0.02 | 0.03 |
| 10 m | 1.11 m | 0.16 | 0.11, along the far end and in a strip down each side 16% of the half silhouette wide at the reach |
| 5 m | 1.02 m | 0.88 | 0.45 at the far end |

Within about 5 m of a wide body the change at a drop is of the same order as the hard shadow's own rebuild step there, which turns its sides by up to `step / distance` radians.

Colonist knowledge at the threshold follows the sim. When a body reaches it, the ground in its wedge behind it, `eyeWidth / distance` radians wide (6.9 degrees at 10 m, 1.2 m across at the body and 3.7 m at a 30.5 m reach), goes out of sight: a mover there is out of sight and is not drawn, under a tint already at its full shadow's darkness. Memory holds what it saw and the seen record only grows, so nothing is forgotten. When a body falls below it the wedge comes back into sight.

### 3.4 A wide eye's geometry and the drawing rule

`shadowLength` is a segment eye's: the two lines from the ray's edges through a strip's opposite edges cross that far behind it. So is the fin's outer line, where a segment eye's penumbra beside a corner ends. The rest is a drawing rule. A segment eye's umbra narrows in width along a line, where the triangle shrinks in angle about the eye, so that it is the point-eye wedge at the threshold. Past the umbra a segment eye's hidden fraction falls toward `strength` and never to 0; the triangle closes to nothing. At a shadow's side a segment eye is half hidden; the fin falls linearly from the side's value. The value is the strip's share of the ray, `strength`.

## 4. Forest depth

### 4.1 The rule

Range in a direction is shortened by how much collidable placed stuff lies that way, as a smooth density, never as each thing's silhouette. For each of the 64 rim directions of the sight polygon (`kSightDirections`, `libs/geometry/visibility/SightPolygon.h`):

```
forestDepth(direction) = sum over placed rings with centre within sightRadius of
    kForestCapMm * widthFactor(ring) * angleWeight(direction, ring) * distanceWeight(ring)
range(direction)       = reach - min(forestDepth(direction), sightRadius - floorRange)
floorRange             = sightRadius * kForestFloorPercent / 100
```

- `widthFactor` is `min(1, perimeter / kForestFullPerimeterMm)`, the ring's perimeter against that of a body 1 m wide on average (3142 mm). The perimeter over pi is a convex body's mean width over every angle, so it is fixed per ring and needs no angle: a pine 0.36, a mature oak 0.70, a veteran oak or a BigRock 1.
- `angleWeight` is a tent: 1 where the direction points at the ring's centre, falling linearly to 0 at `kForestWindowSteps` rim steps away (4 steps, 22.5 degrees, to start).
- `distanceWeight` is `min(1, centreDistance^2 / kForestNearMm^2) * (1 - centreDistance^2 / sightRadius^2)`, `centreDistance` from the eye to the ring's centre. It is 0 at the eye, rises to about 1 at 6 m, and falls to 0 at the sight radius, so a ring coming into sight adds nothing at first, and a ring beside the eye adds little.

Built walls spend no forest depth: they are exact shadows or narrow bodies, by 3. Placed rings spend it whether or not they are wide, so a BigRock is a hard shadow and a little forest depth round it.

The forest rim is the polygon of the 64 rim directions at their ranges. Every range is positive and the directions are 5.6 degrees apart, so the polygon is star-shaped round the eye. Its vertex count is the sight polygon's, whatever stands in reach: forest adds no vertices.

### 4.2 What it gives

**A lone tree.** One ring's depth in any direction is at most `kForestCapMm * widthFactor * 0.96`, the greatest of `distanceWeight` being 0.96 at 6 m: 0.67 m for a mature oak, 0.96 m for a veteran oak or a BigRock, in the direction straight at it, tapering to nothing 22.5 degrees either side. The range is measured from the build to the reach, so at the build point a decision sees the nearer of `sightRadius` and the range, and a lone mature oak pulls that rim in by at most 0.17 m, a veteran oak by 0.46 m.

**Stands.** For trunks at `density` to the square metre with mean `widthFactor`, the expected depth is `density * widthFactor * kForestCapMm * windowAngle * 216 m^2`, where `windowAngle` is the tent's half width in radians (0.39) and 216 m^2 is the integral of `centreDistance * distanceWeight` over 0 to 30 m. That is about `85 m^2 * density * widthFactor` times `kForestCapMm`. With the start values, at the build point:

| Stand | Rim |
|---|---|
| Mature oaks 5 m apart | 28.1 m |
| Pines 3 m apart | 27.1 m |
| Veteran oaks 4 m apart | 25.2 m |
| Mature oaks 3 m apart | 23.9 m |
| Pines 2 m apart | 22.9 m |
| Veteran oaks 3 m apart | 21.1 m |
| Mature oaks 2 m apart | 20.6 m, the floor |

**Continuity.** Every term is continuous in the eye's position: a ring's centre direction, since the eye is never at it; its tent, linear in the direction's place in the table (4.3), which rises steadily with angle; `distanceWeight`, a polynomial in the squared distance; and a ring entering or leaving sight does so at weight 0. So the range in every direction is continuous as a colonist walks. A ring that is felled or placed changes it by at most that ring's depth, as any change to the world does.

**The worst change per step.** Walking `step`, a ring's centre direction turns by at most `step / centreDistance`, and its tent changes by that over the window, while `distanceWeight` changes by at most `2 * step / kForestNearMm` near the eye and `2 * step / sightRadius` far out. The product is greatest at `kForestNearMm`, where it is `step / (windowAngle * kForestNearMm) + 2 * step / kForestNearMm`: 0.21 + 0.17, so one ring changes one direction's range by at most 0.38 of `kForestCapMm * widthFactor` a step, 0.38 m for a veteran oak or a BigRock. Walking past one at 2 m, its weight is 0.11 and the change is 0.13 m; at 10 m, 0.12 m. Without the rise near the eye a ring at arm's length would swing its whole cap across 90 degrees of directions in one step, which is why `distanceWeight` starts at 0. Several rings near 6 m in one window add their changes; the visibility state view reports the greatest change of a direction's range between consecutive builds (8), and the acceptance limits bound it (10). `kForestCapMm`, `kForestWindowSteps`, and `kForestNearMm` are tunables.

### 4.3 Integers

Forest depth is integer arithmetic: millimetres, fractions in `kFractionOne` (65536) parts, and a direction's place in the table. It is the same on every toolchain, uses no trig, exp, or log, and reads the integer square root only when a ring enters the index, for its perimeter.

- A ring's position is `index * kFractionOne + place`, `index` the table direction at or before the ring's centre and `place` = `cross(direction[index], offset) * kFractionOne / (cross(direction[index], offset) + cross(offset, direction[index + 1]))`, `offset` the centre less the eye. The table is at scale `2^30` and offsets are under `2^16` mm, so each cross product is under `2^47`; the numerator is taken in `Int128` (`core/Int128.h`), as `sightPolygon` takes its squares.
- `centreDistance^2` is under `2^33`, and each weight's numerator under `2^49`. A ring's weight, `kForestCapMm` under `2^20` times three fractions, is under `2^20` mm, each product divided by `kFractionOne` before the next and under `2^36`; the tent's product is under `2^38`, and a sum over `2^16` rings under `2^36`.
- A ring whose weight rounds to 0 mm is skipped.

```cpp
const std::int64_t sides = static_cast<std::int64_t>(geometry::kSightSides);
const std::int64_t windowUnits = kForestWindowSteps * kFractionOne;
for (const PlacedOccluder& ring : ringsInSight) {
    const geometry::Vec2i64 offset = ring.centre - eyeCentre;
    const std::int64_t weightMm = kForestCapMm * ring.widthFactor / kFractionOne * distanceWeight(offset, sightMm) / kFractionOne;
    if (weightMm == 0) {
        continue;
    }
    const std::int64_t position = tablePosition(offset);
    const std::int64_t first = position / kFractionOne - kForestWindowSteps;
    for (std::int64_t index = first; index <= first + 2 * kForestWindowSteps + 1; ++index) {
        const std::int64_t offsetUnits = std::abs(index * kFractionOne - position);
        if (offsetUnits < windowUnits) {
            const auto direction = static_cast<std::size_t>((index % sides + sides) % sides);
            forestDepthMm[direction] += weightMm * (windowUnits - offsetUnits) / windowUnits;
        }
    }
}
for (std::size_t direction = 0; direction < geometry::kSightSides; ++direction) {
    rangeMm[direction] = reachMm - std::min(forestDepthMm[direction], sightMm - floorMm);
}
```

## 5. The sight shape

### 5.1 The build

Each build reads the bodies within `reach` and the placed rings within `sightRadius`:

- With none, sight is the plain disc and no shape is built, the outdoor fast path.
- The **forest rim** is 4's polygon. With no wide body, the shape is the forest rim.
- With a wide body or a wall in reach, the **hard polygon** is `geometry::computeVisibilityPolygon` over the wide bodies' outline edges, to the reach. The shape is the nearer of the two along every ray from the eye, `geometry::starNearerOf(forestRim, hardPolygon, eyeCentre)`, an angular sweep of the kind `starClippedToConvex` runs.

`computeVisibilityPolygon` is star-shaped only up to the millimetre snapping of its hits and is not promised simple (`Visibility.h`): `starNearerOf` reads each ring's angles with `unwrapAngles`, takes a vertex that steps back in angle (by the snapping, under 1 mm) at its predecessor's angle, and requires each ring to wind once. Its result is checked with `geometry::simpleAround`, as `PlayerVisibilitySystem` checks vision's polygons before the record queues them: a shape that fails, which only the snapping can cause, serves memory and `isVisible`, which test points against it, and is not queued to the record; the next build's is.

Everything vision decides with a shape is held to `sightRadius` round where he stands (player-visibility.md 3.2).

### 5.2 What uses it

`sightShape(observer)` carries, beside the centre, radius, polygon, revision, and the walls and openings seen, whether a wide body or a wall was in reach, which decides the record's path (6.1), and the forest rim's 64 ranges, which a leg reads (6.2). `polygon` is set whenever a shape was built.

Memory, discovery, and `isVisible` test that a thing lies within sight of where he stands and in the shape. The forest rim has 64 vertices and the hard polygon a few per wide body, so the test is cheap; the shape is star-shaped, so a test by angle (one binary search and one side test) is its cheaper form if the prototype finds it needed.

`VisionOverlay` (V) draws each observer's shape, the forest rim and the hard polygon in two colours, each wide body, and the eye's width at the build.

**The landing.** `PlayerVisibilitySystem::revealLanding` commits a plain disc of `kLandingSightMeters` (150 m). `VisionSystem::rememberArea`, whose only caller is the landing (`GameScene.cpp:1410`), makes no `queryOccluders` call and builds no shape: it gives every crew member the plain disc's things. Rules 8 and 17 say the crew saw the landing as they came down, and the seen area and what the crew knows agree.

## 6. The seen record

The record is player-visibility.md's: an exact polygon union in a second `PatchableMesh`, star-shaped shapes in, the covered test, the frontier, and the skirt (3.3 there).

### 6.1 Three paths

| At the build | Path |
|---|---|
| Nothing in reach | An open leg: one capsule a straight walk |
| Placed rings in sight, none wide, no wall in reach | A leg with a polygon (6.2): the forest rim is star-shaped but not a circle |
| A wide body or any wall in reach | The polygon path: each shape queued with its walk, the skirt, frontier pieces, and the sweep |

Any wall in reach puts a build on the polygon path, wide or not, because seen walls enter the record from new polygons: `enterStructures` runs only when a new polygon arrives and reads only walls (`PlayerVisibilitySystem.cpp`). A leg never has a wall in reach, so it misses none. A leg ends when the path changes; the record only grows, so nothing is lost at the change. At the quickstart landing, a forest with BigRocks scattered at least 15 m apart, most builds are legs with a polygon, and their cost is what the prototype measures (12).

### 6.2 Legs with a polygon

An open leg is one ring however long the walk, and rests on the outdoor shape being convex: the hull of a convex polygon at two ends holds that polygon at every point between, so the ring only grows. A forest rim is not convex. A leg with a polygon holds a convex polygon `legPolygon` fixed at its first build, and its ring is `legPolygon` swept along the leg.

**The leg polygon.** At the first build, at `legStart`, `legPolygon` is the intersection of one half-plane per rim direction, `{point : dot(point - legStart, direction) <= range(direction) - kSoftLegToleranceMm - cornerAllowance(range(direction))}`, the ranges being that build's, cut to the sight polygon of sight less `kSeenSlackMm` round `legStart`. `cornerAllowance(r)` is `r * (1 / cos(halfStep) - cos(halfStep))` for half the angle between two rim directions, 72 mm at 30 m and growing with range, from a compile-time ratio: where two half-planes meet, their corner can stand that much past the side the shape takes between the same two directions. `legPolygon` is found exactly as the dual of the convex hull of each rim direction divided by its half-plane's offset from `legStart`, in integers, its vertices rounded toward `legStart`.

**Confirmation.** A build at `buildPoint` confirms the leg when its shape holds `legPolygon` moved to `buildPoint` and grown by the **margin**, half the distance from the build before it plus `kSeenSlackMm`, its sides moved out by the margin and mitred, tested exactly: every vertex inside the shape and no side crossing it. The build before it must hold the same grown polygon too, which the leg checks against that build's shape, kept with the leg. The first build checks itself by the same test, so the corner allowance places `legPolygon` and does not keep it inside. The leg's ring is the hull of `legPolygon` at `legStart` and at the last confirmed build, `setOpenLeg` passing `legPolygon` (6.3). Every point of the leg's segment lies within the margin of a confirmed build, so every translate of `legPolygon` along it lies in a confirmed build's shape.

The ring advances only to positions a build has confirmed. Between builds it is held at the last one, so it never holds ground no build has confirmed; it moves forward only along the leg, so it never shrinks (rule 16). The leg ends at the last confirmed build when a build does not confirm (forest depth deeper than the tolerance where `legPolygon` reaches, or builds farther apart than the tolerance can bridge), and by an open leg's rules: a turn, a turn back, a jump, or a change of path. The next leg starts at the new build with its ranges. The tolerance must exceed the largest margin a leg bridges.

Forest depth changes a direction's range by at most about 0.4 m a step for any one ring (4.2), well inside a 1 m tolerance, so a straight walk through an even stand is one leg; a walk from open ground into a stand ends legs as the rim draws in.

**The open leg** is the same mechanism with `legPolygon` the sight polygon of sight less the slack: with nothing in reach every position confirms it, so its ring's end is where he stands each frame.

A leg's ring goes through the frontier unskirted, as an open leg's does. A build that does not hold its own `legPolygon`, which only rounding can cause, goes in as the polygon path's shapes do, skirted and swept.

What it gives up: ground between `legPolygon` and the shape, the tolerance band and the part of the forest rim a convex polygon cannot follow, enters the record only from another leg or a later build. At the exploring edge in a forest the record trails sight by that much, under the unseen fill.

### 6.3 The record's interface

- `SeenArea::setOpenLeg(leg, start, end, polygon)` takes the leg's convex polygon round its start in place of a radius (`SeenArea.h`): an open leg passes the sight polygon of sight less the slack, a leg with a polygon its `legPolygon`. `legCapsule` is the hull of that polygon at the two ends, built in one pass over its sides.
- `SeenArea::appendEdges(low, high, out)` gives the record's edge in a box: every live triangle edge with nothing across it, as `(start, end)` with seen ground on the left, through `PatchableMesh::meshEdgesNear`, since the record's mesh is private. The cover's feather reads it (7.2).
- `SeenArea::inRecord(point)` is whether the committed record alone holds the point, by its mesh's `locate`. `isSeen` counts the pending shapes too, and the feather's cut needs the two apart (7.2). Tests read the committed record through it.
- `SeenArea::takeChangedBoxes(out)` gives the boxes of the rings registered since the last call, from which the feather's pieces are rebuilt (7.2).
- `PlayerVisibilitySystem` holds each observer's path: open, leg with a polygon, or polygon path.

**The bright shape.** `drawnVisibleRadiusMm`'s guarantee, that the disc lies in the leg's capsule, rests on the open leg (`SeenArea.h`). `PlayerVisibilitySystem::drawnVisible` gives:

- on the open path, the disc of `drawnVisibleRadiusMm` round his `Position` that frame;
- on the other two paths, vision's shape cut to that disc round its build (`starClippedToConvex`), while the observer's path took this revision. After a jump, nothing until vision builds the next.

On a leg with a polygon the bright shape reaches past the leg's ring by the tolerance and up to a build's spacing ahead of him; there it is under the unseen fill, which is opaque and drawn over the tint, so what shows bright is seen by the fills' order (player-visibility.md 3.1).

### 6.4 Star-shaped shapes

The skirt (`geometry::skirtedSightShape`), the frontier (`sightShapeFrontier`), the covered test's fan, and `simpleAround` all take a shape star-shaped round its centre. Every shape the record takes is: a leg's ring is a convex hull, and this document's shape is star-shaped round its build by construction and checked with `simpleAround` (5.1). The skirt grows a shape's sides off the rim, the forest rim's among them, by `kSeenSkirtMm`, so seen ground can reach that far past the rim on the polygon path, as rule 21 allows past a shadow edge.

### 6.5 What a forest costs the record

Legs per kilometre, rings registered per kilometre and per minute, the sweep's builds on the polygon path (every 100 mm of a walk that opens ground near its rim, `kSeenSweepSpacingMm`), the registry's bytes per kilometre, and the record's tick are what the prototype measures at 1, 10, and 50 colonists (12), against player-visibility.md 6.5 and 6.7.

If the record costs too much in forest, the fallbacks in order:

1. The sweep skipped where every wide body in reach is placed: the seen edge past a rock's shadow then steps with vision's builds, 0.5 m.
2. A larger `kSoftLegToleranceMm`, so fewer builds end a leg: the record trails sight farther at the exploring edge.
3. The forest rim held at the sight polygon on the record's side only, legs open through forest, with the record holding ground the forest rim leaves out of sight: a departure from rule 27 that would go back to Kevin.

## 7. The cover

### 7.1 Two masks

The cover's two fills and their order are player-visibility.md 3.4's: the tint (black at `kSeenDimAlpha`) first, the opaque unseen fill over it. Each draws with a mask whose value at a point is how much of the fill shows there. With `fade(depth) = max(0, 1 - depth / kSightFeatherMm)`:

- The unseen fill's mask is 1 outside the seen area (the record and the pending shapes), and inside it `fade` of the distance to the seen area's edge (7.2).
- The tint's mask is the least, over the colonists, of each colonist's **darkness**: 1 outside his bright shape (6.3), and inside it the greatest of `fade` of the distance to his bright shape's rim and his drawn shadows (3.2). Light, one less the tint's mask, is the greatest light any colonist gives a point (7.4).

The fade lies inside the brighter area on both edges: nothing unseen is ever shown even faintly, and the fully bright core of a 30 m sight is about 28.5 m round him. Sight, the record, and the bright shape are exact vector shapes; the fade is drawn and decides nothing. A click on ground under it is ruled by the record and by `isVisible`, never by the drawing.

### 7.2 The feather

`fade` of the distance to an area's edge is drawn exactly as the greatest of pieces, one per edge of the area's boundary and one per reflex corner: an exact inward offset, combined by the greatest. An edge's piece is the rectangle from the edge `kSightFeatherMm` into the area, 1 on the edge and 0 on its far side. A corner where the area's inside angle exceeds 180 degrees takes a fan of radius `kSightFeatherMm` round it over the gap between its edges' rectangles, 1 at the corner and 0 on the arc, in triangles of at most 15 degrees, within 1% of the fade on their chords. Inside the area the greatest of these is `fade` of the distance to the boundary, because the nearest boundary point to any point inside is a perpendicular foot on an edge or a reflex corner. Where a rectangle runs past the area's boundary it lies where the mask is 1 already. Where the area is narrower than twice the feather the pieces of its two sides overlap and the greatest is the nearer side's, so a beam of sight through a 1 m doorway is never fully bright and is never darkened twice.

This holds only when the pieces are drawn for the area's boundary and for nothing else: an edge two shapes share, or one inside another shape, must draw no piece, or it draws a dark seam inside the area.

**One colonist's bright shape** is one star-shaped polygon, so its boundary is its sides and needs no cut. The build tags each side as rim, shadow side, or body face. A rim side, the forest rim's or the sight polygon's, draws a feather piece. A shadow side, running out from a wide body's corner, draws none: its soft edge is the body's fin (3.2), which is 0 wide at the corner and widens with range, as a wide eye's penumbra does, and which the threshold needs to match a narrow body's (3.3). A body face draws none: the darker side is the body, a wall's or a rock's footprint, so no fade darkens the ground in front of it. On the open path his shape is a disc and its feather an annulus, built once and moved with him.

**The seen area** is the record's union with the pending shapes, so its boundary is found on the CPU, exactly and in integers: the record's edge (`appendEdges`) and the pending shapes' sides, each cut where it meets the others, a piece kept when the point just off its middle on its outer side lies in no other shape (the record by `inRecord`, a pending shape by its outline; a point on another shape's side counts as inside that shape when the shape lies on the outer side). The record's edge is the edge of its whole union, however many meshes the record is uploaded in, so several record meshes leave no seam.

- The record's pieces are kept in cells of `kSeenFeatherCellMm` (32 m), one uploaded mesh a cell, in as many meshes as 16-bit indices need. The cells are a cache of pieces built from the record's vector edge, like player-visibility.md 3.4's seen boxes, and hold no sampled data, so rule 10 stands. When `epoch()` moves, the cells meeting a box from `takeChangedBoxes` grown by the feather are rebuilt from `appendEdges` over the cell grown by the feather, and the rest are untouched: a registered ring changes the union's boundary only inside its own box, and a boundary edge the mesh's patch splits at a point within it draws the same rectangles and no fan, since the split point is straight. Pieces register one a tick (player-visibility.md decisions 13), so an epoch moves at most once a tick. One registered ring's box can be large: a frontier piece from a closed leg's capsule is as long as the walk, so an epoch can rebuild every cell along a long leg, and the cost of an epoch is the record's edges in those cells. The prototype measures cells rebuilt an epoch (12).
- Inside the feather of a pending shape the record's pieces are cut against it. The uploaded pieces are drawn only outside the pending shapes grown by the feather, a hard mask of their outlines, and inside that region the cut pieces are drawn with the pending shapes' own. A queued shape's cut is made once when it is queued and dropped when it leaves the queue. A leg's ring is the hull of its polygon at two ends, whose long sides are fixed lines while its end moves, so its cut is held and redone each frame only within the box of its polygon at its last two ends grown by the feather, and where a changed box of the record meets it. A frame's work is the record's edges in one such box a moving leg, not the length of the leg.

### 7.3 One colonist's drawn shadows

The pieces of 3.2, for each colonist whose sight's box meets the view, built each frame from his position: a triangle and two fins per narrow body, a wedge and two fins per wide body. Values that are products, a fin's fall across times a triangle's fade along, are evaluated at the pieces' vertices and subdivided until each triangle's linear value is within 2% of the product. Pieces are never recorded and never read by the sim.

### 7.4 Several colonists

A colonist's darkness is 1 outside his bright shape and inside it the greatest of his feather and his drawn shadows. The tint's mask is the least darkness over the colonists, so light is the greatest light any colonist gives a point, and one colonist's drawn shadows never darken ground another colonist lights: where another lights it fully, the least is 0. It is drawn in a number of passes linear in the colonists in view, with no layer per pair (7.6). The prototype proves it at 50 colonists in view (7.7).

**The lens.** Each colonist's feather fades from his own rim. Where two bright shapes cross, the ground near both rims is lit by the brighter of two fades, each from a rim the other shape covers, and so is darker than a fade from the union's edge would be. A lens where two sights overlap by under twice the feather is never fully bright, and from each point where two rims cross a dimmer wedge runs into the overlap, longest where they cross at a shallow angle. A fade from the union's boundary would need that boundary found on the CPU every frame for every colonist in view, whose discs move every frame; this design takes the dimmer lens instead (7.8).

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

How salal evaluates partial coverage on the GPU is the prototype's to choose and is named nowhere in the API. The proposed build:

- The hard layers are in the stencil's two bits.
- The partial layer is accumulated in the target's destination alpha. salal blends every channel with `GL_ONE, GL_ONE_MINUS_SRC_ALPHA` (salal `GLBackend.cpp`), so after the world draws, destination alpha holds whatever the world left, and the cover owns it: before each fill it writes the layer's starting value over the view with only alpha written, combines its children with a max or a min blend on alpha, draws the fill weighted by destination alpha, and when the cover is done writes alpha back to 1 over the view, so nothing drawn after it reads cover values.
- An isolated group's own greatest is kept in the depth buffer. For each colonist: his bright shape is drawn to a stencil bit and to depth 0; his pieces are drawn with each piece's value as its depth and the depth test keeping the greater, colour off, inside that bit; then the same pieces and the shape are drawn again with the depth test passing only where a piece's depth equals what is stored, writing their values to destination alpha with a min blend; the shape clears the bit last. Only the piece that holds the greatest passes the equal test at each pixel, so destination alpha takes the least over colonists of each colonist's greatest. The two passes share a vertex shader with an invariant position, so the equal test is exact. Each colonist costs his shape three times and his pieces twice, linear in the colonists.
- What the targets have. The window asks GLFW only for stencil bits (`GLFW_STENCIL_BITS` 8, salal `GlfwPlatform.cpp:593`), leaving alpha and depth at GLFW's defaults. `createRenderTarget(colour, stencil)` takes any colour texture, an `Rgb8` one with no alpha among them, and attaches `GL_DEPTH24_STENCIL8` only when asked for stencil (`GLBackend.cpp`). So step 0 asks for destination alpha and depth on the window and on any target the cover draws into, and checks at start that they are there.
- Rule 10. Destination alpha, depth, and the stencil are per-pixel state of the frame being drawn, written and read inside the cover's draws, holding no game data between frames. No texture is made and nothing is stored as a raster, so rule 10's "no raster, grid, bitmap, or mask texture anywhere in this system" holds. Rule 19's API rule stands; its "drawn with the stencil buffer" covers the hard layers, and the partial layer uses the frame's alpha and depth beside it.

### 7.7 The prototype and its fallbacks

Step 0 is a sandbox scene of the cover's cases over a busy background: two record cells sharing an edge, a pending piece abutting the record along its edge, a pending leg overlapping it, two bright shapes overlapping and two abutting, a 1 m beam, a reflex corner, a narrow body's triangle and fins, a wide body's fins, a piece reaching past its colonist's bright shape, one colonist's shadow inside another's sight, and 50 colonists' shapes overlapping in one view with their shadows. A test reads the mask's value at chosen pixels (cores, mid-fade, overlaps, abutting edges, the shared cell edge, a reflex corner, a shadow under another colonist's light, the clipped piece's far side) against 7.1 to 7.5, and GPU time is taken at 1, 10, and 50 colonists. No golden image.

If the target has no destination alpha or depth, or the build cannot hold those values, the fallbacks in order:

1. Each colonist's pieces made disjoint on the CPU each frame, an overlay of their outlines with each cell carrying the greatest of the pieces over it, subdivided to within 2%, so that the min blend on destination alpha alone gives the least over colonists with no depth pass.
2. The partial layers drawn as fill-coloured geometry carrying alpha, source over, under the stencil's hard result. Where two pieces overlap (inside the feather of a convex corner, a narrow beam, a shadow over a feather) it darkens to `first + second - first * second` in place of the greater, and where two colonists see a point one's shadow darkens the other's light.
3. Hard edges, as player-visibility.md 3.4 draws them, with the sight shape and the record of this document.

### 7.8 Consequences taken on purpose

- Narrow seen and visible ground, a doorway's beam or a strip between two shadows, is never fully bright or fully tinted.
- Where two colonists' sights overlap by under twice the feather the overlap is never fully bright, and a dimmer wedge runs in from each point where their rims cross (7.4).
- A body face, a wall's or a rock's, is a hard edge in the tint, and a shadow side is hard at its corner, its fin widening from there.
- A hard shadow's sides, from the sim, step at each rebuild by the angle a step turns them; the fins drawn each frame soften the step.
- The draw bound and the terrain scissor are player-visibility.md 3.4's: the fade lies inside the record, so nothing past it shows.

## 8. Input, things, and dev tools

The input rules (player-visibility.md 3.7) read the record and `isVisible`, which reads vision's shape. In forest a colonist's bright shape can reach past the record by the tolerance at the exploring edge, under the black, where placing and ordering read unseen. A mover that is not a colonist shows while `isVisible` holds against this document's shape: behind a BigRock seen across its long side it is out of sight; behind a trunk it is in sight, drawn under the trunk's triangle; past the forest rim it is out of sight.

The visibility state view (`/api/state?what=visibility`) reports: builds by path (open, leg with a polygon, polygon path); legs with a polygon held, ended by forest depth, and ended by a gap between builds; placed rings in sight and wide bodies in reach, summed over the last builds; the greatest change of one direction's range between consecutive builds of one observer, and the ring it came from; bodies crossing the threshold, up and down; sweep builds; record epochs a second and feather cells rebuilt; and shapes that failed `simpleAround`. `/api/dev/reveal` is player-visibility.md's.

Every tunable in 9 not marked fixed is a slider on the Dev Tools tab, read live by vision and the cover, so the prototype tunes them with the owner watching. A change of a sim tunable rebuilds every observer's shape at once.

`/api/dev/spawn` makes an ECS entity (`PlacementSystem::spawnEntity`, rotation 0) that never enters the placement index nav gathers obstacles from, so a spawned BigRock or tree is no body; tests and scenarios use placed trees and rocks, and built walls.

## 9. Tunables

Start values are set by the prototype with the owner watching (10).

| Name | Start | What it sets |
|---|---|---|
| `kEyeWidthMm` | 1200 mm | The ray's width. A body at least this wide as seen casts a sim shadow; it sets every drawn triangle's strength and length and every fin's spread |
| `kForestCapMm` | 1000 mm | One placed ring's greatest forest depth, before its width and distance weights |
| `kForestWindowSteps` | 4 | The tent's half width in rim steps of 5.625 degrees: 22.5 degrees. Wider spreads a ring's depth and lowers the change per step |
| `kForestNearMm` | 6000 mm | Where `distanceWeight` reaches about 1. Larger lowers the change per step and the depth near trees |
| `kForestFloorPercent` | 67 | The least range in any direction, as a percentage of sight |
| `kForestFullPerimeterMm` | 3142 mm | The perimeter at which `widthFactor` reaches 1: a body 1 m wide on average |
| `kSightFeatherMm` | 1500 mm | The fade on both edges. The bright core of a 30 m sight is sight less this; a shape narrower than twice it is never fully bright |
| `kSoftLegToleranceMm` | 1000 mm | How far a leg's polygon sits inside its first build's ranges: how deep a change in forest depth ends the leg, and the largest margin it bridges |
| `kSeenFeatherCellMm` | 32000 mm | The record feather's cells: smaller rebuilds less at an epoch and draws more meshes |
| `kOccluderBucketMm` | 16000 mm | The placed occluder index's buckets |
| `kDrawnShadowReachMm` | 30500 mm, the reach | How far from a colonist bodies draw shadows; lowered only by 12's fallback |
| `kSightRebuildStepMeters` | 0.5 m | Player-visibility.md's |
| `kSeenSlackMm`, `kSeenLeastRadiusMm`, `kSeenSkirtMm`, `kSeenSweepSpacingMm` | player-visibility.md's | Legs with a polygon take the slack and the least radius as open legs do |

Fixed, not tunables or sliders: `kFractionOne` (65536) and the sight polygon's 64 directions (`kSightSides`), by 4.3.

## 10. Acceptance limits

The prototype checks these with the owner watching, and the tunables move until they hold or the owner moves them.

1. A 0.2 m ring, from any angle, 5 m off: its drawn triangle's value at most 0.25 at its foot, and gone within a third of its distance behind it.
2. A 0.1 m wall block: its drawn triangle's value at most 0.15, and gone within 1.5 m behind it at 10 m.
3. A lone trunk up to a mature oak shortens the rim at the build point by at most 0.25 m in any direction; a veteran oak or a BigRock by at most 0.5 m, besides a BigRock's hard shadow.
4. Walking past a veteran oak or a BigRock at 2 m and at 10 m, no direction's range changes by more than 0.2 m between consecutive builds, apart from directions a hard shadow's side crosses. Walking past one at any distance, no more than 0.4 m.
5. In the densest stand at the quickstart landing the rim is at the floor or above; through mature oaks 5 m apart it is within 2.5 m of full sight.
6. A body crossing the threshold upward changes no pixel of the cover outside the stale wedge's step; one crossing downward 10 m off or farther changes none by more than 0.15.
7. The feather about 1.5 m wide on both edges, with no hard line on a rim or a seen edge.

## 11. Prior art

| Source | What it does | What carried over |
|---|---|---|
| Orangy Tang, "Dynamic 2D Soft Shadows" | A light with a radius casts a hard umbra and a penumbra beside it, the penumbra drawn as geometry at each shadow edge | The eye's width for the umbra's length and the fin's outer line, drawn as geometry. The sim has a point eye |
| Scott Lembcke, "Super Fast Soft Shadows" | Shadow geometry per light, the penumbra of a light of given radius worked out per pixel | The same geometry. Its per-pixel shading is raster work this game does not do |
| Company of Heroes 2 | Sight blockers are an authored class: a thing blocks sight or does not | Set aside: what occludes is read from the collision map, and how by its width |
| Combat Mission, tabletop wargame rules | Line of sight through woods falls off with the depth of woods crossed | Forest depth: the placed things in a direction shorten sight there, as a density |
| League of Legends | A coarse grid upscaled and blurred (player-visibility.md 5) | Set aside: raster |
| Godot 2D lights, Unity URP Light 2D | Shadow edges softened by a filter and softness settings on the light, in the renderer | Set aside: raster. The fade is vector geometry with partial coverage |
| Order-independent min and max with the depth buffer (depth peeling, "depth equal" resolves) | The depth test as a per-pixel greatest or least, and an equal test to pick the fragment that holds it | The per-colonist greatest in 7.6 |

## 12. Performance plan

Measured, not assumed, under the machine-load rule in [testing](../testing/README.md): times count only with `idle` checks both sides, and counts do not depend on load. Forest is the common case at the quickstart landing, so forest is what is measured.

What could move:

- Vision's tick. Every observer in forest builds a shape each rebuild step: the rings in sight from the index, their weights over 9 rim directions each, the 64 ranges, wide bodies' widths, and with a wide body the hard polygon and the nearer-of sweep. Memory tests candidates against a 64-vertex shape.
- The record's tick. Legs with a polygon, their confirmation tests and ring counts; on the polygon path, the sweep's builds.
- The cover. Each frame, the drawn pieces of every body in reach of each colonist in view, a few hundred bodies a colonist in forest; the record feather's cells at each epoch, and the epoch rate; the pending cuts at each moving leg's end; the per-colonist passes of 7.6; the partial layers' GPU cost against the hard cover's.
- Views with reveal on draw no cover and build no pieces; captures should come out even with `main` apart from vision's tick.

To measure:

- Step 0, the sandbox: GPU time of the cover with its partial layers against the hard one in `perf_mask` at the same size, at 1, 10, and 50 colonists, and whether every case of 7.7 holds.
- Step 1, the prototype: "Prototype: sight ray casting across the nav mesh with tree attenuation, measure perf" on Specboard. A bench of a build at viewpoints in the quickstart landing's forest and in synthetic stands of known density: microseconds a build, rings in sight and wide bodies in reach, vertices out. The sim bench with 1, 10, and 50 colonists walking the landing's forest at 1x and 10x: vision's tick; the record's tick; the share of builds on each path; legs per kilometre; rings registered per kilometre and per minute; record epochs a second and cells rebuilt an epoch; the greatest change of a direction's range between builds against 10's limits; threshold crossings; and how far the record trails sight at the exploring edge. The landing's tree density, read from placement. The cover's CPU time building drawn pieces a frame at 1, 10, and 50 colonists in view. The acceptance limits of 10, checked on screen.
- Step 5, the cover: a capture near the landing with `--no-reveal` at each zoom with the crew, 10, and 50 colonists: GPU and CPU time of the cover, pieces drawn a frame, the per-frame pending cuts, and the record's cells at an epoch.

No-go at step 1 goes back to Kevin with the numbers. For the drawn pieces, in order: built when a colonist has moved 0.1 m rather than every frame; then drawn only for bodies within a lowered `kDrawnShadowReachMm` of him. For the record, 6.5's fallbacks in order. For the cover, 7.7's.

## 13. Build steps

Each step is its own PR with its own done line and a `Perf:` line.

0. Partial coverage in salal. The sandbox scene and test of 7.7, then the API of 7.6, with the window and targets asking for alpha and depth. Done: a tagged salal whose `mask` scene shows every case of 7.7 with no seam, nothing darkened twice, no piece lightening outside its colonist's shape, and no colonist's shadow over another's light at 50 colonists, the value test passing on the GL backend, `docs/masks.md` brought up to date (`mask-mode`, `mix-blend-mode`, `isolation`, per-child masks, and the partial layer), and worldsim's pin on that tag; or the fallback of 7.7 that holds, with the test of its values.
1. Prototype. As 12, on a branch that builds 5's shape from the walls' outline and nav's placed rings, draws 3.2's shadows, and runs legs and the polygon path in the benches. Done: numbers in this document; 10's limits checked with the owner, and the tunables set from it; a go, or the fallback taken for each of vision, the record, and the cover.
2. Bodies. The placed occluder index in `NavigationSystem` with serials, perimeters, and buckets, updated in `collect()` in patch order, the removal callback carrying its ids; silhouettes, widths, distances; the cache keyed on serials. Walls by their outline come with the Specboard item on wall sight occluders. Done: a placed obstacle occludes and a mover, water, a ford, and a bounds ring do not; felling a tree rebuilds an observer's shape and walking over new ground with no obstacle does not; a tree placed on a ring id freed in the same job rebuilds it and leaves the index with one entry; vision never joins the patch job (a test with the job held running).
3. The sight shape. Forest depth and the threshold of 3.1 in `libs/geometry/visibility` (`starNearerOf` among them), `SightShape`'s fields, the overlay, and the landing pass of 5.2. Done: unit tests for a 0.1 m block and a wall end on (no change to the shape), a lone tree (depth at most the cap times its weights), a stand of known density (depth within 10% of 4.2's), a veteran oak walked round at 6 m in 1 cm steps with no direction changing by more than 4.2's bound for the step plus 1 mm, a ring coming into sight, a BigRock turning from end on to wide with its shape equal to `computeVisibilityPolygon`'s for its ring and the forest rim, a wall run at 30 degrees cutting its exact shadow, and a clear direction untouched; forest depth pinned and identical on MSVC, Clang, and GCC.
4. The record. Legs with a polygon, the three paths, `setOpenLeg` with a polygon, `appendEdges`, `inRecord`, `takeChangedBoxes`, the bright shape's rule, and the sweep's builder on this document's shape. Done: tests for a straight walk through a stand (one leg while builds confirm), forest depth ending a leg, builds too far apart ending one, a walk from open ground into forest and to a BigRock; the record oracle (no point on the record outside a true shape) and the sim test that every seen point is on the mesh and the record never lost ground, run in forest.
5. The cover. The feather cells, the pending cuts, each colonist's feather and drawn shadows each frame, and the masks of 7.5. Done: soft edges on both edges in the game, triangles collapsing behind trunks and fins beside hard shadows, scenario 14's new steps passing, the capture of 12.
6. Tests and docs. Scenario 14's steps (14) written in, player-visibility.md's tunables and measured sections brought up to date. Done: scenario 14 run.

## 14. Tests and scenario steps

Unit and sim tests are named in each step's done line. Before they are added, check the existing vision and record suites for the same paths and extend those.

Scenario 14 (`docs/testing/scenarios/14-player-visibility.md`) takes these steps after its step 2, all at the quickstart landing, written in at step 6. Spawned things are not bodies (8), so the steps use the landing's placed trees and rocks, and built walls:

1. Zoom 3 on Bob among trees: the bright area's edge is a fade about 1.5 m wide with no hard line, and the tinted area's edge against the black fades on the tinted side; past it the black is solid.
2. Press V: Bob's rim is drawn in toward denser trees, never closer than two thirds of his sight, and is whole where the trees thin out.
3. Look behind a trunk 5 to 10 m from Bob: a faint darker triangle starts at its foot and closes behind it, faint and short behind a thin trunk, darker and longer behind a thick one.
4. Find a placed BigRock and stand Bob 8 m from it: a shadow runs from its near face to the rim, and beside each side a gradient widens toward the rim, hard at the rock's corner.
5. Walk Bob slowly past a trunk and past the BigRock: the triangle and the shadow turn and grow or shrink with him, with no jump, and the seen area behind him does not shrink.
6. Build a 0.1 m wall block, or the shortest wall the tools allow, 5 m from Bob: a barely visible triangle behind it, gone within a metre.
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

### Options Kevin picked

The wording of each option is the author's.

9. Of the design canvas's options, C, a wide eye plus a sight budget, and 2, feathered edges as vector geometry.
10. The feather inside the brighter area.
11. One sight shape for colonist knowledge and player visibility.
12. The feather built as alpha mask layers in salal.
13. The tapering shadows and fins drawn on the cover while the sim shape stays simple, the drawing able only to show ground darker than its true state.
14. One object's effect on range capped at a small amount, several adding up to a floor.
15. A drawn shadow's strength scaled by the thing's width.

### Author's proposals

Each is the author's and unconfirmed by Kevin.

16. `eyeWidth` 1.2 m to start. It is the least width at which every tree ring nav holds, at every angle, is narrower than the ray (a veteran oak's diagonal is 1.16 m), so no tree casts a sim shadow and trees affect sight "very slightly", through drawn triangles and forest depth; a 0.1 m block's triangle is 0.08 to 0.12 strong and about a tenth of its distance long; a wall run of 3 m or more is a full shadow seen at more than about 22 degrees off its line. A narrower ray makes mature and veteran oaks hard shadows at some angles and draws near-full streaks behind every trunk near its width, since a drawn shadow reaches full strength at the threshold.
17. Departure from C as drawn: the sim casts no shadow for a body narrower than the ray. Its collapsing triangle is only drawn (13), so a mover in it is in sight.
18. The width as seen as the extent across the silhouette's bisector, and a body whose hull holds the eye counted wide (2.2).
19. The drawn rule of 3.2: the triangle as the silhouette shrunk in angle toward its bisector, valued `strength * (1 - progress)`; a wide body's wedge drawn at 1 each frame; fins from the ray's edge through each corner, as every shadow side's soft edge in place of a feather; the drawing in float from each frame's position; and the bound of 3.3 at a downward crossing.
20. Departure from C as drawn: forest depth as a density over the placed rings in sight, with a tent over 22.5 degrees, a distance weight rising to 6 m and falling to 0 at the sight radius, a fixed `widthFactor` from the perimeter, and the 64 rim directions, in place of a budget spent along each ray (4).
21. Built walls spending no forest depth, and any wall in reach putting a build on the polygon path (6.1).
22. The placed occluder index in `NavigationSystem`: serials, buckets, the removal callback carrying its ids, updated in `collect()` in patch order (2.3).
23. Departure from C as drawn: `rememberArea` at the landing reading no occluders, so the crew's memory agrees with the plain disc the record commits (5.2).
24. Legs with a polygon, confirmation with a margin, the open leg as the same mechanism, and `setOpenLeg` taking a polygon; `inRecord` and `takeChangedBoxes`; the bright shape's rule (6.2, 6.3); 6.5's fallbacks.
25. The seen area's feather drawn for the union's boundary only, kept in cells rebuilt from changed boxes, with pending cuts held and redone at a moving end; a body face left hard in the tint (7.2).
26. Departure from C as drawn: several colonists composed as the least darkness, each colonist's darkness the greatest of his pieces, each piece clipped to his bright shape; and the lens, where two sights overlap by under twice the feather, left dimmer than a fade from the union's edge (7.4).
27. Departure from C as drawn: under the alpha mask layers pick, destination alpha as the partial layer, with depth and the stencil beside it, asked for by step 0 and cleared and owned by the cover; the reading of rule 10 that this per-pixel frame state is no raster or mask texture, and of rule 19 that its stencil covers the hard layers while the partial layer uses alpha and depth (7.6).
28. The salal surface: `MaskMode::Luminance`, `MaskImage::content`, `MaskBlend::Lighten` and `Darken`, `MaskIsolation::Isolate`, `MaskPaint::linear`, mesh children with a value at each vertex, and children carrying their own hard mask (7.6).
29. Departure from C as drawn: every start value in 9 and every acceptance limit in 10.

## Related documents

- [Player Visibility](./player-visibility.md): the rules, the seen record, the cover, and the input rules this builds on
- [Vision Architecture](./vision-architecture.md): sight shapes, discovery, doors and windows
- [Nav Mesh: One Mesh, Variable Resolution](./nav-mesh-variable-resolution.md): the collision map and where it exists
- salal `docs/masks.md`: the mask API this extends
- [Testing](../testing/README.md): scenarios and the perf rule
