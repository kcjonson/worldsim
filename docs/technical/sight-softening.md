# Sight softening

**Created:** 2026-11-16
**Companions:** [Player Visibility](./player-visibility.md) (the seen record and the cover), [Vision Architecture](./vision-architecture.md) (where sight shapes come from), [Nav Mesh](./nav-mesh-variable-resolution.md) (the collision map sight reads)

Sight is softened by two mechanisms kept apart. Every body in the collision map less movers has a **kind**, fixed by its own size and never by the angle it is seen from. An **opaque** body (a built wall, a big rock) casts its exact shadow in the sim, and its shadow's sides are drawn soft: a full-tint band beside each shadow side, a fin fading out from it, and close to the body a shadow side that still steps when vision rebuilds (3.1, 7.8). A **soft** body (a tree, a stub of wall) casts no shadow in the sim; a faint triangle that closes behind it is drawn on the cover, growing and shrinking with its width as seen as a colonist walks. **Forest depth**: the placed things in a direction shorten how far he sees that way, as a smooth density down to a floor, so nobody sees deep into a forest. The sight shape the sim uses is the convex forest shape, cut by the exact shadows of opaque bodies. What is drawn only darkens. Both edges of the cover fade inside the brighter area, and where several colonists see a point its light is the greatest any of them gives it.

Terms are player-visibility.md's: **player visibility**, the **unexplored area**, and the three areas **unseen**, **seen**, and **visible**. A **body** is one placed obstacle's collision ring or one connected wall graph's outline. Quantities used throughout, each with its name:

| Name | What it is |
|---|---|
| `sightRadius` | The observer's sight, 30 m for a colonist |
| `step` | The rebuild step, 0.5 m (`kSightRebuildStepMeters`) |
| `reach` | `sightRadius + lead`, the radius a polygon-path shape is built to: 30.7 m at 1x and 32.2 m at 10x, at 60 frames a second |
| `eyeCentre` | The observer's position at the build, the build eye |
| `lead` | How far a colonist can be from his build eye: `step` plus the farthest he walks in one of vision's intervals (`m_updateInterval`, 5 frames, `VisionSystem.h:206`). Computed at each of vision's passes from the measured frame interval, the interval's frames, and the sim speed (`TimeSystem::effectiveTimeScale()`), never a constant, and held to at most `kSightLeadMostMm` (8 m) so `reach` stays under `2^16` mm. At a walk of 2 m/s it is 0.67 m at 1x and 60 frames a second, 0.83 m at 1x and 30, and 2.2 m at 10x and 60 |
| `diameter` | The greatest distance between two vertices of a body's outline |
| `silhouette` | The arc of directions from the eye whose rays meet a body; `bisector` is its middle direction |
| `widthAsSeen` | A body's extent across its bisector |
| `distance` | From the eye to a body's nearest point |
| `cornerDistance` | From the eye to a shadow edge's corner (3.1), or to a soft body's vertex at one end of its silhouette |
| `rangeFromEye` | How far a point on the ground lies from the eye |
| `softRayWidth` | The width of the ray a soft body's triangle is drawn with (`softShadowRayWidthMm`) |
| `edgeWidth` | The width of the ray a shadow's soft edge is drawn with (`shadowEdgeWidthMm`) |
| `shadowLength` | How far behind a soft body its triangle reaches; `progress` is how far along it a point lies, 0 at the body and 1 at the tip |
| `forestDepth` | How much forest depth takes from the range across one side of the sight polygon |

---

## 1. Rules

The rules are player-visibility.md's 23 to 27 (2026-11-16). What affects sight is read from the collision map less movers, and a wall is its whole body (23). Each thing has a fixed kind by its size, so nothing changes kind as a colonist walks: small things cast a faint soft shadow that collapses behind them, drawn on the cover only and scaled by their width, and walls and large things are opaque with soft edges, a full-tint band beside each shadow and, close to the thing, a shadow side that still steps at a rebuild (24; 3.1, 7.8). Things in the way shorten how far a colonist sees, one thing by a small capped amount and many down to a floor (25). Both cover edges fade inside the brighter area, built as alpha mask layers in salal, and rule 10 stands (26). One sight shape serves colonist knowledge and player visibility, and what is only drawn is never read by the sim (27).

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
| A wall longer than 0.5 m and under `sqrt(0.36 m^2 - thickness^2)`: under 0.58 m long at 0.15 m thick, under 0.52 m at 0.30 m | its band end on | under 0.60 m | Soft |
| A pine | 0.28 m | 0.40 m | Soft |
| An oak sapling | 0.35 m | 0.49 m | Soft |
| A mature oak | 0.55 m | 0.78 m | Soft |
| A veteran maple | 0.60 m | 0.85 m | Soft |
| A veteran oak | 0.82 m | 1.16 m | Soft |
| Every longer wall: 0.52 to 0.58 m long or more, by its thickness | its band end on | 0.60 m and up | Opaque |
| A BigRock, 1.40 by 1.07 m | 1.07 m | 1.76 m | Opaque |

### 2.2 Kinds

A body's kind is decided once, from its own outline, when it enters the index that holds it, and never from where it is seen:

- A wall body is opaque when its `diameter` is at least `wallOpaqueLeastDiameterMm` (600 mm), and soft below it.
- A placed body is opaque when its `diameter` is at least `placedOpaqueLeastDiameterMm` (1400 mm), and soft below it. That lies between the widest trunk (a veteran oak, 1.16 m across its diagonal) and a BigRock (1.76 m), so every tree is soft and a BigRock is opaque.

Which threshold applies is the body's provenance in the collision map (a wall outline from construction or a `kProvenancePlaced` ring), never a list of asset kinds.

The `diameter` is exact: the greatest squared distance between two outline vertices (`Int128` products, `core/Int128.h`), against the threshold squared. A wall graph the band offsetter rejects is outlined as each wall's untrimmed band on its own (`GeometryIndex.cpp`); its body is still the whole connected graph, its outline the set of those band outlines and its `diameter` taken over all their vertices, so a graph's kind follows its walls, not whether the offsetter took them, but for the little an untrimmed end can add across a junction.

A kind changes only when the world does. Felling, placing, or building replaces a ring, and the new ring takes its kind from its own size. A tree whose collider grows between growth stages leaves nav as one ring and enters as another (`queueRemovedDetail`, then a limit's add), so if a stage ever crossed `placedOpaqueLeastDiameterMm` it would become opaque at that change, as if a rock were placed there.

### 2.3 What is measured

Every quantity is taken from the eye: the build's `eyeCentre` in the sim, the colonist's position that frame in the drawing. A mover's centre is held outside blocked rings (routes keep its clearance and static collision pushes it out), so the eye is never inside a placed ring.

- The `silhouette` runs from the direction of the body's extreme vertex on one side to the other's. A body seen from outside its convex hull meets one arc less than 180 degrees wide, whose ends move continuously as the eye moves. A soft body is at most 1.4 m across and the eye is held outside it, so its silhouette is always such an arc.
- `widthAsSeen` is the greatest less the least of `dot(vertex - eye, across)` over the body's vertices, `across` the unit direction perpendicular to the bisector. A square face on gives its side, at its diagonal its diagonal; it is never more than the body's `diameter`. It is continuous in the eye's position.
- `distance` is from the eye to the body's nearest point, `cornerDistance` to the vertex at that end of the silhouette.

### 2.4 Where vision reads them

**Walls.** Walls come from `GeometryIndex` (`libs/sim/vision/`). The "wall sight occluders" work ("Wall sight occluders are the wall's center line, so the visibility tint starts mid-wall" on Specboard) has `rebuild()` walk `geometry::connectedWallGraphs` and outline each graph into per-segment `OccluderRecord`s, each `{source, seg}`: a `SegmentId` and one edge (`libs/sim/vision/GeometryIndex.h:45-48`). In this design each record also carries its body, the graph it came from, and the index keeps one record a body with its edges, box, `diameter`, and kind, made in the same pass; that grouping is step 2's (13). `VisionSystem::update` and `rememberArea` (`VisionSystem.cpp`) and the sweep's builder (`PlayerVisibilitySystem.cpp`) call `queryFacingOccluders`, and only `StoreOrDrop.cpp` calls `queryOccluders`. With kinds:

- `queryFacingOccluders` returns the edges of opaque wall bodies only, less their back faces when `backFacesHiddenFrom` holds, and both `backFacesHiddenFrom` and `insideSolid` read opaque bodies only. A soft wall body is left out of every sight query, so the visibility polygon never sees it.
- `queryOccluders` keeps every wall, soft ones included: `StoreOrDrop` asks whether an arm reaches a box, and no wall, however short, lets an arm through.
- The structure pass (`rememberStructures`) reads every wall body, soft ones included, so a stub a colonist sees joins memory and, through `segmentsSeen`, the record (rule 22).

**Placed obstacles.** Vision runs at priority 45 and nav at 51. The patch job nav submits at the end of a tick runs on its worker into the next tick until nav's `settle()` collects it, and `navMesh()` joins it, so a vision read of the mesh would stall the main thread on the job. Vision never reads the mesh. `NavigationSystem` holds a **placed occluder index** on the main thread: each placed ring in the mesh with its outline (stored counter-clockwise), box, perimeter, `diameter`, kind, the sim time it arrived, and a **serial** from a counter that never repeats.

- Adds. Nav tracks every gathered placed obstacle by key in `obstacleInMesh` (`NavigationSystem.h`), and each limit's patch hands back the ids it added in its collect callback (`obstacleInMesh.at(keys[k]) = added[k]`, `NavigationSystem.cpp`). The index takes each added ring there, its outline read with `PatchableMesh::ring(id)`, under a new serial. An add the patch refused comes back as `gnav::kNoRing`, and the callback erases its key from `obstacleInMesh`; it never enters the index, so sight and nav agree that it is not there, and a later limit gathers it again.
- Removals. `queueRemovedDetail` erases a felled thing's key from `obstacleInMesh` and queues its ring id; `gatherRemovals` puts the ids in a patch whose callback carries the list of ids it removes (`edits.removes`), and the index drops each there. A count of them is not enough to update the index.
- Order. `gather` takes removals before limits, so one job can free a ring id and hand it to a new tree (`PatchableMesh` reuses a freed id from `freeRings`). `collect()` runs the job's callbacks in patch order, removal first, so the index drops the old ring before it takes the new one. The index and every cache keyed on it go by serial, never by bare ring id.
- `join()` is const and runs from every query, so the index is never touched there; a ring shows to vision from the tick after nav collects it, and a felled tree occludes for that long.
- Arrival. A mover's limit is taken `kSightBufferMeters` (16 m) past his sight, so rings normally enter the index well beyond anyone's sight. When gathers lag (a fast mover at 10x, the add budget spent), a ring can arrive inside an observer's sight. A soft ring then comes in over `forestArrivalMs` of sim time: its forest depth and its drawn triangle are multiplied by the fraction of that time since it arrived, and while any ring within an observer's `reach` is still coming in his shape is rebuilt at each of vision's passes. An opaque ring casts its shadow from its first build, as a rock placed in sight would. Systems receive the frame's unscaled `deltaTime` (`GameScene.cpp`, `ecsWorld->update(dt)`) and apply the game's speed themselves through `TimeSystem::effectiveTimeScale()` (`MovementSystem.cpp:44-47`), so a clock summed from `deltaTime` would run while paused and at 1x with the game at 10x. Arrivals are stamped against `TimeSystem::simulationSeconds()` (`TimeSystem.h:68-71`), the sum of every frame's `deltaTime * effectiveTimeScale()`, read as an `int64` of milliseconds, `llround(simulationSeconds() * 1000)`: the **clock**. A ring's arrival time is the clock when the index takes it in `collect()`, and vision reads the clock in its own pass. The arrival fraction is `min(kFractionOne, (clock - arrived) * kFractionOne / forestArrivalMs)`, an integer.

The index is bucketed by `kOccluderBucketMm` (16 m) squares of the ground, each ring listed in every bucket its box meets, so a build reads the buckets its reach's box meets and no others.

**The readers.**

| Caller | What it reads |
|---|---|
| `VisionSystem::update` (the build) | Opaque wall edges from `queryFacingOccluders`, every wall body in reach for the path (6.1), and placed rings from the index: this document's whole shape (5) |
| `VisionSystem::rememberArea` (the landing, from `GameScene.cpp`) | Nothing: the plain disc (5.3) |
| The sweep's builder (`PlayerVisibilitySystem.cpp`, `SeenArea::setSightBuilder`) | This document's whole shape at its point, its rim given by the record (6.4) |
| `StoreOrDrop` (`StoreOrDrop.cpp`) | Every wall, through `queryOccluders`; no placed ring |
| The cover (7.3) | Each colonist's bodies from his last build, through `VisionSystem::sightBodies` |

`VisionSystem::sightBodies(observer)` returns the bodies within `reach` of the observer's last build, each as its outline, kind, serial or wall body, and arrival fraction, with the build's shadow edges (3.1: tagged on the hard polygon before the forest clip, and kept where their corners survive it) and the `lead` that pass computed, valid until vision's next pass; the cover copies what it draws that frame. A shadow edge is `{body, corner, litTurn}`: the index of its body in the returned list, the index of its corner in that body's outline, and `litTurn`, +1 when the lit side lies counter-clockwise of the shadow side about the corner and -1 when clockwise. Vision passes every 5th frame, so he can be up to `lead` from the build when it is drawn, not `step`: about 0.67 m at 1x and 60 frames a second, 0.83 m at 30, and 2.2 m at 10x. Every body within `sightRadius` of where he stands is among them while he is within `lead`; a slow frame or a jump past it lacks the bodies beyond until the next pass.

**Rebuilds.** An observer's cached shape is rebuilt when he moves more than `step` from its build, when the walls' version moves, when the sorted serials of the placed rings within `reach` change, and at each pass while a ring in reach is still arriving. The trigger stays `step`; `reach` is what changes, so a shape covers his whole sight from anywhere he can be before the next pass. A shape is also rebuilt when the pass's `lead` exceeds the one it was built with (the game sped up or the frame rate fell), since its `reach` then falls short; a smaller `lead` keeps it, and the serials compared are those within the shape's own `reach`. The mesh's epoch cannot stand for that, since movers patch it every 4 m of travel.

The mesh exists wherever an observer can see: every mover meshes the disc of sight plus 16 m round him, and the landing 150 m. Ground a colonist reaches before nav meshes it (a dev teleport) has no placed occluders until the mesh arrives.

## 3. Shadows

### 3.1 Opaque bodies

In the sim an opaque body casts its exact point-eye shadow: its outline's edges go to `geometry::computeVisibilityPolygon` with the opaque walls' facing edges, and sight ends at its near face. A placed ring's edges go in on both faces; the eye is never inside one, and a few rocks in reach cost nothing worth filtering.

On the cover the drawing goes by **shadow edge**, never by body. A shadow edge is a side of the build's polygon that runs out along a ray from the build eye past a vertex of an opaque body, its **corner**; the build tags each with its body, its corner, and which side of it is lit. `computeVisibilityPolygon` returns a plain `Ring` (`Visibility.h:35`), so the tags are found afterwards, on that ring, before the forest clip, from integers alone:

- Before the call the build maps each opaque body's outline vertices to `(body, vertex index)`. A shadow edge's near end is an occluder's own endpoint, which the hits' snapping does not move, so it is looked up exactly; only its far end is a snapped hit.
- A side of `computeVisibilityPolygon`'s ring whose one end is such a vertex `v` is a shadow edge with corner `v` when `v` is a silhouette vertex from `eyeCentre` (both its neighbours in the outline lie on one side of the ray from the eye through it, by cross products), its other end lies farther from the eye, and that end lies within 1 mm of the ray (the snapping moves a hit by at most 0.71 mm). A body face seen end on fails the first test and stays a body face.
- The ring is counter-clockwise round the eye with its inside on the left, so the lit side follows from the side's direction in the ring: running out from the corner the lit side is counter-clockwise of it (`litTurn` +1), running in to the corner clockwise (-1).
- Then the clip. `starClippedToConvex` keeps every vertex inside the forest shape as it is and makes new ones only where a side crosses the rim, rounding the crossing and stepping it inward up to 8 times, a millimetre each (`insideBoth`, `SightClip.cpp:146-157`), so a clipped shadow edge's far end can lie several millimetres off its ray and a test after the clip would lose its tag; in forest nearly every rock's shadow edge is clipped. A tag is kept when its corner vertex is in the clipped ring unchanged, found by its exact coordinates, and the tagged side is the one that runs from it the same way round the ring, out from it for `litTurn` +1 and in to it for -1, along the same ray to the old far end or to the crossing. A tag whose corner lies outside the forest shape is dropped, and nothing is drawn for it, since its shadow lies past the rim. Where a vertex is shared by two bodies (a rock against a wall) the tag takes the body whose outline turns away from the ray there.

The tags go into `sightBodies` (2.4). Indoors the eye stands inside a wall body's hull, where the body has no single silhouette, but every shadow edge still has its own corner. The rest of a shadow lies outside the bright shape, which reads 1 already, so only the ground beside each shadow edge is drawn. From his position that frame, with `widening = max(lead, distance from him to the build eye)`, `cornerDistance` from him to the corner, and the corner followed from him (the tangent vertex from him reached by walking the body's outline from the build's corner, toward the lit side or away from it as the turn from the build eye to him needs, until the outline seen from him turns away; wall outlines have short junction and cut edges, so this can pass several vertices, never only the next one):

- A **band** at 1: the ground at the corner between two lines through it, turned from the ray from him through the corner by `litAngle = asin(min(widening / cornerDistance, sin(shadowWideningMostDegrees)))` (10 degrees) on the lit side and by `shadowAngle = min(180 degrees, asin(min(1, widening / cornerDistance)) + 90 degrees * clamp(2 - cornerDistance / widening, 0, 1))`, uncapped, on the shadow side, the wedge between them taken from the lit line round through his ray to the shadow line.
- A **fin** on the lit side, from the band's lit line out to the line through the corner turned a further `finAngle = min(shadowFinMostDegrees, atan(edgeWidth / (2 * cornerDistance)))` (15 degrees at most), the value falling linearly across from 1 to 0: the penumbra of an eye `edgeWidth` wide beside the corner, 0 wide at the corner and widening with range.

`shadowAngle` is `asin(widening / cornerDistance)` from `2 * widening` out, the most a side through the corner from an eye within `widening` of him can turn from his ray. Within `widening` of the corner such an eye can stand anywhere round it, past it included, so its side can turn up to 180 degrees, and `shadowAngle` is 180 there; between the two it ramps up continuously and stays above the `asin`. The price is ground on the shadow side of his line to the corner and back toward him drawn at 1 while he is within `2 * widening` of the corner (1.3 m at 1x, 4.4 m at 10x), in-sight ground among it. Standing 0.6 m back from a 1 m doorway, centred, the shadow line from each jamb's far corner turns past the wall's far face and through the jamb into the room, so a wedge of the room's floor beside the doorway is drawn at 1: 51 degrees wide behind a 0.15 m wall and 21 degrees behind a 0.30 m one at 1x, and at 10x everything on the wall's side of his line to each jamb's far corner.

Both are lines through a point of the body's outline turned by continuous angles, so they move continuously as he walks, at any distance, inside a room or out, across a corner switch included, where the point eases along the face over `shadowCornerEaseMm` (1.5 m) of his walk (below).

**Past the cap distance.** The cap binds on the lit side when `cornerDistance` is under `widening / sin(shadowWideningMostDegrees)`, the **cap distance**: 3.9 m at 1x and 60 frames a second (4.8 m at 30) and 12.7 m at 10x. `widening` is never less than `lead`, so the cap distance never falls below these. Past it the band is the exact construction on both sides. A shadow side through the corner from any eye within `widening` of him turns from his ray by at most `asin(widening / cornerDistance)`, so the band holds the sim's side from the last build and from the next, and at a rebuild the ground between the two sides is 1 before (outside the bright shape) and 1 after (in the band): no pixel changes, which limit 8 holds to 0.02 at any pixel as measured, except near a body face's line, where a build eye can take the face's other vertex (below). The cost is ground the sim has in sight drawn at full tint, at most `widening * (rangeFromEye - cornerDistance) / cornerDistance` wide on the lit side of each shadow edge.

**Inside the cap distance.** The shadow side of the band stays uncapped, so a sim side from any eye within `widening` of him lies inside the band or in shadow on that side, and nothing between the shadow and the band is ever drawn bright: a rebuild that moves the sim's side into the shadow changes no pixel. On the lit side the band holds at 10 degrees and the fin at its own angle, and a rebuild that moves the sim's side toward the lit side steps it by the part of its turn the cap leaves: the strip between the band's lit line and the sim's old side, which lay in shadow at 1, goes to the fin's value there. That is all that steps. The worst change on screen is half the fin's fall across the strip:

| Corner off | 1x, `lead` 0.67 m | 10x, `lead` 2.2 m |
|---|---|---|
| 1 m | 0.5, the tint's whole | 0.5 |
| 2 m | 0.32 | 0.5 |
| 3 m | 0.13 | 0.5 |
| 3.9 m, the 1x cap distance | 0 | 0.5 |
| 8 m | 0 | 0.5 |
| 10 m | 0 | 0.39 |
| 12 m | 0 | 0.10 |
| 12.7 m, the 10x cap distance | 0 | 0 |

At 0.5 the step is the hard cover's own (player-visibility.md 3.4), with the fin softening its first 15 degrees or less.

**The bounds.**

- The full-tint ground the sim has in sight is at most 10 degrees of arc beside each shadow side: 5.3 m wide at the rim, 1.7 m at 10 m out. At 2 m from a rock's corner at 1x it is the cap's 10 degrees, against 19 degrees uncapped.
- A doorway 1 m wide with him 0.6 m back from the wall, centred: the jambs' far-face corners bound the beam, so with walls 0.15 to 0.30 m thick they are 0.90 to 1.03 m off and the beam through the doorway is 67 to 58 degrees. Uncapped, each jamb's widening would be 40 degrees and the whole beam would draw at full tint. Capped, each side takes a 10 degree band and a 15 degree fin, and the beam keeps a fully bright core 17 to 8 degrees wide.
- A BigRock face 1.4 m long with him 0.3 m from it, centred: its corners are 0.76 m off and its shadow is 134 degrees of arc. Uncapped the widening needed is 62 degrees a side; capped, the lit side keeps 176 degrees with the bands and fins beside it. Walking along the face, each rebuild steps the shadow side as the hard cover's does.
- The corner followed from him changes when he crosses the line of the body face at it. The two vertices and his position then lie on one line, and moving the band from one apex to the other in one frame would shift its lines sideways by the face's length times the sine of their angle: 0.19 m for a BigRock's 1.4 m face and 0.54 m for a 4 m wall face, each with the corner 5 m off at 1x, against a fin 0.24 m wide 2 m behind the corner, a pop of up to the tint's 0.5 on screen that falls under 0.1 only about 8 m behind for the rock and 22 m behind for the wall. So the apex eases (below), and the switch moves the band a little each frame, as walking does.

**The corner switch.** When the followed corner changes, the band's apex leaves the old corner and travels along the face to the new one in a straight line, `apex = oldApex + ease * (newCorner - oldApex)` with `ease = min(1, walked / shadowCornerEaseMm)` and `walked` his walk since the switch; the band's lines go through the apex, turned from the ray from him through it by the angles above with `cornerDistance` measured to it, and the fin with them. A switch mid-ease starts a new ease from the apex where it is drawn. The cover keeps, per colonist and per shadow edge (its body and `litTurn`), the apex and `walked`; it is float, drawn only, and never read by the sim.

What the band covers during an ease: the apex is a point of the face between the two vertices, and he stands near the face's line, so the band's lines lie within the apex's distance from either vertex, times the sine of their angle, of the lines the band at that vertex would draw. A sim side through a vertex farther from him than the apex starts inside the band, on its ray, and lies in it as 3.1 holds it. A sim side through a vertex nearer him than the apex starts behind the band and lies within that distance of it, so a rebuild that moves the sim's side into the sliver between them toward the lit side steps the sliver by the fin's fall across it. The same sliver is there without an ease whenever a build eye across a face's line takes the face's other vertex, at most the full switch shift wide; during an ease it narrows as the apex nears that vertex and widens as it leaves, never past that. It is the one exception to "no pixel changes" past the cap distance. The band only darkens, and every piece is clipped to the bright shape, so the ease never shows ground the sim has out of sight.

Each frame the apex moves `faceLength * walkedThisFrame / shadowCornerEaseMm` along the face, and on screen the band's lit line moves half that times the sine of `litAngle` over the fin's width there. With the corner 5 m off at 1x, walking 2 m/s at 60 frames a second: 0.025 a frame 2 m behind the corner for a 4 m wall face and 0.009 for a BigRock's face, in proportion to the face's length, rising as the fin narrows toward the corner (0.050 and 0.017 at 1 m behind). At 10x a frame's walk is 0.22 of the ease: 0.32 and 0.11 at 2 m behind (7.8).

### 3.2 Soft bodies

A soft body casts no shadow in the sim: a mover behind a trunk is in sight, and is drawn under the tint the triangle gives that ground. On the cover it draws a **triangle**:

```
footValue    = softShadowStrengthPercent / 100 * widthAsSeen / softRayWidth
shadowLength = distance * widthAsSeen / (softRayWidth - widthAsSeen)
progress     = (rangeFromEye - distance) / shadowLength
```

At a `rangeFromEye` between `distance` and `distance + shadowLength`, the triangle holds the directions of the silhouette shrunk toward the bisector by `progress` (each end moved that fraction of the way to the bisector), and on each such ray the ground from where the ray first meets the body outward, so the body's own footprint is in it. Its value there is `footValue * (1 - progress)`: the body's silhouette at its foot, closing to a point at `shadowLength` behind it and fading to nothing there. `softRayWidth` (3 m) exceeds every soft body's `diameter` (under 1.4 m), so `widthAsSeen` is always below it and every triangle closes; the sliders hold `softShadowRayWidthMm` above both kind thresholds.

Two fins, one beside each side of the triangle on the lit side: at a `rangeFromEye`, a fin's outer edge lies `(edgeWidth / 2) * (rangeFromEye - cornerDistance) / cornerDistance` across the ray from the side, the line from the edge of a ray `edgeWidth` wide through the corner. Its value falls linearly across from the triangle's value at that side to 0, and it ends where the triangle does.

The triangle grows and shrinks with `widthAsSeen` as he walks round the body, so a trunk's shadow turns and lengthens with him; its kind never changes, so it never flips to a hard shadow. What does step on screen is an opaque body's shadow side near its corner at a rebuild (3.1, 7.8). With the start values, at the foot and on screen, where the tint is black at `kSeenDimAlpha` (0.5, `VisibilityCoverRenderer.h`) so a drawn value darkens visible ground by half its value:

| Body | `widthAsSeen` | `footValue` | Darkest on screen | `shadowLength` at 5 m | at 10 m |
|---|---|---|---|---|---|
| The shortest wall | 0.15 to 0.58 m | 0.025 to 0.097 | 0.013 to 0.049 | 0.26 to 1.21 m | 0.53 to 2.41 m |
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

`shadowLength` is a segment eye's: the two lines from a ray `softRayWidth` wide through a strip's opposite edges cross that far behind it. A fin's outer line is where a segment eye's penumbra beside a corner ends, and the opaque band's widening is the same construction with an eye `2 * widening` wide, capped on its lit side (3.1). The rest is a drawing rule. A segment eye's umbra narrows in width along a line, where the triangle shrinks in angle about the eye; past the umbra a segment eye's hidden fraction falls toward the strip's share of the ray and never to 0, where the triangle closes to nothing; and its value is that share scaled by `softShadowStrengthPercent`, so that a trunk's shadow is faint by choice and not by geometry.

## 4. Forest depth

### 4.1 The rule

Range in a direction is shortened by how much collidable placed stuff lies that way, as a smooth density, never as each thing's silhouette. Forest depth is taken at each of the 64 sides of the sight polygon (`kSightSides`, `libs/geometry/visibility/SightPolygon.h`), in the direction of the side's normal, halfway between two rim directions:

```
rawDepth(side)    = sum over placed rings with centre within sightRadius of
    forestCapMm * widthFactor(ring) * angleWeight(side, ring) * distanceWeight(ring) * arrival(ring)
cappedDepth(side) = min(rawDepth(side), sightRadius - floorRange)
forestDepth(side) = min over every side other, side itself included, of (cappedDepth(other) + forestSlopeMm * sidesBetween(side, other))
range(side, radius) = radius - forestDepth(side)
floorRange          = sightRadius * forestFloorPercent / 100
```

The last step is the **slope limit**: no side's depth exceeds a neighbour's by more than `forestSlopeMm` (1000 mm), so a deep dip in one direction cannot pull its neighbours down through convexity (4.2). It is a two-pass integer distance transform over the 64 sides: a forward pass over sides 0 to 127, each taking the least of its own depth and the one before plus `forestSlopeMm` (side indices taken mod 64, so the pass goes round twice), then a backward pass from 126 to 0 the same way, each taking the one after. It only lowers depth, so the floor holds; and it is the least of continuous functions, changing no side by more than the most any capped depth changed, so continuity and the change per step (4.3) hold through it.

- `widthFactor` is `min(1, perimeter / forestFullPerimeterMm)`, the ring's perimeter against that of a body 1 m wide on average (3142 mm). The perimeter over pi is a convex body's mean width over every angle, so it is fixed per ring and needs no angle: a pine 0.36, a mature oak 0.70, a veteran oak or a BigRock 1.
- `angleWeight` is a tent: 1 where the side's normal points at the ring's centre, falling linearly to 0 at `forestWindowSteps` rim steps away (8 steps, 45 degrees).
- `distanceWeight` is `min(1, centreDistance^2 / forestNearMm^2) * (1 - centreDistance^2 / sightRadius^2)`, `centreDistance` from the eye to the ring's centre. It is 0 at the eye, rises to its greatest, 0.89, at `forestNearMm` (10 m), and falls to 0 at the sight radius, so a ring coming into sight adds nothing at first, and a ring beside the eye adds little.
- `arrival` is 1 but for a ring still arriving (2.4).

Every placed ring spends forest depth, opaque or soft: a BigRock is a hard shadow and a little forest depth round it, as any thing in the way is. Built walls spend none; they are opaque bodies with exact shadows or stubs a colonist sees past.

### 4.2 The forest shape

The **forest shape** of a radius round a point is the intersection of 64 half-planes, one a side: the half-plane whose edge is parallel to the sight polygon's side and lies `range(side, radius) * cos(pi / kSightSides)` from the point. With every range equal it is the sight polygon of that radius, to 2 mm. It is convex by definition, holds the sight polygon of `floorRange` round the point (every side lies at least that far out), and is the one outdoor sight shape: what colonist knowledge, `isVisible`, the bright shape, and the record read (5, 6).

Convexity takes ground where the ranges dip. Where a side's range is less than its neighbours', its edge cuts across the neighbouring directions, so a dip is widened and flattened into a straight side. Without a bound that loss grows with the dip: a clump of veteran trunks within 2 m of a point 10 m off takes 1.0 m from directions the ranges leave clear with 8 trunks, 4.3 m with 16, 8.1 m with 40, and a lone side at the floor beside full-range neighbours takes up to 9.8 m. A wider tent or a hull fill does not bound it. The slope limit (4.1) does: with neighbouring sides at most `forestSlopeMm` apart in depth, a side's edge can cut its neighbours' directions only by about that much a side. Convexity with the slope limit:

| Case | Convexity alone | With the slope limit, 1000 mm a side | At 600 mm a side |
|---|---|---|---|
| A lone veteran oak or BigRock at any distance | Nothing: its tent, 0.71 m deep at most and 45 degrees each side, is convex already | Nothing | Nothing |
| Random stands, mature oaks 5 m apart or pines 3 m apart | 0.04 m off the mean range; in the worst direction 0.3 m on average, at most 0.93 m | The same; the limit binds nowhere | The same |
| Random stands, mature oaks 3 m apart | 0.1 to 0.2 m off the mean range; in the worst direction 0.7 to 1 m on average, at most 1.4 m | The same; the limit binds nowhere | At most 0.9 m in the worst direction; up to 0.5 m of depth given up |
| Random stands, veteran oaks 4 m apart | 0.1 to 0.2 m off the mean range; in the worst direction 0.7 to 1 m on average, at most 2.1 m | At most 1.9 m; up to 0.2 m of depth given up | At most 0.9 m; up to 1.2 m of depth given up |
| A clump of 16 to 200 veteran trunks 10 m off | 4.3 to 9.5 m from clear directions | At most 1.9 m from clear directions; the clump gives up 2.5 to 7.9 m of its depth at its edges | At most 0.7 m; 5.3 to 8.7 m given up |

The worst loss in clear directions with the start value is therefore about 1.9 m (1.89 m), in a clump or a dense stand alike, and 0.67 m at 600 mm a side. Inside a wide thicket with a sharp edge, convexity takes up to 2.3 m off the slope-limited ranges themselves, where the limit has already lowered the depth the edge gives up. The price is a narrow thicket: its sides keep their full depth only where they lie a slope's worth of sides inside its edge, so a thicket narrower than about 20 sides (110 degrees) never reaches the floor. The figures are for uniform random stands and for clumps of trunks on a 0.5 m grid, with the start values and 40 viewpoints a stand.

The forest shape is integer arithmetic throughout (4.4), and its vertices are the crossings of consecutive kept edges, rounded toward the point and pulled in a millimetre at a time while any half-plane fails, as `sightPolygon` pulls its vertices in.

### 4.3 What it gives

**A lone tree.** One ring's depth across any side is at most `forestCapMm * widthFactor * 0.89`: 0.26 m for a pine, 0.50 m for a mature oak, 0.71 m for a veteran oak or a BigRock, straight at it, tapering to nothing 45 degrees either side.

**Stands.** For trunks at `density` to the square metre with mean `widthFactor`, the expected depth is `density * widthFactor * forestCapMm * windowAngle * 201 m^2`, where `windowAngle` is the tent's half width in radians (0.785) and 201 m^2 is the integral of `centreDistance * distanceWeight` over 0 to 30 m. That is about `158 m^2 * density * widthFactor` times `forestCapMm`. With the start values, from where he stands:

| Stand | Rim |
|---|---|
| Mature oaks 5 m apart | 26.5 m |
| Pines 3 m apart | 24.9 m |
| Veteran oaks 4 m apart | 22.1 m |
| Mature oaks 3 m apart | 20.2 m |
| Pines 2 m apart, veteran oaks 3 m apart | 20.1 m, the floor |

The floor is `floorRange`, 20.1 m from where he stands, on both paths: the polygon path's shape is cut by the forest shape of `sightRadius` placed where he stands (5.2).

**Continuity.** Every term is continuous in the eye's position: a ring's centre direction, since the eye is never at it; its tent, linear in the side's place in the table (4.4), which rises steadily with angle; `distanceWeight`, a polynomial in the squared distance; and a ring entering or leaving sight does so at weight 0. So every side's range is continuous as a colonist walks, and so is the forest shape, whose edges move with them. A ring felled or placed changes it by at most that ring's depth, as any change to the world does.

**The change between builds.** Walking a distance `moved`, a ring's centre direction turns by at most `moved / centreDistance`, and its tent changes by that over the window, while `distanceWeight` changes by at most `2 * moved / forestNearMm`. The sum is greatest at `forestNearMm`, where it is `moved / (windowAngle * forestNearMm) + 2 * moved / forestNearMm`: for a 0.5 m step 0.064 + 0.100, so one ring changes one side's range by at most 0.164 of `forestCapMm * widthFactor`, 0.13 m for a veteran oak or a BigRock. Builds are up to `lead` apart, not `step`, so between consecutive builds that is 1.34 times as much at 1x (0.17 m) and 4.4 times at 10x (0.57 m). Several rings near 10 m in one window add their changes. Walking straight through uniform random stands with the start values, the greatest change of one side per 0.5 m is 0.22 m through mature oaks 5 m apart, 0.49 m through veteran oaks 4 m apart, and 0.25 m beside a clump of 16 pines; between builds at 1x that is up to 0.30, 0.66, and 0.34 m. The visibility state view reports it (8). What the player sees of it is the eased rim (6.3), not the change itself.

### 4.4 Integers

Forest depth is integer arithmetic: millimetres, fractions in `kFractionOne` (65536) parts, and a side's place in the table. It is the same on every toolchain, uses no trig, exp, or log, and reads the integer square root only when a ring enters the index, for its perimeter.

- A ring's position is `index * kFractionOne + place`, `index` the table direction at or before the ring's centre (found by the sign of a cross product) and `place` = `cross(direction[index], offset) * kFractionOne / (cross(direction[index], offset) + cross(offset, direction[index + 1]))`, `offset` the centre less the eye. The table is at scale `2^30` and offsets are under `2^16` mm (`reach` is at most `sightRadius + kSightLeadMostMm`, 38 m, and a static check holds that under it); the offset lies within one step of the table of `direction[index]`, `360 / kSightSides` = 5.625 degrees, so the first cross product is under `2^30 * 2^16 * sin(5.625 degrees)`, under `2^43`, and the numerator under `2^59`, which fits `int64`. The denominator is positive and under `2^47`.
- Side `side` lies half a step past direction `side`, so its tent reads the ring's position against `side * kFractionOne + kFractionOne / 2`.
- `centreDistance^2` is under `2^31`, and each weight's numerator under `2^47`. A ring's weight, `forestCapMm` under `2^20` times four fractions, is under `2^20` mm, each product divided by `kFractionOne` before the next and under `2^36`; the tent's product is under `2^37`, and a sum over `2^16` rings under `2^36`.
- A ring whose weight rounds to 0 mm is skipped.
- The slope limit's passes add `forestSlopeMm`, under `2^20`, to depths already capped under `2^15` mm, so they stay under `2^21`.
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
for (std::int64_t& depthMm : forestDepthMm) {
    depthMm = std::min(depthMm, sightMm - floorMm);
}
for (std::int64_t index = 1; index < 2 * sides; ++index) {
    const auto side = static_cast<std::size_t>(index % sides);
    const auto before = static_cast<std::size_t>((index - 1) % sides);
    forestDepthMm[side] = std::min(forestDepthMm[side], forestDepthMm[before] + tunables.forestSlopeMm);
}
for (std::int64_t index = 2 * sides - 2; index >= 0; --index) {
    const auto side = static_cast<std::size_t>(index % sides);
    const auto after = static_cast<std::size_t>((index + 1) % sides);
    forestDepthMm[side] = std::min(forestDepthMm[side], forestDepthMm[after] + tunables.forestSlopeMm);
}
for (std::size_t side = 0; side < geometry::kSightSides; ++side) {
    rangeMm[side] = radiusMm - forestDepthMm[side];
}
```

`tunables` is the `SightTunables` read that pass (8), its fields named as in 9 and each within its range there, which these bounds assume.

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

Memory, discovery, and `isVisible` test that a thing lies in the shape: on a leg, the forest shape placed where he stands; on the polygon path, the shape round its build, within `sightRadius` of where he stands, and inside the forest shape of `sightRadius` with the build's ranges placed where he stands, the shape a leg would give him there. The shape round the build is built to `reach`, `lead` past sight, so the forest shape of `reach` round the build holds the forest shape of `sightRadius` round any position within `lead` of it (to `lead * (1 - cos(pi / kSightSides))`, under 3 mm at 10x), and the cut, not the build's radius, sets the rim. Both paths so give the same rim in directions forest depth limits, and a change of path between a leg and the polygon path moves no rim but by the build's own change. The forest shape is convex, so its test is one side test after a binary search by angle; the polygon path's shape is star-shaped round its build and takes the same search, then the forest shape's.

`VisionOverlay` (V) draws each observer's shape, the forest shape and the hard polygon in two colours, each body coloured by kind, and the soft ray's width at the build.

### 5.3 The landing

`PlayerVisibilitySystem::revealLanding` commits a plain disc of `kLandingSightMeters` (150 m). `VisionSystem::rememberArea`, whose only caller is the landing (`GameScene.cpp`), makes no occluder query and builds no shape: it gives every crew member the plain disc's things. Rules 8 and 17 say the crew saw the landing as they came down, so the seen area and what the crew knows agree.

## 6. The seen record

The record is player-visibility.md's: an exact polygon union in a second `PatchableMesh`, star-shaped shapes in, the covered test, the frontier, the sweep, and the skirt (3.3 there).

### 6.1 Three paths

| At the build | Path |
|---|---|
| Nothing in reach | An open leg: a leg whose forest shape is the sight polygon, player-visibility.md's open leg |
| Placed rings in sight, all soft, no wall in reach | A leg (6.2) |
| An opaque body or any wall in reach | The polygon path: each shape queued with its walk, the skirt, frontier pieces, and the sweep (6.4) |

An open leg and a leg are one mechanism. A leg ends when the path changes; the record only grows, so nothing is lost at the change. A BigRock in the landing's forest (`spawnChance` 0.004, `minDistance` 15 m in TemperateDeciduousForest, `BigRock.xml`) puts every build within `reach` of one on the polygon path; the share of builds on each path in forest is among 12's measures.

### 6.2 Legs

A leg holds the shapes he walked with. Its ring is the convex hull of every shape it has taken, and a shape joins at every held position: each `setOpenLeg` call, made every frame, passes his position that frame and his build's 64 ranges, and joins the forest shape of `sightRadius - kSeenSlackMm` with those ranges placed there (6.3). With every range equal that is the hull of one sight polygon placed at each position he held, his positions lying within `kSeenSlackMm` of the leg's line as an open leg's rules hold them.

A leg ends by an open leg's rules (a turn, a turn back, a jump, or a change of path), when a build's range on some side differs from that side's least range over the leg by more than `softLegToleranceMm`, or when its ring would pass `kSoftLegMostVertices` vertices. The next leg starts at the new build with its ranges, holding as its first shape the ease's start (6.3), the shape drawn on the frame it opens, which lies in the leg before it, so the eased rim has both shapes it blends in one ring; after a jump, or after the polygon path, it holds none. The inherited shape is in the ring but not in the spread test: the test reads the new leg's own builds only. Counting it would end legs at once, since the build that ended the last leg can differ from the inherited shape by more than the tolerance (a build's change reaches 0.66 m at 1x, 4.3).

Within those rules the leg keeps both containments of player-visibility.md 3.1:

- Drawn inside seen, exactly. On a leg the bright shape is the eased forest shape of 6.3 placed where he stands, and the ring holds it on every frame, by induction over the eases. The ring holds the ease's start shape, the shape drawn at `startPosition` on the frame the ease started (inside the ring then, by induction, and inherited by a new leg; a leg opened after a jump or the polygon path has no start shape in it, and 6.3 says what shows for its first `rimEaseMm`), and the newest build's shape at his position this frame, whose side offsets are `newestOffset` plus `rimEaseMm` (6.3). The ring is convex, so it holds every blend `(1 - blend) * startShape + blend * newShape` of the two (a Minkowski combination), and since both shapes have their edges on the same 64 normals, that blend is the forest shape whose side offsets are the blend of theirs, placed at `(1 - blend) * startPosition + blend * position`. Its offsets exceed the bright shape's by `blend * rimEaseMm`, and the bright shape, placed at his position, lies `(1 - blend) * distance(position, startPosition)` from the blend's place, at most `(1 - blend) * walked = (1 - blend) * blend * rimEaseMm` (6.3), which is no more than `blend * rimEaseMm`; a shape moved by a distance lies inside the same shape with every offset grown by that distance, so the bright shape lies inside. The start shape carries no margin of its own, so the inset must cover the whole of `(1 - blend) * blend * rimEaseMm` from the new shape's term alone, which is what makes it `rimEaseMm` and not a quarter of it: with an inset `k * rimEaseMm` the condition is `(1 - blend) * blend <= k * blend` for every blend, `k >= 1 - blend`, so `k = 1`. The ring is pending from the frame it holds a point, so no bright ground is ever unseen.
- Seen inside swept sight, to the tolerance. Every shape on the leg lies inside the shape whose sides take each side's greatest range over the leg, and that shape lies within the tolerance of every one of them on every side. The hull of shapes along a straight leg therefore lies inside that greatest shape swept along the leg. As a Euclidean distance it is within `softLegToleranceMm / cos(half the sharpest corner of the leg's shapes)` of a shape some position on the leg held: about 1.1 times the tolerance, 0.55 m with the start value. Measured radially from where he stood, it reaches up to 1.5 times the tolerance, 0.78 m, at the ends of a side at the floor, where the side's edge runs on past the direction it was taken in. With the inherited first shape included, a side's range over the ring strays from its least by up to the tolerance plus what the ease had left to cover, at most one build's change (6.3), so where the inherited shape meets the leg's own the bound is 1.1 times `softLegToleranceMm` plus that change as a distance and 1.5 times it radially: 0.88 m and 1.20 m through mature oaks 5 m apart at 1x (a 0.30 m change), 1.28 m and 1.74 m in the densest stands of 4.3 (0.66 m), and more at 10x, where builds are farther apart. The record can hold ground that far past where sight reached on a leg, as the skirt lets it reach past a shadow edge. A tunable changed on a slider (8) rebuilds every shape at once and can change a build's ranges by any amount, so across a tunable's change the inherited shape can stray by that much and this bound does not hold.

`isVisible` reads his whole sight shape, which reaches `kSeenSlackMm` past the shape the ring holds at his position, so ground in that band, 50 mm wide, can be visible to the input rules and not yet in the record for as long as he holds it in sight, as on an open leg in the open. The bright shape stops short of the band, and what is drawn bright is always seen.

The ring only grows: each shape joins the hull, and the hull of a set holds the hull of any part of it. A shape joins only at a position he held, so the ring holds no ground but the shapes he walked with and the hull between them.

**What a forest costs a leg.** Forest depth drifts as he walks among trees. Walking uniform random stands with the start values, legs average 2.4 m through mature oaks 5 m apart, 2.6 m through pines 3 m apart, and 1.4 to 1.5 m through veteran oaks 4 m apart and mature oaks 3 m apart, against unbounded in the open, and shorter at 10x, where builds are farther apart. Each leg's ring goes through the frontier when it ends.

This is the main cost risk of the design. Legs are not skirted (player-visibility.md 3.3, the skirt), and each forest leg's rim differs from the last leg's by up to the tolerance on some side, while the covered test lets only 5 mm through, so nearly every leg of 1.4 to 2.6 m, even one over ground already seen, adds thin crescents along its rim: the failure player-visibility.md 6.3 measured for unskirted pieces. Each piece registers on its own tick, and a patch's cost grows with the rings registered over the spot it lands on, so legs a tenth as long as player-visibility.md 6.1's 20 m legs raise that count roughly tenfold where colonists walk the same forest. The rate is the sharper problem. Legs of 1.4 to 2.6 m at about 2 m a second end about once a second a colonist; with at least one crescent piece a leg, 50 colonists queue about 50 pieces a second at 1x and about 500 at 10x, against `kSeenPiecesRegisteredPerTick` (1, `SeenArea.h:42`), 60 a second. At 1x that is close to the whole budget; at 10x the pending queue grows by about 440 pieces a second without bound unless forest legs stop registering crescents. The measures, the go thresholds, and the fallbacks are 6.6's.

### 6.3 The record's interface

- `SeenArea::setOpenLeg(leg, start, position, ranges, inherited)` takes his position that frame and the build's 64 ranges in place of an end and a radius (`SeenArea.h`). `ranges` is a `std::array<std::int64_t, kSightSides>` of absolute ranges in millimetres from `position`, side by side: `sightRadius - kSeenSlackMm - forestDepth(side)`, the build's ranges at the slack radius, never depths. `inherited` is a `std::optional` of `{position, offsets}`, the ease's start as `PlayerVisibilitySystem` holds it (below), the shape drawn on the frame the leg opens (6.2), read only on the call that opens the leg and empty on an observer's first leg, after a jump, and after the polygon path. It is called every frame he holds the leg, and each call joins that forest shape, placed at `position`, to the leg's ring: the ring joins the shape at every held position, the one rule both containments of 6.2 rest on. `legCapsule` becomes that merge. The ring is a convex polygon with sides at any angle (where two shapes with different ranges meet, the hull bridges them off the 64 normals), and the new shape is convex with its vertices listed by angle round `position`, which the ring holds (it holds the shape at the last position, a frame's walk away, whose floor is 20 m out). Both vertex lists are merged by angle round `position` and the merge is run through one monotone hull pass with integer cross products, linear in the two lists. An open leg passes every range at `sightRadius - kSeenSlackMm`.
- `SeenArea::queueSightPolygon` takes the shape's rim (6.4) with the shape and its walk.
- `SeenArea::appendEdges(low, high, out)` gives the record's edge in a box: every live triangle edge with nothing across it, as `(start, end)` with seen ground on the left, through `PatchableMesh::meshEdgesNear`, since the record's mesh is private. The cover's feather reads it (7.2).
- `SeenArea::inRecord(point)` is whether the committed record alone holds the point, by its mesh's `locate`. `isSeen` counts the pending shapes too, and the feather's cut needs the two apart (7.2). Tests read the committed record through it.
- `SeenArea::takeChangedBoxes(out)` gives the boxes of the rings registered since the last call, from which the feather's pieces are rebuilt (7.2).
- `PlayerVisibilitySystem` holds each observer's path: leg or polygon path.

**The eased rim.** A build changes some sides' ranges by up to 0.66 m at 1x in a dense stand (4.3), and the bright rim taking each build's ranges at once would step 0.07 to 0.16 on screen at four or more builds a second, with convexity scaling a side's change by up to 1.5 on a straight floor side. So the bright shape's ranges ease to the newest build's over `rimEaseMm` (400 mm) of his walk, from the rim as it is drawn when the ease starts, never from the previous build's: an arrival rebuild, a serial change, or a wall-version change can start a new build mid-ease, and easing from the previous build would snap the rim back at each, a sawtooth every 5 frames while rings arrive. An ease starts at each new build, mid-ease included, and its blend restarts at 0. With `startOffset` the 64 offsets drawn that frame, `startPosition` his position that frame, and `walked` his walk since then, summed frame by frame:

```
blend        = min(1, walked / rimEaseMm)
drawnOffset  = (1 - blend) * startOffset + blend * newestOffset      for each of the 64 sides
drawnRadius  = drawnVisibleRadiusMm(sightRadius) - rimEaseMm
```

`newestOffset` is the newest build's side offsets at `drawnRadius`, each taken as the shape's support along that side's normal, so a side convexity drops reads the vertex beyond it. On the frame an ease starts the blend is 0 and the shape drawn is the one the old ease gives that frame, so a restart moves nothing; from there the rim moves only as he walks. The start shape is the drawn shape, which the leg's ring holds (6.2), and the bright shape lies within `(1 - blend) * blend * rimEaseMm` of the blend the ring holds, which the inset of the whole `rimEaseMm` covers at every blend through any run of restarts (6.2); a quarter of it would cover the shift only when the start shape was itself a shape the ring holds at its own place. The inset draws the bright shape 0.4 m inside `drawnVisibleRadiusMm` on both paths. The ease's state is per observer in `PlayerVisibilitySystem`, as integers: the 64 start offsets in millimetres, rounded down and then 1 mm less, which pays for the start position's rounding to the millimetre; that position; and `walked`, each frame's walk rounded up to the millimetre and summed, so it is never less than his straight distance from `startPosition`, and the blend never falls back as he turns. `drawnVisible` stays const and computes the blend from them; the system's update writes the start when a build arrives and adds to `walked` each frame. The blend is float and drawn only. Builds by `step` are at least 500 mm apart and the ease ends after 400 mm of walk, so an ease a step starts ends before the next. It lags a build by at most `rimEaseMm` of walk, a fifth of a second at a walk. Each frame the rim moves by `distance walked that frame / rimEaseMm` of the ease's span, `newestOffset - startOffset`, times up to 1.5 where convexity scales a side's change. The span is what the last ease left plus the new build's change, and an ease that restarts after `walked` has covered `walked / rimEaseMm` of the last span, so while builds come by the walk the span stays within the greater of one build's change and `rimEaseMm` times the change per metre walked (0.39 m in the densest stands of 4.3). At 1x, at a walk of about 2 m/s and 60 frames a second, the worst build's change (0.66 m) moves the rim at most 0.083 m a frame, under the 1.5 m feather 0.028 on screen, under the restart rule as before it. A ring felled or placed mid-ease adds its own change to the span, up to a veteran oak's 0.71 m (4.3), for at most 0.06 on screen in a frame. At 10x a frame's walk is 0.83 of the ease, and a build's change lands within one or two frames: 0.24 on screen in one frame for a lone veteran oak's 0.57 m change, up to the tint's whole 0.5 in dense stands.

**The bright shape.** `PlayerVisibilitySystem::drawnVisible` gives:

- on a leg, the eased forest shape, round his `Position` that frame. 6.2 shows it lies inside the leg's ring.
- on the polygon path, vision's shape cut by `starClippedToConvex` to the sight polygon of `drawnVisibleRadiusMm` round his current `Position` (`PlayerVisibilitySystem.cpp`) and then to the eased forest shape round his `Position`, while the observer's path took this revision. After a jump, nothing until vision builds the next.

Both paths so draw the same rim where forest depth limits it, and the ease runs across a change of path as within one. On the polygon path, and in the first `rimEaseMm` after a change from it to a leg, the bright shape can reach up to `lead` past the record ahead of him, onto ground nothing holds until the next build; there it is under the unseen fill, which is opaque and drawn over the tint, so what shows bright is seen by the fills' order (player-visibility.md 3.1).

### 6.4 The polygon path's rim, the skirt, and the sweep

On the polygon path a shape's **rim** is the convex ring it is cut to for the record: the forest shape of `sightRadius` round its build. With no placed ring in sight that is the sight polygon, the rim player-visibility.md 3.3 cuts to.

- What the record takes is the shape cut to its rim by `starClippedToConvex`.
- The skirt is `geometry::skirtedSightShape(shape, centre, rim, kSeenSkirtMm)` (`SightSkirt.h`), which takes any convex ring round the centre that the shape lies in: the sides along the rim, the forest shape's or the sight polygon's, are not grown, and only shadow sides and body faces are, of walls and opaque bodies. Seen ground reaches past a true shadow edge by up to the skirt, as rule 21 allows with walls, here with opaque bodies as well.
- A shape is swept when its frontier pieces reach within `kSeenSweepRimBandMm` of its rim, measured against the rim above.
- A sweep point's rim is the forest shape whose range on each side is the lesser of the point's own forest depth's (the sight builder computes it at the point) and the linear interpolation of the walk's two builds' ranges by where the point lies between them, less the sweep's inset. With every range equal that is player-visibility.md 3.3's sweep, the sight polygon of sight less the inset round the point. The inset is that sweep's, `apart * pi / kSightSides + apart^2 / (2 * sightRadius)`, with `apart` the farthest any vertex of the point's rim lies from the same vertex of the nearer build's rim; with every range equal that is the point's distance to the build. The interpolated rim shares the 64 side normals of both builds' rims and lies inside their hull, so its rim vertices stand past the builds' rims by no more than two sight polygons' do past each other, and the inset holds them in.
- `SeenArea::setSightBuilder` takes a builder of `(centre, rim)` that returns the shape at the point, cut to the rim, with the opaque bodies and walls standing when it builds.

### 6.5 Star-shaped shapes

The skirt (`geometry::skirtedSightShape`), the frontier (`sightShapeFrontier`), the covered test's fan, and `simpleAround` all take a shape star-shaped round its centre. Every shape the record takes is: a leg's ring is a convex hull, and the polygon path's shape is a star-shaped polygon cut to a convex rim round its build and checked with `simpleAround` (5.1).

### 6.6 What a forest costs the record

What decides it, against player-visibility.md 6.5 and 6.7 (measured as 12 says): K where registered (below) where colonists walk the same forest; the pending queue's greatest length; pieces registered per leg; legs per kilometre and their lengths; the share of builds on each path; rings registered per kilometre and per minute; the sweep's builds on the polygon path (every 100 mm of a walk that opens ground near its rim, `kSeenSweepSpacingMm`); the registry's bytes per kilometre; and the record's tick. K and the queue are the ones the crescents of 6.2 move: about 50 pieces a second at 50 colonists at 1x and 500 at 10x against 60 registered a second (6.2).

The go, at step 1a, with 50 colonists walking the landing's forest, several over the same ground, for ten simulated minutes at 1x and at 10x:

- The pending queue bounded: at 10x its length at the run's end no greater than at its fifth minute, and never over 1,000 (player-visibility.md 6.6's sim bench reached 58 walking open ground and 1,021 in a base, at 50 colonists at 10x).
- K where registered, mean and max, the metric of player-visibility.md 6.6 (K as its 6.1 defines it, the registered rings whose boxes meet the shape's box, taken at each ring that registers): mean at most 100 and max at most 400, against player-visibility.md 6.6's 43 / 163 to 78 / 342 with walls. The sim bench (`BM_PlayerVisibility*`) reports the tick and the pending queue but not K, so step 1a adds a K counter to `SeenArea`, read at each registration.
- The record's mean tick at most twice player-visibility.md 6.6's open walk at 50 colonists (0.101 ms at 1x, 0.113 ms at 10x): 0.20 ms and 0.23 ms.
- The worst tick at most 5 ms, against player-visibility.md 6.6's sim-bench worst ticks of 0.79 to 3.8 ms (4.7 ms in a base at 10x).

If the record costs too much in forest, the fallbacks in order of what each fixes:

1. Forest legs tolerance-snapped: each leg's ranges rounded down to a multiple of a snap, with the bright shape taking the same snapped ranges, so neighbouring legs' rims coincide on every side whose range did not cross a multiple. This is the only fallback that removes crescents. Skirting legs does not: the skirt is `kSeenSkirtMm`, 50 mm (`SightSkirt.h:14`), and neighbouring legs' rims differ by up to the tolerance, 500 mm.
2. A larger `softLegToleranceMm`, so fewer builds end a leg: it cuts the rate of leg ends, and so of crescents, without removing them, and the record then holds ground up to about 1.1 times that past where sight reached on a leg (1.5 times radially, more where an inherited shape meets a leg, 6.2).
3. The sweep skipped where every opaque body in reach is placed: the seen edge past a rock's shadow then steps with vision's builds, up to `lead`. It cuts the polygon path's sweep builds and does not touch legs.
4. The record's legs taking the sight polygon in place of the forest shape, the record holding ground the forest left out of sight: a departure from rule 27.

## 7. The cover

### 7.1 Two masks

The cover's two fills and their order are player-visibility.md 3.4's: the tint (black at `kSeenDimAlpha`) first, the opaque unseen fill over it. Each draws with a mask whose value at a point is how much of the fill shows there. With `fade(depth) = max(0, 1 - depth / sightFeatherMm)`:

- The unseen fill's mask is 1 outside the seen area (the record and the pending shapes), and inside it `fade` of the distance to the seen area's edge (7.2).
- The tint's mask is the least, over the colonists, of each colonist's **darkness**: 1 outside his bright shape (6.3), and inside it the greatest of `fade` of the distance to his bright shape's rim and his drawn shadows (3). Light, one less the tint's mask, is the greatest light any colonist gives a point (7.4).

The fade lies inside the brighter area on both edges: nothing unseen is ever shown even faintly, and the fully bright core of a 30 m sight in the open is about 27.9 m round him (`drawnVisibleRadiusMm`, 29.84 m, less the rim ease's 0.4 m inset and the 1.5 m feather). Sight, the record, and the bright shape are exact vector shapes; the fade is drawn and decides nothing. A click on ground under it is ruled by the record and by `isVisible`, never by the drawing.

### 7.2 The feather

`fade` of the distance to an area's edge is drawn exactly as the greatest of pieces, one per edge of the area's boundary and one per reflex corner: an exact inward offset, combined by the greatest. An edge's piece is the rectangle from the edge `sightFeatherMm` into the area, 1 on the edge and 0 on its far side. A corner where the area's inside angle exceeds 180 degrees takes a fan of radius `sightFeatherMm` round it over the gap between its edges' rectangles, 1 at the corner and 0 on the arc, in triangles of at most `kFeatherFanStepDegrees` (15 degrees), whose chords then fall short of the arc by `1 - cos(kFeatherFanStepDegrees / 2)` of the radius, 0.86%, so the drawn fade is within 1% of `fade`. Inside the area the greatest of these is `fade` of the distance to the boundary, because the nearest boundary point to any point inside is a perpendicular foot on an edge or a reflex corner. Where a rectangle runs past the area's boundary it lies where the mask is 1 already. Where the area is narrower than twice the feather the pieces of its two sides overlap and the greatest is the nearer side's, so a beam of sight through a 1 m doorway is never fully bright and is never darkened twice.

This holds only when the pieces are drawn for the area's boundary and for nothing else: an edge two shapes share, or one inside another shape, must draw no piece, or it draws a dark seam inside the area.

**One colonist's bright shape** is one star-shaped polygon, so its boundary is its sides and needs no cut. On a leg every side is rim; it draws a feather piece, and the feather is rebuilt each frame while the rim eases (6.3) and moved with him once the ease has ended, as the open path's annulus is. On the polygon path the build tags each side as rim, shadow side, or body face. A rim side draws a feather piece. A shadow side, running out from an opaque body's corner, draws none: its soft edge is its fin (3.1). A body face draws none: the darker side is the body, a wall's or a rock's footprint, so no fade darkens the ground in front of it.

**The seen area** is the record's union with the pending shapes, so its boundary is found on the CPU, exactly and in integers: the record's edge (`appendEdges`) and the pending shapes' sides, each cut where it meets the others, a piece kept when the point just off its middle on its outer side lies in no other shape (the record by `inRecord`, a pending shape by its outline; a point on another shape's side counts as inside that shape when the shape lies on the outer side). The record's edge is the edge of its whole union, however many meshes the record is uploaded in, so several record meshes leave no seam.

- The record's pieces are kept in cells of `kSeenFeatherCellMm` (32 m), one uploaded mesh a cell, in as many meshes as 16-bit indices need. When `epoch()` moves, the cells meeting a box from `takeChangedBoxes` grown by the feather are rebuilt from `appendEdges` over the cell grown by the feather, and the rest are untouched: a registered ring changes the union's boundary only inside its own box, and a boundary edge the mesh's patch splits at a point within it draws the same rectangles and no fan, since the split point is straight. Pieces register one a tick (player-visibility.md decisions 13), so an epoch moves at most once a tick. One registered ring's box can be large: a closed leg's ring is as long as the walk, so an epoch can rebuild every cell along a long leg, and the cost of an epoch is the record's edges in those cells, a count in 12.
- Inside the feather of a pending shape the record's pieces are cut against it. The uploaded pieces are drawn only outside the pending shapes grown by the feather, and inside that region the cut pieces are drawn with the pending shapes' own. The mask that says so, `outsidePendingFeather`, is the view less the union of hard layers made for each pending shape from pieces it already has: the shape's own outline, the rectangle `sightFeatherMm` outward from each of its sides, and a fan of that radius round each convex corner, in the triangles of the feather's fans. That union is the shape grown by the feather to within the fan's chords, with no offset routine. A queued shape's cut is made once when it is queued and dropped when it leaves the queue. A leg's ring changes each frame only in the sides that join its held hull to the shape at his position, so its cut is redone each frame within the box of those sides grown by the feather, and where a changed box of the record meets it. With every range equal those sides are fixed lines and the box is the end's; in forest the sides can pivot from far back along the leg, which the leg's tolerance keeps short. The per-frame cut is a count in 12.

### 7.3 One colonist's drawn shadows

The pieces of 3, for each colonist whose sight's box meets the view, built each frame from his position over his `sightBodies`: a triangle and two fins per soft body, and a band and a fin per shadow edge of his last build's polygon. Values that are products, a fin's fall across times a triangle's fade along, are evaluated at the pieces' vertices and subdivided until each triangle's linear value is within `kDrawnValueTolerancePercent` of the product. Pieces are never recorded and never read by the sim.

### 7.4 Several colonists

A colonist's darkness is 1 outside his bright shape and inside it the greatest of his feather and his drawn shadows. The tint's mask is the least darkness over the colonists, so light is the greatest light any colonist gives a point, and one colonist's drawn shadows never darken ground another colonist lights: where another lights it fully, the least is 0. It is drawn in a number of passes linear in the colonists in view, with no layer per pair (7.6), and 7.7's test holds it at 50 colonists in view.

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

### 7.7 The sandbox test and its fallbacks

Step 0 is a sandbox scene of the cover's cases over a busy background: two record cells sharing an edge, a pending piece abutting the record along its edge, a pending leg overlapping it, two bright shapes overlapping and two abutting, a 1 m beam, a reflex corner, a soft body's triangle and fins, an opaque body's bands and fins seen from outside and from inside a room, a piece reaching past its colonist's bright shape, one colonist's shadow inside another's sight, and 50 colonists' shapes overlapping in one view with their shadows. A test reads the mask's value at chosen pixels (cores, mid-fade, overlaps, abutting edges, the shared cell edge, a reflex corner, a shadow under another colonist's light, the clipped piece's far side) against 7.1 to 7.5, and GPU time is taken at 1, 10, and 50 colonists. No golden image.

If the target has no destination alpha or depth, or the backend cannot hold those values, the fallbacks in order:

1. Each colonist's pieces made disjoint on the CPU each frame, an overlay of their outlines with each cell carrying the greatest of the pieces over it, subdivided to within `kDrawnValueTolerancePercent`, so that the min blend on destination alpha alone gives the least over colonists with no depth pass.
2. The partial layers drawn as fill-coloured geometry carrying alpha, source over, under the stencil's hard result. Where two pieces overlap (inside the feather of a convex corner, a narrow beam, a shadow over a feather) it darkens to `first + second - first * second` in place of the greater, and where two colonists see a point one's shadow darkens the other's light.
3. Hard edges, as player-visibility.md 3.4 draws them, with the sight shape and the record of this document.

### 7.8 Consequences taken on purpose

- Narrow seen and visible ground, a doorway's beam or a strip between two shadows, is never fully bright or fully tinted.
- Where two colonists' sights overlap by under twice the feather the overlap is never fully bright, and a dimmer wedge runs in from each point where their rims cross (7.4).
- A body face, a wall's or a rock's, is a hard edge in the tint, and a shadow side is hard at its corner, its fin widening from there.
- Beside every shadow edge a sliver the sim has in sight is drawn at full tint, at most 10 degrees of arc (5.3 m wide at the rim), so the hard shadow does not move at a rebuild past the cap distance (3.1). Nearer a corner than the cap distance, 3.9 m at 1x and 12.7 m at 10x, the shadow side steps toward the lit side at a rebuild by the part of its turn the cap leaves, softened by the fin: 0.13 on screen at 3 m and 0.32 at 2 m at 1x, the tint's whole 0.5 out to 8 m at 10x.
- When he crosses the line of a body face at a shadow edge's corner, the band's apex eases along the face to the new corner over 1.5 m of his walk, so the band slides instead of popping: 0.025 on screen a frame 2 m behind a corner 5 m off for a 4 m wall face at 1x, in proportion to the face's length and rising toward the corner, and 0.32 at 10x (3.1). A rebuild from a build eye across a face's line can step a sliver beside the band up to that face's switch shift wide, past the cap distance as well (3.1).
- Within `2 * widening` of an opaque corner the band's shadow side turns up to 180 degrees and draws in-sight ground on that side of his line to the corner at full tint, a wedge of a room's floor beside a doorway among it (3.1).
- The forest rim is convex, so a dip in forest depth is a straight side across it, and the slope limit gives up a narrow thicket's depth at its edges to keep that side from taking more than about 1.9 m from clear directions (4.2).
- The bright rim lags each build's ranges by up to `rimEaseMm` of walk (6.3), and at 10x a build's change shows within a frame or two.
- The draw bound and the terrain scissor are player-visibility.md 3.4's: the fade lies inside the record, so nothing past it shows.

## 8. Input, things, and dev tools

The input rules (player-visibility.md 3.7) read the record and `isVisible`, which reads vision's shape. A mover that is not a colonist shows while `isVisible` holds: behind a BigRock it is out of sight; behind a trunk or a wall stub it is in sight, drawn under the triangle's tint; past the forest rim it is out of sight.

The visibility state view (`/api/state?what=visibility`) reports: builds by path (leg or polygon path); legs held, and ended by a forest-depth spread, by vertices, and by an open leg's rules; placed rings in sight and opaque bodies in reach, summed over the last builds; the greatest change of one side's range between consecutive builds of one observer, and the ring it came from; rings that arrived inside an observer's sight; sweep builds; record epochs a second and feather cells rebuilt; K where registered, mean and max (6.6), and the pending queue's greatest length; pieces registered per leg; and shapes that failed `simpleAround`. salal's state providers take no arguments (`DevServer::Provider`, salal `DevServer.h:74`), so the body list is a view of its own, `/api/state?what=sightBodies`, registered beside it: each colonist's `sightBodies` with kind, `diameter` in millimetres, centre, and for a placed body its def name, so a scenario can find a BigRock. `/api/dev/reveal` is player-visibility.md's.

Every tunable in 9 is a field of one runtime struct, `SightTunables`, named as in the table, whose defaults are the start values and which vision and the cover read each pass and each frame. `VisionSystem` owns it; `PlayerVisibilitySystem`, `NavigationSystem`'s placed occluder index, and the cover read it from there, and its one setter clamps each field to its range in 9, in a fixed order: every field first to its own fixed bounds, then `softShadowRayWidthMm` up to at least 1 mm more than the larger kind threshold, so the thresholds win and a threshold slider pushes the ray width up rather than being held down by it (both thresholds are at most 2,000 mm and the ray width at most 10,000 mm, so the order always ends inside every range). The ranges are what 4.4's overflow bounds and the divisions by `forestNearMm`, `forestArrivalMs`, `forestFullPerimeterMm`, `sightFeatherMm`, `rimEaseMm`, and `shadowCornerEaseMm` rely on. The pinned cross-toolchain tests (13, step 3) run at the defaults. Each is a slider on the Dev Tools tab, so it can be tuned in the running game. A change of a sim tunable re-decides every body's kind where a threshold moved and rebuilds every observer's shape at once; the slider for `softShadowRayWidthMm` cannot go below the larger kind threshold.

`/api/dev/spawn` makes an ECS entity (`PlacementSystem::spawnEntity`, rotation 0) that never enters the placement index nav gathers obstacles from, so a spawned BigRock or tree is no body; tests and scenarios use placed trees and rocks, and built walls.

## 9. Tunables

| Name | Start | Range | What it sets |
|---|---|---|---|
| `wallOpaqueLeastDiameterMm` | 600 mm | 100 to 2,000 mm, below `softShadowRayWidthMm` | A wall body at least this across is opaque, below it soft. The shortest wall the tools allow is 0.52 to 0.58 m across |
| `placedOpaqueLeastDiameterMm` | 1400 mm | 100 to 2,000 mm, below `softShadowRayWidthMm` | A placed body at least this across is opaque, below it soft: between a veteran oak (1.16 m) and a BigRock (1.76 m) |
| `softShadowRayWidthMm` | 3000 mm | above both thresholds, to 10,000 mm | The ray a soft body's triangle is drawn with: wider shortens every triangle. Above both thresholds |
| `softShadowStrengthPercent` | 50 | 0 to 100 | A soft body's triangle at its foot, as a percentage of its width over the ray: lower makes every trunk fainter |
| `shadowEdgeWidthMm` | 1200 mm | 0 to 5,000 mm | The ray every fin is drawn with: wider widens every shadow's soft edge |
| `shadowWideningMostDegrees` | 10 degrees | 0 to 45 degrees | The cap on a shadow edge's band each side of its ray. Larger hides more of the rebuild step near corners and draws more seen-in-sight ground at full tint |
| `shadowFinMostDegrees` | 15 degrees | 0 to 45 degrees | The cap on a fin's angle beyond the band, so a beam through a doorway keeps a bright core |
| `shadowCornerEaseMm` | 1500 mm | 100 to 5,000 mm; never 0, it divides | How far he walks while a band's apex eases along a body face from its old corner to its new one (3.1). Longer slides the band more slowly at a corner switch and leaves the apex between the corners longer |
| `forestCapMm` | 800 mm | 0 to 5,000 mm, under `2^20` (4.4) | One placed ring's greatest forest depth, before its width and distance weights |
| `forestWindowSteps` | 8 | 1 to 32: past 32 a ring adds to some sides twice | The tent's half width in rim steps of 5.625 degrees: 45 degrees. Wider spreads a ring's depth and lowers the change per step |
| `forestNearMm` | 10000 mm | 1,000 mm to `sightRadius`; never 0, it divides | Where `distanceWeight` peaks. Larger lowers the change per step and the depth near trees |
| `forestFloorPercent` | 67 | 1 to 100 | The least range on any side, as a percentage of sight |
| `forestSlopeMm` | 1000 mm | 0 to 10,000 mm, under `2^20` (4.4) | The most one side's forest depth may exceed a neighbour's. Lower bounds convexity's loss in clear directions tighter (0.7 m at 600 mm against 1.9 m) and gives up more of a thicket's depth |
| `forestFullPerimeterMm` | 3142 mm | 100 to 10,000 mm; never 0, it divides | The perimeter at which `widthFactor` reaches 1: a body 1 m wide on average |
| `forestArrivalMs` | 2000 ms | 100 to 10,000 ms; never 0, it divides | How long a ring arriving inside sight takes to come into forest depth and its drawn triangle |
| `sightFeatherMm` | 1500 mm | 100 to 5,000 mm; never 0, it divides | The fade on both edges. The bright core of a 30 m sight is `drawnVisibleRadiusMm` less this and `rimEaseMm`, about 27.9 m; a shape narrower than twice it is never fully bright |
| `softLegToleranceMm` | 500 mm | 50 to 5,000 mm | How far a side's range may stray from its least over a leg before the leg ends; the record holds ground up to about 1.1 times this past where sight reached on a leg, 1.5 times radially at the ends of a floor side, plus one build's change where a leg's inherited shape meets it (6.2) |
| `rimEaseMm` | 400 mm | 100 to 450 mm, under `step`; never 0, it divides | How far he walks while the bright rim eases from where it is drawn to the newest build's ranges; at most the least distance between builds less a margin, and the bright shape is drawn the whole of it inside `drawnVisibleRadiusMm`, which keeps it inside the leg's ring through any run of restarted eases (6.2, 6.3) |
| `drawnShadowReachMm` | `reach`: 30.7 m at 1x, 32.2 m at 10x | 0 mm to `reach` | How far from a colonist bodies draw shadows; lowered only by 12's fallback |
| `drawnShadowRebuildMm` | 0 mm | 0 to 1,000 mm | How far a colonist moves before his drawn pieces are rebuilt; 0 is every frame, raised only by 12's fallback |

Fixed, not tunables or sliders, and named as constants:

| Name | Value | What it sets |
|---|---|---|
| `kFractionOne`, `kSightSides` | 65536, 64 | The integer fractions and the sight polygon's sides, by 4.4 |
| `kSeenDimAlpha` | 0.5 | The tint's darkness (`VisibilityCoverRenderer.h`) |
| `kSoftLegMostVertices` | 256 | The most vertices a leg's ring holds |
| `kSeenFeatherCellMm` | 32000 mm | The record feather's cells: smaller rebuilds less at an epoch and draws more meshes |
| `kOccluderBucketMm` | 16000 mm | The placed occluder index's buckets |
| `kDrawnShadowSliceToleranceMm` | 5 mm | How far a triangle's sliced side may stray from its rule |
| `kDrawnValueTolerancePercent` | 2 | How far a drawn piece's linear value may stray from the value it stands for |
| `kFeatherFanStepDegrees` | 15 degrees | The widest triangle of a reflex corner's fan |
| `kSightRebuildStepMeters` | 0.5 m | Player-visibility.md's |
| `kSightLeadMostMm` | 8000 mm | The most `lead` is taken as, so `reach` stays under `2^16` mm; a frame slower than that lacks the bodies beyond until the next pass |
| `kSeenSlackMm`, `kSeenLeastRadiusMm`, `kSeenSkirtMm`, `kSeenSweepSpacingMm`, `kSeenSweepRimBandMm` | player-visibility.md's | Legs take the slack and the least radius as open legs do |

## 10. Acceptance limits

Each is stated at the start values and at 1x unless it names 10x.

1. A pine 5 m off, from any angle: its triangle darkens visible ground by at most 0.04 on screen at its foot and is gone within 1 m behind it.
2. The shortest wall the tools allow, from any angle: at most 0.06 on screen at its foot (0.049 at its diagonal), gone within a quarter of its distance behind it.
3. A veteran oak, from any angle at any distance: at most 0.1 on screen at its foot, gone within its own distance behind it; no trunk draws a streak to the rim darker than 0.05.
4. A lone trunk up to a mature oak shortens the rim by at most 0.6 m on any side; a veteran oak or a BigRock by at most 0.8 m, besides a BigRock's hard shadow.
5. Walking past a veteran oak or a BigRock at 2 m and at 10 m, no side's range changes by more than 0.2 m between consecutive builds (0.3 m at 10x). Through the landing's forest, no side's range changes by more than 0.7 m between consecutive builds; at 10x the change is reported, not limited, since what shows of it is the eased rim.
6. Forest depth shows in ordinary forest: through mature oaks 5 m apart the rim is 2.5 to 6 m in from full sight, and in the densest stand at the landing it is at the floor or above.
7. In the landing's forest, measured against the star polygon of the slope-limited ranges: convexity takes at most 0.5 m off the mean range, at most 2 m in any clear direction (a side whose own capped depth is 0; 1.89 m by 4.2), and at most 2.5 m in any direction, inside a wide, sharp-edged thicket included (2.3 m by 4.2).
8. Per source of change, from one frame to the next, on screen (the 10x figures are reported, not limited):
   - The eased rim, walking through forest: at most 0.03 (6.3 gives 0.028). At 10x up to 0.24 for a lone veteran oak's change and up to 0.5 in dense stands.
   - A change of path between a leg and the polygon path: nothing beyond the eased rim's own, at 1x and 10x (5.2).
   - A rebuild with a shadow edge's corner past the cap distance (3.9 m at 1x, 12.7 m at 10x): at most 0.02 at any pixel of the cover, but where the build eye and he stand on opposite sides of a body face's line, where a sliver up to that face's switch shift wide can step by the fin's fall across it (3.1), reported, not limited.
   - A rebuild with the corner nearer: at most 0.5 at 1 m, 0.32 at 2 m, and 0.13 at 3 m (3.1). At 10x, 0.5 out to 8 m, 0.39 at 10 m, and 0.10 at 12 m.
   - A corner switch, as he crosses the line of a body face: no pop; the apex eases, and with the corner 5 m off the band moves at most 0.03 a frame from 2 m behind the corner for a face up to 4 m long (0.025; 0.009 for a BigRock's face), in proportion to a longer face's length, and 0.05 at 1 m behind (3.1). At 10x, 0.32 at 2 m behind for a 4 m face and 0.11 for a BigRock's.
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
- The record's tick. Legs, their spread tests and a hull merge every frame for each colonist on one, and the rings their ends register, crescents among them (6.2); on the polygon path, the sweep's builds.
- The cover. Each frame, the drawn pieces of every body in reach of each colonist in view, a few hundred bodies a colonist in forest; the record feather's cells at each epoch, and the epoch rate; the pending cuts at each moving leg's end; the per-colonist passes of 7.6; the partial layers' GPU cost against the hard cover's.
- Views with reveal on draw no cover and build no pieces; captures should come out even with `main` apart from vision's tick.

To measure:

- Step 0, the sandbox: GPU time of the cover with its partial layers against the hard one in `perf_mask` at the same size, at 1, 10, and 50 colonists, and whether every case of 7.7 holds.
- Step 1a, the sim bench: "Prototype: sight ray casting across the nav mesh with tree attenuation, measure perf" on Specboard. No rendering, and placed rings only, so it does not wait for the "wall sight occluders" work. A bench of a build at viewpoints in the quickstart landing's forest and in synthetic stands of known density: microseconds a build, rings in sight and opaque bodies in reach, vertices out, the range convexity takes with and without the slope limit, and the depth the limit gives up. The sim bench with 1, 10, and 50 colonists walking the landing's forest at 1x and 10x, several walking the same ground: vision's tick; the record's tick; K where registered, mean and max, from the counter step 1a adds to `SeenArea` (6.6), against player-visibility.md 6.6's; the pending queue's greatest length; pieces registered per leg; the share of builds on each path; legs per kilometre and their lengths; rings registered per kilometre and per minute; record epochs a second and cells rebuilt an epoch; the per-frame pending cut and the per-frame hull merge; the greatest change of a side's range between builds against 10's limit 5; rings arriving inside sight; and how the landing's tree and BigRock density read from placement compare with 4.3's stands. K, the queue, and pieces per leg come first: they are the main cost risk (6.2), and the go for the record is 6.6's thresholds.
- Step 1b, the limits check: in the game, after step 0 and step 2's wall bodies, since several limits are wall shadows (the doorway, a wall end's corner switch). The cover's CPU time building drawn pieces a frame at 1, 10, and 50 colonists in view, and each of 10's limits measured on screen at the start values, at 1x and 10x.
- Step 5, the cover: a capture near the landing with `--no-reveal` at each zoom with the crew, 10, and 50 colonists: GPU and CPU time of the cover, pieces drawn a frame, the per-frame pending cuts, and the record's cells at an epoch.

No-go at step 1a or 1b takes the fallbacks in order and measures again; a no-go after them is a change to this design. For vision's tick: builds budgeted per tick, oldest first, an observer past the budget keeping his last shape a tick longer (on a leg it moves with him and only its ranges are stale; on the polygon path the far side of his sight shortens by how far past a step he has gone); then `forestWindowSteps` 4 with `forestCapMm` raised to keep a stand's depth, halving the sides each ring adds to, with 10's limits checked again. For the drawn pieces: `drawnShadowRebuildMm` 100 mm in place of every frame; then a lowered `drawnShadowReachMm`. For the record, 6.6's fallbacks in order. For the cover, 7.7's.

## 13. Build steps

Each step is its own PR with its own done line and a `Perf:` line. Steps 1b and 2 build on the "wall sight occluders" work ("Wall sight occluders are the wall's center line, so the visibility tint starts mid-wall" on Specboard) once it is on `main`. No done line waits on a sign-off: the tunables are sliders (8), set afterwards in the running game. Steps 0, 1a, and step 3's geometry can start at once; the rest wait as each step's line says.

0. Partial coverage in salal. The backend work of 7.6 (min and max blend equations, alpha-only and no-colour writes, the two depth states, a depth clear, the targets' alpha and depth checked at start), the sandbox scene and test of 7.7, then the API of 7.6. Done: a tagged salal whose `mask` scene shows every case of 7.7 with no seam, nothing darkened twice, no piece lightening outside its colonist's shape, and no colonist's shadow over another's light at 50 colonists, the value test passing on the GL backend, `docs/masks.md` brought up to date (`mask-mode`, `mix-blend-mode`, `isolation`, per-child masks, and the partial layer), and worldsim's pin on that tag; or the fallback of 7.7 that holds, with the test of its values. Depends on: nothing.
1a. Sim bench. As 12, on a branch that builds 5's shape from nav's placed rings only, no wall bodies, and runs legs and the polygon path through the record, with no rendering. Done: 12's step 1a numbers in this document; a go by 6.6's thresholds (the pending queue bounded over ten minutes at 50 colonists at 10x, K, the record's mean tick, and its worst tick), or the fallback of 12 or 6.6 taken, for vision and for the record. Depends on: nothing; it reads nav's placed rings as they are.
1b. Limits check. As 12, after step 0 and step 2: the bench branch, with step 2's wall bodies merged into it, draws 3's shadows and the eased rim with step 0's masks. Done: every limit of 10 measured and reported in this document at the start values, at 1x and 10x, and the cover's CPU time; a go, or 12's fallback taken, for the drawn pieces; 6.6's thresholds checked again in the game at 50 colonists at 10x. Depends on: step 0, step 1a's go, and step 2 (the wall bodies it measures, on the "wall sight occluders" work).
2. Bodies. Wall bodies in `GeometryIndex`: each record's body, one record a body with its edges, box, `diameter`, and kind, made in `rebuild()`, with the rejected graph's bands as one body; `queryFacingOccluders`, `backFacesHiddenFrom`, and `insideSolid` reading opaque bodies only. The placed occluder index in `NavigationSystem` with serials, perimeters, diameters, kinds, arrival times on `TimeSystem::simulationSeconds()`, and buckets, updated in `collect()` in patch order, the removal callback carrying its ids. Silhouettes, widths, distances; shadow edges tagged with their corners; the cache keyed on serials; `lead` from the measured frame interval and `sightBodies` to `reach`; `SightTunables`, its ranges, and its sliders; the `sightBodies` state view. Done: a placed obstacle occludes by its kind and a mover, water, a ford, and a bounds ring do not; a refused add never enters the index; a wall stub is left out of the visibility polygon and still joins memory and `StoreOrDrop`'s walls; a rejected graph takes the kind the same walls take when accepted, away from the threshold; felling a tree rebuilds an observer's shape and walking over new ground with no obstacle does not; a tree placed on a ring id freed in the same job rebuilds it and leaves the index with one entry; vision never joins the patch job (a test with the job held running); the state view lists a BigRock with kind opaque. Depends on: the "wall sight occluders" work on `main` and step 1a's go.
3. The sight shape. Forest depth and the forest shape in `libs/geometry/visibility` (the side normals' table among them), the polygon path's clip, `SightShape`'s fields, the overlay, and the landing of 5.3. Done: unit tests for a wall stub (no change to the shape), a lone tree (depth at most the cap times its weights), a stand of known density (depth within 10% of 4.3's), a veteran oak walked round at 10 m in 1 cm steps with no side changing by more than 4.3's bound for the step plus 1 mm, a ring coming into sight, a ring arriving inside sight over `forestArrivalMs`, equal ranges giving the sight polygon to 2 mm, the forest shape convex and holding the floor's sight polygon over random stands, the slope limit holding every pair of neighbouring sides within `forestSlopeMm` and a clump of 40 trunks taking at most 2 m from any clear direction, a BigRock's shape equal to `computeVisibilityPolygon`'s for its ring clipped to the forest shape, a wall run cutting its exact shadow, and a clear direction untouched; forest depth and the forest shape pinned at the defaults and identical on MSVC, Clang, and GCC. Depends on: nothing for the geometry in `libs/geometry/visibility` (forest depth, the forest shape, the side normals, and their tests), which can start at once; step 2 for the polygon path's clip, `SightShape`'s fields, the overlay, and the tests that read bodies.
4. The record. Legs as hulls joined at every held position with the spread and vertex ends, a new leg holding the shape drawn on the frame it opens, `setOpenLeg` with a position, ranges, and the inherited shape, the rim passed to `queueSightPolygon`, the skirt and the sweep's band on that rim, the sweep point's interpolated rim and its builder, `appendEdges`, `inRecord`, `takeChangedBoxes`, the eased rim, and the bright shape's rule on both paths. Done: tests for a straight walk through a stand (one leg while the spread holds), the spread ending a leg, a walk from open ground into forest and to a BigRock, the bright shape inside the record or pending on every frame of a walk through a stand with sideways wobble and leg ends, the eased rim moving no side more than `distance walked / rimEaseMm` of the ease's span in a frame and nothing at a restart, a change between a leg and the polygon path leaving the rim where the ease puts it, the record never more than 1.1 times the tolerance past the shapes a leg held as a distance and 1.5 times radially, plus one build's change where an inherited shape meets the leg (6.2), and the record oracle (no point on the record outside a true shape grown by the leg tolerance and the skirt); the sim test that every seen point is on the mesh and the record never lost ground, run in forest. Depends on: step 3 and step 1a's go for the record.
5. The cover. The feather cells, the pending cuts, each colonist's feather and drawn shadows each frame, and the masks of 7.5. Done: soft edges on both edges in the game, faint triangles collapsing behind trunks and wall stubs, bands and fins beside hard shadows, outdoors and inside a room, scenario 14's new steps passing, the capture of 12. Depends on: steps 0, 2, 3, and 4, and step 1b's go for the drawn pieces.
6. Tests and docs. Scenario 14's steps (14) written in, player-visibility.md's tunables and measured sections brought up to date. Done: scenario 14 run. Depends on: step 5.

## 14. Tests and scenario steps

Unit and sim tests are named in each step's done line. Before they are added, check the existing vision and record suites for the same paths and extend those.

Scenario 14 (`docs/testing/scenarios/14-player-visibility.md`) takes these steps after its step 2, all at the quickstart landing, written in at step 6. Spawned things are not bodies (8), so the steps use the landing's placed trees and rocks, and built walls:

1. Zoom 3 on Bob among trees: the bright area's edge is a fade about 1.5 m wide with no hard line, and the tinted area's edge against the black fades on the tinted side; past it the black is solid.
2. Press V: Bob's rim is drawn in toward denser trees, never closer than about 20 m, and is whole where the trees thin out.
3. Look behind a trunk 5 to 10 m from Bob: a barely visible darker triangle starts at its foot and closes behind it, shorter behind a thin trunk and a little longer behind a thick one.
4. `curl.exe "http://127.0.0.1:8081/api/state?what=sightBodies"` and find a BigRock among Bob's bodies (walk Bob through the forest until one is listed), then stand Bob 8 m from it: a shadow runs from its near face to the rim, and beside each side a gradient widens toward the rim, hard at the rock's corner.
5. Walk Bob slowly past a trunk, and past the BigRock keeping 4 m or more from its corners: the triangle and the shadow turn and grow or shrink with him with no jump, the shadow's soft side sliding along the rock as he passes the line of one of its faces, where a rebuild can still step a sliver beside the shadow (limit 8), and the seen area behind him does not shrink.
6. Build the shortest wall the tools allow 5 m from Bob: a barely visible triangle behind it, gone within about a metre. Build a 2 m wall beside it: a full shadow with soft edges.
7. Draw a room with a door and a window and stand Bob inside: sight passes the doorway and the window and ends at the walls' near faces, and both sides of the light through the doorway fade. Stand Bob about half a metre back from the doorway: the beam through it keeps a bright core between its soft sides.
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
19. The kind measure, a body's `diameter`, and its two thresholds: a wall body opaque from 0.6 m across, a placed body from 1.4 m (2.2). Kevin's "0.1 m" block cannot be built (a wall is at least 0.5 by 0.15 m), so 0.6 m is set to make the shortest walls the tools allow soft, above the 0.3 to 0.5 m first suggested; it also leaves walls up to 0.52 to 0.58 m long soft, by their thickness. The 600 mm wall threshold is the author's and flagged. Every tree is soft and a BigRock opaque. A rejected wall graph's kind taken over the whole graph; a growth stage crossing a threshold changing kind as a change in the world. The 600 mm wall threshold leaves the shortest buildable wall (0.5 m) soft, against his "The whole wall body is opaque. It's a rectangle, not a line." (6); unconfirmed.
20. A soft body's triangle drawn with a 3 m ray and its foot at 50% of its width over the ray, so a trunk darkens visible ground by under a tenth of the tint at its foot (3.2); fins from a separate 1.2 m edge ray. Departure from C as drawn: a soft body casts no shadow in the sim.
21. Opaque shadows drawn per shadow edge of the build's polygon, each with a band at full tint turned up to `asin(widening / cornerDistance)` either side of the ray through its corner, `widening` the greater of `lead` and his distance from the build eye, capped at 10 degrees on the lit side and uncapped on the shadow side, where it ramps up from twice `widening` of the corner and is 180 degrees within `widening`, and a fin capped at 15 degrees (3.1). Past the cap distance (3.9 m at 1x, 12.7 m at 10x) a hard shadow does not move on screen at a rebuild, but by a body face's line, where a build eye across the line can step a sliver up to the face's switch shift wide; nearer, its side steps toward the lit side by what the cap leaves (0.13 on screen at 3 m, 0.32 at 2 m, a full step at 1 m or nearer, at 1x). Crossing the line of a body face at a corner eases the band's apex along the face over 1.5 m of walk (`shadowCornerEaseMm`) instead of popping it, 0.025 on screen a frame 2 m behind a corner 5 m off for a 4 m wall face at 1x. Within twice `widening` of a corner the shadow side's 180 degrees draws in-sight ground on that side at full tint, a wedge of a room's floor beside a doorway among it; flagged. The cost is up to 10 degrees of in-sight ground beside each shadow side drawn at full tint, 5.3 m wide at the rim. A doorway seen from 0.6 m keeps a 17 to 8 degree bright core, by the wall's thickness.
22. Departure from C as drawn: forest depth as a density over the placed rings in sight, evaluated at the 64 sides of the sight polygon, with a tent over 45 degrees, a distance weight peaking at 10 m and falling to 0 at the sight radius, a fixed `widthFactor` from the perimeter, an 0.8 m cap, and a floor at 67% of sight, in place of a budget spent along each ray (4). Opaque placed bodies spend it as soft ones do; walls spend none.
23. The outdoor sight shape convex by definition, the intersection of one half-plane a side, with a slope limit of 1 m of forest depth between neighbouring sides (4.1, 4.2). Convexity alone takes up to 9.8 m from clear directions beside a clump; with the limit the worst loss in clear directions is about 1.9 m (0.7 m at a 0.6 m limit), and inside a wide, sharp-edged thicket convexity takes up to 2.3 m off the slope-limited ranges, and a narrow thicket gives up depth at its edges to pay for it, up to 7.9 m for 200 trunks in one place. Both the remaining loss and the thicket's lost depth are flagged.
24. Legs as the hull of every shape walked with, joined at every held position, a new leg holding the shape drawn on the frame it opens, ended by a 0.5 m spread of any side's range or 256 vertices, so bright ground is always seen and the record holds ground up to about 1.1 times the tolerance past where sight reached on a leg as a distance, 1.5 times (0.78 m) radially at the ends of a floor side, plus one build's change where a leg's inherited first shape meets it (that shape held in the ring but not in the spread test), an allowance like the skirt's (6.2). This may overshoot "seen" as Kevin means it. Unskirted forest legs registering crescents are the design's main cost risk, with tolerance-snapped legs the first fallback, the only one that removes crescents, ahead of the one that departs from rule 27 (6.6).
25. The skirt grown off opaque bodies' sides as well as walls' (6.4), which extends rule 21 from walls to opaque bodies.
26. The polygon path's rim, the sweep point's interpolated rim, and the inset measured between corresponding rim vertices (6.4).
27. A ring arriving inside sight coming into forest depth and its drawn triangle over 2 s of sim time; an opaque one casting its shadow at once (2.4).
28. The placed occluder index in `NavigationSystem`: serials, buckets, kinds, arrival times stamped on `TimeSystem::simulationSeconds()`, the removal callback carrying its ids, updated in `collect()` in patch order (2.4); wall bodies and kinds in `GeometryIndex` on the "wall sight occluders" work's records, grouped into bodies; `sightBodies` reaching `reach`, `sightRadius + lead`, and its own state view, `what=sightBodies` (8).
29. Departure from C as drawn: `rememberArea` at the landing reading no occluders, so the crew's memory agrees with the plain disc the record commits (5.3).
30. Rule 10 read against three caches: the placed occluder index's 16 m buckets, the record feather's 32 m cells, and player-visibility.md 3.4's seen boxes are lookups over vector rings and edges and hold nothing sampled, so they are no raster or grid of game data.
31. Rules 10 and 19 read against 7.6: destination alpha, depth, and the stencil are per-pixel state of the frame being drawn, written and read inside the cover's draws, holding no game data between frames, with no texture made, so rule 10's "no raster, grid, bitmap, or mask texture anywhere in this system" holds; rule 19's "drawn with the stencil buffer" covers the hard layers, and the partial layer uses the frame's alpha and depth beside it.
32. The seen area's feather drawn for the union's boundary only, kept in cells rebuilt from changed boxes, with pending cuts held and redone where a leg's ring moves; a body face left hard in the tint (7.2).
33. Departure from C as drawn: several colonists composed as the least darkness, each colonist's darkness the greatest of his pieces, each piece clipped to his bright shape; and the lens, where two sights overlap by under twice the feather, left dimmer than a fade from the union's edge (7.4).
34. The salal surface: `MaskMode::Luminance`, `MaskImage::content`, `MaskBlend::Lighten` and `Darken`, `MaskIsolation::Isolate`, `MaskPaint::linear`, mesh children with a value at each vertex, and children carrying their own hard mask (7.6).
35. Every start value in 9, every acceptance limit in 10 (limit 2 at 0.06 on screen, leaving room over the 0.049 a stub gives at its diagonal; limits 5 and 8 at 1x, 10x reported), and 6.6's fourth fallback, a departure from rule 27.
36. The bright rim eased over 400 mm of walk from where it is drawn to the newest build's side offsets, the blend restarting at 0 at every build and measured by the walk since, its start held per observer in `PlayerVisibilitySystem` and inherited by the next leg, so a restart moves nothing and the rim changes by at most 0.028 on screen a frame at 1x in dense stands (0.06 when a ring is felled or placed mid-ease); it lags a build by up to that walk, and at 10x a build's change lands within a frame or two. The bright shape is drawn the whole `rimEaseMm` inside `drawnVisibleRadiusMm` so it stays inside the leg's ring through restarts, a fully bright core of about 27.9 m in the open (6.2, 6.3).
37. The polygon path's `isVisible`, discovery, and bright shape cut by the forest shape a leg would give at his position, so both paths give the same rim (5.2, 6.3).
38. Step 1 split into a sim bench and an on-screen limits check, with no done line waiting on a sign-off; the tunables set afterwards with the sliders (13).
39. `lead` computed each pass from the measured frame interval and the sim speed, held to at most 8 m (`kSightLeadMostMm`), and `reach` set to `sightRadius + lead`, so both paths give the same rim (2.4, 5.2).
40. Shadow edges tagged on `computeVisibilityPolygon`'s ring before the forest clip, from its exact corner vertices, a 1 mm test on the far end, and the ring's orientation, each tag kept when its corner survives the clip unchanged (3.1).
41. The go thresholds for the record in forest (6.6): the pending queue no longer at ten minutes than at five and never over 1,000, K at most 100 mean and 400 greatest, the mean tick at most 0.20 ms at 1x and 0.23 ms at 10x, and the worst at most 5 ms, at 50 colonists. The order of 6.6's fallbacks by what each fixes, tolerance-snapped legs first.
42. Each tunable's range in 9, clamped by `SightTunables`' setter, fixed bounds first and then the soft ray width raised above the larger kind threshold (8).

## Related documents

- [Player Visibility](./player-visibility.md): the rules, the seen record, the cover, and the input rules this builds on
- [Vision Architecture](./vision-architecture.md): sight shapes, discovery, doors and windows
- [Nav Mesh: One Mesh, Variable Resolution](./nav-mesh-variable-resolution.md): the collision map and where it exists
- salal `docs/masks.md`: the mask API this extends
- [Testing](../testing/README.md): scenarios and the perf rule
