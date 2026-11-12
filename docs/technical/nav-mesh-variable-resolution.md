# Nav mesh: one mesh, variable resolution

**Status:** In progress. Steps 1 to 5 built 2026-11-11 (section 10, entries 1 to 96); steps 6 and 7 not started. The design is section 1, rules 22 to 38. The code does not match it in four places, listed in section 1.1. Sections that describe what is built say so; where an entry in section 10 differs from the text above it, the entry is what was built.
**Epic:** WOR-702 (Nav mesh: one mesh, variable resolution)
**Created:** 2026-11-10
**Replaces, once built:** the "As built: multi-region sim area" section of [pathfinding-architecture.md](./pathfinding-architecture.md), and its rule that agents are never mesh obstacles (risk 7)
**Fixes:** WOR-644 (Pathing can't route between two points on connected ground when nav regions overlap), WOR-645 (Chunk streaming follows the camera, so panning away unloads the colony's chunks)

Before this epic the nav mesh was a handful of separate rectangles, each rebuilt whole when
anything in it changed. This document replaces them with one persistent mesh at full detail
that is never regenerated. An obstacle that appears, disappears, or moves patches the triangles
around it and leaves the rest alone. Movers generate the mesh, in full, within their sight
distance plus a buffer, and it only grows; the camera has no effect on it (section 1). The
title is the epic's name: the mesh has no resolution levels (rule 31). The code still keeps a
second, structural mesh, lets the camera decide detail, and removes mesh (section 1.1).

Numbers marked *measured* come from `geometry-benchmarks` on this branch (RelWithDebInfo, the
dev machine, 2026-11-10). Numbers marked *estimate* are mine and are gated on a measurement
named beside them.

---

## 1. Intent

Rules 22 to 37 are the design, settled 2026-11-11 (rules 31 to 38 later that day), and
everything below is judged against them. The earlier rules follow; each one a later rule
replaces is marked, and the rest still stand.

### The design (rules 22 to 38)

22. Only movers generate mesh. The camera has no effect on the mesh: it cannot reveal unexplored area, so it has no reason to bring ground or obstacles in.
23. A mover generates the full real mesh around it as it moves: ground, water, walls, doors, and every object movers cannot path through, of which there will be many. There is no lesser tier of mesh where nobody is looking. (How far around it is rule 32.)
24. The mesh is one entity, patched in place, never destroyed and regenerated. It is small, and it never unloads. (Rule 33 states the second half exactly: the mesh only grows. "Small" holds for ground; with every object movers cannot path through in it, the measured cost is in section 6.)
25. Nav is any place to any place, with no distance limit. There is no colony ground and no owned area.
26. A mover whose destination has no mesh heads in that direction and generates the mesh as it goes. Meeting an obstacle it cannot pass on the way is a future exploring task, WOR-775 (Exploring: a colonist blocked on the way to a remembered place looks for a way round), and is not needed for the current merge.
27. The meshed area is larger than the visible area. Some movers (approaching enemies) generate mesh without revealing anything, so the area the player sees is a subset of the meshed area. The player builds and interacts only on visible terrain, which a mover has seen and so meshed. "Has mesh" never stands for "the player can see or act here". Player visibility is WOR-754 (Player visibility: unexplored areas (visible, seen, unseen)), planned and not built. (Restated for placement by rule 35.)
28. All movers use the same movement system, from enemies to small animals.
29. Mesh, nav, and collision code is generic. It never names one kind of obstacle.
30. Rules 22 and 23 replace the earlier model, in which the camera decided detail, small placed obstacles were ignored off camera, and a forward snap handled detail arriving with the view. The snap rule itself stands wherever an obstacle lands on a mover: the mover moves forward along its route, and never stalls or loses progress.
31. The nav mesh is one mesh at full detail. There is no coarse or structural layer, no second mesh, and no resolution levels.
32. Each mover adds to the mesh everything within its own sight distance plus a buffer: ground, water, walls, doors, and every object movers cannot path through. The distance is per mover, taken from its sight distance; it is not a fixed constant.
33. The mesh only grows. Meshed ground never leaves it: a chunk unloading does nothing to the mesh, and there is no cap and no trim. The one thing that changes what is in it is a change in the world (an obstacle felled, built, or moved), which patches the mesh in place (rule 24).
34. The camera has no effect on the mesh. (Restates rule 22.)
35. Placement needs no rule of its own. The player can only place in the viewable area, which is smaller than the meshed area. (Restates rule 27 for placement.)
36. Open, and being scoped under WOR-799 (Nav: one full-detail mesh with no coarse layer): how reach and "is there a way" are answered on one mesh; what the search budgets become; how large the buffer of rule 32 is; how a route is planned to a goal beyond meshed ground (rule 26 says the mover heads that way and meshes as it goes, not how the route is planned).
37. Since the mesh only grows, its cost per object movers cannot path through is part of bringing the code to the design (section 6). Paging unused parts of a very large mesh out to disk is a possible later follow-up and not part of the design: WOR-800 (Nav: page unused parts of a very large mesh out to disk (later, maybe)). Mesh paged out would come back unchanged.
38. The geography mesh (also called the regional mesh), planned separately in the pathfinding doc, is a separate mesh from the nav mesh, here called the real mesh where the two are contrasted. It is planned and not built. It is much less detailed than the real mesh. The real mesh is a detail view of the geography mesh, as game tiles are a detail view of world tiles. The two must be aligned. One is derived from the other and then filled in. Which is derived from which in implementation terms, what alignment requires of vertices or edges, and what the geography mesh answers are not settled. It is not the coarse layer that rule 31 removes.

### Earlier rules

The first rules, 2026-11-10:

1. One connected nav mesh with varying resolution. Not island meshes. (The varying resolution is replaced by rule 31: one mesh at full detail. One connected mesh stands.)
2. If the mesh for a piece of ground is computed, it is never computed twice.
3. The mesh is an entity that gets modified. A change patches it; nothing destroys and recreates it.
4. Terrain and large dynamic things are always in: walls, doors, later fences, barriers, large mobile objects. (Widened by rule 23 to every object movers cannot path through.)
5. Small things (trees) are detail. Where nobody is looking, movers walk straight through them. (Replaced by rules 22 and 23.)
6. The camera decides detail. It never decides reachability, what a mover chooses, or whether it acts. (Replaced by rule 22: the camera decides nothing about the mesh.)
7. Things off camera can force detail when imprecision would change gameplay. Combat meets that bar. The list is decided case by case. (Replaced by rules 22 and 23: there is no off-camera tier to force detail into.)
8. When detail appears under a mover, the mover snaps forward along its route, never backward. Scrolling the view on and off a traveller can't stall or reverse it. A short forward teleport through a tree is acceptable. (Its trigger, the view, is replaced by rule 30; the forward rule stands.)
9. A chunk with active simulation in it is never unloaded. A mover walking into an ungenerated chunk generates it. That is expected to be rare. (A chunk that does unload does nothing to the mesh: rule 33.)
10. Everything that moves uses the same movement system.
11. Movers have an impassable footprint, and it is in the mesh. They move, so the mesh is patched as they do.
12. The static geography mesh (Phase 2 in the pathfinding doc) is a separate thing and stays planned. It is not the nav mesh. (Restated and extended by rule 38.)
13. Nav is expected to be the most expensive thing the game computes. Every step is measured against main.

The rulings of the morning of 2026-11-11, which narrowed the rules above (section 10, entries 79 to 89):

14. Nav's job is to build a path to given coordinates: any place to any place, with no distance limit. Nav does not know who asks or why.
15. There is no colony ground and no owned area.
16. Mesh is made for movers (under them, along their routes, at their goals) and under the camera (for placement validity and other player interaction). It is small, and mesh a mover has caused is never unloaded. (Replaced by rules 22, 23, 24 and 27, and by 32 and 33: only movers make mesh, each within its sight distance plus a buffer, the player acts only on ground a mover has seen and so meshed, and the mesh only grows.)
17. Mesh existing is not ground being visible. Some movers mesh ground without revealing it, so "has mesh" never stands for "the player can see or act here". Player visibility (unexplored areas hidden from the player, revealed only by what colonists see) is planned and not built: WOR-754 (Player visibility: unexplored areas (visible, seen, unseen)). (Restated by rule 27.)
18. Compute sets limits, and some places cannot be reached, but running out of search budget means "didn't finish", never "can't reach". Budgets come from measurements. (What the budgets become on one mesh is open: rule 36.)
19. A far route may stall the main thread for 19 to 34 ms once per far target, for now. Moving far requests to a worker is WOR-789 (Far route requests stall the main thread for 19 to 34 ms; run them on a worker). (The far route this covers is the one built on the structural mesh, which rule 31 removes; see 1.1.)
20. Nav and mesh code never names trees or flora. Obstacle kinds are data: "placed obstacle" (a placed entity's collider) and "detail" (as built, the class the view brings in and the search prices; see 1.1). The "After" column in section 2 and sections 4.1, 4.2, 4.4 to 4.6, 4.8 and 4.10, rewritten on 2026-11-11, follow the same rule; older text, the measurements, and log entries 1 to 78 still say "tree" and "flora". (Restated by rule 29.)
21. Travelling toward a goal whose ground is not meshed is deferred: WOR-776 (Nav: build a path toward a goal whose ground is not meshed yet). (The design is rule 26, which that ticket builds.)

### 1.1 Where the code does not match the design yet

As built on this branch, four things depart from rules 22 to 38. Every section below marked
"as built" describes one of them.

- **The camera shapes the mesh.** WOR-796 (Nav: the camera must have no effect on the mesh). The camera loads chunks (3 by 3 around it in the game, kept to 5 by 5), and every loaded chunk's ground is meshed, after the ground anchors hold. The viewport rect, with its margin, is the only requester of placed obstacles. Collision with placed obstacles (`StaticRectCollisionSystem`) and the forward snap (`snapMovers`) act only where the view has turned detail on (`detailOnAt`). Exact paths depend on where the camera has been (4.8).
- **Movers mesh only structure, over a fixed range.** WOR-797 (Nav: movers generate the full real mesh around them, placed obstacles included). An anchor brings in ground, water, walls, and doors for the chunks within a fixed 128 m of it, the same for every mover; rule 32 makes the area each mover's sight distance plus a buffer. Away from the view a mover's mesh holds no placed obstacles unless the view brought them in earlier, so it walks straight through them.
- **Mesh is removed.** Also WOR-797, where rule 33 says nothing leaves the mesh. Three things remove it as built: ground only the camera held is shed past `kCameraOnlyUnloadedChunks` (16) unloaded chunks; a chunk's placed obstacles leave the nav mesh when the chunk unloads; and past the `kDetailRingBudget` cap (40,000 placed obstacles) whole cells are trimmed.
- **Nothing a mover cannot path through blocks.** WOR-798 (Nav: objects movers cannot path through block in the mesh instead of being priced). A placed obstacle is priced at `kDetailPassMultiplier` (each metre through it costs 50 metres of open ground) and is never impassable, so a goal ringed by them is reached by walking through.
- **There are two meshes.** WOR-799 (Nav: one full-detail mesh with no coarse layer). Beside the nav mesh is a structural mesh of ground, water, walls, and doors only (4.2). Built on it: reach (`walk()`), the reach forests, the goal-side probe, the far route taken from the structural walk, the near search's cap (scaled by the structural walk's steps), the structural epoch and the reach and gave-up stamps keyed on structural content, placement buildability (`isAreaBuildable`) and the landing search, and the 2,000 / 5,000 / 60,000 step budgets (`kReachProbeExpansions`, `kReachExpansionCap`, `kFarRouteExpansions`). Rule 31 removes the structural mesh; what replaces reach and the budgets on one mesh is open (rule 36).

Bringing the cost of a placed obstacle in the mesh down is part of the correction, since the mesh
only grows (rule 37, section 6).

Symptoms and follow-ups already filed: WOR-795 (A colonist on a far route walks straight through
placed obstacles on screen); WOR-794 (A colonist with a critical need and a known fix is left
idle when the route search to it gives up); WOR-791 (Nav: a goal on meshed ground cut off from
the start by unmeshed ground reads as a definite no); WOR-789 (Far route requests stall the main
thread for 19 to 34 ms; run them on a worker); WOR-776 (Nav: build a path toward a goal whose
ground is not meshed yet); WOR-775 (Exploring: a colonist blocked on the way to a remembered
place looks for a way round); WOR-754 (Player visibility: unexplored areas (visible, seen,
unseen)).

## 2. Current state

| Area | Today | After |
|---|---|---|
| Storage | A list of rectangles: a 60 m square per colonist, one for the viewport, overlapping ones merged, re-squared, clamped to 128 m. Each has its own complete mesh | One persistent mesh at full detail over the ground movers have meshed, each within its sight distance plus a buffer; it only grows (rules 31 to 33). As built: a structural mesh beside it (WOR-799), both over the chunks movers and the camera have loaded; unloading a chunk drops its placed obstacles and keeps its ground, and ground the camera alone held is shed past 16 unloaded chunks (1.1) |
| Change | Any trigger rebuilds a whole rectangle. A wall edit or a felled tree anywhere rebuilds every rectangle (global counters) | The triangles around a changed obstacle are patched, nothing else |
| Repeated work | Built rectangles overlap; a recenter re-meshes mostly unchanged ground; the placement mesh is a third copy | An obstacle enters the mesh once and leaves it once |
| Connectivity | A path needs both ends in one rectangle. Another rectangle reads unreachable, no rectangle holds | Paths cross all meshed ground |
| Detail | Every tree, everywhere a rectangle exists | Every object movers cannot path through within a mover's sight distance plus a buffer enters the mesh, blocks, and stays (rules 23, 32 and 33). As built: placed obstacles enter the nav mesh under the view and stay until their chunk unloads or a cap trims them; they are priced, not walled, wherever they are, and detail being on decides only collision and the forward snap (1.1) |
| Camera | Drives a rectangle like a colonist does, so it reshapes and can stall colonist coverage | No effect on the mesh (rule 22). As built: turns detail on and off, and brings ground into the mesh; routes and reach use that ground like any other (4.5 and 1.1) |
| Movers | Colonists only, and not in the mesh | Anything with a position and a clearance, each with a footprint in the mesh |
| Chunks | Loaded 1 chunk around the camera, unloaded at 2 | Also held and loaded around active simulation |
| Threads | Input gathered on the main thread in the frame, mesh on one `std::async` thread per build | Edits gathered on the main thread under per-tick count budgets, applied on worker threads after every system has run, and joined at the next update or the first nav query. As built: two workers, one per mesh (WOR-799) |

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
| Unreal navigation invokers ([docs](https://dev.epicgames.com/documentation/unreal-engine/using-navigation-invokers-in-unreal-engine)) | Detail exists only around agents, with a generation radius and a larger removal radius | Two rects for detail, so a view edge doesn't thrash. As built; rules 22 and 33 drop both the view and the removal radius (1.1) |
| Unreal per-area navmesh resolution ([docs](https://dev.epicgames.com/documentation/en-us/unreal-engine/navigation-mesh-resolutions-user-guide)) | Resolution there is voxel size, and low resolution can delete real paths | Not a precedent. Our detail is which obstacle classes block, never geometric precision |
| Godot region connection ([docs](https://docs.godotengine.org/en/4.2/tutorials/navigation/navigation_connecting_navmesh.html)) | Regions join cleanly only when edge vertices coincide exactly; margin heuristics fail on partial overlaps | Chunk borders are exact shared integer vertices in one mesh |
| Factorio FFF-317 ([post](https://cf-www.factorio.com/blog/post/fff-317)) | The reachability abstraction ignores entities, so placing buildings never touches it; only terrain changes do | Reachability is computed from structural obstacles only (4.2). As built; rule 31 removes the structural layer (1.1) |
| Kallmann, dynamic CDT and Local Clearance Triangulations ([TOG 2014](http://graphics.ucmerced.edu/papers/14-tog-lct.pdf)) | Local insert and remove with a list of obstacle ids per constrained edge. Floating point with an epsilon. Repeated small moves pile up stray points unless removal is done fully | The patch model, with our exact integer predicates. A move re-derives its hole from rings, which removes strays |
| Demyen and Buro ([AAAI 2006](https://cdn.aaai.org/AAAI/2006/AAAI06-148.pdf)); Kallmann on the same measure | A corridor width depends on the nearest obstacle in a wedge, which can sit outside the patched triangles. Kallmann reports the measure is wrong in some CDT cases | Width search is capped at a maximum clearance, and a patch invalidates widths within that distance of its hole |
| jdxdev RTS series ([part 2](https://www.jdxdev.com/blog/2021/07/06/rts-pathfinding-2-dynamic-navmesh-with-constrained-delaunay-triangles/), [part 3](https://www.jdxdev.com/blog/2021/12/07/rts-pathfinding-3-variable-agent-size-smoke-tests-navmesh-fixes/)) | A hobby project. Cuts out the triangles an obstacle touches and retriangulates. Updating subsets took longer to get right than the first mesh; overlapping obstacles were the trap | The cut-and-retriangulate shape. Overlaps go through the existing arrangement, on the hole only |
| CGAL, snap rounding ([docs](https://doc.cgal.org/latest/Snap_rounding_2/index.html)) | Exact predicates don't cover constraint crossings; a snapped intersection can land on the wrong side of a nearby edge | Crossing points are made only by the arrangement, which already iterates to a fixpoint on the integer grid |
| Shewchuk and Brown; Devillers | Segment insertion costs time linear in the edges it crosses. Vertex deletion is local to the triangles around the vertex | The two primitives a patch is built from |
| Dynamically Pruned A* ([abstract](https://webspace.science.uu.nl/~gerae101/pdf/DPAStar-Abstract-ICTOPEN-2016.pdf)), Detour `dtPathCorridor::isValid` | A path that doesn't cross the changed region stays valid | A patch replans only the movers whose remaining route crosses its hole |
| Holm, de Lichtenberg, Thorup, dynamic connectivity | Theory only. No incremental Kruskal reconstruction tree exists in the navmesh literature | Reachability lives where it rarely changes (4.2). As built; how reach is answered on one mesh is open (rule 36) |
| S.T.A.L.K.E.R. A-Life ([interview](https://www.gamedeveloper.com/game-platforms/interview-inside-the-ai-of-i-s-t-a-l-k-e-r-i-)) | Offline agents route on a coarse graph, online ones on detail. Going online needs a valid nav vertex and has shipped stuck-in-transition bugs | The shape of the idea, as first planned. Rule 31 drops the coarse side: there is one mesh at full detail |
| Minecraft chunk tickets ([wiki](https://minecraft.wiki/w/Chunk)) | Chunks stay simulating while something holds a ticket on them | Chunk anchors |

No source documents what happens to a mover's position or route at a detail switch, and none
has a forward-only rule. Section 4.7 is new.

## 4. Design

### 4.1 One mesh

The nav mesh is a single triangulation at full detail of the ground movers have meshed, each
within its sight distance plus a buffer (rules 31 and 32). As built, it covers every chunk that
has been loaded, and a structural mesh sits beside it (4.2, 1.1). It is created once and lives as
long as the game does, and it only grows (rule 33). Triangles and vertices have stable ids; a patch frees some and allocates
others, and every id it didn't touch still means what it meant.

Each constrained edge carries the list of obstacle rings that put it there. The arrangement
already produces that list; today's mesh keeps one id per edge and throws the rest away. The
list is what makes removal safe, and it makes the mesh its own record of what is in it.

**Why there are no nav cells.** An earlier draft of this document stored the mesh in fixed
32 m or 64 m cells and re-derived a cell on any change. With patches there is nothing left for
them to do. A patch is already smaller than any cell. An obstacle enters the mesh once, so
there is nothing to key or cache. And a grid of cell borders would only add triangles, plus a
seam problem where a detailed cell meets a plain one.

**The chunk is the one partition, and it already exists.** As built, ground arrives a chunk at
a time, so each chunk's square is a ring of constrained edges in the mesh. Vertices on a border
between two chunks are shared, exactly, because it is one mesh, and paths and patches cross it
like any other edge. In that narrow sense the chunk is a cell: it is the unit ground arrives
in and the unit its first mesh is built in (and, as built, the unit ground is shed in; rule 33
removes that). It is never the unit a change is applied in. Whether a mover's sight distance
plus buffer (rule 32) brings ground in as whole chunks or as the area itself is not settled.

The borders do one more job. The triangulation's in-circle test is exact only while the points
involved are within about 2^30 mm of each other, and a planet is bigger than that. With chunk
borders in the mesh no triangle is larger than a chunk, so every test is local however far
apart two meshed areas are.

The design is that the mesh only grows, and a chunk unloading does nothing to it (rule 33). The
next two paragraphs are as built (1.1, WOR-797).

**Unloading a chunk drops its detail; it does not remove its ground.** A chunk that unloads
gives back its tiles and its placed entities, so its placed obstacles come out of the nav mesh.
Its ground stays in both meshes: the square, its water, its walls and doors. A chunk that
reloads does not get its ground meshed again.

**Ground a mover has held is never dropped.** A chunk any anchor has ever held (a mover, the rest
of a live route, or the goal of a task waiting for ground, each holding the chunks within the
anchor load range of 128 m) keeps its ground for the life of the world. Only
ground the camera alone ever held is shed: up to `kCameraOnlyUnloadedChunks` (16) such chunks
stay meshed after they unload, and past that the one unloaded longest ago that holds no
construction loses its ground, one chunk a tick, on a tick no ground arrives. Measured on the
Quick Start planet's chunks, real ground costs 100 to 150 KB a chunk on each mesh: both meshes
together are 29 MB at 121 chunks and 265 MB at 1,000 (entry 88). Ground that has never been
loaded has no triangles.

### 4.2 Obstacle classes and the structural layer (as built)

This section describes what is built, which is not the design. In the design there is one mesh
at full detail and no structural layer (rule 31), and every object movers cannot path through
blocks (rule 23). How reach and "is there a way" are answered on one mesh is open (rule 36).
Section 1.1 lists the tickets, WOR-798 and WOR-799 here.

As built, every obstacle belongs to a class.

- **Structural:** terrain polygons (water), walls, doors, and what joins them later: fences, barriers, large mobile objects.
- **Detail:** placed obstacles (the colliders of placed entities) and, once step 7 is built, mover footprints.

Structural obstacles always block. Detail obstacles are priced for movement wherever they are in
the nav mesh, and block placement, spawns and drops (4.4).

The reasoning when this was built: reachability has to be something the camera cannot touch, and
something a walking colonist's footprint cannot touch either. So each chunk also keeps its
**structural layer**: the
triangulation of its structural obstacles alone, built once with the chunk and patched only
when a wall, door, or other structural thing changes. The width-aware reachability forests
are built on it. It is small: open ground is two triangles a chunk, and the
measured structural-only cost of a 128 m region with river is 1.7 to 12.6 ms even with today's
quadratic loop.

This is a second, much smaller triangulation of the same ground, and it was built on purpose.
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
| A placed entity with a collider goes away, a wall or door is removed | Remove (4.3.1) |
| A wall or door is built, placed obstacles first enter an area, a placed entity gains a collider | Add (4.3.2) |
| A mover's footprint or a large object moves | Move (4.3.3) |
| A chunk's ground is wanted | Its square and water rings are added to the mesh (as built, to both meshes), through the same patch workers as every other edit |
| A chunk unloads | Nothing happens to the mesh (rule 33). As built, its detail obstacles are removed (4.3.1) and its ground stays (1.1) |
| As built only: more than 16 unloaded chunks hold ground only the camera ever held | The one unloaded longest ago without construction loses its ground (4.1). Rule 33 removes this (1.1) |

As built, a structural change applies the same operation to the structural layer (4.2).

#### 4.3.1 One mechanism: re-derive a hole from the rings

Remove, add, and move are the same act. Pick the ring edges whose geometry is in question, cut
out the triangles along them, and rebuild that hole from the rings' original outlines. Remove
leaves a ring out, add puts one in, move does both in one hole. An earlier draft removed a tree
by deleting its vertices one at a time; one mechanism replaces that, so there is one code path
and the property in 4.3.2.

The mesh keeps three things to make it possible:

- **A ring registry.** Every ring in the mesh, under a stable id, with its original outline, its class, and its tags.
- **Sources on each constrained edge.** The list of (ring, which edge of that ring) that put the edge there. A piece of edge two rings share lists both.
- **A cover set on each triangle.** The set of rings the triangle lies inside, stored once and shared by every triangle with the same set. Floor, water, wall, tree, and door tags are computed from the set by the rule the builder uses today (the smallest solid ring wins; water goes by nesting parity), so that rule exists in one place.

The patch:

1. **Start set.** The original edges of the ring being added, removed, or moved.
2. **Closure.** An original edge joins the set when an edge in the set crosses it, touches it, or shares a split point with it. Edges join whole, corner to corner, never as pieces.
3. **The hole.** The triangles along the current pieces of every edge in the set, plus the triangles under a ring being added. The mesh edges around that set are the boundary, held fixed.
4. **Re-derive.** The set's original edges, without the removed ring and with the new one, go through the arrangement and triangulation a first build uses, together with the fixed edges in and around the hole. If that splits a fixed edge, or an edge leaves the hole, the edge it hit joins the set, the hole grows, and this step repeats. The boundary is checked, never assumed.
5. **Splice and tag.** New triangles take the freed ids. Cover sets flood in from one seed, changing at each constrained edge by the rings on that edge; the seed is one exact point-in-ring test.
6. **Legalize.** Unconstrained edges on the hole's boundary are flipped until the mesh is constrained Delaunay again.
7. **Tell the rest.** Corridor widths within the maximum clearance of the hole are invalidated, the locate index over the hole is refreshed, and movers whose remaining route crosses the hole replan (4.6).

The triangle walk under a new ring is the local replacement for the all-pairs loop, and the
flood in step 5 replaces the builder's test of every face against every ring, which from
reading the code is where a large first build's remaining time goes.

#### 4.3.2 The mesh does not remember its history

Because every edge in a hole is re-derived from its ring's original outline, with everything
it crosses, the vertices and constrained edges after any sequence of patches are meant to be
exactly the ones a from-scratch build of the same rings produces. Three things follow:

- **The oracle is exact.** A test can compare the patched mesh's vertices, constrained edges, and tags with a fresh build, with no tolerance.
- **Save and load agree.** Ground rebuilt from its rings on load is the ground the running game had.
- **Nothing piles up.** Crossing points a mover's footprint made at its old position are gone after the move. A felled tree leaves no stray vertex on the shoreline it overlapped.

Triangles can still differ where four points lie on one circle, which the corners of every rect
do: Delaunay has a free choice of diagonal there. Walkable ground, tags, and reachability don't
depend on it.

**Tested in step 3, and it holds.** `PatchableMesh`'s tests compare every patched state with a
from-scratch arrangement of the same rings: vertices and constrained edges, with the rings on
each, are equal exactly. That is true on the three game fixtures through random adds, removes,
moves, and re-adds, and on 12,000 states of adversarial input (rings a few millimetres across,
overlapping, nested, sharing edges, hanging off loaded ground, two chunk squares arriving and
leaving in either order).

Getting there found three things:

- **A bug in the arrangement, on main today.** A segment's cut points were sorted by coordinate, and a rounded crossing sits off its line, so two cuts could come out in the wrong order along the segment. The edge then zigzagged and forked. Fixed, with a test; nav output on the game fixtures is unchanged.
- **Rings can meet off loaded ground.** A new ring can cross a ring that hangs over the edge of the mesh, out where there are no triangles, and the rounded crossing can land back on the mesh. The mesh can't show that, so where a changed edge reaches off ground or near its edge the ring registry is asked instead.
- **The builder's tags depend on which triangle it samples.** `buildNavMesh` tags a face by testing one point of its largest triangle against each ring's original outline. After rounding, a face can straddle an original outline by a fraction of a millimetre, so the tag can depend on the triangulation. It showed in a third of the adversarial states and in none of the game fixtures. The patched mesh doesn't have the problem: it gets tags by counting ring edges crossed, never by sampling. The builder is left as it is until step 4 decides what still uses it.

One assumption is known to be false in the general case. The off-ground rule needs to know how
far rounding can move a piece of an edge from its original line. The arrangement re-splits
pieces, not originals, so each cascade of rounded crossings can add about 0.7 mm and nothing
bounds the depth; a search found 2.98 mm from six nearly parallel segments. The rule now
reaches 8 mm, and reaching too far is always safe. Game geometry (axis-aligned tree rects,
metre-scale shoreline edges) is nowhere near such a cascade, but the sound fix is an
arrangement with a proven bound (hot-pixel snap rounding), which changes the builder's output
and is filed as its own item.

**Measured patch costs** (step 3, RelWithDebInfo, medians, the three game fixtures at full
tree density; the hole is triangles freed / created):

| Patch | Time | Hole |
|---|---|---|
| Add a lone tree | 34 to 41 us | 11 / 16 |
| Remove a lone tree | 39 to 61 us | 16 / 8 |
| Move a 0.6 m footprint 0.3 m, open ground | 82 to 86 us | 24 / 24 |
| Move a footprint through trees | 96 to 110 us | about 27 |
| Add a tree on the shoreline | 59 to 73 us | 11 / 23 |
| Add a tree across a border split to 16 m | 63 to 80 us | 17 / 22 |
| All of an area's trees in one patch | 9.8 to 14.0 ms | |
| The same in batches of 64 | 12.1 to 15.3 ms | |
| The same in batches of 16 | 14.4 to 19.2 ms | |
| First build of the area through the patch path | 7.1 to 10.8 ms | |
| `buildNavMesh` of the same area, for comparison | 11.5 to 17.0 ms | |

A first build through the patch path is faster than the builder, and stays faster as the area
grows: 177 ms against 441 ms on the 512 m tiling, because tags come from a flood and not from
testing every face against every ring. Small batches cost more in total than one build (about
30 us of fixed cost per patch, most of it the arrangement and triangulation allocating for a
dozen segments), which is the price of spreading the work over ticks on the main thread.

Point location is about 0.25 us against 0.08 us for the builder's static grid. Whether that
matters depends on how often the game locates per tick, which step 4 measures.

**Queries on the patched mesh** (step 3). Path queries, the heuristic, corridor widths, and the
reachability forests read a mesh through one view type, so each has one implementation whether
the mesh was built or patched.

- Paths through a patched mesh match the builder's in verdict and in length to the millimetre, on the fixtures and on wall-and-door inputs, at agent radii from 0 to 1.5 m. Query time and nodes expanded are the same (2.54 ms against 2.58 ms for 96 queries on one fixture).
- Corridor widths are capped at 4 m of clearance and recomputed on request, not inside each patch: computing them in the patch added 100 to 240% to a lone tree. A corner's width is recomputed only when a patch came within that width of the corner. One request serves any number of patches, so the game asks once per tick after its patches.
- A reachability forest rebuild is about 0.13 us per triangle: 0.03 to 0.1 ms for a 128 m region's structural layer, 0.67 ms for a 512 m one. So the structural layer rebuilds its forests whole when a wall or door changes, and the per-chunk portal graph in 4.6 is not needed yet.

**The cost of whole edges.** A long edge that many things cross pulls all of them into the
hole. A chunk's side is 512 m. Measured in step 3: adding a tree across a 512 m shared side
with trees along it takes about 1 ms and rebuilds about 200 triangles. So chunk squares enter
the mesh with their sides divided into 16 m edges, which brings the same patch to about
0.17 ms and 36 triangles. The split points are exact, since a chunk's side is axis-aligned.

#### 4.3.3 What each operation adds

**Remove.** The start set is the ring's edges. No new points are needed beyond re-rounding the
crossings of edges it had split. As built, reachability reads the structural layer, which a tree
is not in (4.2), and no stored path can be broken by ground opening up; a path that would now be shorter is
found at its next replan.

**Add.** The start set is the new ring's edges, and the walk under them finds what they cross.
For a tree that touches nothing the hole is the few triangles its rect overlaps and the
arrangement has four segments in it. From the measured triangulation cost that is tens of
microseconds (*estimate*, confirmed or not in step 3). Flora entering an area for the first
time is many adds: adds whose holes overlap merge into one hole, and the batch is spread over
ticks inside the patch budget, nearest the view centre first.

**Move.** One hole over the old footprint and the new one, re-derived with the object at its
new position.

**A chunk's square** is a ring like any other, of the class that makes ground exist: a triangle
is kept only if its cover set holds one. Adding a square meshes new ground and joins it to its
neighbours along their shared side; a ring that hung over the edge of loaded ground is
completed then.

**Mover footprints.** Every mover has a footprint ring, sized from its clearance. It is a
detail-class obstacle that blocks every mover but its owner: the path query already filters
faces per agent for belief, and "this face is my own footprint" is one more case of that
filter. As first planned it stayed out of the structural layer, so someone standing in a doorway
would never change what is reachable, only what is passable right now. With one mesh (rule 31)
how a footprint bears on reach is part of the open reach question (rule 36).

Motion adds what a one-off edit doesn't have:

- **Rate.** A footprint is patched when its mover has moved a set distance or come to rest, not every tick. Between patches, movers keep apart by the collision systems that already run. The distance is tuned from a measurement; with dozens of movers this is the patch that runs most.
- **Paths.** Each patch replans the movers whose remaining route crosses the hole, and only those.
- **Who is underneath.** A mover standing where a large object arrives is inside a blocked face and takes the snap in 4.7. Two movers' footprints don't overlap, because collision keeps them apart.
- **Large mobile objects** can open or close a gap. As first planned they were structural, so their moves also patched the structural layer, and the first one in the game would force the reachability forests to be repaired per chunk and not rebuilt whole (4.6). With one mesh (rule 31) that is part of the open reach question (rule 36).

This reverses risk 7 in the pathfinding doc ("dynamic agents are never navmesh obstacles").
That rule came from StarCraft 2, which by secondhand accounts resets its mesh and reinserts
every obstacle on a change, so a unit in the mesh would mean a rebuild per step. With a patch
that costs what a move costs, the reason for it is gone. Steering and
crowd behaviour (P5 there) still stand, and still do the work between patches.

### 4.4 Detail is a filter (as built)

This section describes what is built, which is not the design. Under rules 22, 23, 32 and 33
the view decides nothing, and a mover brings every object it cannot path through within its
sight distance plus a buffer into the mesh, where it blocks and stays: no chunk unload, cap, or
trim takes it out. Section 1.1 lists the tickets for the gap.

Detail obstacles are in the nav mesh as tagged faces, the way wall interiors already are, and
only in the nav mesh; the structural mesh never holds them. For movement they are priced, not
walled (entries 45 and 50): a path query reads each detail kind's price from one table, and
walking a metre through a placed obstacle costs `kDetailPassMultiplier` (50) metres of open
ground, on camera or off. So a mover goes round one when it can and through one when the way
round is longer, a mover standing inside one is on valid ground, and detail never makes a goal
unreachable. Placement, spawns, drops and construction still treat a placed obstacle as blocked
(`isValidPosition`), reading colliders from placement data, not from the mesh.

Detail being on decides only what collision and the forward snap do (4.7). Turning it on or off
for an area flips flags and does no mesh work.

- **Entering the mesh.** Placed obstacles are added to a 16 m cell (`kDetailCellMm`) the first time the view wants it (4.3.2), at most two cells and 32 rings a tick, and then stay. Scrolling away and back costs nothing.
- **Leaving the mesh.** A chunk that unloads takes its placed obstacles out. Past `kDetailRingBudget` (40,000 rings) whole cells leave, longest out of the view first, down to 7/8 of the cap, never one where detail is on or inside the keep rect. At main's placement density a ring costs about 6.4 KB, so the cap is about 250 MB, about half a square kilometre of dense cover (entry 88). Because the price is constant, leaving changes path shape and never whether a goal can be reached: off camera a mover routes round the obstacles still in the mesh and straight where there are none.
- **Who asks for detail.** The viewport rect, with its margin, is the only requester (entry 77). The earlier plan to add gameplay requesters (combat) one at a time is replaced by rule 23: every mover generates the full mesh around it.
- **Two rects.** Detail turns on within `kDetailOnMarginMm` (8 m) of the view rect and stays on until the view is `kDetailKeepMarginMm` (32 m) farther away, so a view edge resting on a boundary doesn't flip it back and forth.
- **Pending.** Between a request and the adds finishing, those obstacles are not in the mesh. The view margin exists so that happens off screen.
- **A route through detail on purpose.** A route that crosses priced faces is exempt from collision and the forward snap until it ends (entry 58).

### 4.5 Coverage and chunks

In the design only movers mesh ground, each within its own sight distance plus a buffer, and
nothing leaves the mesh (rules 22, 32 and 33). As built, the camera meshes ground too, a mover
meshes the chunks within a fixed 128 m of it, and ground can be shed; the rest of this section
describes what is built (1.1, WOR-796, WOR-797).

Both meshes cover every chunk whose ground has been meshed and not shed (4.1). There is no
colony ground and no owned area (rule 25): as built, ground is meshed for movers and for the
camera, and a route or a reach walk uses all of it, whoever caused it. A route can be planned across
unloaded ground that is still meshed; the chunks along it load as the anchors on the route reach
them.

A chunk's ground is gathered on the main thread and patched in by the two workers (4.8):
ground an anchor holds first, nearest an anchor, then the camera's, nearest the view, so panning
never holds a mover's ground back. Open chunks share a tick up to `kGroundVerticesPerTick` (256)
square vertices, and a chunk with water goes alone. Ground is not built on the chunk's
generation worker; entry 38 records why that alone would not help.

`ChunkManager` gets anchors. In the game the camera loads the chunks within one chunk of its own
and keeps those within two (3 by 3 and 5 by 5 chunks of 512 m). Every anchor loads the chunks
within 128 m of it and keeps those within 256 m. The
anchors are every mover the AI drives, the rest of every live route (points no more than half
the load range apart along it), and the goal of every task waiting for ground. That is the
unloader enhancement in rule 9, and it fixes WOR-645 (Chunk streaming follows the camera, so
panning away unloads the colony's chunks). Placement runs for anchored chunks the same as for
camera chunks, because colonists harvest off camera.

So a mover always has mesh under it and around it, and its ground stays meshed after it leaves.
Two groups of movers are joined by mesh when the ground between them has been meshed. Where it
never has, they are not joined in this epic; see 4.10.

**Mesh is not visibility (rule 27).** The meshed area is larger than the visible area, and
nothing may read "has mesh" as "the player can see or act here". As built, ground the camera
meshes joins the mesh like any other, so a walk or a route can use a way the camera brought in,
including ground no mover has seen. Rule 22 removes that: the camera meshes nothing (1.1).
Placement needs no rule of its own: the player places only in the viewable area, which is
smaller than the meshed area (rule 35). Player visibility is WOR-754 (Player visibility:
unexplored areas (visible, seen, unseen)).

### 4.6 Queries

In the design every query runs on the one mesh (rule 31). How reach and "is there a way" are
answered there, and what the search budgets become, are open (rule 36, WOR-799 (Nav: one
full-detail mesh with no coarse layer)). The Reach, Routes, Budgets, Reachability forests,
Detail that seals, Staleness and Placement items below describe what is built on the structural
mesh (1.1).

- **Path.** Triangle A* over the nav mesh scored by taut corridor length (decision 40), with the per-agent filter deciding which tagged faces block and which are passable at a price: belief for walls, one price per detail kind as built (decisions 45, 50 and 54; 1.1), and later ownership for the agent's own footprint.
- **Reach (as built).** `walk()` gives one of three answers: a walk length, a definite no, or far. It searches the structural mesh under the mover's belief (known walls block) over all meshed ground, so detail and the view change nothing. A search from the goal toward the start goes first, to `kReachProbeExpansions` (2,000): a goal sealed in a pocket runs it dry at once, a definite no, and one that finds the start has the walk. Then a search from the start, to `kReachExpansionCap` (5,000). A search that stops on the caller's bound on length is a definite "no walk that short". When both run out of steps the goal is far, and far is never read as no (rule 18): a far candidate stays a candidate and is taken when no candidate with a known walk is, nearest first in a straight line. A start off meshed ground answers the straight line. A goal off meshed ground answers the straight line while its chunk is loaded, since its ground is on the way, and unreachable otherwise (rule 21; the design is rule 26).
- **Routes (as built).** `requestPath` plans between any two points on meshed ground, for anyone, at any distance (rule 25). It runs the same structural walk first, once: the goal-side search to 2,000 steps, then the start-side search carried on to `kFarRouteExpansions` (60,000) in the same pass. Where the walk is a definite no there is no route. Where the probe found it, or the start-side search found it within `kReachExpansionCap` (5,000) steps, the goal is near and the nav search runs with detail priced, capped at `min(kNavExpansionFloor + kNavExpansionsPerStructural x (steps of the search that found the walk), kNavExpansionCeiling)`, that is `min(11,000 + 16 x steps, 20,000)`; if it runs out, the walk itself is the route, through detail at its price. Where the start-side search took more than 5,000 steps the goal is far, there is no nav search, and that walk is the route. If the start-side search runs out at 60,000 the answer is "search gave up" (`PathAnswer::gaveUp`), never "no route"; the decision trace and the task list show it as its own status. A route taken from the structural walk (far, or a near search that fell back to it) is re-priced in place when detail lands on it, never marked stale (Staleness, below).
- **Budgets (as built).** Set from measurements of searches on both meshes (entries 81 and 92); each constant in `NavigationSystem.h` states its main-thread time and where the number came from. A far route request is the one that can cost 19 to 34 ms on the main thread, once per far target, accepted for now (rule 19).
- **Reachability forests (as built).** The width-aware forests (truth and terrain) on the structural mesh. A path query uses them to reject a goal in another component before any search (an islet costs no steps). They are rebuilt whole on the structural worker on every structural job, which passes the 2 ms job budget on real ground past about 121 meshed chunks; WOR-722 (Reach forests are rebuilt whole on every structural change) is the per-chunk repair (the FFF-317 shape).
- **Heuristic.** None beyond the search's own score. The reverse-search heuristic was removed (decision 44); as built, a detour bound from the structural layer was the planned follow-up (rule 31 removes that layer).
- **Detail that seals (as built).** Structural-reachable means reachable, because detail is never a hard wall for movement: it is priced (4.4). Rule 23 reverses that: an object movers cannot path through blocks (1.1), so it can seal a gap the structural layer reads as open.
- **Locate.** A spatial index over triangles, updated by each patch for its hole.
- **Widths.** Capped at a maximum clearance; refreshed on the worker outward from the edges a patch changed. Each corner stores two: the full width, and one with detail read as open.
- **Staleness (as built).** `generation()` moves only when a structural job lands on ground an anchor holds; routes that the change does not close ground across are re-stamped current at the join. Camera-only ground moves nothing. Detail landing marks stale only the routes whose rest comes within the mover's radius of a new obstacle, except a route taken from the structural walk, which detail cannot change: it keeps its waypoints and is re-priced in place (whether it now crosses priced faces). Detail leaving marks nobody. A mover more than `kOffRouteMm` (2 m) off the leg of its route it is on has its route marked stale.
- **Validity.** Two predicates. `isOnMesh`: ground a mover may stand on, anything but water, placed obstacles included, since they are priced. `isValidPosition`, the one predicate for spawns, drops, placement and teleports: that and clear of every placed obstacle's collider, read from placement data whether or not it is in the mesh. Both read every wall face as open.
- **Placement.** Needs no rule of its own (rule 35). As built, `isAreaBuildable` reads the structural layer (WOR-799). The separate cached terrain-only mesh is deleted.
- **Work targets.** The stand point beside a placed obstacle comes from its collider, not from the mesh, so it is the same point on and off camera.

### 4.7 Snap forward

The rule (rule 30): wherever an obstacle lands on a mover, the mover moves forward along its
route, never stalls or loses progress. As built, the trigger is detail turning on with the view,
and the snap and collision with placed obstacles act only where detail is on (1.1); the
rest of this section describes that.

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
out of placed obstacles where detail is on, and the snap runs before it in the same tick, so its
nearest-edge push can't undo a forward snap.

### 4.8 Threads, determinism, and correctness

Each tick's edits are gathered on the main thread in `NavigationSystem::update`, in a fixed
order and under per-tick count budgets, never a time budget, so which tick an obstacle enters
doesn't depend on frame time (entry 18). After every system has run, the end-of-tick hook
(`ISystem::endTick`) hands them to worker threads. As built there are two, one per mesh (the
structural mesh is WOR-799, 1.1); the nav worker refreshes its widths and the structural
worker rebuilds its forests. The next `update` or the first nav query, whichever comes first,
waits for both. So a query always sees exactly the
patches gathered through the previous tick, however long the workers took, and nothing reads a
mesh while it is patched. There is no second copy of either mesh. The main thread pays only when
a job outlasts the rest of the frame: up to 4.3 ms with a 2 ms gap between ticks when a
shoreline chunk's ground arrives (entry 67); WOR-721 (Shoreline chunk arrival can make the main
thread wait on the nav patch job) tracks it.

Queries themselves run on the main thread. As built, a far route request (4.6) is the longest, 19 to
34 ms once per far target (rule 19).

As built, the camera is real time and detail follows it, so exact paths and arrival times are not
reproducible across runs with different camera movement (1.1). Under rule 22 the camera
is not an input to the mesh, so this goes away with that ticket.

A mesh that is never rebuilt has nothing to wash a bad patch out, so correctness is carried by
three things:

- **The oracle.** For any sequence of adds, removes, and moves, the mesh's constraints and tagged faces equal those of a mesh built from scratch from the same obstacles. Tested on every real fixture with obstacles added and removed one at a time in several orders, including overlapping obstacles, obstacles on waterlines, and obstacles on chunk borders. It is a face-level comparison because of the co-circular diagonals.
- **The validator.** `PatchableMesh::validate()` checks the whole mesh: free lists and counts, and per triangle its shape, links, ring edges, the Delaunay property, cover sets, tags and the locate index. The geometry and sim tests run it after their patches; the game does not.
- **Exact arithmetic throughout.** No epsilons enter. The one place new coordinates are made is the arrangement, which is the code that already handles it.

`finishPendingBuilds` is the blocking settle for tests and the headless sim: it gathers every
queue with no budget, applies it and waits, until nothing is left. It reads only chunks that are
ready, so its caller settles chunk generation and placement first.

### 4.9 All movers

Coverage, routing, staleness, footprints, and the snap key on a mover, not a colonist. Today
the route logic (request, replan on staleness, deferred retry, off-mesh recovery) lives in
`AIDecisionSystem` and reads colonist tasks and memory. It moves to a mover-generic route
system that the colonist AI sets goals on. A wandering deer with no memory and no tasks is the
second client and the test that nothing colonist-shaped leaked in.

### 4.10 World scale

The nav mesh covers ground that has been meshed. In the design a mover whose goal has no mesh
heads that way and meshes the ground as it goes (rule 26); how that route is planned is open
(rule 36). As built, a goal off the mesh waits
while its chunk is loaded and reads unreachable otherwise (4.6); travelling toward it is
WOR-776 (Nav: build a path toward a goal whose ground is not meshed yet). Beyond the mesh, two things are planned and neither
is this epic:

- The geography mesh (the regional mesh), unchanged from the pathfinding doc, much less detailed than the real mesh and aligned with it (rule 38).
- A landmass check so nothing ever tries to path off an island: flood the planet's land hexes from the landing site's hex once at load and keep the set. The single-hex-island test case falls out of it. At the quickstart resolution the planet has about 42 million hexes, so this is a bounded flood from the colony, not a table for the whole planet. It has no consumer until goals can lie outside loaded ground, so it is filed as a follow-up and built with that consumer.

## 5. What gets deleted

No fallbacks. When the one mesh routes, these go in the same change:

- `SimulationRegion`, region clustering, recentering, the edge margin and size-change gates, `retiringBuilds`, the per-region `std::async` builds.
- `regionContaining` dispatch, the cross-region "unreachable" and off-area "hold" branches, `inSimArea`.
- The global `meshGeneration`, the global construction-version and removal-epoch rebuild triggers.
- `buildTerrainOnlyMesh` and the cached placement mesh.
- The per-build `extractWalls` over the whole construction world, and `nav::buildInput`'s area sweep.

Rule 31 adds to this list the structural mesh and what is built on it (1.1, WOR-799 (Nav: one full-detail
mesh with no coarse layer)); what replaces reach on one mesh is open (rule 36). Rule 33 adds the
ground shedding, the removal of a chunk's placed obstacles on unload, and the placed-obstacle cap
and trim (WOR-797).

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
500 bytes, because the two reachability forests ride on it (as built; rule 31 removes the
structural layer). Today every triangle pays the
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
chunks is about 20 MB. Flora is nearly all of it, so as built the detail cap in section 4.4 is the
control; rule 33 removes the cap, which leaves the cost per placed obstacle. This estimate put 40,000 rings at 64 MB; measured at main's density a ring is about
6.4 KB, so the cap is about 250 MB (entry 88). `NavTriangle` is mostly 64-bit fields holding a
sentinel, and packing it could halve the figure; measure before making that a step.

**Memory, measured 2026-11-11** (`BM_PlacedObstacleBytes`, `BM_PlacedObstacleSystemBytes` and
`BM_PlacedObstacleDensity` in `NavigationSystem.bench.cpp`; entry 95). This replaces the
estimate above wherever the two differ.

- On the real planet around the landing, placed obstacles run at 0.060 to 0.068 per square metre, 75 to 85% of the 0.0802 worst case.
- A placed obstacle costs about 7.2 KB in the mesh as built: 8 triangles and 4 vertices. The triangle box grid is 29% of that, the triangle records 21%.
- So meshed ground costs about 435 to 490 MB per square kilometre with its placed obstacles, against about 1 MB for ground alone.
- A field-by-field accounting puts a placed obstacle at about 1.25 KB with mechanical changes to the records (no design change), about 75 to 85 MB per square kilometre.

The mesh only grows (rule 33), so nothing caps this: the meshed area times that cost is the
memory. Bringing the per-obstacle cost down is part of bringing the code to the design (rule 37,
1.1). Paging unused parts of a very large mesh out to disk is a possible later follow-up, not
part of the design: WOR-800 (Nav: page unused parts of a very large mesh out to disk (later,
maybe)). Mesh paged out would come back unchanged.

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
4. **One mesh in the game.** `NavigationSystem` holds the one mesh and the structural layers, built per chunk and patched from then on, with flora added where regions are today so behaviour is comparable. Reachability moves to the structural layer. Regions are deleted. WOR-644 closes here: a colonist's reach no longer has an input the camera can touch.
5. **Detail as a filter.** Flora enters on first need and blocks only where detail is on. The two rects, the memory budget, the forward snap, the collision gate.
6. **Movers.** The route system comes out of `AIDecisionSystem`. A wandering deer uses it.
7. **Footprints.** Every mover's footprint is in the mesh and moves with it. The rate rule, the own-footprint filter, and what a blocked mover does (open question 2).

Step 5 as written is the camera-detail model that rules 22 and 23 replace. Steps 4 and 5 as
written also build the structural layers, reach on them, the fixed anchor range, and the detail
cap, which rules 31 to 33 replace. Bringing the code to the design is the four tickets in 1.1.

Step 6 doesn't depend on 3 to 5 and can move earlier. Step 3 is where the risk is: it is the
part every source says takes longest, and it is deliberately headless and finished before the
game depends on it.

## 8. Open questions

Questions 1, 3 and 4 are answered by rules 22 and 23: there is no camera-driven detail and no
off-camera tier, and an object movers cannot path through blocks. Question 5 is answered for the
mesh by rule 32: a mover meshes its own sight distance plus a buffer. They stay as written for
the record. Question 2 is still open, and its reasoning leans on the structural layer, which rule
31 removes; whether a footprint in the only doorway bears on reach is part of the open reach
question (rule 36). Question 5 was settled for chunk loading by entry 14.

1. **Flora that seals.** Trees can close a gap or ring a spot that the structural layer says is reachable. Options: the mover walks through the flora at a high path cost when there is no way around (visible, rare, and the same thing it would do off camera), or flora stays a hard wall and the answer differs on and off camera. Only the first satisfies rule 6. Confirm.
2. **A mover in the way.** A footprint is impassable, so a colonist standing in the only doorway blocks it. The goal stays reachable, because footprints are not in the structural layer. What does the blocked mover do: wait and retry when the footprint moves, or ask the blocker to step aside? Waiting is the simple answer and can deadlock two movers in a corridor; the pathfinding doc's door queues and avoidance are the longer one.
3. **Footprints off camera.** As detail-class obstacles, footprints don't block where detail is off, so off-camera movers pass through each other on the mesh while collision still separates them. Confirm, or say footprints block everywhere.
4. **Detail off camera around working colonists.** A colonist chopping off camera stands inside the trunk and is moved out if the view arrives. Acceptable under rule 5 as written. Confirm it doesn't meet the "affects gameplay" bar.
5. **Load range around a mover.** Enough chunks for its vision and goal range, likely the chunks within about 128 m. The memory cost per off-camera group gets measured in step 2.

## 9. Related documents

- [Pathfinding architecture](./pathfinding-architecture.md): the tier model, belief filtering, the geography mesh
- [Terrain polygons architecture](./organic-terrain/terrain-polygons-architecture.md): the rings ground and water are built from
- [Chunk management system](./chunk-management-system.md)
- Dev log 2026-06-24 (sim-area nav LOD) and 2026-06-28 (multi-region nav): why regions exist

## 10. Decisions log

Calls made without Kevin while he was away, for him to check. Newest last. Each says what was
decided, why, and how to undo it. An entry is left as written once a later one changes it; the
later entry names what it reverses. Entries 79 to 89 record Kevin's rulings of 2026-11-11
(section 1, rules 14 to 21) and what was built under them. Entries 90 to 92 record the settled
design (rules 22 to 30), where the code departs from it, and facts that changed in code after
entry 89. Entries 93 to 96 record Kevin's later rulings of 2026-11-11 (rules 31 to 38), the
departures they add, and the memory measurements behind rule 37.

**2026-11-10**

1. **Order of work.** Steps 1 and 3 first, then 2, 4, 5, 7, and 6 last. Step 6 (route system out of `AIDecisionSystem`) touches files the parent branch is changing, so it waits for a merge of that branch.
2. **One patch mechanism.** Remove, add, and move all re-derive a hole from the rings' original outlines (4.3.1). The earlier draft had removal as vertex deletion. Why: one code path, and the mesh then equals a from-scratch build after any history. Undo: none needed; vertex deletion was never built.
3. **Edges join a hole whole.** A patch re-derives every affected ring edge corner to corner. Why: it is what makes the mesh independent of its history. Cost: a patch crossing a long edge is larger. Measured in step 3; the remedy, if needed, is to split chunk sides into shorter edges on entry.
4. **Arrangement cut-order fix (commit 1c103a4d).** A bug on main, fixed here because the oracle tests could not pass without it. It changes `buildArrangement`'s output on inputs that hit the bug, for every user of the arrangement (rooms, booleans). Nav output on the game fixtures is unchanged. Checked through geometry-tests only; the sim suite has not been run against it. Undo: revert that commit, which also breaks the patch oracle.
5. **Builder tags left alone.** `buildNavMesh` can tag a face by its triangulation (4.3.2). Not fixed, because step 4 decides what still uses the builder. Flagged, no behaviour change.
6. **Tie between solid rings of equal area.** The lower ring id wins. The builder took whichever came first in its input. Affects only two exactly equal-area solid rings over the same spot.
7. **No game launches from this branch's session.** The rule was to ask Kevin first, and he is away. Everything here is measured headless; in-game captures are left to the single integrated run on the parent branch.

8. **Chunk sides enter the mesh split into 16 m edges.** Measured: a patch across a whole 512 m side costs about 1 ms, across a 16 m piece about 0.17 ms. Cost: about 128 more vertices per chunk, so an open chunk is a few hundred triangles, not two. One mesh parameter; 0 turns it off.
9. **Off-ground reach raised from 2 mm to 8 mm.** The 2 mm bound on rounding drift was disproved (4.3.2). Reaching further only makes a hole slightly larger. The sound fix is filed separately.
10. **Step 3 gate on batched detail entry is read as a total against the builder, and small batches miss it by design.** One patch of all trees beats the builder; batches of 16 cost 25 to 30% more in total. Spreading the work over ticks is the point, so the per-tick budget decides the batch size in step 5, not this gate.

11. **Corridor widths are recomputed on request, once per tick, not inside each patch.** Measured: inside the patch they double or triple a lone tree's cost. The mesh refuses to hand out a query view while widths are stale, so a missed refresh is an assert, not a wrong path.
12. **Clearance cap of 4 m.** Widths at or above it read as unconstrained, which is exact for any agent up to 2 m radius. Today's only agent is 0.3 m. One mesh parameter.
13. **Forests rebuild whole.** At 0.13 us per triangle a structural-layer rebuild is well under a millisecond at current sizes, so no per-chunk repair is built.

14. **A mover loads chunks within 128 m and keeps them within 256 m (step 2).** The gap is hysteresis, so a mover pacing near a chunk side doesn't load and unload it. Two constants on `ChunkManager`. Measured: one loaded chunk is 2.8 MB on land and about 7.5 MB beside water before placement data, so an off-camera group at a chunk corner costs 11 to 31 MB.
15. **A mover is a colonist for now.** Anchors are the Position + Colonist entities, the set that drove nav regions. Step 6 widens it when the route system stops being colonist-only.
16. **`ChunkManager::update`'s anchor argument has a default.** Making it required means editing one call in `AIDecisionSystem.test.cpp`, which the parent branch is changing. To be made required after that merge.
17. **Two things found and filed, not fixed:** WOR-717 (Felled trees and harvested nodes come back when their chunk unloads and reloads), an existing bug that anchors make rarer; WOR-718 (Off-camera chunks hold flora meshes on the GPU and grass instance lists on the CPU), memory that now grows with how spread out the colony is.

18. **The per-tick patch budget is a count, not a time (step 4).** Section 4.8 said "time budget". A time budget makes the tick a tree enters depend on frame time, which breaks rule 6's bar on reproducible outcomes. So: a fixed number of flora rings and of chunks' ground per tick, the numbers chosen from measurement.
19. **The path heuristic stays on the nav mesh in step 4.** Section 4.6 moves RRA* to the structural layer. That needs a map from nav triangles to structural triangles and a check that a coarse structural triangle still gives a useful estimate, so it waits for a measurement. Until then the caches are cleared on any tick that patches the nav mesh, as a region swap cleared them before.
20. **`generation()` still bumps for everyone, once per tick with patches.** Targeted replans (only movers whose route crosses a hole) need a one-line change in `AIDecisionSystem`, which the parent branch is editing. Done with step 6.
21. **Public nav members that only made sense with regions keep their signatures until step 6**, for the same reason, with meanings on the one mesh. The list is in the step 4 notes on WOR-707 (Step 4: One mesh in the game).
22. **Between steps 4 and 5 flora is a hard wall where it has been added.** Reachability reads the structural layer, which has no flora, so a spot ringed by trees reads reachable and then fails to path. Step 5's pass-through cost closes that. The two steps merge together.

23. **Independent review of the step 1 and 3 geometry, and what it found.** A reviewer that did not write the code read all of it. Five correctness findings, none hit by today's tests: a 32-bit visit counter in the width search that wraps after hours of footprint patches and then lets paths through gaps too narrow; path-heuristic caches and query views that go stale across a patch, with no patch counter on the mesh to catch it; the locate table's keys wrapping beyond 68,700 km from the origin; the equal-area tie in entry 6 depending on the order rings were added, which breaks "save and load agree"; and a one-millimetre gap in the width invalidation test. Four cost findings, the main one being that every patch clears an array the size of the whole mesh. All are fixed before the branch is called mergeable, after step 4, because the two share a build directory. Entry 6 is reversed by this: the tie will break on the rings' content, not their ids.

24. **Step 4 works and misses its main-thread gate; patches move to a worker thread.** Measured headless, before and after, main-thread time in `NavigationSystem::update` per tick (mean): steady 7.6 to 8.7 us; one tree felled 8.3 to 10.2 us; a pan at 83 m/s 40 to 373 us; cold start with 5 colonists 135 to 838 us, worst tick 8 ms (one chunk's ground). Total mesh work went down where ground is revisited or edited (a felled tree 4.4 ms to 0.14 ms, a wall 4.7 to 1.4 ms, panning back and forth 169 to 51 ms) and up where ground or flora enters for the first time (cold start 3.8 to 49 ms). The reason is plain in hindsight: the old system meshes on a worker and its main-thread cost is only gathering inputs, and step 1 made a region rebuild cheap. Section 4.8's "patches run on the main thread" is therefore reversed. The plan: the tick's edits are gathered on the main thread, a job applies them on a worker after every system has run, and the next `update` or the first nav query waits for it. Waiting is what keeps results independent of timing: a query always sees every patch submitted so far. The main thread pays only if a job outlasts the render phase. There is no second copy of the mesh.
25. **A dense-water chunk costs 28 ms to mesh in one patch** (a geometry benchmark of a 512 m square full of shoreline; sparse river chunks measured 0.8 to 3.3 ms per mesh). If real chunks get near that, the answer is to build a chunk's ground as its own mesh on the chunk's generation worker and absorb it, which is not built. Real chunk costs get measured first.
26. **A recorded reach failure is keyed on structural patches only.** Reachability is answered by the structural layer, so a flora patch somewhere must not make every colonist retry goals it already found unreachable. This replaces the per-region mesh generation the key used before.
27. **Eight tests in `AIDecisionSystem.test.cpp` assert region behaviour** (no mesh after the first update, exactly two regions, river banks that only looked disconnected inside a 120 m region). That file belongs to the parent branch's session, which cleared edits to those three fixtures on this branch: the first-mesh tests get their chunks late, the two-region test is inverted (one mesh, the nearer routable water wins), the test rivers are widened so the banks stay apart, and the "change elsewhere" becomes a flora patch.
28. **Unverified and being chased:** 26 of 63 benchmark paths differ in length from the old system's (2% longer in total), and `requestPath` is 28 to 53% slower. Neither is explained yet. The branch is not mergeable until both are.
29. **The review findings of entry 23 are fixed, each with a test** (2026-11-11). The width search's counter clears on wrap; the mesh has a patch counter (`epoch()`) that resets path-heuristic caches and that query views read at the moment of use; locate cells are keyed by full coordinates and tested at 2^40 mm; equal-area solid rings tie on content (provenance, opening, outline), so entry 6 is reversed; misuse is refused in release builds, not only asserted. One finding has no test: the one-millimetre width invalidation gap, where no fixture could be built that lands in it; the oracle's full width recheck still covers it. `buildNavMesh` output is byte-identical before and after.
30. **A patch no longer costs more on a bigger mesh.** Working storage lives on the mesh and is cleared from the list of what was marked; ring edges have their own spatial index. A lone tree add measures 27 us at 87,000 triangles and 28 us at 349,000 (was 45 and 50), a remove 29 and 32 us, a footprint move 63 to 82 us, every tree of a region in one patch 9 to 12 ms. A lone add allocates 32 times, down from 216. Measured under load (GPU about 40%, CPU idle), so these say "no regression" and are not baselines.
31. **The path search follows the triangulation, and the one mesh makes that visible.** This is the cause of entry 28's path lengths. The search costs a corridor by the distance between triangle centroids, then straightens the winner. Fine triangles make a cheaper-looking chain, so the search leans toward chunk sides, which carry a vertex every 16 m. An oracle compares one ground square against the same ground as adjacent squares: reach answers, bottlenecks, and corridor widths match exactly; path lengths do not (92 of 114 paths differ on a 2 by 2 tiling, the worst 32 m longer; a 36.7 m straight walk comes out 41.8 m with seven points). The old regions had no internal sides, so main has the same search and hides it. Decision: fix the search in this epic, because the epic is what exposes it. The replacement must agree across tilings within a stated tolerance, be no slower, and be able to take a cost per triangle later (step 5), which rules out exact methods that only work on uniform cost. If no candidate meets both bars the current search stays and the difference is reported as is.
32. **Paths must not depend on the order of edits.** Found by the review of step 4: the search breaks ties by triangle slot, and slots follow patch order, which follows the camera. The same final ground reached two ways could give two paths, so save and load would not agree. Ties and point location resolve on coordinates instead; tested by building one ring set through two edit orders.
33. **Independent review of steps 2 and 4.** Chunk anchors (hysteresis, borders, precision at 10,000 km), the wall and door diff, the removals log, and system order were read and found sound. Three must-fix findings, all being fixed with a test each before merge: the reach key moving on flora patches (entry 26 was decided but not built); flora removals taking the whole per-tick budget, so a chunk unloading could hold flora out of the mesh ahead of walking colonists for seconds; and entry 32. Also fixed: the flora scan skipping on a count that can alias, removals patched in one box across a whole chunk, and benchmark labels that claimed more than they measured (the bench colony stood 12 m from a chunk side, which is why its paths showed the seam bias).
34. **Structural ground is kept after a chunk unloads, as section 4.2 says, so reach forests grow with every chunk ever visited** until step 5's memory budget drops the oldest. The forest rebuild is 0.13 us per triangle and runs on the patch worker; its growth is measured in the step 4 numbers rather than redesigned now.
35. **Patches run on two worker threads, one per mesh (entry 24 built).** A new end-of-tick hook on systems hands the tick's gathered edits to the workers after every system has run; the next `update` or the first nav query waits for them. Measured headless against the old regions, main-thread time per tick (mean): steady 7.6 to 1.5 us, a pan at 83 m/s 40 to 11.7 us, a wall 17.7 to 2.3 us, colonists walking 100 m 9.3 to 2.8 us, twenty colonists cold 13.7 to 14.4 us. Lower or equal everywhere. Flora-only jobs run 0.7 to 1.9 ms with a worst of 2.8 ms at 32 trees a tick (was 4 on the main thread). Under GPU load, so "no regression", not a baseline.
36. **What the old system never did: the main thread can wait on a job.** With ticks back to back the wait is the whole job; with a 2 ms gap between ticks it was zero for trees, walls and slow pans, up to 0.9 ms for fast pans and zooms, and up to 5.2 ms when a chunk's ground arrives. On real Quick Start chunks around the landing site (read from the baked planet, 25 chunks, 12 water rings) one chunk's ground is a job of 4.4 ms on average and 9.7 ms at worst, and the main thread waited up to 6.9 ms with a 2 ms gap. That is one long frame per arriving shoreline chunk in an uncapped run. Not accepted as final; entries 37 and 38 are the work on it.
37. **Tried and rejected: ground arriving in smaller squares.** A chunk as a grid of 256 m or 128 m ground squares was built and measured. It does not bound the job. An empty 128 m square costs 0.7 ms per mesh, a third of a whole open chunk for a sixteenth of the area; water squares stop shrinking at about 4 ms; the extra sides nearly double the triangle count (9,900 to 18,800 slots on the real chunks), so forest rebuilds grow from 2.2 to 4.1 ms; total mesh work rises 3.8 times. Removed. One ground square per chunk stays. (This would also have sat close to the rule against tiles as the patch unit; it is recorded so it is not tried again.)
38. **The two costs behind entry 36 are in the geometry and are being cut there:** adding an open chunk costs 1.7 to 2.3 ms where a lone tree costs 27 us, and the reach forest rebuild costs 0.33 to 0.57 us per triangle (section 2.1 said 0.13). What will remain after that is the shoreline chunk, whose cost is its triangles. The fix for that is to split a patch into a read-only prepare that can span ticks and a short commit, or to absorb a chunk meshed on its generation worker in bulk (section 4.5's intent). Neither is built; "build on the chunk worker and absorb" alone would not help, because the splice into the shared mesh is itself about a microsecond a triangle and has to happen while queries wait.
39. **Ground of unloaded chunks is kept for at most 16 chunks** (one constant), the least recently unloaded dropped first, never one with walls or doors. Brought forward from step 5 because without it the reach forests grow with every chunk ever visited. Sixteen is a 4 by 4 block: turning back across ground just left does not re-mesh.
40. **The path search costs a corridor by its taut length (entry 31 built).** Each search label carries its corridor's funnel, and the score is the string-pulled length to the goal, so the winner depends on walls and corners and not on the triangulation. Measured: one ground square against the same ground as adjacent squares now agrees within 0.67% at colonist radii (worst was 37%), and the oracle asserts 1% of length plus five radii. Against the old search on the same mesh, total path length is 2 to 7% shorter on every fixture and no pair is longer by more than 24 mm. The same rings reached through different edit orders give identical paths (entry 32 built): ties and point location break on coordinates. The benchmark rows are about four times faster (17,000 expansions down to 1,400). Two things are worse and being worked: paths of 100 to 500 m on a 100,000-triangle mesh take about 5 ms where the old search took 3, and the reverse-search heuristic had to become a weaker bound to stay correct against the new score, so a long detour around water explores more than it did.
41. **Two limits of the width model, found by the path tests and left alone.** At radius 1.5 m one path in the tiling oracle differs by 5% between tilings, and in a lattice with 2 m gaps one verdict differs between edit histories. Both come from corridor widths being stored per triangle: a free diagonal can open or close a gap for a wide disc. Colonists are 0.3 m and do not hit either. A large mover would, so this needs settling before one exists.
42. **Review of the path search found its score was not a lower bound; fixed.** The search stopped at the first goal it reached on a score that could exceed the true length, so a longer corridor could win. Now the score never exceeds the taut length and a goal is accepted only at its exact pulled length. An exact visibility-graph shortest path that shares no code with the search referees 80 random pairs to within 4 mm; the first version fails it by 175 mm on one pair. The same review confirmed the incremental funnel, termination, completeness, and determinism.
43. **The path search after its second round.** Each step is cheaper (a label's funnel restarts at a bend and the exact length is pulled once, at the goal). Long paths on large meshes are back to the old search's time on whole ground and about 10% over it on split ground, with paths 2 to 7% shorter. On the game's query shape (64 starts, 4 goals, 20 to 100 m, flora meshed) a geometry benchmark reads 11 to 14 us a query without a heuristic against 14 to 20 for the old search with a warm one. The in-game number is not re-measured yet. Split-versus-whole agreement is asserted at 2% of length plus two radii for discs under 1 m.
44. **The reverse-search heuristic (RRA) is removed.** Against the new score the only admissible form found prunes 3 to 5% of expansions and costs 5 to 20% of the time; a stronger form made paths up to 4% longer. The search now expands about as many labels as its corridor has triangles on ordinary queries, so there is little left for a heuristic to save there. Where it would still help is a long detour around water (85 expansions on the test scene where the old heuristic gave 16). Section 4.6's structural-layer heuristic is therefore not built; a detour bound from the structural mesh is the follow-up if in-game detours turn out expensive.
45. **Flora is priced, not walled (open question 1 built in the geometry).** A query can mark a face passable at a cost multiplier by tag and place: a tree where detail is on costs a high multiple of its length, a tree where detail is off costs nothing, so trimming flora from the mesh off camera changes no answer. A gap between two trees too narrow for the mover is charged like the trees, not taken for free. The path reports that it crosses priced faces so collision and the snap let the mover through.
46. **A path can pass closer to an obstacle corner than the mover's radius** (35 to 82 mm from water at 0.3 m radius, measured). The funnel insets each portal along itself, which is an approximation at oblique corners; the old search did the same. An arc-aware funnel is its own task and is filed, not built.
47. **Step 5, first part, built:** detail is on for 16 m flora cells within 8 m of the view and stays on out to 32 m; collision pushes a mover out of a tree only where detail is on and the tree is in the mesh; a mover a tree lands on moves forward along its route to the first clear point and keeps its task; flora rings are capped at 40,000 (about 64 MB), whole cells leaving least recently wanted first. On a pan over 86,000 trees the nav mesh peaked at 324,000 triangle slots with the cap and 689,000 without. Its benchmarks were not timed: the machine read busy.
48. **Second review of the game-side code (worker threads, ground budget, step 5 first part): no race, no break in what a query sees, no must-fix.** Three findings being fixed with tests: the forward snap judged points as if walls were open, so it could move a colonist past a wall it had not seen; a chunk holding only a blueprint could lose its kept ground; trimming flora re-routes colonists until entry 45 is wired into the game. One limit recorded, not fixed: ground dropped under entry 39 cannot be routed across until its chunk loads again, so a remembered source more than 16 chunk-unloads back reads unreachable. The old regions had the same limit at 60 m.
49. **The parent branch is merged in at 3a932b09** with no conflicts; none of its new tests fail on the one mesh.
50. **Flora is priced wherever it is in the mesh, on camera or off. This changes section 4.4 and narrows entry 45.** Section 4.4 says that where detail is off movers go straight through trees, and that flora leaving the mesh changes cost and never answers. Pricing a tree at nothing where detail is off was built and reviewed, and cannot be made exact: stored corridor widths count a tree as an obstacle whatever it costs, so a free tree beside water still shuts a gap that is open once the tree is trimmed, and trees on either side of the detail boundary made one query's answer depend on earlier queries. So a price is per kind of obstacle, constant for a query, and detail decides only what collision and the forward snap do. Consequence: off camera, a mover in ground that was once viewed routes around the trees still in the mesh and walks straight where there are none, and when the flora cap trims a cell, routes through it get straighter. Path shape off camera depends on where the camera has been; whether a goal can be reached does not, because priced flora never blocks. That is inside rule 6 as I read it, and it is the one place this epic knowingly departs from the spec as you approved it.
51. **Chunk sides carry a vertex every 64 m, not 16.** Measured in the geometry: an open chunk entering beside an open neighbour costs 58 us against 345 and adds 30 triangle slots against 126; beside water 0.9 ms against 1.1; a tree landing exactly on a shared side 180 us against 139, the one case that gets dearer.
52. **The geometry costs behind entry 38, after cutting.** The reach forest rebuild is 0.05 us per triangle slot run alone and about 0.08 beside other work, down from 0.14 to 0.17 in the same benchmark (2 to 3 times). An open chunk beside open ground went from 430 to 345 us at 16 m sides; what remains is box queries over long thin triangles. A chunk beside a neighbour with trees along the side went from 4.7 to 2.5 ms. A chunk full of shoreline is still about 1.3 us a triangle (21.6 ms for 18,000 triangles, a third of it inserting into the locate index); that is being cut next. All measured with the machine busy.
53. **Third review, of the path search fixes and pricing.** It could not build a counterexample to the score any more and found acceptance at exact length sound. It found four must-fixes in the pricing code, two of which are what led to entry 50; all four are being fixed with tests before merge.
54. **How a gap between trees is judged: a second stored width.** The first pricing code searched, per query, for the width of a gap with priced obstacles read as open, and cached the answers. In the game that made priced queries about three times slower than the version before it (4,217 us per 64 against 1,436), because every request alternates the structural and nav meshes and the cache was per thread. Now each triangle corner stores two widths, refreshed together on the patch worker: the full one, and one with detail-class obstacles read as open. A gate is two compares: passes the full width, free; fails it but passes the second, a squeeze charged at the dearest detail price; fails both, shut. The mesh is told which obstacle kinds are detail-class (trees today). What this cannot express: charging a squeeze at the cheapest price that opens it, or pricing something that is not detail-class. Cost: a lone tree add with its width refresh went from about 160 to 335 us on the worker, and flora jobs are 25 to 40% longer on average.
55. **A path could shave a priced tree's corner for free; fixed.** The fourth review traced it and a test reproduced it: two walks cut a tree's corner at zero clearance, unflagged and uncharged. A run of priced triangles now costs at least one disc diameter at the cheapest price, so the search rounds the corner instead.
56. **Routes are no longer all replanned when the mesh changes.** The AI replans a route when the nav generation moves, and after step 4 it moved on every tick that published a patch: with 20 colonists on move orders and the view sweeping, 402 replans in 600 ticks, up to all 20 in one tick. Now the generation moves only on structural changes, and when flora lands the nav system itself marks stale just the routes that come within the mover's radius of a newly landed tree. Flora leaving marks nobody. The same run: 15 replans. This is section 4.6's staleness rule, built without touching `AIDecisionSystem`.
57. **Standing ground and clear ground are two questions.** A mover inside a tree is on valid ground (it can be routed out through the tree, so the AI's stranded recovery no longer fires for it). Placement, drops, spawns, the wall tool, work targets and move-order goals still need clear ground. One line in `AIDecisionSystem.cpp`, cleared by the session that owns that file, makes a move order clicked on a tree end beside the trunk.
58. **A mover following a route that crosses trees on purpose is left alone** by collision and by the forward snap until that route ends. The exemption covers the whole route, so a mover knocked off it into another tree stays there until the route ends; accepted.
59. **`MovementSystem` spends a tick's stride along the route**, passing every corner it reaches. It used to advance one corner a frame, and routes through trees have corners 5 to 10 cm apart, so a mover overshot and stepped back a stride.
60. **The flora cap no longer costs the main thread.** Choosing what to trim walked all 40,000 rings and sorted them, 8 ms when it fired. It now takes whole cells from a queue, and flora adds and removals each touch at most two cells a tick. Main-thread worst on the long pan: 0.7 ms with the cap on or off.
61. **Open, being cut: jobs of 10 to 20 ms on a large mesh.** On the long pan (the nav mesh at 300,000 to 690,000 triangle slots) the worst jobs are 10 to 20 ms, and the main thread waited up to 15 ms on them. Two causes in the geometry: the width refresh costs more the bigger the mesh is (about 1 ms a job at 200,000 slots, 5 to 19 ms above 300,000, even for two small patches), and the triangle storage copies the whole mesh when it grows (single patches of 5 to 12 ms at 207,000, 262,000, 311,000, 466,000 and 524,000 slots). At ordinary sizes (23,000 slots) a two-cell flora job is 3 to 4 ms, most of it width refresh. Not mergeable until these are down.
62. **In-game path request cost with flora priced, unconfirmed** (the parent session's game was running): 64 requests take 2,067 us by the river and 1,554 at a chunk centre on repeat, 2,902 and 1,886 first time; the old system 516 on repeat and 3,361 first time. So a first request to a goal is faster than before and a repeat is three to four times slower. The geometry benchmark puts priced at 1.2 to 1.4 times unpriced, which would be about 900 us; the gap is unexplained until measured on a quiet machine.
63. **Two defects found by the new tests and filed, not fixed.** WOR-730 (Path search returns walks up to 30% longer than the shortest when wrapping a wall end): a random hunt against the exact referee found it on 5 of 20 layouts with no pricing involved; the cause is known and a candidate fix moved the tiling oracle by 5%, so it was reverted. WOR-729 (Stored corridor widths can miss an obstacle tip next to a corner): a 0.6 m disc is let through a 0.28 m gap between a rock tip and a tree corner; main has the same rule.
64. **Entry 61's long jobs are gone.** The width refresh now starts from the edges a patch changed and walks outward, so it costs the same on a small mesh and a large one, and the mesh's storage is paged and never copied when it grows. On the long pan the worst flora job went from 20 ms to 3.4 ms. The paged storage costs ordinary queries a few percent.
65. **WOR-730 fixed for a point, and it was an old flaw.** Where a walk wraps a wall end, the search's funnel now resumes exactly instead of restarting. Over 2,297 pairs on 300 wall-and-post layouts every walk is within 4 mm of the exact shortest path; before the fix 7 were longer, the worst by 35%. The old search, the one on main, is longer than the exact path on 633 of the same pairs, the worst by 350%. Cost: unpriced queries 4 to 8% slower. The sixth review then found the fix picks the wrong bend point for a disc when an opening between two corners is narrower than the disc; that is being settled with a test and fixed before merge.
66. **A free corridor beats a priced one at the same point in the search**, since their futures are identical. On chunk-sized meshes that brought priced requests from 2.1 to 2.4 times unpriced down to 1.5 to 1.6 in the geometry benchmark. A squeeze between trees now costs the chord a disc cuts at half the gap's width.
67. **The gate run** (2026-11-11, commit 210b09a1 with the parent merged at 49af5a21; headless; no game or build running, CPU 2 to 3%, GPU 30 to 40% from desktop apps, so the load check read busy before each pass and mostly-idle after: not a baseline by the project rule). The old column is from an earlier run on a busier machine at the commit before the rewrite.
    - Main-thread time in the nav update, worst tick, old to new: pan at 83 m/s 737 to 84 us; zoom sweep 381 to 98; a wall built 210 to 90; steady 152 to 25; twenty colonists cold 783 to 512. The mean is lower in every row but one: twenty colonists cold, 13.7 to 18.1 us.
    - Worst job by kind: a shoreline chunk's ground 6.2 ms; ground with flora 4.2; flora 2.1 to 3.4; a wall 1.0 to 1.4.
    - The wait the old system never had, worst case with a 2 ms gap between ticks: none for slow pans, walls and walking colonists; 0.1 ms panning at 83 m/s; 1.2 ms on the long pan; 2.1 ms on a cold start; 4.3 ms when a shoreline chunk's ground arrives (6.2 ms with ticks back to back).
    - Path requests per 64 with flora priced: first time 1,519 us in a chunk's middle and 3,143 beside a chunk side, old 3,361; repeat 1,249 and 2,153, old 516. Unpriced, repeat is 712 and 760.
    - Replans over a 600-tick pan with twenty colonists: 33.
68. **Resource drops wait for the patch job.** The parent branch drains its drop queue right after the sim tick, just after the nav job is launched, so the first clear-ground check on a drop frame waits for the whole job. Drops are events (a felled tree's yield, a haul with no box in reach). Draining the queue inside the tick removes the wait; that code belongs to the parent branch and is left to it.
69. **Steps 6 and 7 are not in this merge.** Step 6 (the route system out of `AIDecisionSystem`, a wandering deer) would conflict with nearly everything the parent branch changed in that file; its targeted replans were built inside the nav system instead (entry 56). Step 7 (mover footprints) has not started. The region-era members of `NavigationSystem` that `AIDecisionSystem` still calls stay until step 6.
70. **The disc case of entry 65 was real and is fixed.** It only bites where a mover crosses an opening that collapses to a point, which a priced squeeze between trees allows; around plain walls the width check already shuts such an opening. On 20,000 priced pairs at 0.3 m radius the search returned a dearer walk 213 times before the fix and 22 after. The seventh review accepted that the 22 come from the dominance rule and named the likely line; filed as WOR-735 (Priced path search returns a dearer walk on about 0.1% of pairs). The wall-end hunt now also runs at 0.3 m radius. The path-request rows of entry 67 were re-run on the final commit and every answer was identical.
71. **State at the mergeable point** (2026-11-11, commit d3e1f1d1 plus documents): steps 1 to 5 are in. All four suites pass in both build configurations (sim 910, geometry 391, world 203, assets 261). Ten independent review passes; every must-fix found is fixed with a test, and what was filed instead is WOR-721 to WOR-729 and WOR-735 under the epic. Nothing was run in the game from this branch; the in-game pass and the capture against main belong to the parent branch's integration.
72. **Entry 71 was wrong: the branch was not mergeable.** The parent branch merged it and had a reviewer read it from the AI's side. It found what ten reviews of the nav code had not, because the fault was in how the nav was asked. Reach was answered over all meshed ground, which includes everything the camera loads (five by five chunks of 512 m), and the AI took the nearest reachable source by straight line. A bush 10 m away across a river won if any loaded ground let the river be walked round, the walk could be 2 km, and panning the camera changed the choice. The river fixture in my own tests had been stretched so the land would not join round its ends, which hid exactly this. Three more: a route to a goal behind a known wall searched the whole nav mesh (measured afterwards: 94 ms at 56,000 triangle slots, 1.6 s at 505,000); the nav generation still moved when camera-only ground arrived; and ground could be dropped under a live route.
73. **A colonist's reach is local, camera-free, and measured by walking.** Reach and walking distance come from a search on the structural mesh under the colonist's belief, confined to the square of half-side 128 m around it. That is the ground its own anchor keeps loaded, so the camera cannot change it, and it is a superset of the old regions. Sources are still visited nearest first by straight line, but the shortest walk wins. Anything farther, or not walkable to inside the square, is not chosen by a colonist on its own; long-range answers are left to the geography mesh. Routes for a colonist's own choices are confined to the same square, fixed at the first plan and kept through replans. A player's move order is not confined. The path search enforces the square exactly, by clipping at its sides, so a large triangle cannot carry a walk past it.
74. **No failing search is unbounded.** A route request first walks the structural mesh under the same belief, where known walls block, and the nav search has an expansion cap. When the cap is reached the structural walk itself is the route, through the trees, so a route exists on camera exactly when it does off camera. A walled-off request costs about 0.09 ms where it cost 94 ms.
75. **What a colonist remembers about reach.** A walk, including "none", is cached per position under the content of the chunks its square meets. A route that failed after reach said yes is held under an anchor until the colonist leaves that anchor's square, so it is not retried while wandering. A chunk's version counts walls, doors and water, not ground arriving or leaving, so camera-loaded ground coming and going clears nothing. While any chunk the square meets is still without ground, nothing is remembered and a failing candidate is skipped for that decision only.
76. **The nav generation moves only for ground an anchor holds, and anchors include live routes.** Camera-only ground moves nothing and re-requests nobody. When the generation does move, routes the change does not cross are re-stamped in place. Every live route's waypoints and every waiting goal hold their chunks like a mover does, so ground is never unloaded or dropped under one.
77. **Only the viewport brings flora into the mesh** (open question 4, as the spec said; step 4 had colonists requesting it too). With entry 50 this is most of "trees do not matter off camera": off camera a tree matters only in ground the camera has shown and the cap has not yet trimmed. Clear-ground checks read tree colliders from placement data, not from the mesh, and the harvest stand point comes from the tree's collider, so both are the same on camera and off.
78. **Costs of entries 73 and 74** (headless, under load): one walking-distance check 4 us on average and 26 us at worst with no known walls, 27 us on average and 149 us at worst when the colonist knows a doorless room; one need decision 2 to 5 us cold and under 1 us with the memo warm, one walk per cold decision. Path requests now pay for the structural walk first: 64 repeat requests take 1.7 ms in a chunk's middle and 3.0 ms beside a chunk side with flora priced, where entry 67 had 1.2 and 2.2 and the old system 0.5.
79. **The reach square is removed (Kevin's rulings, rules 14 and 15). This reverses entry 73 and the square in entries 75 and 78.** Nav builds a path between any two points on meshed ground, for anyone, at any distance, and there is no colony ground or owned area. The 128 m square around a colonist that bounded its reach, its walks and its routes is gone, with the route square fixed at a task's first plan and the route kinds; a colonist's own choice and a player's move order are the same request. What stays from entry 73: reach is a walk on the structural mesh under the colonist's belief, sources are visited nearest first in a straight line, and the shortest walk wins, so a bush across a river loses to any nearer walk. What replaces the square's bound is the step budget of entries 80 and 81. Ground the camera meshed is used like any other (entry 85). `PathLimits::within`, the box the path search clipped at, stays in the geometry with its tests and is no longer used by the game; whether to keep it is open. Entry 72's count of camera-loaded ground was the `ChunkManager` default; the game loads 3 by 3 chunks round the camera and keeps 5 by 5.
80. **Reach has three answers: a walk, a definite no, or far (rule 18). This replaces entry 73's "not walkable inside the square" and the cache keys of entry 75.** Before this a search that ran out of steps read as unreachable, so a reachable target past the cap was skipped at every decision with nothing shown. Now a search from the goal toward the start runs first and a search from the start second (4.6); a goal sealed in a pocket (a room whose walls the belief knows) is a definite no at the cost of the pocket, a stop on the caller's bound on length is a definite "no walk that short", and only a stop on the step cap is far. Far is never no: a far candidate stays a candidate, is remembered like any other answer, and is taken when no candidate with a known walk is, nearest first in a straight line. A walk (a length, none, or far) is remembered per 2 m start cell (`kReachCellMm`), under the structural mesh's epoch, which moves with every structural job anywhere, and the belief's version. A route that failed where the walk said yes is still held under its anchor; the anchor's stamp reads the structural content of the chunks within twice the anchor load range of it, and each failed route also keeps the stamp of the ground between the anchor and its target, so a wall removed beside a far target clears it.
81. **The search budgets come from measurements: 2,000, 5,000 and 60,000 steps.** Measured on commit 7e68a0e2 (entry 88) with `NavScale.bench.cpp`'s `BM_SearchReach`, `BM_SearchCases`, `BM_SearchDeadEnds` and `BM_DefaultBudgets` on grounds of 1,024 chunks: open land, the Quick Start planet's chunks round its landing (rivers and lakes), open land with its middle chunk under dense placed obstacles at main's density (searched on the nav mesh, priced), and a 5 by 5 block of 10 m rooms whose walls the belief knows. A structural step costs 0.29 to 0.32 us on the main thread, 0.56 us at worst; a priced nav step in dense cover 0.45 us, 1.48 us at worst. Open land costs almost nothing in steps (a 10 km walk is 190). The limits chosen: a reach walk at most 4 ms at worst, since several can run in one decision and must not hitch a 16.7 ms frame, and a far route request at most 35 ms at worst, since it runs once per far target.
    - The goal-side probe, `kReachProbeExpansions`, stays at 2,000: 0.6 ms, 1.1 ms at worst. Every dead end measured answers inside it: a sealed room in 2 steps, the rooms of the block in 67 to 482, an islet in none (the reach forest rejects it before any search).
    - The start-side search, `kReachExpansionCap`, drops from 20,000 to 5,000: 1.6 ms, 2.8 ms at worst, and 2.2 / 3.9 ms with the probe. On real ground it still reaches 1.1 km at the median; a goal past it is far, and the route request settles it. The worst start on real ground reaches only 110 m, a goal across water with a long way round. Dense cover never needed a budget of its own: priced searches stayed under 7,100 steps to 10 km.
    - A far goal's route, `kFarRouteExpansions`: 60,000 structural steps, 19 ms, 34 ms at worst. Every real far goal measured to 10 km needed 25,000 to 41,006, so about 1.5 times headroom. A far goal with no way at all costs the whole 60,000 once per target.
82. **A far goal is routed by the structural walk.** An earlier version gave a far goal its own nav search of 20,000 steps after the reach walk gave up. On the real Quick Start ground it routed none of 18 goals between 1 and 10 km, where the structural search run to completion found a walk for all 18 in 25,000 to 41,000 steps and 8 to 17 ms. With those budgets about 3% of real goals at 1 km, 9% at 2 km, 13% at 5 km and 20% at 10 km read "search gave up" though a walk existed. So a far goal gets no nav search: the structural search runs from the start to 60,000 steps and its walk is the route, through detail at its price, replanned as the mover goes. Where reach found a walk, entry 74 stands: the nav search is capped at 4,000 plus 16 for each step the walk took, and past that the walk is the route. The far request runs on the main thread, 19 to 34 ms once per far target; Kevin accepted that for now (rule 19), and WOR-789 (Far route requests stall the main thread for 19 to 34 ms; run them on a worker) is the follow-up.
83. **"Search gave up" is its own status (rule 18).** When the far route search also runs out of steps, `requestPath` answers `gaveUp`, never "no route", and the decision trace ("gave up searching for a way there") and the task list show it apart from "no source". The colonist keeps a record of it per target: where the search started, the target, the stamp of the structural content in the box spanning them (grown by the anchor load range), and the belief's version. While it holds the target is not tried again. It clears when that stamp or the belief changes, or when the colonist has moved the larger of a quarter of the straight line to the target and 128 m (`kGaveUpMoveFloorMeters`) from where the search started, so a colonist that knows only far targets with no way to them tries each once, not once a step. Known limit, recorded on `GaveUpRoute`: a way that opens outside the box, or ground that only arrives, doesn't clear it.
84. **Ground a mover has ever held is never dropped; only ground the camera alone held is shed (rule 16). This reverses entry 39, removes the limit entry 48 recorded, and widens entry 76.** Every chunk an anchor has held (a mover, the rest of a live route, the goal of a task waiting for ground) keeps its ground in both meshes for the life of the world. Of unloaded chunks whose ground only the camera ever held, 16 (`kCameraOnlyUnloadedChunks`) stay meshed; past that the one unloaded longest ago that holds no construction loses its ground, one a tick, on a tick no ground arrives. Measured cost (entry 88): real ground is 100 to 152 KB a chunk on the structural mesh and 87 to 130 KB on the nav mesh, both together 4.7 MB at 25 chunks, 29 MB at 121, 130 MB at 500 and 265 MB at 1,000; open ground is about 40 and 30 KB. WOR-728 (Kept nav ground ignores crafting stations, storage and loose piles) now applies only to ground the camera alone held.
85. **Mesh existing is not ground being visible (rule 17).** Ground the camera meshes, for placement and other player interaction, joins the mesh like any other, so a walk or a route can take a way the camera brought in, and some movers mesh ground without revealing it. Nothing may read "has mesh" as "the player can see or act here". Camera-meshed ground is safe to route over once player visibility, WOR-754 (Player visibility: unexplored areas (visible, seen, unseen)), bounds the camera to ground colonists have seen. It is planned, not built, so until then the camera can mesh ground no colonist has seen.
86. **Nav, mesh and collision code name obstacles by role, not by kind (rule 20; commit c3388b05).** An obstacle from a placed entity's collider is a placed obstacle; the class the view brings in and the search prices is detail. `FloraCollider.h` became `PlacedObstacle.h`, and the single price became a table of detail kinds with one row (placed obstacles at `kDetailPassMultiplier`, 50), which the mesh's detail list, the price table and every "is this detail" check read. No behaviour change.
87. **A mover whose route failed while ground near it was still arriving is woken when the ground lands.** Entry 75 stands: while any chunk within the anchor load range of a colonist is without ground, a failure is not remembered and carries the unsettled stamp. Instead of the colonist retrying on its decision interval, the update whose join lands ground in such a chunk, once every one of them is meshed, asks it to decide (`requestDecision`, the one way to make a colonist decide at the AI's next update). Only a task still standing on that failure is woken, and `Task::clear` resets the stamp and the gave-up flag, so a stale stamp cannot keep waking it. A task waiting for its goal's ground needs no wake: that ground landing moves the generation, and the AI retries on that.
88. **The second measurement run** (2026-11-11, commit 7e68a0e2, RelWithDebInfo, headless, `sim-benchmarks` with main's placement density on the Quick Start planet's chunks; one iteration a row). No game, build or asset-manager ran and the CPU read 2 to 5%. The load script read the GPU, idling at its lowest clock, as busy, which is WOR-790 (machine-load.mjs reads an idle GPU as busy); Kevin ruled these numbers baselines. Entry 81 has the search costs. The rest:
    - Detail memory: a dense chunk fully viewed is 21,025 rings and 128 MB of nav mesh, about 6.4 KB a ring, a 792 ms patch done cell by cell with the worst cell 1.37 ms. So `kDetailRingBudget` (40,000 rings) is about 250 MB, about half a square kilometre, not the 64 MB entry 47 estimated.
    - The structural job breaks the 2 ms job budget on real ground past about 121 meshed chunks: a wall edit's forest rebuild is 0.78 ms at 25 chunks, 2.2 at 121, 8.9 at 500 and 14.7 at 1,000; a chunk's ground arriving costs 2.1 ms in forests at 121 chunks and 15 ms at 1,000. That is WOR-722 (Reach forests are rebuilt whole on every structural change).
    - Main's placement density doubles job and path cost against the density entry 67 measured, and every job still runs: job mean 1.4 to 2.9 ms against 0.8 to 1.6, worst 4.6 to 6.3 ms against 1.1 to 4.3; a priced `requestPath` 133 us against 40, with 2.1 times the nodes. Detail is fully in 20 ticks after a cold nine-chunk start, against 15.
    - Reach while ground lands (`BM_ReachWhileGroundLands`: 20 colonists with 40 sources each, deciding every 30 ticks, a far wall toggling every 5): with sources on both sides of a river and in walled rooms, 4 us a tick at the mean and 21 us at p99; with none reachable, 61 and 278 us. Every walk was answered from the goal side, and walking colonists cost the same as still ones. These supersede entry 78's costs.
89. **What is left open.** Steps 6 and 7 are not built: WOR-709 (Step 6: Movers (route system out of AIDecisionSystem, a wandering deer)) and WOR-710 (Step 7: Mover footprints in the mesh). Travelling toward a goal whose ground is not meshed is deferred by Kevin (rule 21): WOR-776 (Nav: build a path toward a goal whose ground is not meshed yet); today such a goal waits while its chunk is loaded and reads unreachable otherwise. When the way is blocked with no known way round, WOR-775 (Exploring: a colonist blocked on the way to a remembered place looks for a way round) takes over. Far requests on a worker are WOR-789. Nothing from entries 79 to 88 has been run in the game.
90. **The design is settled: rules 22 to 30 (section 1).** Only movers generate mesh, and the camera has no effect on it. A mover generates the full real mesh around it, every object movers cannot path through included, and such an object blocks. The mesh is patched in place and never unloads. Nav is any place to any place. A mover whose goal has no mesh heads that way and meshes as it goes. The meshed area is larger than the visible area. All movers share one movement system, and mesh, nav and collision code never names a kind of obstacle. This reverses:
    - Rules 5, 6, 7 and 16, and rule 8's trigger (the view); rule 21's deferral now has a design, rule 26.
    - The camera-detail entries: 47 (detail on near the view, collision only there), 77 (only the viewport brings placed obstacles in), 58 (a route through detail on purpose is exempt from collision and the snap, which follows from pricing), and the defaults for open questions 1, 3 and 4 below the log.
    - Entries 45 and 50 (priced, not walled): an object movers cannot path through blocks.
    - Entry 84, as far as it sheds ground the camera alone held, and the detail cap of entries 47, 60 and 88, which trims placed obstacles out of the mesh: under rule 24 no mesh unloads.
    - Entry 85, which presented camera-meshed ground as safe once player visibility bounds the camera: the camera meshes nothing.
    - Entry 86's definition of detail as "the class the view brings in" (its naming rule stands, as rule 29).
91. **The code does not match rules 22 to 24 yet.** Section 1.1 lists the three departures: the camera shapes the mesh, WOR-796 (Nav: the camera must have no effect on the mesh); movers mesh only structure, WOR-797 (Nav: movers generate the full real mesh around them, placed obstacles included); and placed obstacles are priced at 50, never impassable, WOR-798 (Nav: objects movers cannot path through block in the mesh instead of being priced). Sections 2, 4.1, 4.2, 4.4, 4.5, 4.7 and 4.8 label what is built.
92. **What changed in code after entry 89.** Each checked against `NavigationSystem.{h,cpp}` and `AIDecisionSystem.cpp` at this commit.
    - The near nav search's cap is `min(kNavExpansionFloor + kNavExpansionsPerStructural x (steps of the structural search that found the walk), kNavExpansionCeiling)`, that is `min(11,000 + 16 x steps, 20,000)`. This replaces the "4,000 plus 16" of entry 82. The floor is the dense-cover worst measured (7,090 steps at 2 km, 5,938 at 500 m) with the far budget's 1.5 times headroom; the ceiling is the accepted 34 ms less both reach searches at worst (3.9 ms), about 20,300 priced steps at 1.48 us.
    - A route request runs the structural walk once: the goal-side probe, then the start-side search carried on to `kFarRouteExpansions` (60,000) in the same pass. A walk found past `kReachExpansionCap` (5,000) start-side steps is far and is the route. Entry 82's "the structural search runs again from the start" is replaced.
    - A route taken from the structural walk (a far route, or a near search that fell back to the walk) is re-priced in place when detail lands on it, never marked stale, since the structural mesh holds no detail and a new request would find the same walk. This narrows entry 56.
    - `PathLimits::within`, the box limit, is deleted from the geometry; the question entry 79 left open is closed.
    - The wake of entry 87 runs in the AI's update (`AIDecisionSystem::wakeSettledFailures`), on what nav reports as landed (`groundLandedThisUpdate`, `reachSettledThisUpdate`); nav no longer asks a colonist to decide. Who is woken is unchanged.
93. **One full-detail mesh, meshed per mover by sight, that only grows (Kevin's rulings; rules 31 to 35).** The nav mesh is one mesh at full detail, with no coarse or structural layer, no second mesh, and no resolution levels. Each mover adds everything within its own sight distance plus a buffer, a distance per mover and not a constant. The mesh only grows: a chunk unloading does nothing to it, and nothing caps or trims it. The camera has no effect on it. Placement needs no rule of its own, because the player can only place in the viewable area, which is smaller than the meshed area. Not covered by the rulings and open under WOR-799 (Nav: one full-detail mesh with no coarse layer), rule 36: how reach and "is there a way" are answered on one mesh, what the search budgets become, how large the buffer is, and how a route is planned to a goal beyond meshed ground. The static geography mesh (rule 12) is a separate thing and unaffected. This reverses:
    - Rule 1's varying resolution, and section 4.2's structural layer as the spec first set it out on 2026-11-10, with the reasoning there that reachability needs a layer the camera and footprints cannot touch.
    - The entries that built on the structural layer: 13 (its forests rebuild whole), 19 (the heuristic planned on it), 26 (reach failures keyed on structural patches), 34 (structural ground kept so its forests grow), 35 (two workers, one per mesh), 73 and 79 (reach as a walk on the structural mesh), 74 (the structural walk first, and as the route when the nav search's cap runs out), 80 (the structural epoch under remembered walks), 81 (the 2,000, 5,000 and 60,000 budgets, measured in structural steps), 82 (a far goal routed by the structural walk), 83 (the gave-up record's stamp of structural content), and 92 (the near search's cap scaled by structural steps, the single-pass structural walk, and routes from it re-priced in place). Each stays as a record of what is built (1.1).
    - Entries 90 and 91 where they let the structural mesh stand: entry 90's reversals kept reach on it, and entry 91 listed three departures without it.
    - Entry 84 wherever it implies ground can be dropped: no ground is, whoever caused it. Its "never dropped" part stands as part of rule 33. Entry 90 had already reversed its shedding of camera-only ground.
    - Entry 14 and the default for question 5 (the chunks within 128 m of a mover) as the area a mover meshes: rule 32 makes that each mover's sight distance plus a buffer. Entry 14 stands for chunk loading.
    - Rule 24's "small" as said of the whole mesh: it holds for ground, not with placed obstacles in it (entry 95).
94. **The code departs from the design in four places now (section 1.1).** WOR-796 (Nav: the camera must have no effect on the mesh), as entry 91 said. WOR-797 (Nav: movers generate the full real mesh around them, placed obstacles included) now also covers the meshed area being each mover's sight distance plus a buffer, where as built every anchor meshes the chunks within a fixed 128 m; and nothing being removed, where as built three things remove mesh: ground only the camera held is shed past `kCameraOnlyUnloadedChunks` (16) unloaded chunks, a chunk's placed obstacles leave the nav mesh when it unloads, and past `kDetailRingBudget` (40,000 placed obstacles) whole cells are trimmed. WOR-798 (Nav: objects movers cannot path through block in the mesh instead of being priced), as entry 91 said. New: WOR-799 (Nav: one full-detail mesh with no coarse layer). As built there are two meshes, and reach, the reach forests, the goal-side probe, the far route taken from the structural walk, the near search's cap, the structural epoch, and the 2,000 / 5,000 / 60,000 budgets are built on the structural one, with placement buildability and the landing search. Sections 2, 3, 4.1 to 4.6, 4.8, 4.10, 5 and 6 to 8 label what is built.
95. **What a placed obstacle costs in the mesh (measured 2026-11-11; `BM_PlacedObstacleBytes`, `BM_PlacedObstacleSystemBytes` and `BM_PlacedObstacleDensity` in `NavigationSystem.bench.cpp`).** On the real planet around the landing, placed obstacles run at 0.060 to 0.068 per square metre, 75 to 85% of the 0.0802 worst case. A placed obstacle costs about 7.2 KB in the mesh as built (8 triangles, 4 vertices; the triangle box grid is 29% of it, the triangle records 21%), so meshed ground with its placed obstacles is about 435 to 490 MB per square kilometre, against about 1 MB for ground alone. Entry 88 measured about 6.4 KB a ring on its dense chunk; this is the later, finer measurement. A field-by-field accounting puts it at about 1.25 KB with mechanical changes to the records and no design change, about 75 to 85 MB per square kilometre. Since the mesh only grows, bringing the per-obstacle cost down is part of the correction (rule 37). Kevin named paging unused parts of a very large mesh out to disk as a possible later follow-up, not part of the design today: WOR-800 (Nav: page unused parts of a very large mesh out to disk (later, maybe)). Paged mesh would come back unchanged; nothing about it is designed here.
96. **The geography mesh stays planned, and the real mesh is a detail view of it (Kevin's ruling; rule 38).** The separately planned geography mesh (also called the regional mesh) is much less detailed than the real mesh, the nav mesh this document specifies. The real mesh is a detail view of it, as game tiles are a detail view of world tiles; the two must be aligned; one is derived from the other and then filled in. It is not built, and it is not the coarse layer entry 93 removes. Which is derived from which in implementation terms, what alignment requires of vertices or edges, and what the geography mesh answers are not settled by the ruling. Rule 12 and section 4.10 stand, with the names.

**Defaults for the open questions in section 8**, applied when the step that needs each is
reached, each isolated as one constant or one function so it can be changed in one place:

- **Flora that seals (question 1):** a mover with no way around walks through flora at a high path cost. The alternative makes answers differ on and off camera, which rule 6 forbids.
- **A mover in the way (question 2):** the blocked mover waits and replans when the footprint moves; after a bounded wait it treats footprints on its route as passable at a high cost, so two movers in a corridor cannot deadlock. No "ask the blocker to move" behaviour is built.
- **Footprints off camera (question 3):** they do not block where detail is off, the same as flora. Collision still separates movers.
- **Detail around off-camera working colonists (question 4):** none. Only the viewport requests detail in this epic.
- **Load range around a mover (question 5):** the chunks within 128 m of it.
