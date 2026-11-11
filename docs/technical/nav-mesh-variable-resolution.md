# Nav mesh: one mesh, variable resolution

**Status:** Draft, not approved. No implementation starts until it is.
**Created:** 2026-11-10
**Replaces, once built:** the "As built: multi-region sim area" section of [pathfinding-architecture.md](./pathfinding-architecture.md)
**Fixes:** WOR-644 (Pathing can't route between two points on connected ground when nav regions overlap), WOR-645 (Chunk streaming follows the camera, so panning away unloads the colony's chunks)

Today the nav mesh is a handful of separate rectangles, each rebuilt whole when anything in it
changes. This document replaces them with one persistent mesh, stored in fixed cells, that is
patched a cell at a time, carries two levels of detail, and never lets the camera decide
anything larger than the shape of a path around a tree.

Numbers marked *measured* come from `geometry-benchmarks` on this branch (RelWithDebInfo, the
dev machine, 2026-11-10). Numbers marked *estimate* are mine and are gated on a measurement
named beside them.

---

## 1. Intent

Kevin's rules for this system, 2026-11-10. Everything below is judged against them.

1. One connected nav mesh with varying resolution. Not island meshes.
2. If the mesh for a piece of ground is computed, it is never computed twice.
3. The mesh is an entity that gets modified. A change patches it; nothing destroys and recreates it.
4. Terrain and large dynamic things are always in: walls, doors, later fences, barriers, large mobile objects.
5. Small things (trees) are detail. Where nobody is looking, movers walk straight through them.
6. The camera decides detail. It never decides reachability, what a mover chooses, or whether it acts.
7. Things off camera can force detail when imprecision would change gameplay. Combat meets that bar. The list is decided case by case.
8. When detail appears under a mover, the mover snaps forward along its route, never backward. Scrolling the view on and off a traveller can't stall or reverse it. A short forward teleport through a tree is acceptable.
9. A chunk with active simulation in it is never unloaded. A mover walking into an ungenerated chunk generates it. That is expected to be rare.
10. Everything that moves uses the same movement system.
11. The static geography mesh (Phase 2 in the pathfinding doc) is a separate thing and stays planned. It is not the nav mesh.
12. Nav is expected to be the most expensive thing the game computes. Every step is measured against main.

## 2. Current state

| Area | Today | After |
|---|---|---|
| Storage | A list of rectangles: a 60 m square per colonist, one for the viewport, overlapping ones merged, re-squared, clamped to 128 m. Each has its own complete mesh | One mesh keyed by fixed world cells |
| Change | Any trigger rebuilds a whole rectangle. A wall edit or a felled tree anywhere rebuilds every rectangle (global counters) | The triangles around a changed obstacle are patched, nothing else |
| Repeated work | Built rectangles overlap; a recenter re-meshes mostly unchanged ground; the placement mesh is a third copy | A cell at a given level and content is built once and kept |
| Connectivity | A path needs both ends in one rectangle. Another rectangle reads unreachable, no rectangle holds | Paths cross cells. Reachability covers all loaded ground |
| Detail | Every tree, everywhere a rectangle exists | Fine cells where the view or gameplay needs them, coarse elsewhere |
| Camera | Drives a rectangle like a colonist does, so it reshapes and can stall colonist coverage | Chooses which cells are fine. Nothing else |
| Movers | Colonists only | Anything with a position and a clearance |
| Chunks | Loaded 1 chunk around the camera, unloaded at 2 | Also held and loaded around active simulation |
| Threads | Input gathered on the main thread in the frame, mesh on one `std::async` thread per build | Section 4.9 |

### 2.1 Measured: what a build costs

`buildNavMesh` on real inputs dumped from the quickstart game (128 m square regions). Median of 5.

| Input | Trees | Triangles | Build |
|---|---|---|---|
| manyTreesDump | 512 | 4,418 | 152 ms |
| controlDump | 639 | 5,434 | 234 ms |
| realRings | 637 | 6,215 | 295 ms |
| controlDump, trees removed | 0 | 310 | 1.7 ms |
| realRings, trees removed | 0 | 1,109 | 12.6 ms |

Tree count swept inside the fixed 128 m area: 64 trees 7.2 ms, 128 trees 16.6 ms, 256 trees
46.7 ms, 512 trees 149 ms. The growth exponent climbs from 1.2 to 1.8.

Area swept at fixed density (the region tiled k by k): 128 m 228 ms, 256 m 3.3 s, 384 m
17.6 s, 512 m 56 s. Exponent 2.0.

Stage split on controlDump: `buildArrangement` alone is 211 to 229 ms of the 234. The
constrained Delaunay triangulation is below the noise. Classification, corridor widths, the
locate grid, and the reachability forests together are 5 to 20 ms.

The cause is in the code, not inferred: `buildArrangement` tests every segment against every
other segment, and repeats the whole pass until nothing splits
(`libs/geometry/arrangement/Arrangement.cpp`, the `i`/`j` loops). Trees are over 99% of the
cost of a build, and nearly all of that is asking whether tree A's edges touch tree B's
edges for trees 100 m apart.

Two consequences shape the design. The expensive part is finding which segments interact, not
triangulating, so a change that already knows what it touches (one tree's own edges) can skip
nearly all of the cost. And the cost is superlinear in how much goes into one build, so small
builds are disproportionately cheap even before the loop is fixed.

### 2.2 Not measured yet

These need the running game, one launch (section 6):

- Tree count per region on today's flora. The fixtures are from September.
- `nav::buildInput` cost on the main thread, split into water, flora, and walls.
- How often regions rebuild in normal play, and which trigger caused each.
- How many builds are in flight at once, and how much ground is meshed more than once.

## 3. Prior work

What was found, and what this design takes from each.

| Source | What it shows | What we take |
|---|---|---|
| Recast/Detour tile cache, Unreal, Unity, A* Pathfinding Project `NavmeshCut` ([Granberg](https://arongranberg.com/2013/08/navmesh-cutting/)) | Every shipping engine patches at tile granularity. None documents a sub-tile patch. Smaller tiles are the main cost lever | Cells are the patch unit |
| Unreal navigation invokers ([docs](https://dev.epicgames.com/documentation/unreal-engine/using-navigation-invokers-in-unreal-engine)) | Tiles exist only around agents, with a generation radius and a larger removal radius | Two radii for detail, so a view edge doesn't thrash |
| Unreal per-area navmesh resolution ([docs](https://dev.epicgames.com/documentation/en-us/unreal-engine/navigation-mesh-resolutions-user-guide)) | Resolution there is voxel size, and low resolution can delete real paths | Not a precedent. Our levels differ by which obstacle classes are in, never by geometric precision |
| Godot region connection ([docs](https://docs.godotengine.org/en/4.2/tutorials/navigation/navigation_connecting_navmesh.html)) | Regions join cleanly only when edge vertices coincide exactly; margin heuristics fail on partial overlaps | Seams are exact shared integer vertices |
| Factorio FFF-317 ([post](https://cf-www.factorio.com/blog/post/fff-317)) | The chunk-component abstraction ignores entities, so placing buildings never touches it; only terrain changes do | Reachability is computed without flora, so detail changes never touch it |
| Kallmann, dynamic CDT and Local Clearance Triangulations ([TOG 2014](http://graphics.ucmerced.edu/papers/14-tog-lct.pdf)) | Local insert and remove with reference-counted constraints. Floating point with an epsilon. Local updates beat a global pass only when under roughly a quarter to a half of the vertices change | In-place edits for single obstacles, with our exact integer predicates and per-edge ring lists (4.4.1, 4.4.2); whole-cell derive for bulk changes |
| Demyen and Buro ([AAAI 2006](https://cdn.aaai.org/AAAI/2006/AAAI06-148.pdf)); Kallmann on the same measure | A corridor width depends on the nearest obstacle in a wedge, which can sit outside any patched region. Kallmann reports the measure is wrong in some CDT cases | Widths are bounded by a maximum clearance and invalidated one cell around a patch |
| jdxdev RTS series ([part 2](https://www.jdxdev.com/blog/2021/07/06/rts-pathfinding-2-dynamic-navmesh-with-constrained-delaunay-triangles/), [part 3](https://www.jdxdev.com/blog/2021/12/07/rts-pathfinding-3-variable-agent-size-smoke-tests-navmesh-fixes/)) | A hobby project, one informal timing. Updating subsets took longer to get right than the first mesh; overlapping obstacles were the trap | Crossings are resolved on the integer grid by the existing arrangement, per cell, before triangulation |
| CGAL, snap rounding ([docs](https://doc.cgal.org/latest/Snap_rounding_2/index.html)) | Exact predicates don't cover constraint crossings; a snapped intersection can land on the wrong side of a nearby edge | Same conclusion |
| Dynamically Pruned A* ([abstract](https://webspace.science.uu.nl/~gerae101/pdf/DPAStar-Abstract-ICTOPEN-2016.pdf)), Detour `dtPathCorridor::isValid` | A path that doesn't cross the changed region stays valid | Per-cell versions on stored paths; only crossing paths replan |
| Holm, de Lichtenberg, Thorup, dynamic connectivity | Theory only. No incremental Kruskal reconstruction tree exists in the navmesh literature | Reachability is rebuilt, on the coarse form only, where it is small |
| S.T.A.L.K.E.R. A-Life ([interview](https://www.gamedeveloper.com/game-platforms/interview-inside-the-ai-of-i-s-t-a-l-k-e-r-i-)) | Offline agents route on a coarse graph, online ones on detail. Going online needs a valid nav vertex and has shipped stuck-in-transition bugs | The shape of the idea. The switch rule is ours |
| Minecraft chunk tickets ([wiki](https://minecraft.wiki/w/Chunk)) | Chunks stay simulating while something holds a ticket on them | Chunk anchors |

No source documents what happens to a mover's position or route at a detail switch, and none
has a forward-only rule. Section 4.8 is new.

## 4. Design

### 4.1 Cells

The world is divided into fixed square cells aligned to the chunk grid. The nav mesh is a
store of cells keyed by cell coordinate. A cell's triangles are a pure function of its square
and the obstacle rings that touch it, so the same cell with the same content is the same mesh,
whoever asked for it.

Cell size is 32 m or 64 m, chosen from the step 1 and step 3 measurements. A chunk is 512 m,
so it holds 16 or 8 cells a side. From the density sweep, a 64 m forest cell is about 160
trees and a 32 m cell about 40.

A cell's border is a constraint in its triangulation, so no triangle crosses a cell. For
pathing a border edge is an ordinary passable edge.

### 4.2 Two forms per cell

Every obstacle belongs to a class.

- **Structural:** terrain polygons (water), walls, doors, and what joins them later (fences, barriers, large mobile objects).
- **Detail:** flora colliders and other small things.

Every cell that exists has a **coarse form**: its triangulation with structural obstacles
only. A cell where detail is on also has a **fine form**: structural plus detail. This is one
mesh with two levels, the way a texture has mip levels. Each level of each cell is built once
for a given content.

Routing uses the finest form a cell has. Reachability always uses the coarse form. That split
is rule 6 made mechanical: the camera can only ever add fine forms, and no large-scale answer
reads one.

The coarse form is cheap. Open ground is two triangles. The measured structural-only cost of a
whole 128 m region with river is 1.7 ms to 12.6 ms with today's all-pairs loop, so a cell's
share is well under a millisecond.

The fine form does not reuse the coarse form's triangles. It could, by inserting trees into
the coarse triangulation, but the coarse work is under 1% of the fine cost, so there is
nothing to save.

### 4.3 Seams

Two neighbouring cells share a border segment. The vertices on it are the cell corners plus
the points where rings cross the border line, computed by one canonical clip so both sides get
the same integer points. Chunk terrain rings already meet this way at chunk borders.

- Coarse beside coarse, and fine beside fine with the same flora: the vertex sets are identical, and border edges pair one to one.
- Fine beside coarse: the fine side has extra vertices where trees straddle the border. Every fine border edge lies inside exactly one coarse border edge, so the link is one coarse edge to an ordered run of fine edges. Seam links are recomputed, by a merge of two sorted vertex lists, when either side's routing form changes.

A triangle's neighbour across a seam is a handle (cell, triangle). A* and the funnel run across
cells without knowing where the seams are.

### 4.4 Patches

The mesh persists. Events change it a cell at a time:

| Event | Effect |
|---|---|
| A chunk becomes ready | Its cells get coarse forms |
| A tree is felled, a node depleted | Removed in place from the fine forms that hold it (4.4.1) |
| A wall or door is removed | Removed in place from the coarse and fine forms that hold it |
| A wall or door is built, flora gains a collider | Added in place (4.4.2) |
| A large object moves | One hole over its old and new footprints is re-derived (4.4.3) |
| Detail turns on for a cell | Its fine form is derived, or taken from the cache |
| Detail turns off | Its fine form moves to the cache. Nothing is rebuilt |
| A chunk unloads | Its cells are dropped |

Deriving a whole cell runs the existing exact pipeline (arrangement, constrained Delaunay,
face classification) on that cell's rings. It happens when a cell or a level first appears. A
single obstacle changing never derives a cell.

#### 4.4.1 Removing an obstacle

A tree's base is a ring, usually a rect. Its vertices are mesh vertices and its edges are
constrained triangle edges. Felling it touches the triangles around those vertices and nothing
else.

1. Every constrained edge carries the list of rings that put it there; the arrangement already produces this. The tree's id comes off its edges. An edge whose list is now empty stops being a constraint. An edge another ring also covers stays one.
2. A vertex with no constrained edge left, and not on a seam, is deleted: the triangles around it come out and the hole is retriangulated (Delaunay vertex deletion).
3. Freed edges are legalized with flips, so the result is still constrained Delaunay.
4. The new triangles are classified. Normally they join the floor around them; where another ring still covers the spot they keep that ring's tag.
5. Corridor widths within the maximum clearance of the hole are invalidated, since a width can depend on an obstacle outside the triangles that changed. The locate buckets over the hole are refreshed.

Nothing global moves. Reachability reads the coarse form, which a tree is not in. No stored
path can be broken by ground opening up; a path that would now be shorter is found at its next
replan.

Removal makes no new points. Every vertex involved already exists on the integer grid, so
every decision is one of the exact predicates the builder uses today. That is why this
direction is safe where insertion needs care.

Three cases need the list, not the picture of an isolated rect:

- **Shared edges and vertices.** Two overlapping trunks, a trunk against a waterline, a trunk on a cell border. The per-edge ring list is what keeps the neighbour's edge when one of them goes.
- **Crossing points.** Where the tree's edge crossed another ring, the arrangement split both at a rounded point. That vertex stays on the other ring after the tree is gone. Taking it out would move the other ring's edge by up to a millimetre and reopen the rounding question. It is tidied the next time the cell is derived.
- **Co-circular points.** The four corners of a rect lie on one circle, so Delaunay has a free choice of diagonal there, and a patched cell can differ from a freshly derived one in which diagonals it picked. Walkable ground, face tags, and reachability are identical. So the test oracle is at the face level: after any sequence of removals, the cell's constraints and classified faces equal a fresh derive of the same content.

#### 4.4.2 Adding an obstacle

Adding is the harder direction, because a new ring can cross edges that are already there, and
a crossing point has to be rounded to the grid.

The triangles under the new ring, and under any ring it crosses, are cut out along existing
edges. That hole, with its boundary held fixed, goes through the same arrangement and
triangulation a cell does, and the result is spliced back. For a tree that touches nothing the
hole is the handful of triangles its rect overlaps. Crossings are resolved by the arrangement,
on the integer grid, the way they are today. A cell is the same operation with the hole as big
as the cell.

A new obstacle can break stored paths, so paths crossing the hole's triangles replan (4.7), and
a new wall also changes the coarse form and so reachability.

#### 4.4.3 Moving an obstacle

A move is the same operation with one hole covering both footprints.

1. Find the triangles the old footprint and the new footprint intersect, plus those under any ring either one crosses.
2. Take the closed loop of existing edges around that set as the hole's boundary and hold it fixed.
3. Re-derive only the inside, from the rings that touch the hole with the object at its new position, and splice it back.

Because the inside is derived from rings and not from the old triangles, a move cleans up after
itself: crossing points the object made at its old position are gone, so nothing piles up
along its track the way stray vertices do in Kallmann's repeated-move tests. An object that
straddles a seam makes a hole in both cells and the seam between them is relinked.

Motion adds four things a one-off edit doesn't have:

- **Rate.** Patching on every tick of continuous motion is a per-tick cost. The object patches when it has moved a set distance or stops, and between patches other movers avoid it by collision. The distance is tuned from a measurement.
- **Reachability.** A large mobile object is structural, so it is in the coarse form and each patch can open or close a gap. A whole-forest rebuild per patch is too much, so the first mobile obstacle is what forces reachability to be per cell with a portal graph on top (4.7).
- **Paths.** Each patch replans the movers whose remaining route crosses the hole, and only those.
- **Who is underneath.** A mover standing where the object arrives is inside a blocked face and takes the snap in 4.8.

Nothing in the game moves as a mesh obstacle today, so this is built with the first one, on
top of steps 6 and 7.

Rule 2 holds because cells are keyed by coordinate and level, and every change after a cell's
first derive is a patch. Two requesters of the same ground get the same cell.

### 4.5 Coverage and chunks

Coarse forms exist for every loaded chunk. The terrain part of a coarse cell depends only on
the chunk's terrain polygons, which are already built on the chunk's generation worker, so the
terrain-only coarse cells are built there too and arrive with the chunk. Cells that also hold
walls are re-derived when walls change.

`ChunkManager` gets anchors. The camera is one. Every active mover is one. A chunk inside any
anchor's keep range is never unloaded, and a chunk inside any anchor's load range is loaded.
That is the unloader enhancement in rule 9, and it fixes WOR-645 (Chunk streaming follows the
camera, so panning away unloads the colony's chunks). Placement runs for anchored chunks the
same as for camera chunks, because colonists harvest off camera.

So a mover always has coarse mesh under it and around it, and "off area, holding" stops
existing inside loaded ground. Two groups of movers in loaded areas that don't touch are not
joined by mesh in this epic; see 4.11.

### 4.6 Detail: where, when, how long

A cell is fine when a detail request covers it. The viewport rect, with today's margin, is the
first requester. Gameplay requesters (combat) are added one at a time as they exist, each
decided with Kevin; none is built in this epic.

Detail has two rects, a smaller one that turns it on and a larger one that keeps it, so a view
edge resting on a cell border doesn't toggle the cell. A fine form that leaves the keep rect
goes to a cache bounded by memory and is reused if its content version still matches, so
scrolling back costs nothing. A cached fine form is not routed on. Off camera, every mover
gets the same coarse answer regardless of where the camera was a minute ago.

### 4.7 Queries

- **Path.** Triangle A* over the routing forms, crossing seams by handle, then the funnel. The RRA* heuristic runs on the coarse forms, so one reverse search per goal still serves every agent and survives detail changes.
- **Reachability.** The existing width-aware forests (truth and terrain), built over the coarse forms of all covered cells. Flora is not in them. They change only when structural content changes: a chunk loads or unloads, a wall is built. Rebuild cost is measured in step 4; if it is over budget the forest is split per cell with a portal graph on top (the FFF-317 shape).
- **Soundness.** Coarse walkable ground contains fine walkable ground, so coarse-unreachable still means unreachable.
- **Flora never seals.** Coarse-reachable must mean reachable, so flora cannot be a hard wall in the fine form. Open question 1 in section 8.
- **Locate.** Cell from the coordinate, then the cell's own grid.
- **Widths.** Corridor width search is capped at a maximum clearance, far smaller than a cell. A patch invalidates widths in its cell and the eight around it.
- **Staleness.** A stored path records the version of each cell it crosses. A path replans only when one of those moves. The global generation counter and "any swap replans everyone" go away. RRA* caches key on coarse content only.
- **Validity.** `isValidPosition` stays the one predicate. A point is valid if it is on a walkable face of the routing form.
- **Placement.** `isAreaBuildable` reads the coarse forms. The separate cached terrain-only mesh is deleted.
- **Work targets.** The stand point beside a tree comes from the tree's collider, not from the mesh, so it is the same point on and off camera.

### 4.8 Snap forward

When a fine form appears, each mover in or routed through that cell is checked.

1. Its remaining route doesn't cross a blocked fine face and it stands on a walkable face: nothing happens. It keeps its route.
2. Its remaining route crosses a blocked face ahead: it replans from where it stands.
3. It stands inside a blocked face and has a route: it moves to the first point along its remaining route that is on a walkable face with its clearance, then replans from there. Points behind it are never candidates.
4. It stands inside a blocked face and has no route (idle, or working from inside a trunk): it moves to the nearest walkable point. There is no progress to protect.

The result is a function of the mover's position, its remaining waypoints, and the mesh.
Frame time and thread timing are not inputs.

When a fine form goes away nothing happens to anyone. A path planned around trees is still
walkable on coarse ground, so movers finish the route they have.

So toggling the view over a traveller can move it forward by at most an obstacle's length per
toggle and can never move it back or stop it. `StaticRectCollisionSystem` only pushes movers
out of flora in cells that are fine, and the snap runs before it in the same tick, so its
nearest-edge push can't undo a forward snap.

### 4.9 Threads and determinism

Which of two designs applies depends on a number step 1 produces.

- If a fine cell derives in well under a millisecond (*estimate*, plausible once the all-pairs loop is gone, since the rest of a 128 m build is 5 to 20 ms for 4 to 16 cells' worth), patches run on the main thread inside a fixed per-tick budget, in a fixed order. No futures, no landing-time variance.
- If not, fine cells build on the task pool and are applied at tick boundaries in request order.

Either way coarse forms, and so every large-scale answer, don't depend on worker timing beyond
when a chunk becomes ready, which rule 9 keeps away from movers.

The camera is real time and detail follows it, so exact paths and arrival times are not
reproducible across runs with different camera movement. Rule 6 is the bar: large-scale
outcomes are. Multiplayer would need more (each player's camera would have to be a simulation
input); that is out of scope and noted here so nobody is surprised.

`finishPendingBuilds` stays as the blocking settle for tests.

### 4.10 All movers

Coverage, routing, staleness, and the snap key on a mover, not a colonist. Today the route
logic (request, replan on staleness, deferred retry, off-mesh recovery) lives in
`AIDecisionSystem` and reads colonist tasks and memory. It moves to a mover-generic route
system that the colonist AI sets goals on. A wandering deer with no memory and no tasks is the
second client and the test that nothing colonist-shaped leaked in.

### 4.11 World scale

The nav mesh covers loaded ground. Beyond it, two things are planned and neither is this epic:

- The static geography mesh, unchanged from the pathfinding doc.
- A landmass check so nothing ever tries to path off an island: flood the planet's land hexes from the colony's hex once at load and keep the set. The single-hex-island test case falls out of it. At the quickstart resolution the planet has about 42 million hexes, so this is a bounded flood from the colony, not a table for the whole planet. It has no consumer until goals can lie outside loaded ground, so it is filed as a follow-up and built with that consumer.

## 5. What gets deleted

No fallbacks. When the cell store routes, these go in the same change:

- `SimulationRegion`, region clustering, recentering, the edge margin and size-change gates, `retiringBuilds`.
- `regionContaining` dispatch, the cross-region "unreachable" and off-area "hold" branches, `inSimArea`.
- The global `meshGeneration`, the global construction-version and removal-epoch rebuild triggers.
- `buildTerrainOnlyMesh` and the cached placement mesh.
- The per-build `extractWalls` over the whole construction world.

## 6. Performance plan

Rule 12. Every step has a headless A/B and a gate; the game is launched as few times as the
numbers allow.

**Baselines**

- Headless: `geometry-benchmarks`, section 2.1. Rerun on main for each step that touches `libs/geometry`.
- In game, one launch, needs Kevin's go-ahead: main's flora, quickstart, a scripted session (idle, working with one tree felled and one wall built, one slow pan, one zoom sweep), reading the existing `[NavBuild]` debug lines plus two additions (a cause on each build, and a water/flora/walls split of input time). It also dumps the build inputs, and from then on builder A/Bs replay those dumps headless.

**What is gated, per step, against main**

| Metric | Source | Gate |
|---|---|---|
| Builder time on the fixtures | benchmark | No row slower |
| Main-thread nav time per frame (input gather, patch apply, seam relink) | scoped timer, debug log | Not above main's input-gather time |
| Mesh work per minute of the scripted session (ms, and m² derived) | debug log | Below main |
| Ground derived more than once at the same level and content | debug counter | Zero from step 3 on |
| Path query cost (A* nodes expanded, ms) on fixed start and goal pairs | benchmark on a multi-cell fixture | Within noise of the single-mesh result |
| Path length on the same pairs | benchmark | Equal to the single-mesh result to the millimetre where cells are all fine |
| Frame time, three views | `scripts/perf-capture.ps1`, once, at the end on the parent branch | No gross regression |

**Launch budget:** one for the baseline, one after step 5 to confirm the saving off camera.
The final three-view capture runs once on the parent branch after the merge, with the rest of
that branch's regression pass.

## 7. Steps

Each is its own commit series, compiles, has targeted tests, and reports its numbers.

0. **Baseline.** The benchmark (done), the two debug-log additions, the one in-game capture.
1. **Arrangement broad phase.** Replace the all-pairs loop with a uniform grid over segment bounds. Output must be byte-identical to today's on every fixture, which the canonical arrangement makes checkable. Then profile again and take the next hot stage if one stands out (face classification is also rings times faces). No design change; this is a win for main on its own.
2. **Chunk anchors.** `ChunkManager` holds and loads chunks around active simulation. Test with a streaming chunk manager, which the nav tests lack today.
3. **Cell store.** The mesh becomes cells with seams and cross-cell queries, fine everywhere today's regions are, so behaviour is comparable. 3a is the geometry (cell container, seam links, A*, funnel, locate, and widths across cells, tested and benchmarked against the single-mesh result). 3b moves `NavigationSystem` onto it and deletes regions. Cell size is fixed here.
4. **Coarse forms and reachability.** Coarse forms for all loaded chunks, built with the chunk. Forests and RRA* move to coarse. Placement reads coarse. WOR-644 closes here: a colonist's reach no longer has an input the camera can touch.
5. **Detail by request.** Fine forms only under detail requests. The two-rect rule, the cache, per-cell path versions, the forward snap, the collision gate.
6. **Remove in place.** Felling a tree or removing a wall patches the triangles around it (4.4.1). Tested against a fresh derive at the face level on every fixture, with trees removed one at a time in several orders, including overlapping trunks and trunks on water edges and seams.
7. **Add in place.** A built wall or a new collider cuts and re-derives only its hole (4.4.2).
8. **Movers.** The route system comes out of `AIDecisionSystem`. A wandering deer uses it.

Until steps 6 and 7 land, a single-obstacle change derives its cell. Each of those steps
replaces that for its direction; nothing is left behind as a second path. Step 8 doesn't
depend on 3 to 7 and can move earlier.

## 8. Open questions

1. **Flora that seals.** In a fine cell, trees can close a gap or ring a spot that coarse reachability says is reachable. Options: the mover walks through the flora at a high path cost when there is no way around (visible, rare, and the same thing it would do off camera), or flora stays a hard wall and the answer differs on and off camera. Only the first satisfies rule 6. Confirm.
2. **Cell size.** 32 m or 64 m, from measurements. No input needed unless the numbers are close.
3. **Detail off camera around working colonists.** A colonist chopping off camera stands inside the trunk and is moved out if the view arrives. Acceptable under rule 5 as written. Confirm it doesn't meet the "affects gameplay" bar.
4. **Load range around a mover.** Enough chunks for its vision and goal range, likely the chunks within about 128 m. The memory cost per off-camera group gets measured in step 2.

## 9. Related documents

- [Pathfinding architecture](./pathfinding-architecture.md): the tier model, belief filtering, the geography mesh
- [Terrain polygons architecture](./organic-terrain/terrain-polygons-architecture.md): the rings cells are built from
- [Chunk management system](./chunk-management-system.md)
- Dev log 2026-06-24 (sim-area nav LOD) and 2026-06-28 (multi-region nav): why regions exist
