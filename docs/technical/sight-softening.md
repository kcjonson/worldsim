# Sight softening

**Created:** 2026-11-16
**Companions:** [Player Visibility](./player-visibility.md) (the seen record and the cover), [Vision Architecture](./vision-architecture.md) (where sight shapes come from), [Nav Mesh](./nav-mesh-variable-resolution.md) (the collision map sight reads)

Sight is two layers kept strictly apart. The **sight shape** is what the simulation uses for colonist memory, discovery, `isVisible`, and the seen record: one polygon, star-shaped round the eye's centre, in which every occluder takes a **share** of sight on one continuous scale, from nearly nothing for a 0.1 m block to a full shadow once it is as wide as the eye, growing and shrinking smoothly as the colonist walks. The **drawn layer** is the cover's only: shadows behind partial occluders whose strength follows their share of the eye and which taper along their length, and fins beside full shadows, able only to show ground darker than the sim has it. Both edges of the cover fade over the feather width inside the brighter area, and where several colonists see a point its light is the greatest any of them gives it. What occludes is read from the collision map, never from a list of kinds.

Terms are player-visibility.md's: **player visibility**, the **unexplored area**, and the three areas **unseen**, **seen**, and **visible**. An **occluder** is a group of touching collision outlines (2.2). `S` is the sight radius (30 m for a colonist), `R` the **reach**, sight plus the 0.5 m rebuild step, `E` the eye width, and `c` the eye's centre, the observer's position at the build.

---

## 1. Rules

The rules are player-visibility.md's 23 to 27 (2026-11-16), which replace its rules 5 and 11. In short: collidable objects from the collision map less movers affect sight, walls by their band outline (23); every occluder takes from sight on a scale with how much of the eye it covers as seen, from nearly nothing to a full shadow, never flipping, with the shape colonists know kept simple and the tapering shadows and fins drawn on the cover only, their strength following the occluder's share of the eye (24); both cover edges fade inside the brighter area, and rule 10 stands (25); one sight shape serves colonist knowledge and player visibility, and the drawing is never read by the sim (26); the fade is built as alpha mask layers in salal (27).

## 2. Occluders

### 2.1 What occludes

The collision map is the nav mesh's blocked rings: wall bands and junctions, placed obstacles, and water. Sight reads two of those kinds, and reads walls through construction rather than through nav:

| Source | Sight |
|---|---|
| A built wall | Occludes by its band outline: `geometry::wallPiecesAndOutline(...).outline` (`libs/geometry/offset/WallOffset.h`), the boundary of the union of the trimmed bands and junctions, cut at every opening whose type is `transparentToSight` (`ConstructionRegistry.h`), as `GeometryIndex` keys its gaps on that flag. Its share follows its width as seen, like any occluder's; at a full share sight stops at its near face |
| A placed obstacle (`kProvenancePlaced`) | Occludes by the ring nav holds for it |
| Water (`kProvenanceWater`) | Never occludes: water has no height |
| A ford (`kProvenanceFord`) | Never occludes: a cover-only ring |
| A mesh limit or the walkable bounds (`kProvenanceBorder`) | Never: bounds rings, `blocked = false` (`NavInputBuilder.h`) |

Movers are never rings, so a colonist never hides another. A door and a window pass sight (vision-architecture.md D7); a closed solid door, when there is one, flips the same flag. The wall outline is the same geometry as nav's wall rings, the bands and junctions `resolveWallBands` gives, with windows cut as well as doors, so walls and placed obstacles together are every blocking thing in the collision map that stands above the ground.

What lands in the set follows the collision data alone, and no code in vision names a kind of thing. A tree's trunk is a Rect collider, so it is in; a bush, groundcover, and most rocks have no collision, so they do nothing to sight; a BigRock is in. Nav pads a Rect collider by `kPlacedObstaclePadMm` (50 mm, `PlacedObstacle.h`) on every side before scaling by the entity's scale (`placedObstacleBox`, `NavInputBuilder.cpp`), and sight reads that ring as it is. Trees take no `randomRotation` and no placement scale, so their rings are axis-aligned squares of the trunk width plus 0.1 m; a BigRock (`randomRotation` 180) is a 1.5 m by 1.17 m rectangle at any angle. A square's width as seen runs from its side face on to its diagonal, `sqrt(2)` times the side; a rectangle's from its shorter side to its diagonal:

| Ring | Face on | Worst angle |
|---|---|---|
| A 0.1 m wall block (a wall outline, unpadded) | 0.10 m | 0.14 m |
| A 0.1 m post | 0.20 m | 0.28 m |
| A pine | 0.28 m | 0.40 m |
| An oak sapling | 0.35 m | 0.49 m |
| A mature oak | 0.55 m | 0.78 m |
| A veteran maple | 0.60 m | 0.85 m |
| A veteran oak | 0.82 m | 1.16 m |
| A BigRock | 1.17 m | 1.90 m |

### 2.2 Groups and what is measured

Placed rings that touch or overlap are one occluder, and the connected parts of the wall outline are one each. A placed ring touching a wall is its own group: in front of a wall at a full share it changes nothing, and beside a stub both take their own shares. Groups depend on the rings alone, never on where the observer stands, so walking never merges or splits one; they change only when a ring is added or removed.

Every quantity is taken from `c` and is continuous in it while `c` is outside the group, which it always is: a mover's centre stays outside blocked rings (routes keep its clearance and static collision pushes it out), and a group whose outline holds `c` is skipped for that build.

- The silhouette is the arc of directions from `c` whose rays meet the group, from `A1` to `A2`, and its angle `theta = A2 - A1`. A connected group seen from outside meets one arc, and the arc's ends move continuously, even where the extreme vertex changes, since the extreme direction is the greatest of continuous angles. A group that closes round `c` (a room with no opening) meets every direction and is at a full share.
- The width as seen `w` is the group's extent across the bisector of its silhouette: the greatest less the least of `dot(v - c, n)` over its vertices `v`, `n` the unit direction across the bisector. A square face on gives its side, at its diagonal the diagonal. It is continuous for the same reason: the bisector turns continuously and the extent of a fixed set of points across a turning direction changes by at most the set's diameter `G` times the turn.
- The distance `D` is from `c` to the group's nearest point, exact in integers and one square root.
- The distance along a direction `d(u)` for `u` in the silhouette is, for a group of placed rings, the greater of `D` and the distance along `u` to the group's convex hull (0 while `c` is inside the hull), and for a wall group, the distance along `u` to its outline. For one ring the hull is the ring, and `d(u)` is the ring's near face.

The hull keeps `d(u)` continuous for touching rings: the first hit on a concave group jumps when the ray slips off a near member onto a far one, and the hull's does not. With `c` in a notch between touching rings, inside their hull, `d(u)` is `D` across the silhouette. Wall groups take their outline so that a partial wall group reaching a full share becomes its exact shadow (3.3); where a wall group is concave and partial, `d(u)` can jump by up to its depth, which the range takes only times its share (3.4).

### 2.3 Where vision reads them

`GeometryIndex` (`libs/sim/vision/`) gives the wall outline, from the change "wall sight occluders" on Specboard. Placed obstacles come from nav.

Vision runs at priority 45 and nav at 51. The patch job nav submits at the end of a tick runs on its worker into the next tick until nav's `settle()` collects it, and `navMesh()` joins it too, so a vision read of the mesh would stall the main thread on the job. Vision never reads the mesh. `NavigationSystem` keeps a **placed occluder index** on the main thread: each placed ring in the mesh with its outline, box, group, and a **serial** from a counter that never repeats. Nav tracks every gathered placed obstacle by key in `obstacleInMesh` (`NavigationSystem.h`), and each limit's patch hands back the ids it added in its collect callback (`obstacleInMesh.at(keys[k]) = added[k]`, `NavigationSystem.cpp`). The index is updated there, in `collect()`, once per joined job: each added ring's outline is read with the public `PatchableMesh::ring(id)` and takes a new serial, and the ids a removal patch took out, which `gatherRemovals`'s callback holds, leave the index. `join()` is const and runs from every query, so the index is never touched there. A ring's key is its chunk, position, and def, so a thing that moves is a removal and an add, and takes a new serial. Groups are kept with the index: an add joins the groups its ring touches, a removal splits its group into the connected parts left.

Vision reads the index read-only. A ring the last job patched shows to vision from the tick after nav collects it; a felled tree occludes for that long.

An observer's cached shape is rebuilt when he moves more than the rebuild step from its build, when the walls' version moves, or when the sorted serials of the placed rings in groups in reach change. The mesh's epoch cannot stand for that, since movers patch it every 4 m of travel. Ring ids cannot either: `PatchableMesh` recycles a freed ring's id through its free list, so a felled tree's id can return as a new tree's. A serial is never reused.

The mesh exists wherever an observer can see: every mover meshes the disc of sight plus 16 m round him, and the landing 150 m. Ground a colonist reaches before nav meshes it (a dev teleport) has no placed occluders until the mesh arrives.

## 3. The sight shape

### 3.1 The share

A group's share is

```
x = min(1, w / E)
s = x ^ kSightShareExponent
```

with `E` 1.5 m (`kEyeWidthMm`) and the power 4 to start. `s` is continuous and rises steadily with `w`, is near 0 for small things, and is 1 from `w = E` on: a group as wide as the eye casts a full shadow. Walls are on the same scale: a long wall seen across is far wider than the eye, a 0.1 m block is not. At the worst angle:

| Ring | `s` face on | `s` worst angle |
|---|---|---|
| A 0.1 m wall block | 0.00002 | 0.00008 |
| A 0.1 m post | 0.0003 | 0.0013 |
| A pine | 0.0012 | 0.0049 |
| An oak sapling | 0.0030 | 0.012 |
| A mature oak | 0.018 | 0.072 |
| A veteran maple | 0.026 | 0.10 |
| A veteran oak | 0.089 | 0.36 |
| A BigRock | 0.37 | 1, from any angle where it is 1.5 m wide or more: 76 degrees of every 90 |

The fourth power is what keeps small things small: a ring a third of the eye wide takes about 1% of the sight past it, a ring half the eye wide 6%.

### 3.2 Range in a direction

A group at share `s < 1` acts on the directions of its silhouette with a **weight** `a(u)` that rises from 0 at each end of the silhouette to 1 over a **ramp** of angle `h = (1 - s) * theta / 2`, and is 1 between the ramps. Where the silhouette is narrow the two ramps meet in the middle, and at the middle the weight is 1. Its effect on direction `u` is `sigma = s * a(u)`.

The range in a direction starts at the reach and takes the groups whose silhouettes hold the direction nearest first by `d(u)`. Each takes its share of the sight left beyond it:

```
if d < range:  range = range - (range - d) * sigma
```

A group at a full share sets the range to its own distance; one at a small share takes a small part of what lies past it; a group past the shortened range is behind what was taken and takes nothing. The range is never nearer than the last group that took from it, so the ground before a partial occluder, and the occluder's own footprint, stay in sight: a partial occluder shortens sight in its directions and never hides ground nearer than ground it leaves in sight.

Two groups at the same distance give the same range in either order, so ties are taken by group id with no effect on the result. A direction no group's silhouette holds is untouched: a clear direction is never shortened.

**Against what baseline.** The range is computed to the reach, because a held shape must cover sight round every point within a step of its build; vision holds what it decides to `S` round where he stands (player-visibility.md 3.2). Taking a share of the sight to the reach and taking it of the sight to `S` and carrying the result a step further by the sight left, `range_S + step * product(1 - sigma)`, are the same number, so the reach is the right baseline and nothing is lost by it. What a decision sees at the build point is the nearer of the range and `S`, so a group shows only once `(R - d) * sigma` passes the step times the sight it leaves. The tables in 3.5 give that rim.

### 3.3 Full shares and the shape

Each build sorts the groups in reach by share:

- A wall group at a full share goes to `geometry::computeVisibilityPolygon` as occluder segments, its outline's edges: its shadow is exact. This is the **full polygon**.
- Every other group, placed groups at a full share included, goes into the **share ring**: the ranges of 3.2 evaluated at a set of directions and joined by straight sides.

The share ring's directions are the ends of every group's silhouette and ramps (`A1`, `A1 + h`, `A2 - h`, `A2`), the directions of the vertices of each group's near side, and the directions of a fixed table of `kSightFieldDirections` (288, 1.25 degrees apart, `directionTable` at compile time, `core/DirectionTable.h`) that fall inside some group's silhouette. A placed group at a full share has ramps of no width: its silhouette ends carry two vertices each, one at the reach or the next group's range and one at its corner, and between them the ring follows `d(u)`, its near face or, where `D` is greater, an arc of the table's directions. The ring is cut to the sight polygon of the reach (`starClippedToConvex`), so a stretch no group touches has the sight polygon's vertices and no others of its own.

The shape is the nearer of the full polygon and the share ring along every ray from `c`, `geometry::starNearerOf(full, shares, centre)`, an angular sweep of the kind `starClippedToConvex` runs. The share ring is star-shaped round `c` by construction (one range a direction). `computeVisibilityPolygon` is star-shaped only up to the millimetre snapping of its hits and is not promised simple (`Visibility.h`): `starNearerOf` reads each ring's angles with `unwrapAngles`, takes a vertex that steps back in angle (by the snapping, under 1 mm) at its predecessor's angle, and requires each ring to wind once. Its result is checked with `geometry::simpleAround`, as `PlayerVisibilitySystem` checks vision's polygons before the record queues them: a shape that fails, which only the snapping can cause, serves memory and `isVisible`, which test points against it, and is not queued to the record; the next build's is.

Taking the nearer of the two rather than feeding the full polygon into the shares has one effect: a partial group in front of a full wall takes its share of the sight to the reach rather than to the wall, so it shortens a little less than the rule of 3.2 would. Full wall shadows stay exact for it.

With no group in reach, sight is the plain disc and no shape is built, the outdoor fast path. Everything vision decides with a shape is held to `S` round where he stands (player-visibility.md 3.2).

### 3.4 Continuity

The range in every direction is continuous in the colonist's position, except across the side of a full shadow, which turns about its corner as he walks, as every shadow does. Every input is continuous in `c` (2.2): `w`, `x`, `s`, `theta`, `h`, `D`, the ends of the silhouette, and, for a placed group, `d(u)`. The weight is continuous in direction and position as long as `h > 0`, that is for `s < 1`. Each step of 3.2 is continuous in its inputs, including where `d` reaches the range (the step takes nothing there) and where two groups swap order (equal distances give equal results). The share ring's vertices move continuously; a vertex that appears or goes, as a ramp's end crosses a table direction or a group comes into reach, changes the ring by at most the chord error between neighbouring directions, `R * (1 - cos(delta / 2))`, 1.8 mm at 30.5 m and 1.25 degrees.

**The bound.** Each step of 3.2 changes the range by at most `(R - d) * |change in sigma| + sigma * |change in d|`, and passes on a change in the range before it times `1 - sigma`, at most 1. So a direction's change is at most the sum over the groups in it of what each changes. For one group, per metre walked, with `D` its distance and `G` its diameter:

- the ends of its silhouette and its bisector turn at most `1 / D` radians;
- `theta` changes by at most `2 / D`;
- `w` changes by at most `G / D`, so `s` by at most `4 * x^3 * G / (E * D)`;
- `h` changes by at most `(1 - s) / D + |change in s| * theta / 2`.

Putting those into `sigma = s * a` and `a = min(1, distance from the nearer end / h)`, the range in any direction changes per metre by at most

```
K = (R - d) * (ds / (1 - s) + 2 * s * (2 - s) / ((1 - s) * theta * D)) + s * (1 + 2 * G / ((1 - s) * theta * D))
```

with `ds = 4 * x^3 * G / (E * D)`, and between any two positions by no more than the group's whole dent, `(R - d) * s` plus `s * G`. Worked at the worst angle, at arm's length (`D` 0.4 m) and at 10 m:

| Ring | `K` at 0.4 m | `K` at 10 m | Largest change at all |
|---|---|---|---|
| A 0.1 m wall block | 0.09 m per metre | 0.05 m per metre | 0.003 m |
| A post | 0.94 | 0.38 | 0.04 m |
| A pine | 3.1 | 1.1 | 0.15 m |
| A mature oak | 38 | 8.7 | 2.2 m |
| A veteran oak | 240 | 38 | 10.8 m |

So between two builds 0.5 m apart no direction moves by more than a group's dent, and a group's dent grows and shrinks with its width as seen. The large `K` near a wide group is its dent's side turning about it as he walks past, not a dent appearing: the side of a dent `theta` wide sweeps a direction in about `h * D` of walking.

The cases:

- A square ring turning from face on to its diagonal. `w` is its extent across the turning bisector and rises from the side to the diagonal at most `G / D` a metre; `s` follows it. A veteran oak walked round at arm's length goes from a 2.7 m dent face on to a 10.8 m dent at its diagonal over about 1.3 m of walking, through every depth between.
- A ring coming into reach. It enters at `d = R`, where its step takes nothing, and its factor `R - d` starts at 0.
- Touching rings. They are one group whatever he does (2.2), and the hull keeps `d(u)` from jumping where one member stands in front of another.
- The eye's disc overlapping a ring at arm's length. The sim reads only `c`, so nothing changes when the disc reaches a ring; `theta` grows toward 180 degrees as he closes on it, which widens the ramps and lowers `K`. The disc is the drawing's (5.3).
- The eye's centre in a notch of touching rings. `c` is inside the group's hull, so `d(u)` is `D` across the silhouette, continuous through the hull's edge, where the hull distance reaches 0 on the rays into the notch.
- A share reaching 1. As `s` rises to 1 the ramps close to no width and the weight to 1 across the silhouette, so the dent becomes the group's shadow from its near face; at `s = 1` it is that shadow, for a placed group in the share ring, and for a wall group in the full polygon, which is the same shadow. Nothing jumps: the range in each direction goes continuously to the shadow, and the side steepens into the shadow's side. No smoothing across directions is used, so nothing outside a silhouette is shortened and nothing behind a full shadow is in sight.

The one place a partial group's range can step is a concave wall group narrower than the eye, an L or a T stub, whose outline distance jumps by up to its depth where a ray slips off a near arm; the range takes that jump times the group's share. A convex wall group, a straight stub or a 0.1 m block, has none.

### 3.5 Worked examples

One group, a 30 m sight, `E` 1.5 m, at the worst angle. "Rim" is what a decision sees at the build point in the direction through the group's middle, the nearer of `S` and the range (3.2).

| Occluder | `w` | `s` | Rim at 2 m | Rim at 10 m |
|---|---|---|---|---|
| A 0.1 m wall block | 0.14 m | 0.00008 | 30 m | 30 m |
| A post | 0.28 m | 0.0013 | 30 m | 30 m |
| A pine | 0.40 m | 0.0049 | 30 m | 30 m |
| An oak sapling | 0.49 m | 0.012 | 30 m | 30 m |
| A mature oak | 0.78 m | 0.072 | 28.4 m | 29.0 m |
| A veteran maple | 0.85 m | 0.10 | 27.6 m | 28.4 m |
| A veteran oak | 1.16 m | 0.36 | 20.3 m | 23.2 m |
| A veteran oak, face on | 0.82 m | 0.089 | 28.0 m | 28.7 m |
| A BigRock, end on | 1.17 m | 0.37 | 20.0 m | 22.9 m |
| A BigRock, otherwise | 1.5 m or more | 1 | a full shadow | a full shadow |

Ordinary trunks cost nothing a decision sees from any angle, apart from a mature oak or a veteran maple seen at its diagonal, which pulls the rim in by a metre or two in its directions. A veteran oak takes a third of the sight past it at its diagonal and a tenth face on. A BigRock is a full shadow from most sides and a deep dent end on.

**Stands.** Trunks at `n` to the square metre with mean width as seen `w_mean` cross a ray about `n * w_mean` times a metre, and across a silhouette the mean weight is `(1 + s) / 2`, so each crossing takes a mean share `sigma_mean = s * (1 + s) / 2` of the sight left beyond it. Counting only trunks nearer than the range as it shortens, the range in expectation is `ln(1 + k * R) / k` with `k = n * w_mean * sigma_mean`, means taken over every angle:

| Stand | Rim |
|---|---|
| Pines 2 m apart | 30 m |
| Mature oaks 5 m apart | 30 m |
| Mature oaks 3 m apart | 29.6 m |
| Mature oaks 2 m apart | 28.5 m |
| Mature oaks 1.2 m apart | 25.6 m |
| Veteran oaks 4 m apart | 26.4 m |
| Veteran oaks 3 m apart | 24.0 m |

The quickstart landing's density is read by the prototype (9).

### 3.6 What uses the shape

`VisionSystem::update` builds the shape for every observer with a group in reach, on the rebuild rule of 2.3. `sightShape(observer)` carries, beside the centre, radius, polygon, revision, and the walls and openings seen, whether a group at a full share was in reach, which decides the record's path (4.1), and the build's ranges at the table's directions when none was, which a leg reads (4.2). `polygon` is set whenever a shape was built.

Memory, discovery, and `isVisible` test that a thing lies within sight of where he stands and in the shape (player-visibility.md 3.2). Among trees the shape has up to a few hundred vertices and the test runs for every candidate; the shape is star-shaped, so a test by angle (one binary search and one side test) is its cheaper form, and the prototype measures which is needed.

`VisionOverlay` (V) draws each observer's shape, each group with its share, the full polygon and the share ring in two colours, and the eye's disc.

**The landing.** `PlayerVisibilitySystem::revealLanding` commits a plain disc of `kLandingSightMeters`. `VisionSystem::rememberArea`, whose one caller is the landing (`GameScene.cpp`), makes no `queryOccluders` call and builds no shape: it gives every crew member the plain disc's things. Rules 8 and 17 say the crew saw the landing as they came down, and the seen area and what the crew knows agree.

### 3.7 Integers

Every quantity is an integer: millimetres, angles as a table index and a place within the table's wedge, and fractions in `kFractionOne` (65536) parts. The result is the same on every toolchain.

- Angles. The table is `directionTable<288>(kSightFieldDirectionScale)`, scale `2^20`. A direction `v` in the wedge from `t_i` to `t_(i+1)` has place `cross(t_i, v) * kFractionOne / (cross(t_i, v) + cross(v, t_(i+1)))`, which rises steadily from 0 to `kFractionOne` across the wedge; the angle is `i * kFractionOne + place`. The direction at a given place is `t_i * (kFractionOne - place) + t_(i+1) * place`, whose place is that place exactly. `theta`, `h`, and the weight are taken in these units.
- Magnitudes. Group vertices relative to `c` are under `2^23` mm (8.4 km) in each coordinate, so a cross product with a table direction is under `2^44` and a place's numerator under `2^61`. For distances along a direction, a group's edges are cut to the box of the reach first, so their ends are under `2^16` mm and a hit's numerator times the direction's length is under `2^54`. `w` in millimetres is a cross product over the table direction's length, under `2^24`, and `x` is `w * kFractionOne / kEyeWidthMm`, under `2^40`. `s` is `kSightShareExponent - 1` products of fractions, each under `2^32` and divided by `kFractionOne` after each. A step of 3.2 multiplies a range difference under `2^17` mm by `sigma` under `2^17`.

```cpp
for (const ShareCrossing& crossing : crossingsNearestFirst) {
    if (crossing.distanceMm >= rangeMm) {
        break;
    }
    const std::int64_t taken = crossing.share * crossing.weight / kFractionOne;
    rangeMm -= (rangeMm - crossing.distanceMm) * taken / kFractionOne;
}
```

## 4. The seen record

The record is player-visibility.md's: an exact polygon union in a second `PatchableMesh`, star-shaped shapes in, the covered test, the frontier, and the skirt (3.3 there).

### 4.1 Three paths

| At the build | Path |
|---|---|
| No group in reach | An open leg: one capsule a straight walk |
| Groups in reach, every share under 1 | A leg with a polygon (4.2) |
| A group at a full share | The polygon path: each shape queued with its walk, the skirt, frontier pieces, and the sweep |

A leg ends when the path changes, as an open leg ends when walls come into sight. A group whose share reaches 1 changes the path; the record only grows, so nothing is lost at the change. The sweep's builder (`SeenArea::setSightBuilder`) builds this document's shape at its point, so a sweep point sees what vision's own build there would. Which path is common among trees depends on the stand: a BigRock seen across its long side, a wall, or a pair of touching veteran oaks put a build on the polygon path, and the prototype counts builds on each (9).

### 4.2 Legs with a polygon

An open leg is one ring however long the walk, and rests on the outdoor shape being convex: the hull of a convex polygon at two ends holds that polygon at every point between, so the ring only grows. A shape with partial groups is not convex. A leg with a polygon holds a convex polygon `Q` fixed at its first build, and its ring is `Q` swept along the leg.

**The leg polygon.** At the first build, at position `p0`, `Q` is the intersection of one half-plane per table direction `t`, `{y : (y - p0) . t <= range(t) - kSoftLegToleranceMm - chord(range(t))}`, the ranges being that build's, cut to the sight polygon of sight less `kSeenSlackMm` round `p0`. `chord(r)` is `r * (1 / cos(delta / 2) - cos(delta / 2))` for the angle `delta` between two table directions, 3.6 mm at 30 m and growing with range, from a compile-time ratio: where two half-planes meet, their corner can stand that much past the side the shape takes between the same two directions. `Q` is found exactly as the dual of the convex hull of the points `t / h`, in integers, its vertices rounded toward `p0`.

**Confirmation.** A build at `p` confirms the leg when its shape holds `Q` moved to `p` and grown by the **margin** `r`, half the distance from the build before it plus `kSeenSlackMm`, its sides moved out by `r` and mitred, tested exactly: every vertex inside the shape and no side crossing it. The build before it must hold the same grown polygon too, which the leg checks against that build's shape, kept with the leg. The first build checks itself by the same test, so the chord allowance places `Q` and does not keep it inside. The leg's ring is the hull of `Q` at `p0` and at the last confirmed build, `setOpenLeg` passing `Q` (4.3). Every point of the leg's segment lies within the margin of a confirmed build, so every translate of `Q` along it lies in a confirmed build's shape.

The ring advances only to positions a build has confirmed. Between builds it stays at the last one, so it never holds ground no build has confirmed; it moves forward only along the leg, so it never shrinks (rule 16). The leg ends at the last confirmed build when a build does not confirm (a dent deeper than the tolerance where `Q` reaches, or builds farther apart than the tolerance can bridge), and by an open leg's rules: a turn, a turn back, a jump, or a change of path. The next leg starts at the new build with its ranges. The tolerance must exceed the largest margin a leg bridges.

**The open leg** is the same mechanism with `Q` the sight polygon of sight less the slack: with nothing in reach every position confirms it, so its ring's end is where he stands each frame.

A leg's ring goes through the frontier unskirted, as an open leg's does. A build that does not hold its own `Q`, which only rounding can cause, goes in as the polygon path's shapes do, skirted and swept.

What it gives up: ground between `Q` and the shape, the tolerance band and the part of a dent's notch a convex polygon cannot follow, enters the record only from another leg or a later build. At the exploring edge in a stand the record trails sight by that much, under the unseen fill.

### 4.3 The record's interface

- `SeenArea::setOpenLeg(leg, start, end, polygon)` takes the leg's convex polygon round its start in place of a radius (`SeenArea.h`): an open leg passes the sight polygon of sight less the slack, a leg with a polygon its `Q`. `legCapsule` is the hull of that polygon at the two ends, built in one pass over its sides.
- `SeenArea::appendEdges(low, high, out)` gives the record's edge in a box: every live triangle edge with nothing across it, as `(start, end)` with seen ground on the left, through `PatchableMesh::meshEdgesNear`, since the record's mesh is private (`SeenArea.h`). The cover's feather reads it (5.2).
- `SeenArea::inRecord(point)` is whether the committed record alone holds the point, by its mesh's `locate`. `isSeen` counts the pending shapes too, and the feather's cut needs the two apart (5.2).
- `SeenArea::takeChangedBoxes(out)` gives the boxes of the rings registered since the last call, which the feather's pieces are rebuilt from (5.2).
- `PlayerVisibilitySystem` holds each observer's path: open, leg with a polygon, or polygon path.

**The bright shape.** `drawnVisibleRadiusMm`'s guarantee, that the disc lies in the leg's capsule, rests on the open leg (`SeenArea.h`). `PlayerVisibilitySystem::drawnVisible` gives:

- on the open path, the disc of `drawnVisibleRadiusMm` round his `Position` that frame;
- on the other two paths, vision's shape cut to that disc round its build (`starClippedToConvex`), while the observer's path took this revision. After a jump, nothing until vision builds the next.

On a leg with a polygon the bright shape reaches past the leg's ring, by the tolerance and up to a build's spacing ahead of him; there it is under the unseen fill, which is opaque and drawn over the tint, so what shows bright is seen by the fills' order (player-visibility.md 3.1).

### 4.4 Star-shaped shapes

The skirt (`geometry::skirtedSightShape`), the frontier (`sightShapeFrontier`), the covered test's fan, and `simpleAround` all take a shape star-shaped round its centre. Every shape the record takes is: a leg's ring is a convex hull, and this document's shape is star-shaped round its build by construction and checked with `simpleAround` (3.3). The skirt grows a shape's sides off the rim, the share ring's among them, by `kSeenSkirtMm`, so seen ground can reach that far past a dented rim on the polygon path, as rule 21 allows past a shadow edge.

### 4.5 What a forest costs the record

The sweep's builder builds the whole shape every 100 mm of a walk that opens ground near its rim (`kSeenSweepSpacingMm`), with every group in reach. That, legs per kilometre, the registry's bytes per kilometre, and the record's tick are what the prototype measures at 1, 10, and 50 colonists (9), against player-visibility.md 6.5 and 6.7.

If the record costs too much in forest, the fallbacks in order:

1. The sweep skipped where every group at a full share in reach is placed: the seen edge past a rock's shadow then steps with vision's builds, 0.5 m.
2. A larger `kSoftLegToleranceMm`, so fewer builds end a leg: the record trails sight farther at the exploring edge.
3. 144 table directions in place of 288.

## 5. The cover

### 5.1 Two masks

The cover's two fills and their order are player-visibility.md 3.4's: the tint (black at `kSeenDimAlpha`) first, the opaque unseen fill over it. Each draws with a mask whose value at a point is how much of the fill shows there. With `fade(t) = max(0, 1 - t / kSightFeatherMm)`:

- The unseen fill's mask is 1 outside the seen area (the record and the pending shapes), and inside it `fade` of the distance to the seen area's edge (5.2).
- The tint's mask is the least, over the colonists, of each colonist's **darkness**: 1 outside his bright shape (4.3), and inside it the greatest of `fade` of the distance to his bright shape's edge and his drawn shadows (5.3). Light, one less the tint's mask, is then the greatest light any colonist gives a point (5.4).

The fade lies inside the brighter area on both edges: nothing unseen is ever shown even faintly, and the fully bright core of a 30 m sight is about 28.5 m round him. Sight, the record, and the bright shape are exact vector shapes; the fade is drawn and decides nothing. A click on ground under it is ruled by the record and by `isVisible`, never by the drawing.

### 5.2 The feather

`fade` of the distance to an area's edge is drawn exactly as the greatest of pieces, one per edge of the area's boundary and one per reflex corner. An edge's piece is the rectangle from the edge `kSightFeatherMm` into the area, 1 on the edge and 0 on its far side. A corner where the area's inside angle exceeds 180 degrees takes a fan of radius `kSightFeatherMm` round it over the gap between its edges' rectangles, 1 at the corner and 0 on the arc, in triangles of at most 15 degrees, within 1% of the fade on their chords. Inside the area the greatest of these is `fade` of the distance to the boundary, because the nearest boundary point to any point inside is a perpendicular foot on an edge or a reflex corner. Where a rectangle runs past the area's boundary it lies where the mask is 1 already. Where the area is narrower than twice the feather the pieces of its two sides overlap and the greatest is the nearer side's, so a beam of sight through a 1 m doorway is never fully bright and is never darkened twice.

This holds only when the pieces are drawn for the area's boundary and for nothing else: an edge two shapes share, or one inside another shape, must draw no piece, or it draws a dark seam inside the area.

**One colonist's bright shape** is one star-shaped polygon, so its boundary is its sides and needs no cut. A side lying on the face of a group at a full share draws no piece: the darker side is the occluder itself, a wall's or a rock's footprint, and the edge is hard there, so no fade darkens the ground in front of a wall or a rock. The build tags each side of the shape as rim, shadow side, or full face for this. On the open path his shape is a disc and its feather an annulus, built once and moved with him.

**The seen area** is the record's union with the pending shapes, so its boundary is found on the CPU, exactly and in integers: the record's edge (`appendEdges`) and the pending shapes' sides, each cut where it meets the others, a piece kept when the point just off its middle on its outer side lies in no other shape (the record by `inRecord`, a pending shape by its outline; a point on another shape's side counts as inside that shape when the shape lies on the outer side). The record's edge is the edge of its whole union, however many meshes the record is uploaded in, so several record meshes leave no seam.

- The record's pieces are kept in cells of `kSeenFeatherCellMm` (32 m), one uploaded mesh a cell, in as many meshes as 16-bit indices need. When `epoch()` moves, the cells meeting a box from `takeChangedBoxes` grown by the feather are rebuilt from `appendEdges` over the cell grown by the feather, and the rest are untouched: a registered ring changes the union's boundary only inside its own box, and a boundary edge split by the mesh's patch at a point within it draws the same rectangles and no fan, since the split point is straight. Pieces register one a tick (player-visibility.md decisions 13), so an epoch moves at most once a tick, and its cost is the record's edges in the cells one registered ring's box meets: a frontier piece is at most a sight shape's box, 61 m across, so at most 9 cells.
- Inside the feather of a pending shape the record's pieces are cut against it. The uploaded pieces are drawn only outside the pending shapes grown by the feather, a hard mask of their outlines, and inside that region the cut pieces are drawn with the pending shapes' own. A queued shape's cut is made once when it is queued and dropped when it leaves the queue. A leg's ring is the hull of its polygon at two ends, whose long sides are fixed lines while its end moves, so its cut is held and redone each frame only within the box of its polygon at its last two ends grown by the feather, and where a changed box of the record meets it. A frame's work is then the record's edges in one such box a moving leg, not the length of the leg.

### 5.3 One colonist's drawn shadows

For each colonist whose sight's box meets the view, the drawn shadows are built from his build's groups and position when vision builds it, kept until the next, and drawn each frame. They are never recorded and never read by the sim. With `x` a group's share of the eye (3.1), `D` its distance, and `L = D * x / (1 - x)`:

- A group at a share under 1 draws its **wedge**: the region between the rays from `c` past the two ends of its silhouette, from its near side outward. Its value is `x * taper(r)`, `r` the distance from `c`: the taper is 1 out to `D + L` and falls linearly to 0 at `D + kShadowTaperPercent / 100 * L`. Beside each side of the wedge, on the lit side, a feather piece `kSightFeatherMm * x` wide falls from the wedge's value to 0.
- Every group draws two **fins**. Beside each side of its silhouette at a corner `D_corner` from `c`, the fin is the wedge round the corner on the lit side out to the line from the far edge of the eye's disc (`E / 2` from `c`, across the line to the corner) through the corner. Its value falls linearly in angle round the corner from the wedge's value at the side (1 for a group at a full share) to 0 at that line. It widens with range, `(E / 2) * (r - D_corner) / D_corner` at range `r`, 1.5 m at a 30 m rim behind a corner 10 m off.
- A wall group's footprint past its near face is drawn at 1, as the wall's fill is opaque; a placed group's, inside its wedge, at the wedge's value.

Seen at its diagonal, a post 10 m off draws a wisp of 0.19 that is gone 4.6 m behind it; a pine one of 0.26 gone 7.2 m behind; a mature oak one of 0.52 that is still 0.08 at a 30 m rim; a veteran oak one of 0.77 to the rim; and each tapers along its length. As `x` rises to 1, `L` grows without bound, the taper is 1 to the rim, the side feathers reach `kSightFeatherMm`, and the fins reach a full shadow's. At `x = 1` the share is 1 too, the sim takes the wedge out of the bright shape, where the drawing already showed it at 1, and the side feather passes to the bright shape's own feather at the same width. The drawing is continuous in his position for the same reasons the sim's ranges are (3.4).

What is a disc eye's geometry and what is a rule:

- The umbra's length `L` is a disc eye's: the two lines from the eye's edges through a strip's edges cross `L` behind it. So is the fin's outer line, where a disc eye's penumbra beside an edge ends.
- The rest is a drawing rule. The wedge drawn is the point eye's, which inside `L` is wider than a disc eye's closing umbra. Past `L` a disc eye's hidden fraction falls toward `x` and never to 0; the drawing fades to 0 by twice `L`. At the side of a shadow a disc eye is half hidden; the fin falls linearly in angle from the side's value, where a disc's hidden fraction follows the area of a disc's segment. The strength `x` is the strip's share of the eye.

The drawn shadows only darken, and only ground the sim has in sight. A mover standing in one is visible to the sim, so it draws, under the tint, and can be selected, and a click there is ruled by the sim. Products such as the taper times a fin are evaluated on the CPU at the vertices of the pieces, which are cut at `D + L` and at the taper's end and subdivided until each triangle's linear field is within 2% of the product.

### 5.4 Several colonists

A colonist's darkness `dark_k` is 1 outside his bright shape and inside it the greatest of his feather and his drawn shadows. The tint's mask is the least darkness over the colonists, so light, `1 - mask`, is the greatest light any colonist gives a point, and one colonist's drawn shadows never darken ground another colonist lights: where another lights it fully, the least is 0. Where two colonists see a point, each in his own feather or shadow, the brighter wins. Where two bright shapes cross, the ground inside both but within the feather of both rims shows the brighter of the two fades, darker than a fade from the union's edge would be; that is the semantic, the brightest single colonist, and no seam.

It is drawn in a number of passes linear in the colonists in view, with no layer per pair (5.6). The prototype proves it at 50 colonists in view (5.7).

### 5.5 Composition

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

The tint's mask is one partial layer: the view at 1, then one isolated group a colonist, combined with what is under it by `darken`. A colonist's group holds his bright shape at 0 and over it his feather pieces and drawn shadows, combined among themselves by `lighten`:

```cpp
std::vector<MaskShape> tint = {{.points = viewPoints, .paint = MaskPaint::flat(1.0F)}};
for (const ColonistCover& colonist : colonistsInView) {
    MaskShape group{.blend = MaskBlend::Darken, .isolation = MaskIsolation::Isolate};
    group.children.push_back({.points = colonist.brightShape, .paint = MaskPaint::flat(0.0F)});
    for (const DarknessPiece& piece : colonist.pieces) {
        group.children.push_back({.points = piece.screenPoints, .paint = piece.paint, .blend = MaskBlend::Lighten});
    }
    tint.push_back(std::move(group));
}
Primitives::drawRect({.bounds = view, .style = {.fill = kTintColor}, .mask = {{.image = MaskImage::content(tint), .mode = MaskMode::Luminance}}, .key = coverKey});
```

A colonist's group covers only his bright shape, since his pieces lie inside it, so outside it the view's 1 stands. Inside, the group is the greatest of his pieces over 0, his darkness, and `darken` takes the least of that and what the colonists before him left.

### 5.6 Masks with partial coverage (salal)

salal's masks (`MaskImage.h`, salal `docs/masks.md`) are layers of `rect`, `polygon`, and `mesh` images composited by `MaskComposite::Add` and `Subtract`, each 0 or 1 at a pixel. Partial coverage takes CSS's and SVG's names and semantics:

- `MaskMode`, CSS `mask-mode`, per layer. `Alpha` is the default and what every hard layer uses. `Luminance` reads the layer's content as luminance; a hard layer's content is white where it covers, so the two agree on it.
- `MaskImage::content(children)`: an SVG `<mask>` whose content is the children. A child is a `<polygon>` (`.points`), an uploaded mesh under a camera (`.mesh`), or a group of children (`.children`, an SVG `<g>`). It is filled flat with a value (`MaskPaint::flat`) or by an SVG `linearGradient` in user space with two stops (`MaskPaint::linear(from, to, fromValue, toValue)`). A mesh child's vertices may each carry a value: a field linear over a triangle is one two-stop linear gradient, so each triangle is a `<polygon>` filled by the gradient its three values define. A child may carry its own `mask` of hard layers, as an element inside an SVG `<mask>` may.
- `MaskBlend`, CSS `mix-blend-mode`, per child: `Lighten`, the greater of its value and what is under it, and `Darken`, the lesser. `MaskIsolation::Isolate`, CSS `isolation: isolate`, makes a group composite its children together before it blends with what is under it.
- `MaskComposite::Add` and `Subtract` keep CSS's meanings on partial values: `add` is source over, `subtract` is source out.

Kevin's pick, alpha mask layers (rule 27), is masks whose value at a point is a coverage between 0 and 1, which salal carries as alpha. CSS composites alpha only by its Porter-Duff operators, none of which is the greater or the lesser of two values; `lighten` and `darken` are, and they blend colour, so a layer whose content combines values that way is read as luminance, its content painted in grey and the grey the coverage. The coverage it yields masks the draw as an alpha mask's does. The two words name one thing.

How salal evaluates partial coverage on the GPU is the prototype's to choose and is named nowhere in the API. The author's proposed build:

- The hard layers stay in the stencil's two bits.
- The partial layer is accumulated in the target's destination alpha. salal blends every channel with `GL_ONE, GL_ONE_MINUS_SRC_ALPHA` (salal `GLBackend.cpp`), so after the world draws, destination alpha holds whatever the world left, and the cover owns it: before each fill it writes the layer's starting value over the view with only alpha written, combines its children with a max or a min blend on alpha, draws the fill weighted by destination alpha, and when the cover is done writes alpha back to 1 over the view, so nothing drawn after it reads cover values.
- An isolated group's own greatest is kept in the depth buffer, which salal's targets have (the window's default and `GL_DEPTH24_STENCIL8` offscreen) and leave with the depth test off. For each colonist: his bright shape is drawn to a stencil bit and to depth 0; his pieces are drawn with each piece's value as its depth and the depth test keeping the greater, colour off, inside that bit; then the same pieces and the shape are drawn again with the depth test passing only where a piece's depth equals what is stored, writing their values to destination alpha with a min blend; the shape clears the bit last. Only the piece that holds the greatest passes the equal test at each pixel, so destination alpha takes the least over colonists of each colonist's greatest. The two passes share a vertex shader with an invariant position, so the equal test is exact. Each colonist costs his shape three times and his pieces twice, linear in the colonists.
- Rule 10: destination alpha, depth, and the stencil are per-pixel state of the frame being drawn, written and read inside the cover's draws, holding no game data between frames. No texture is made and nothing is stored as a raster, so rule 10's "no raster, grid, bitmap, or mask texture anywhere in this system" holds.

### 5.7 The prototype and its fallbacks

Step 0 is a sandbox scene of the cover's cases over a busy background: two record cells sharing an edge, a pending piece abutting the record along its edge, a pending leg overlapping it, two bright shapes overlapping and two abutting, a 1 m beam, a reflex corner, a fin and a tapered shadow, one colonist's shadow inside another's sight, and 50 colonists' shapes overlapping in one view with their shadows. A test reads the mask's value at chosen pixels (cores, mid-fade, overlaps, abutting edges, the shared cell edge, a reflex corner, a shadow under another colonist's light) against 5.1 to 5.4, and GPU time is taken at 1, 10, and 50 colonists. No golden image.

If the target has no destination alpha or depth, or the build cannot hold those values, the fallbacks in order:

1. Each colonist's pieces made disjoint on the CPU at each build, an overlay of their outlines with each cell carrying the greatest of the pieces over it, subdivided to within 2%, so that the min blend on destination alpha alone gives the least over colonists with no depth pass.
2. The partial layers drawn as fill-coloured geometry carrying alpha, source over, under the stencil's hard result. Where two pieces overlap (inside the feather of a convex corner, a narrow beam, a shadow over a feather) it darkens to `a + b - a * b` in place of the greater, and where two colonists see a point one's shadow darkens the other's light.
3. Hard edges, as player-visibility.md 3.4 draws them, with the sight shape and the record of this document.

### 5.8 Consequences taken on purpose

- Narrow seen and visible ground, a doorway's beam or a strip between two shadows, is never fully bright or fully tinted.
- The face of a group at a full share, a wall's or a rock's, is a hard edge in the tint (5.2), and so is the near side of a drawn wedge, where a trunk's foot begins; everywhere else the tint fades.
- Each partial occluder's footprint is drawn at its wedge's value, and a wall's at 1.
- The draw bound and the terrain scissor are player-visibility.md 3.4's: the fade lies inside the record, so nothing past it shows.

## 6. Input, things, and dev tools

The input rules (player-visibility.md 3.7) read the record and `isVisible`, which reads vision's shape. Among trees a colonist's bright shape can reach past the record by the tolerance at the exploring edge, under the black, where placing and ordering read unseen. A mover that is not a colonist shows while `isVisible` holds against this document's shape: behind a BigRock seen across its long side it is out of sight; behind a trunk it is in sight unless the trunk's dent reaches back to it, and drawn under any drawn shadow.

The visibility state view (`/api/state?what=visibility`) reports: builds by path (open, leg with a polygon, polygon path); legs with a polygon held, ended by a dent, and ended by a gap between builds; groups in reach and those at a full share, summed over the last builds; the largest change of one direction's range between consecutive builds of one observer, and the group it came from; sweep builds; record epochs a second and feather cells rebuilt; and shapes that failed `simpleAround`. `/api/dev/reveal` is player-visibility.md's.

## 7. Tunables

The starting values are placeholders, set by the prototype's screenshots and numbers.

| Name | Start | What it must cover |
|---|---|---|
| `kEyeWidthMm` | 1500 mm | The eye's width `E`. A group as wide as it casts a full shadow; it sets every share and every drawn wedge's strength and length |
| `kSightShareExponent` | 4 | The curve's power: the share is `x` to this power. Higher keeps trees smaller and steepens the climb to a full shadow |
| `kSightFieldDirections` | 288 | The share ring's table, 1.25 degrees apart. More adds vertices to every shape with a group in reach |
| `kSightFeatherMm` | 1500 mm | The fade on both edges. The bright core of a 30 m sight is sight less this; a shape narrower than twice it is never fully bright |
| `kShadowTaperPercent` | 200 | Where a drawn wedge ends, as a percentage of its umbra's length `L`; it fades from `L` to there |
| `kSoftLegToleranceMm` | 1000 mm | How far a leg's polygon sits inside its first build's ranges: how deep a new dent ends the leg, and the largest margin it bridges. Larger means fewer rings and a record that trails sight farther among trees |
| `kSeenFeatherCellMm` | 32000 mm | The record feather's cells: smaller rebuilds less at an epoch and draws more meshes |
| `kSightRebuildStepMeters` | 0.5 m | Player-visibility.md's |
| `kSeenSlackMm`, `kSeenLeastRadiusMm`, `kSeenSkirtMm`, `kSeenSweepSpacingMm` | player-visibility.md's | Legs with a polygon take the slack and the least radius as open legs do |

`kFractionOne` (65536) and `kSightFieldDirectionScale` (`2^20`) are fixed by 3.7 and are not tunables.

## 8. Prior art

| Source | What it does | What carried over |
|---|---|---|
| Orangy Tang, "Dynamic 2D Soft Shadows" | A light with a radius casts a hard umbra and a penumbra beside it, the penumbra drawn as geometry at each shadow edge | The eye as a disc for the umbra's length and the fin's outer line, drawn as geometry. The sim keeps a point eye |
| Scott Lembcke, "Super Fast Soft Shadows" | Shadow geometry per light, the penumbra of a light of given radius worked out per pixel | The same geometry. Its per-pixel shading is raster work this game does not do |
| Company of Heroes 2 | Sight blockers are an authored class: a thing blocks sight or does not | Set aside: what occludes is read from the collision map, and by how much from its width |
| Combat Mission, tabletop wargame rules | Line of sight through woods falls off with the depth of woods crossed | Trees in a direction shorten sight there, each by its share of what is left |
| League of Legends | A coarse grid upscaled and blurred (player-visibility.md 5) | Set aside: raster |
| Godot 2D lights, Unity URP Light 2D | Shadow edges softened by a filter and softness settings on the light, in the renderer | Set aside: raster. The fade is vector geometry with partial coverage |
| Order-independent min and max with the depth buffer (depth peeling, "depth equal" resolves) | The depth test as a per-pixel greatest or least, and an equal test to pick the fragment that holds it | The per-colonist greatest in 5.6 |

## 9. Performance plan

Measured, not assumed, under the machine-load rule in [testing](../testing/README.md): times count only with `idle` checks both sides, and counts do not depend on load.

What could move:

- Vision's tick. Every observer among trees builds a shape each rebuild step: the groups in reach from the index, widths, silhouettes, ramps, the share ring's ranges at its directions, the full polygon, and the nearer-of sweep. Memory tests candidates against the shape.
- The record's tick. The sweep's builder builds the whole shape every 100 mm of a walk opening ground near its rim, with every group in reach. Legs with a polygon, their confirmation tests and ring counts.
- The cover. The record feather's cells at each epoch, and the epoch rate; the pending cuts at each moving leg's end; the drawn shadows per build for colonists in view, tens of pieces a colonist in forest; the per-colonist passes of 5.6; the partial layers' GPU cost against the hard cover's.
- Views with reveal on draw no cover and build no pieces; captures should come out even with `main` apart from vision's tick.

To measure:

- Step 0, the sandbox: GPU time of the cover with its partial layers against the hard one in `perf_mask` at the same size, at 1, 10, and 50 colonists, and whether every case of 5.7 holds.
- Step 1, the prototype: "Prototype: sight ray casting across the nav mesh with tree attenuation, measure perf" on Specboard. A bench of a build at viewpoints in the quickstart landing's forest and in synthetic stands of known density: microseconds a build, groups in reach and at a full share, vertices out. The sim bench with 1, 10, and 50 colonists walking the landing's forest at 1x and 10x: vision's tick; the record's tick; the share of builds on each path; sweep builds and their cost; rings registered per kilometre and per minute; legs per kilometre; record epochs a second and cells rebuilt an epoch; the largest change of a direction's range between builds against 3.4's bound; and how far the record trails sight at the exploring edge. The landing's tree density, read from placement. Overlay and cover screenshots among trees for Kevin to judge `E`, the curve, the taper, and the feather.
- Step 5, the cover: a capture near the landing with `--no-reveal` at each zoom with the crew, 10, and 50 colonists: GPU and CPU time of the cover, pieces drawn a frame, the per-frame pending cuts, and the record's cells at an epoch.

No-go at step 1 goes back to Kevin with the numbers. For vision's build, 144 table directions, then a cached silhouette per group per observer. For the record, 4.5's fallbacks in order. For the cover, 5.7's.

## 10. Build steps

Each step is its own PR with its own done line and a `Perf:` line.

0. Partial coverage in salal. The sandbox scene and test of 5.7, then the API of 5.6. Done: a tagged salal whose `mask` scene shows every case of 5.7 with no seam, nothing darkened twice, and no colonist's shadow over another's light at 50 colonists, the value test passing on the GL backend, `docs/masks.md` brought up to date (`mask-mode`, `mix-blend-mode`, `isolation`, and the partial layer), and worldsim's pin on that tag; or the fallback of 5.7 that holds, with the test of its values.
1. Prototype the sight shape. As 9, on a branch that builds 3's shape from the walls' outline and nav's placed rings and runs legs and the polygon path in the benches only. Done: numbers in this document; Kevin's call on the tunables from the screenshots; a go, or the fallback taken for each of vision, the record, and the cover.
2. Occluders. The placed occluder index in `NavigationSystem` with serials and groups, updated in `collect()`; widths, silhouettes, distances; the cache keyed on serials. Walls by their outline come with "wall sight occluders". Done: a placed obstacle occludes and a mover, water, a ford, and a bounds ring do not; felling a tree rebuilds an observer's shape and walking over new ground with no obstacle does not; a tree placed on a recycled ring id rebuilds it; vision never joins the patch job (a test with the job held running).
3. The sight shape. 3.1 to 3.4 in `libs/geometry/visibility` (`starNearerOf` among them), `SightShape`'s fields, the overlay, and the landing pass of 3.6. Done: unit tests for a 0.1 m block (no change a decision sees), a post, a pine, a stand, a veteran oak walked round at arm's length in 1 cm steps with no direction changing by more than 3.4's bound for the step plus 2 mm, a ring coming into reach, touching rings, the eye in a notch, a BigRock turning from end on to a full share with its shape at the full share equal to `computeVisibilityPolygon`'s for its ring, a partial group behind a full wall taking nothing, and a clear direction beside a dent untouched; rings pinned and identical on MSVC, Clang, and GCC.
4. The record. Legs with a polygon, the three paths, `setOpenLeg` with a polygon, `appendEdges`, `inRecord`, `takeChangedBoxes`, the bright shape's rule, and the sweep's builder on this document's shape. Done: tests for a straight walk through a stand (one leg while builds confirm), a dent ending a leg, builds too far apart ending one, a walk from open ground into forest and to a BigRock; the record oracle (no point on the record outside a true shape) and the sim test that every seen point is on the mesh and the record never lost ground, run in forest.
5. The cover. The feather cells, the pending cuts, each colonist's feather and drawn shadows, and the masks of 5.5. Done: soft edges on both edges in the game, the drawn shadows tapering behind trunks, scenario 14's new steps passing, the capture of 9.
6. Tests and docs. Scenario 14's steps (11) written in, player-visibility.md's tunables and measured sections brought up to date. Done: scenario 14 run.

## 11. Tests and scenario steps

Unit and sim tests are named in each step's done line. Before they are added, check the existing vision and record suites for the same paths and extend those.

Scenario 14 (`docs/testing/scenarios/14-player-visibility.md`) takes these steps after its step 2, all at the quickstart landing, written in at step 6:

1. Zoom 3 on Bob among trees: the bright area's edge is a fade about 1.5 m wide with no hard line, and the tinted area's edge against the black fades on the tinted side; past it the black is solid.
2. Press V: Bob's rim is whole past pines and young trees, and shows shallow notches behind mature and veteran trunks; any BigRock in reach shows a full shadow or, end on, a deep notch.
3. Look behind a trunk 5 to 10 m from Bob: a darker taper starts at its far side and fades out along its length, faint behind a thin trunk and clear behind a thick one.
4. `/api/dev/spawn` a BigRock 8 m from Bob: a shadow runs from it to the rim with straight sides, and beside each side a gradient widens toward the rim.
5. Walk Bob round a veteran oak at arm's length (move orders round it): the notch behind it deepens toward its diagonal and shallows face on, with no jump; the seen area behind him does not shrink.
6. Draw a room with a door and a window and stand Bob inside: sight passes the doorway and the window and stops at the walls' near faces, and both sides of the light through the doorway fade.
7. Stand a second colonist beside Bob among trees: where either sees a trunk's taper, the ground is as bright as the brighter of the two has it, and no taper darkens ground the other colonist lights.

## 12. Decisions log

### Kevin's words

1. 2026-11-16, the brief: "we currently have hard edges on the sight cones, and visibilty edges, which we need to soften greatly ... to be less ... precise. for instance if there was just a single wall block a 0.1m in diameter, it really shouldn't effect sight. and right now trees have no effect, but they very slightly should and the cones/rays should kinda ... collapse behind things. and visual softening around the edges as well".
2. 2026-11-16, after a design canvas comparing sight models and edge treatments: "lets go with C + 2".
3. 2026-11-16, first question round, offered a list of what blocks or thins sight: "collidable objects. this needs to use the true collision map (minus movers) not bound to an arbitrary list."
4. 2026-11-16, to the building construction session, relayed: "Note, the shadows start at the wall center, not the wall edge, which is wrong." and "the wall fill is essentially always unseeable, its opaque." Walls occlude by their band outline (2.1).
5. 2026-11-16, after the second draft's review, shown pictures of a trunk switching between "narrow, no shadow" and "hard, full shadow" as a colonist walks round it, and offered rules that only moved the switch:

   > shouldn't things be on a scale, not on/off so they never "flip" but would grow as they walk?

   So there is no narrow class and no hard class. Every occluder from the collision map, built walls included, sits on one continuous scale: what it takes from sight grows smoothly with how much of the eye it covers as seen from where the colonist stands, from nearly nothing for a 0.1 m block to a full shadow once it covers the whole eye, and grows or shrinks smoothly as he walks. This replaces "things wider than the eye cast full shadows and narrower cast none" (7a) and the reading of walls as always hard. It supersedes the literal "capped per object" (7b) for wide things; small things stay small by the shape of the curve.

### Options Kevin picked (the wording is the author's)

6. C, a wide eye plus a sight budget; 2, feathered edges as vector geometry.
7. First round: the feather inside the brighter area; softened sight changes both colonist knowledge and player visibility, one sight shape; the feather built as alpha mask layers in salal. Second round, after three reviews of the first draft: (a) "Drawn, sim stays simple": colonist knowledge and the seen record use a plain shape, and the tapering shadows behind things and the fins beside full shadows are drawn on the cover each frame as not in sight right now, smooth, and able only to show ground darker than its true state; its narrow and hard classes are replaced by 5. (b) "Capped per object": one object pulls the rim in by at most a small tunable amount, several in a row adding up to a floor; superseded by 5 for wide things.
8. Third round, asked how strong the drawn shadow behind a thin thing should be: "Scaled by width". Its strength follows the thing's share of the eye, so a post draws a faint wisp and a wide trunk a clear taper, each fading along its length (5.3).
9. The values shown with the options, a 1 m eye, plus or minus 5 degrees of smoothing, a 1.5 m feather, a 3 m cap, and a 20 m floor, were the author's placeholders.

### Departures from option C as drawn on the canvas

10. C's budget spent the share of the eye an occluder hid. Here each group takes `x^4` of the sight left beyond it, `x` its share of the eye: the curve keeps trunks small, as the brief's "very slightly" asks, and taking a share of what is left counts trunks in line once each without the double count that moved C's rim 10 m between two builds at arm's length.
11. Shares are taken from the eye's centre with a ramp across each silhouette, not from a disc eye's hidden fraction at each point. In the sim a partial occluder's footprint and the ground before its range are in sight, and the closed umbra behind a thin thing is only drawn (from 7a).
12. Full shadows are cast from the eye's centre. Ground a disc eye would partly see on the shadow side of a full shadow's side is out of sight in the sim, and the fin on the lit side is only drawn (from 7a).
13. A drawn wedge fades to nothing by twice its umbra's length, where a disc eye's hidden fraction tends to the strip's share (5.3).
14. No floor: a floor would stop a group nearing the eye's width short of the full shadow it reaches there, against 5.
15. Where several colonists see a point its light is the greatest any of them gives, and one colonist's drawn shadows never darken ground another lights.

### Author's proposals

Each is the author's and unconfirmed by Kevin.

16. `E` 1.5 m and the share `x^4` (3.1), against which a 0.1 m block and a post change nothing a decision sees, a mature oak takes a metre or two only at its diagonal, a veteran oak up to a third of the sight past it, and a BigRock is a full shadow from most sides.
17. The width as seen as the extent across the silhouette's bisector; groups of touching placed rings kept by nav's index, a placed ring touching a wall its own group; a placed group's distance along a ray the greater of its hull's and its nearest point's, a wall group's its outline's (2.2).
18. The ramp `(1 - s) * theta / 2` inside each silhouette, the nearest-first rule taken to the reach, the share ring and full polygon joined by `starNearerOf`, and the continuity bound (3.2 to 3.4). A concave wall group narrower than the eye can step by its depth times its share (3.4).
19. The placed occluder index updated in `collect()` from `obstacleInMesh`'s added ids and the removal patches' ids, keyed by serials that never repeat (2.3).
20. `rememberArea` at the landing reading no occluders, so the crew's memory agrees with the plain disc the record commits (3.6).
21. Legs with a polygon: `Q`, confirmation with a margin, the open leg as the same mechanism, and `setOpenLeg` taking a polygon; `inRecord` and `takeChangedBoxes`; the bright shape's rule (4.2, 4.3); 4.5's fallbacks.
22. The seen area's feather drawn for the union's boundary only, kept in cells rebuilt from changed boxes, with pending cuts held and redone at a moving end; a full face left hard in the tint (5.2).
23. A drawn shadow's strength `x`, the strip's share of the eye, reading "scaled by width" (8) as the share of the eye and not the sim's `x^4`; its side feathers `kSightFeatherMm * x`; a wall's footprint drawn at 1 (5.3).
24. Several colonists composed as the least darkness, each colonist's darkness the greatest of his pieces, drawn with a depth pass and a min blend linear in the colonists (5.4, 5.6).
25. Under the pick "alpha mask layers", destination alpha as the mechanism for the partial layer, with the stencil and depth beside it, cleared and owned by the cover; and the reading of rule 10 that these per-pixel frame state are no raster or mask texture (5.6). The reading of rule 27 as replacing rule 19's "drawn with the stencil buffer" for the partial layer, rule 19's API rule standing.
26. The salal surface, `MaskMode::Luminance`, `MaskImage::content`, `MaskBlend::Lighten` and `Darken`, `MaskIsolation::Isolate`, `MaskPaint::linear`, mesh children with a value at each vertex, and children carrying their own hard mask, for Kevin to judge against CSS and SVG (5.6).

## Related documents

- [Player Visibility](./player-visibility.md): the rules, the seen record, the cover, and the input rules this builds on
- [Vision Architecture](./vision-architecture.md): sight shapes, discovery, doors and windows
- [Nav Mesh: One Mesh, Variable Resolution](./nav-mesh-variable-resolution.md): the collision map and where it exists
- salal `docs/masks.md`: the mask API this extends
- [Testing](../testing/README.md): scenarios and the perf rule
