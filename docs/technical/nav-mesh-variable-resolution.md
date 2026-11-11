# Nav mesh: one mesh, variable resolution

**Status:** In progress. Go-ahead given 2026-11-10; each open question in section 8 is settled before the step that depends on it.
**Epic:** WOR-702 (Nav mesh: one mesh, variable resolution)
**Created:** 2026-11-10
**Replaces, once built:** the "As built: multi-region sim area" section of [pathfinding-architecture.md](./pathfinding-architecture.md), and its rule that agents are never mesh obstacles (risk 7)
**Fixes:** WOR-644 (Pathing can't route between two points on connected ground when nav regions overlap), WOR-645 (Chunk streaming follows the camera, so panning away unloads the colony's chunks)

Today the nav mesh is a handful of separate rectangles, each rebuilt whole when anything in it
changes. This document replaces them with one persistent mesh that is never regenerated. An
obstacle that appears, disappears, or moves patches the triangles around it and leaves the rest
alone. Detail is a matter of which obstacles block, and the camera decides nothing larger than
the shape of a path around a tree.

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
11. Movers have an impassable footprint, and it is in the mesh. They move, so the mesh is patched as they do.
12. The static geography mesh (Phase 2 in the pathfinding doc) is a separate thing and stays planned. It is not the nav mesh.
13. Nav is expected to be the most expensive thing the game computes. Every step is measured against main.

## 2. Current state

| Area | Today | After |
|---|---|---|
| Storage | A list of rectangles: a 60 m square per colonist, one for the viewport, overlapping ones merged, re-squared, clamped to 128 m. Each has its own complete mesh | One persistent mesh over every chunk that has been loaded. Unloading a chunk lowers its resolution and keeps its mesh |
| Change | Any trigger rebuilds a whole rectangle. A wall edit or a felled tree anywhere rebuilds every rectangle (global counters) | The triangles around a changed obstacle are patched, nothing else |
| Repeated work | Built rectangles overlap; a recenter re-meshes mostly unchanged ground; the placement mesh is a third copy | An obstacle enters the mesh once and leaves it once |
| Connectivity | A path needs both ends in one rectangle. Another rectangle reads unreachable, no rectangle holds | Paths cross all meshed ground |
| Detail | Every tree, everywhere a rectangle exists | Flora enters the mesh the first time an area needs it, and blocks only where detail is on |
| Camera | Drives a rectangle like a colonist does, so it reshapes and can stall colonist coverage | Turns detail on and off. Nothing else |
| Movers | Colonists only, and not in the mesh | Anything with a position and a clearance, each with a footprint in the mesh |
| Chunks | Loaded 1 chunk around the camera, unloaded at 2 | Also held and loaded around active simulation |
| Threads | Input gathered on the main thread in the frame, mesh on one `std::async` thread per build | Patches on the main thread inside a per-tick budget; a chunk's first mesh on its generation worker |

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
constrained Delaunay triangulation is below the noise; on the tree-free input, where it can be
resolved, it is 0.08 ms for 160 vertices. Classification, corridor widths, the locate grid, and
the reachability forests together are 5 to 20 ms.

The cause is in the code, not inferred: `buildArrangement` tests every segment against every
other segment, and repeats the whole pass until nothing splits
(`libs/geometry/arrangement/Arrangement.cpp`, the `i`/`j` loops). Trees are over 99% of the
cost of a build, and nearly all of that is asking whether tree A's edges touch tree B's edges
for trees 100 m apart.

That decides the design. The expensive part of a build is finding which segments interact,
not triangulating. A persistent mesh already holds the answer: an obstacle being added finds
what it touches by walking the triangles under it. So a patch skips the cost a rebuild is
made of, and the quadratic loop only matters where a whole chunk's terrain is meshed for the
first time.

**After step 1** (the all-pairs loop replaced by a uniform grid; output byte-identical on the
three fixtures, the 2x2 tiling, and 340 random inputs). Median of 5, same machine:

| Input | Before | After |
|---|---|---|
| manyTreesDump | 152 ms | 11.7 ms |
| controlDump | 234 ms | 14.5 ms |
| realRings | 295 ms | 17.3 ms |
| controlDump, trees removed | 1.7 ms | 0.77 ms |
| realRings, trees removed | 12.6 ms | 3.0 ms |
| Tiled 256 m (2,556 trees) | 3.3 s | 67 ms |
| Tiled 384 m (5,751 trees) | 17.6 s | 191 ms |
| Tiled 512 m (10,224 trees) | 56 s | 441 ms |
| `buildArrangement` alone, controlDump | 211 to 229 ms | 1.9 ms |

A second run with the machine busier read about a third higher on every row. Arrangement is
now about 2 ms of a 14.5 ms build and the triangulation about 2 ms; the other 10 ms is
classification, widths, the locate grid, and the forests. Area still scales worse than linear
(16 times the input takes 30 times as long), and that is in those later stages. It gets
profiled before step 4 relies on meshing a whole chunk at once.

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
| Recast/Detour tile cache, Unreal, Unity, A* Pathfinding Project `NavmeshCut` ([Granberg](https://arongranberg.com/2013/08/navmesh-cutting/)) | Every shipping engine patches at tile granularity, and none documents a smaller patch. They rasterize to voxels, so a tile is the smallest thing they can re-derive | We depart from this. Our mesh is exact polygons in a triangulation, where the smallest unit is the hole around one obstacle |
| Unity `NavMeshObstacle` carving (its Move Threshold, Time To Stationary, and Carve Only Stationary settings; from memory, not re-read for this document) | A moving obstacle carves the mesh only after it has moved past a threshold or been still for a set time; in between, agents avoid it by steering | The same rule for mover footprints (4.3.3) |
| Unreal navigation invokers ([docs](https://dev.epicgames.com/documentation/unreal-engine/using-navigation-invokers-in-unreal-engine)) | Detail exists only around agents, with a generation radius and a larger removal radius | Two rects for detail, so a view edge doesn't thrash |
| Unreal per-area navmesh resolution ([docs](https://dev.epicgames.com/documentation/en-us/unreal-engine/navigation-mesh-resolutions-user-guide)) | Resolution there is voxel size, and low resolution can delete real paths | Not a precedent. Our detail is which obstacle classes block, never geometric precision |
| Godot region connection ([docs](https://docs.godotengine.org/en/4.2/tutorials/navigation/navigation_connecting_navmesh.html)) | Regions join cleanly only when edge vertices coincide exactly; margin heuristics fail on partial overlaps | Chunk borders are exact shared integer vertices in one mesh |
| Factorio FFF-317 ([post](https://cf-www.factorio.com/blog/post/fff-317)) | The reachability abstraction ignores entities, so placing buildings never touches it; only terrain changes do | Reachability is computed from structural obstacles only (4.2) |
| Kallmann, dynamic CDT and Local Clearance Triangulations ([TOG 2014](http://graphics.ucmerced.edu/papers/14-tog-lct.pdf)) | Local insert and remove with a list of obstacle ids per constrained edge. Floating point with an epsilon. Repeated small moves pile up stray points unless removal is done fully | The patch model, with our exact integer predicates. A move re-derives its hole from rings, which removes strays |
| Demyen and Buro ([AAAI 2006](https://cdn.aaai.org/AAAI/2006/AAAI06-148.pdf)); Kallmann on the same measure | A corridor width depends on the nearest obstacle in a wedge, which can sit outside the patched triangles. Kallmann reports the measure is wrong in some CDT cases | Width search is capped at a maximum clearance, and a patch invalidates widths within that distance of its hole |
| jdxdev RTS series ([part 2](https://www.jdxdev.com/blog/2021/07/06/rts-pathfinding-2-dynamic-navmesh-with-constrained-delaunay-triangles/), [part 3](https://www.jdxdev.com/blog/2021/12/07/rts-pathfinding-3-variable-agent-size-smoke-tests-navmesh-fixes/)) | A hobby project. Cuts out the triangles an obstacle touches and retriangulates. Updating subsets took longer to get right than the first mesh; overlapping obstacles were the trap | The cut-and-retriangulate shape. Overlaps go through the existing arrangement, on the hole only |
| CGAL, snap rounding ([docs](https://doc.cgal.org/latest/Snap_rounding_2/index.html)) | Exact predicates don't cover constraint crossings; a snapped intersection can land on the wrong side of a nearby edge | Crossing points are made only by the arrangement, which already iterates to a fixpoint on the integer grid |
| Shewchuk and Brown; Devillers | Segment insertion costs time linear in the edges it crosses. Vertex deletion is local to the triangles around the vertex | The two primitives a patch is built from |
| Dynamically Pruned A* ([abstract](https://webspace.science.uu.nl/~gerae101/pdf/DPAStar-Abstract-ICTOPEN-2016.pdf)), Detour `dtPathCorridor::isValid` | A path that doesn't cross the changed region stays valid | A patch replans only the movers whose remaining route crosses its hole |
| Holm, de Lichtenberg, Thorup, dynamic connectivity | Theory only. No incremental Kruskal reconstruction tree exists in the navmesh literature | Reachability lives where it rarely changes (4.2) |
| S.T.A.L.K.E.R. A-Life ([interview](https://www.gamedeveloper.com/game-platforms/interview-inside-the-ai-of-i-s-t-a-l-k-e-r-i-)) | Offline agents route on a coarse graph, online ones on detail. Going online needs a valid nav vertex and has shipped stuck-in-transition bugs | The shape of the idea. The switch rule is ours |
| Minecraft chunk tickets ([wiki](https://minecraft.wiki/w/Chunk)) | Chunks stay simulating while something holds a ticket on them | Chunk anchors |

No source documents what happens to a mover's position or route at a detail switch, and none
has a forward-only rule. Section 4.7 is new.

## 4. Design

### 4.1 One mesh

The nav mesh is a single triangulation of every chunk that has been loaded. It is created once and lives as
long as the game does. Triangles and vertices have stable ids; a patch frees some and allocates
others, and every id it didn't touch still means what it meant.

Each constrained edge carries the list of obstacle rings that put it there. The arrangement
already produces that list; today's mesh keeps one id per edge and throws the rest away. The
list is what makes removal safe, and it makes the mesh its own record of what is in it.

**Why there are no nav cells.** An earlier draft of this document stored the mesh in fixed
32 m or 64 m cells and re-derived a cell on any change. With patches there is nothing left for
them to do. A patch is already smaller than any cell. An obstacle enters the mesh once, so
there is nothing to key or cache. And a grid of cell borders would only add triangles, plus a
seam problem where a detailed cell meets a plain one.

**The chunk is the one partition, and it already exists.** Ground arrives a chunk at a time,
so each chunk's square is a ring of constrained edges in the mesh. Vertices on a border
between two chunks are shared, exactly, because it is one mesh, and paths and patches cross it
like any other edge. In that narrow sense the chunk is a cell: it is the unit ground arrives
in, the unit its first mesh is built in, and the unit memory is reclaimed in. It is never the
unit a change is applied in.

The borders do one more job. The triangulation's in-circle test is exact only while the points
involved are within about 2^30 mm of each other, and a planet is bigger than that. With chunk
borders in the mesh no triangle is larger than a chunk, so every test is local however far
apart two meshed areas are.

**Unloading a chunk lowers its resolution; it does not remove its mesh.** A chunk that unloads
gives back its tiles and its placed entities, so its flora and footprints come out of the
mesh. Its structural triangles stay: shoreline, walls, doors. The mesh therefore covers every
chunk that has ever been loaded, at full resolution where something is simulating or looking
and at structural resolution everywhere else, and it stays one connected thing as the camera
and the colony move apart. A chunk that reloads does not get its structural mesh built again.

Structural triangles are reclaimed only under a memory budget, starting with chunks that hold
no built structures and were loaded longest ago. Open ground costs two triangles a chunk, so
the budget is about chunks with a lot of shoreline; what they cost is measured in step 4.
Ground that has never been loaded has no triangles.

### 4.2 Obstacle classes and the structural layer

Every obstacle belongs to a class.

- **Structural:** terrain polygons (water), walls, doors, and what joins them later: fences, barriers, large mobile objects.
- **Detail:** flora colliders, mover footprints, and other small things.

Structural obstacles always block. Detail obstacles block only where detail is on (4.4).

Reachability has to be something the camera cannot touch, and something a walking colonist's
footprint cannot touch either. So each chunk also keeps its **structural layer**: the
triangulation of its structural obstacles alone, built once with the chunk and patched only
when a wall, door, or other structural thing changes. The width-aware reachability forests and
the RRA* heuristic are built on it. It is small: open ground is two triangles a chunk, and the
measured structural-only cost of a 128 m region with river is 1.7 to 12.6 ms even with today's
quadratic loop.

This is a second, much smaller triangulation of the same ground, and it is there on purpose.
The alternative is one triangulation with reachability repaired on every tree and footprint
patch, and the literature has no incremental form of the structure we use for that. Where a
chunk has no detail in it yet, the nav mesh's triangles for that chunk and its structural layer
are the same data.

Walkable ground on the structural layer contains walkable ground on the nav mesh, so
structural-unreachable still means unreachable.

### 4.3 Patches

Every change after a chunk's first mesh is one of three operations. None of them regenerates
anything outside its hole.

| Event | Operation |
|---|---|
| A tree is felled, a node depleted, a wall or door removed | Remove (4.3.1) |
| A wall or door is built, flora first enters an area, flora gains a collider | Add (4.3.2) |
| A mover's footprint or a large object moves | Move (4.3.3) |
| A chunk loads | Its square and terrain rings are added; on its generation worker for the structural layer |
| A chunk unloads | Its detail obstacles are removed (4.3.1). Its structural triangles stay |
| The mesh passes its memory budget | The oldest unloaded chunks without structures lose their triangles |

A structural change applies the same operation to the structural layer.

#### 4.3.1 Remove

A tree's base is a ring, usually a rect. Its vertices are mesh vertices and its edges are
constrained triangle edges. Felling it touches the triangles around those vertices and nothing
else.

1. The tree's id comes off the ring list of each of its edges. An edge whose list is now empty stops being a constraint. An edge another ring also covers stays one.
2. A vertex with no constrained edge left, and not on a chunk border, is deleted: the triangles around it come out and the hole is retriangulated (Delaunay vertex deletion).
3. Freed edges are legalized with flips, so the result is still constrained Delaunay.
4. The new triangles are tagged from the faces around the hole and the ring lists on the edges between. Normally they join the floor; where another ring still covers the spot they keep its tag.
5. Corridor widths within the maximum clearance of the hole are invalidated, since a width can depend on an obstacle outside the triangles that changed. The locate index over the hole is refreshed.

Nothing global moves. Reachability reads the structural layer, which a tree is not in. No
stored path can be broken by ground opening up; a path that would now be shorter is found at
its next replan.

Removal makes no new points. Every vertex involved already exists on the integer grid, so every
decision is one of the exact predicates the builder uses today.

Three cases need the ring lists and not the picture of an isolated rect:

- **Shared edges and vertices.** Two overlapping trunks, a trunk against a waterline, a trunk on a chunk border. The list keeps the neighbour's edge when one of them goes.
- **Crossing points.** Where the tree's edge crossed another ring, the arrangement split both at a rounded point. That vertex stays on the other ring after the tree is gone. Taking it out would move the other ring's edge by up to a millimetre and reopen the rounding question. The next add or move whose hole covers it tidies it.
- **Co-circular points.** The four corners of a rect lie on one circle, so Delaunay has a free choice of diagonal there. The mesh's triangles therefore depend on the order of edits. Its walkable ground, face tags, and reachability do not.

#### 4.3.2 Add

Adding is the harder direction, because a new ring can cross edges that are already there, and
a crossing point has to be rounded to the grid.

1. Walk the triangles under the new ring. That finds every constrained edge it crosses or touches. This walk is the local replacement for the all-pairs loop.
2. The hole is those triangles plus the triangles under any ring it crosses, bounded by a closed loop of existing edges. The boundary is held fixed.
3. The constrained edges inside the hole, with their ring lists, and the new ring go through the same arrangement and triangulation a chunk's first mesh does. The result is spliced in, and its triangles are tagged as in removal.
4. Widths near the hole are invalidated and the locate index refreshed.
5. Movers whose remaining route crosses the hole replan (4.6).

For a tree that touches nothing the hole is the few triangles its rect overlaps, and the
arrangement has four segments in it. From the measured triangulation cost, a tree is tens of
microseconds (*estimate*, confirmed or not in step 3).

Flora entering an area for the first time is many adds. Adds whose holes overlap merge into
one hole, and the batch is spread over ticks inside the patch budget, nearest the view centre
first.

#### 4.3.3 Move

A move is an add and a remove with one hole covering both footprints.

1. Find the triangles the old footprint and the new footprint intersect, plus those under any ring either one crosses.
2. Take the closed loop of existing edges around that set as the boundary and hold it fixed.
3. Re-derive only the inside, with the object at its new position, and splice it back.

Because the inside is derived from the rings and not from the old triangles, a move cleans up
after itself. Crossing points the object made at its old position are gone, so nothing piles up
along its track.

**Mover footprints.** Every mover has a footprint ring, sized from its clearance. It is a
detail-class obstacle that blocks every mover but its owner: the path query already filters
faces per agent for belief, and "this face is my own footprint" is one more case of that
filter. It is not in the structural layer, so someone standing in a doorway never changes what
is reachable, only what is passable right now.

Motion adds what a one-off edit doesn't have:

- **Rate.** A footprint is patched when its mover has moved a set distance or come to rest, not every tick. Between patches, movers keep apart by the collision systems that already run. The distance is tuned from a measurement; with dozens of movers this is the patch that runs most.
- **Paths.** Each patch replans the movers whose remaining route crosses the hole, and only those.
- **Who is underneath.** A mover standing where a large object arrives is inside a blocked face and takes the snap in 4.7. Two movers' footprints don't overlap, because collision keeps them apart.
- **Large mobile objects** are structural, so their moves also patch the structural layer, and each can open or close a gap. The first one in the game is what forces the reachability forests to be repaired per chunk and not rebuilt whole (4.6).

This reverses risk 7 in the pathfinding doc ("dynamic agents are never navmesh obstacles").
That rule came from StarCraft 2, which by secondhand accounts resets its mesh and reinserts
every obstacle on a change, so a unit in the mesh would mean a rebuild per step. With a patch
that costs what a move costs, the reason for it is gone. Steering and
crowd behaviour (P5 there) still stand, and still do the work between patches.

### 4.4 Detail is a filter

Detail obstacles are in the mesh as tagged faces, the way wall interiors already are. Whether
one blocks is decided by the path query, per area: where detail is on, flora and footprints
block; where it is off, movers go straight through them. Turning detail on or off for an area
flips flags. It does no mesh work at all.

- **Entering the mesh.** Flora is added to an area the first time detail is wanted there (4.3.2), and then stays. Scrolling away and back costs nothing.
- **Leaving the mesh.** Flora is removed from ground that has had no detail for a long time, oldest first, when the mesh passes a memory budget. That is the only way the camera's history shows up, and it changes cost, never answers.
- **Who asks for detail.** The viewport rect, with today's margin, is the first requester. Gameplay requesters (combat) are added one at a time as they exist, each decided with Kevin; none is built in this epic.
- **Two rects.** A smaller one turns detail on and a larger one keeps it, so a view edge resting on a boundary doesn't flip it back and forth.
- **Pending.** Between a request and the adds finishing, detail in that area is still off. The view margin exists so that happens off screen.

Off camera, in ground that was once viewed, a path search crosses more triangles than it would
on bare terrain, all of them passable. That is a query cost to measure in step 5, not a
behaviour difference.

### 4.5 Coverage and chunks

The mesh covers every chunk that has been loaded and not reclaimed (4.1). A route can be
planned across unloaded ground that is still meshed; the chunks along it load as the mover's
anchor reaches them. A chunk's structural layer depends only on its terrain
polygons, which are already built on the chunk's generation worker, so it is built there too
and arrives with the chunk. This is the one place a whole chunk's rings go through the
arrangement at once, which is why the quadratic loop still has to go (step 1).

`ChunkManager` gets anchors. The camera is one. Every active mover is one. A chunk inside any
anchor's keep range is never unloaded, and a chunk inside any anchor's load range is loaded.
That is the unloader enhancement in rule 9, and it fixes WOR-645 (Chunk streaming follows the
camera, so panning away unloads the colony's chunks). Placement runs for anchored chunks the
same as for camera chunks, because colonists harvest off camera.

So a mover always has mesh under it and around it, and "off area, holding" stops existing
inside meshed ground. Two groups of movers are joined by mesh when the ground between them has
been loaded at some point. Where it never has, they are not joined in this epic; see 4.10.

### 4.6 Queries

- **Path.** Triangle A* over the nav mesh, then the funnel, with the per-agent filter deciding which tagged faces block: belief for walls, the detail flag for flora and footprints, ownership for the agent's own footprint.
- **Reachability.** The existing width-aware forests (truth and terrain), on the structural layer of all meshed chunks. They change when a chunk is first meshed or reclaimed, or a structural obstacle changes. Rebuild cost is measured in step 4. If it is over budget, or once a large mobile object exists, the forests are kept per chunk with a portal graph across chunk borders (the FFF-317 shape), so a change repairs one chunk.
- **Heuristic.** RRA* runs on the structural layer, so one reverse search per goal still serves every agent and survives every detail patch.
- **Detail that seals.** Structural-reachable must mean reachable, so detail cannot be a hard wall where there is no way around. Open question 1 in section 8.
- **Locate.** A spatial index over triangles, updated by each patch for its hole.
- **Widths.** Capped at a maximum clearance and invalidated within that distance of a patch.
- **Staleness.** A patch that adds blocking ground tells the movers whose remaining route crosses its hole to replan. A patch that only opens ground tells nobody. The global generation counter and "any swap replans everyone" go away.
- **Validity.** `isValidPosition` stays the one predicate: a point is valid if it is on a walkable face for the asking agent.
- **Placement.** `isAreaBuildable` reads the structural layer. The separate cached terrain-only mesh is deleted.
- **Work targets.** The stand point beside a tree comes from the tree's collider, not from the mesh, so it is the same point on and off camera.

### 4.7 Snap forward

When detail turns on for an area, or a blocking obstacle is added, each mover in or routed
through it is checked.

1. Its remaining route doesn't cross a blocked face and it stands on a walkable face: nothing happens. It keeps its route.
2. Its remaining route crosses a blocked face ahead: it replans from where it stands.
3. It stands inside a blocked face and has a route: it moves to the first point along its remaining route that is on a walkable face with its clearance, then replans from there. Points behind it are never candidates.
4. It stands inside a blocked face and has no route (idle, or working from inside a trunk): it moves to the nearest walkable point. There is no progress to protect.

The result is a function of the mover's position, its remaining waypoints, and the mesh. Frame
time and thread timing are not inputs.

When detail turns off nothing happens to anyone. A path planned around trees is still walkable
with the trees ignored, so movers finish the route they have.

So toggling the view over a traveller can move it forward by at most an obstacle's length per
toggle and can never move it back or stop it. `StaticRectCollisionSystem` only pushes movers
out of flora where detail is on, and the snap runs before it in the same tick, so its
nearest-edge push can't undo a forward snap.

### 4.8 Threads, determinism, and correctness

Patches run on the main thread, in a fixed order, inside a per-tick time budget. There are no
nav futures and no landing-time variance: a patch requested on a tick is applied on that tick
or queued in order behind the budget. The only nav work on another thread is a chunk's first
structural mesh, on the chunk's own generation worker, and rule 9 keeps chunk arrival away from
movers. Whether the budget is comfortable is the first thing step 3 measures.

The camera is real time and detail follows it, so exact paths and arrival times are not
reproducible across runs with different camera movement. Rule 6 is the bar: large-scale
outcomes are. Multiplayer would need more (each player's camera would have to be a simulation
input); that is out of scope and noted so nobody is surprised.

A mesh that is never rebuilt has nothing to wash a bad patch out, so correctness is carried by
three things:

- **The oracle.** For any sequence of adds, removes, and moves, the mesh's constraints and tagged faces equal those of a mesh built from scratch from the same obstacles. Tested on every real fixture with obstacles added and removed one at a time in several orders, including overlapping trunks, trunks on waterlines, and trunks on chunk borders. It is a face-level comparison because of the co-circular diagonals.
- **The validator.** In development builds every patch is followed by a check of the hole and its ring of neighbours: adjacency symmetric, orientation positive, every constrained edge still present, tags consistent across unconstrained edges.
- **Exact arithmetic throughout.** No epsilons enter. The one place new coordinates are made is the arrangement, which is the code that already handles it.

`finishPendingBuilds` stays as the blocking settle for tests; it drains the patch queue and
waits on chunk workers.

### 4.9 All movers

Coverage, routing, staleness, footprints, and the snap key on a mover, not a colonist. Today
the route logic (request, replan on staleness, deferred retry, off-mesh recovery) lives in
`AIDecisionSystem` and reads colonist tasks and memory. It moves to a mover-generic route
system that the colonist AI sets goals on. A wandering deer with no memory and no tasks is the
second client and the test that nothing colonist-shaped leaked in.

### 4.10 World scale

The nav mesh covers ground that has been loaded. Beyond it, two things are planned and neither is this epic:

- The static geography mesh, unchanged from the pathfinding doc.
- A landmass check so nothing ever tries to path off an island: flood the planet's land hexes from the colony's hex once at load and keep the set. The single-hex-island test case falls out of it. At the quickstart resolution the planet has about 42 million hexes, so this is a bounded flood from the colony, not a table for the whole planet. It has no consumer until goals can lie outside loaded ground, so it is filed as a follow-up and built with that consumer.

## 5. What gets deleted

No fallbacks. When the one mesh routes, these go in the same change:

- `SimulationRegion`, region clustering, recentering, the edge margin and size-change gates, `retiringBuilds`, the per-region `std::async` builds.
- `regionContaining` dispatch, the cross-region "unreachable" and off-area "hold" branches, `inSimArea`.
- The global `meshGeneration`, the global construction-version and removal-epoch rebuild triggers.
- `buildTerrainOnlyMesh` and the cached placement mesh.
- The per-build `extractWalls` over the whole construction world, and `nav::buildInput`'s area sweep.

## 6. Performance plan

Rule 13. Every step has a headless A/B and a gate; the game is launched as few times as the
numbers allow.

**Baselines**

- Headless: `geometry-benchmarks`, section 2.1. Rerun on main for each step that touches `libs/geometry`.
- In game, one launch, needs Kevin's go-ahead: main's flora, quickstart, a scripted session (idle, working with one tree felled and one wall built, one slow pan, one zoom sweep), reading the existing `[NavBuild]` debug lines plus two additions (a cause on each build, and a water/flora/walls split of input time). It also dumps the build inputs, and from then on mesh A/Bs replay those dumps headless.

**What is gated, per step, against main**

| Metric | Source | Gate |
|---|---|---|
| Builder time on the fixtures | benchmark | No row slower |
| One add, one remove, one move, on each fixture at full tree density | benchmark | Reported in step 3; no later step slower |
| Detail entering a 128 m area (all its trees added) | benchmark | Below main's full build of the same area |
| Main-thread nav time per frame (patches, replans they cause) | scoped timer, debug log | Not above main's input-gather time, and inside the tick budget |
| Mesh work per minute of the scripted session | debug log | Below main |
| Path query cost (A* nodes expanded, ms) on fixed start and goal pairs | benchmark | Within noise of today's single-region result |
| Path length on the same pairs | benchmark | Equal to today's result to the millimetre |
| Frame time, three views | `scripts/perf-capture.ps1`, once, at the end on the parent branch | No gross regression |

**Memory (estimate, from struct sizes and the measured triangle counts; measured in steps 4 and 5)**

A triangle on the mesh is about 200 bytes: the 136-byte `NavTriangle`, plus its share of
vertices, the locate index, and the per-edge ring lists. A structural-layer triangle is about
500 bytes, because the two reachability forests ride on it. Today every triangle pays the
larger figure.

| Ground | Triangles | Memory |
|---|---|---|
| Open chunk, no water | a handful | negligible |
| Chunk with a river through it | 1,200 to 4,500 (scaled from two measured 128 m regions) | 0.6 to 2.2 MB |
| Chunk full of shoreline | up to about 18,000 | up to about 9 MB |
| One tree | about 8 (measured) | about 1.6 KB |
| A 128 m forest view, 640 trees | about 5,000 (measured) | about 1 MB |
| A whole forest chunk, all viewed, 10,000 trees | about 87,000 (measured) | about 17 MB |

Ten loaded chunks, three with rivers, forest biome: about 10 MB when only the screen has
detail, about 40 MB once the player has looked over two chunks' worth of forest, about 180 MB
if every chunk is dense forest and all of it has been viewed. Tile data for the same ten
chunks is about 20 MB. Flora is nearly all of it, so the flora budget in section 4.4 is the
control: 64 MB holds about 40,000 trees. `NavTriangle` is mostly 64-bit fields holding a
sentinel, and packing it could halve the figure; measure before making that a step.

**Launch budget:** one for the baseline, one after step 5 to confirm the saving off camera,
one after step 7 for footprints under a crowd. The final three-view capture runs once on the
parent branch after the merge, with the rest of that branch's regression pass.

## 7. Steps

Each is its own commit series, compiles, has targeted tests, and reports its numbers. Steps 0
to 7 are tracked as WOR-703 to WOR-710, in order, under the epic.

0. **Baseline.** The benchmark (done), the two debug-log additions, the one in-game capture.
1. **Arrangement broad phase.** Replace the all-pairs loop with a uniform grid over segment bounds. Output must be byte-identical to today's on every fixture, which the canonical arrangement makes checkable. A win for main on its own, and what makes a chunk's first mesh affordable.
2. **Chunk anchors.** `ChunkManager` holds and loads chunks around active simulation. Test with a streaming chunk manager, which the nav tests lack today.
3. **The patchable mesh, in geometry.** Stable ids, per-edge ring lists, an updatable locate index, capped widths with local invalidation, and the three operations: remove, add, move. Headless only. The oracle tests, the validator, and the patch benchmarks land here. Nothing in the game uses it yet.
4. **One mesh in the game.** `NavigationSystem` holds the one mesh and the structural layers, built per chunk and patched from then on, with flora added where regions are today so behaviour is comparable. Reachability and RRA* move to the structural layer. Regions are deleted. WOR-644 closes here: a colonist's reach no longer has an input the camera can touch.
5. **Detail as a filter.** Flora enters on first need and blocks only where detail is on. The two rects, the memory budget, the forward snap, the collision gate.
6. **Movers.** The route system comes out of `AIDecisionSystem`. A wandering deer uses it.
7. **Footprints.** Every mover's footprint is in the mesh and moves with it. The rate rule, the own-footprint filter, and what a blocked mover does (open question 2).

Step 6 doesn't depend on 3 to 5 and can move earlier. Step 3 is where the risk is: it is the
part every source says takes longest, and it is deliberately headless and finished before the
game depends on it.

## 8. Open questions

1. **Flora that seals.** Trees can close a gap or ring a spot that the structural layer says is reachable. Options: the mover walks through the flora at a high path cost when there is no way around (visible, rare, and the same thing it would do off camera), or flora stays a hard wall and the answer differs on and off camera. Only the first satisfies rule 6. Confirm.
2. **A mover in the way.** A footprint is impassable, so a colonist standing in the only doorway blocks it. The goal stays reachable, because footprints are not in the structural layer. What does the blocked mover do: wait and retry when the footprint moves, or ask the blocker to step aside? Waiting is the simple answer and can deadlock two movers in a corridor; the pathfinding doc's door queues and avoidance are the longer one.
3. **Footprints off camera.** As detail-class obstacles, footprints don't block where detail is off, so off-camera movers pass through each other on the mesh while collision still separates them. Confirm, or say footprints block everywhere.
4. **Detail off camera around working colonists.** A colonist chopping off camera stands inside the trunk and is moved out if the view arrives. Acceptable under rule 5 as written. Confirm it doesn't meet the "affects gameplay" bar.
5. **Load range around a mover.** Enough chunks for its vision and goal range, likely the chunks within about 128 m. The memory cost per off-camera group gets measured in step 2.

## 9. Related documents

- [Pathfinding architecture](./pathfinding-architecture.md): the tier model, belief filtering, the geography mesh
- [Terrain polygons architecture](./organic-terrain/terrain-polygons-architecture.md): the rings the structural layer is built from
- [Chunk management system](./chunk-management-system.md)
- Dev log 2026-06-24 (sim-area nav LOD) and 2026-06-28 (multi-region nav): why regions exist
