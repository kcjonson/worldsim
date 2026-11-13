# Nav mesh: one mesh, variable resolution

**Status:** In progress. Steps 1 to 5 built 2026-11-11 (section 10, entries 1 to 98). The correction that brings the code to the design built 2026-11-12 (entries 99 to 113). The mesh as the union of movers' visibility limits, ending at its edge, built 2026-11-13 (entry 116; rules 39 to 42), on five stacked pull requests not yet merged. Steps 6 and 7 (section 7) are not built. The design is section 1, rules 22 to 42; section 1.1 lists where the code still departs from it. Sections 2 and 4 describe the code as it stands; section 10 is the record of how it got there.
**Epic:** WOR-702 (Nav mesh: one mesh, variable resolution)
**Created:** 2026-11-10
**Replaces:** the "As built: multi-region sim area" section of [pathfinding-architecture.md](./pathfinding-architecture.md), and its rule that agents are never mesh obstacles (risk 7)
**Fixes:** WOR-644 (Pathing can't route between two points on connected ground when nav regions overlap), WOR-645 (Chunk streaming follows the camera, so panning away unloads the colony's chunks)

Before this epic the nav mesh was a handful of separate rectangles, each rebuilt whole when
anything in it changed. This document replaces them with one persistent mesh at full detail
that is never regenerated. An obstacle that appears, disappears, or moves patches the triangles
around it and leaves the rest alone. Movers generate the mesh, in full, within their sight
distance plus a buffer, and it only grows; the camera has no effect on it (section 1). The
title is the epic's name: the mesh has no resolution levels (rule 31). The mesh is the union of
what movers have seen, each mover's visibility limit a polygon round where he stands, and it ends
at that union's edge (rules 39 to 42, section 4.4). Route searches run on a worker under a fixed
amount of work per tick and carry on across ticks (4.6).

Numbers marked *measured* come from `geometry-benchmarks` or `sim-benchmarks` on this branch
(RelWithDebInfo, the dev machine). Numbers marked *estimate* are gated on a measurement named
beside them. Numbers measured on mechanisms the correction deleted (the structural mesh, prices,
step budgets, the placed-obstacle cap) are marked *historical*.

---

## 1. Intent

Rules 22 to 38 are the design, settled 2026-11-11 (rules 31 to 38 later that day), with rules 39
to 42 of 2026-11-12 on where the mesh ends, and everything below is judged against them. The earlier rules follow; each one a later rule
replaces is marked, and the rest still stand.

### The design (rules 22 to 38)

22. Only movers generate mesh. The camera has no effect on the mesh: it cannot reveal unexplored area, so it has no reason to bring ground or obstacles in.
23. A mover generates the full real mesh around it as it moves: ground, water, walls, doors, and every object movers cannot path through, of which there will be many. There is no lesser tier of mesh where nobody is looking. (How far around it is rule 32.)
24. The mesh is one entity, patched in place, never destroyed and regenerated, and it never unloads. (Rule 33 states the second half exactly: the mesh only grows. The rule as first given called the mesh small; measured, ground alone is about 1 MB a square kilometre, but with its placed obstacles meshed ground is about 435 to 490 MB a square kilometre as built, about 7.2 KB a placed obstacle (5.8 KB measured since, section 6), and about 75 to 85 MB once the records are slimmed under WOR-801 (Nav: cut the mesh cost of a placed obstacle from about 7.2 KB to about 1.25 KB). Section 6 has the measurements.)
25. Nav is any place to any place, with no distance limit. There is no colony ground and no owned area.
26. A mover whose destination has no mesh heads in that direction and generates the mesh as it goes. Meeting an obstacle it cannot pass on the way is a future exploring task, WOR-775 (Exploring: a colonist blocked on the way to a remembered place looks for a way round), and is not needed for the current merge. Until the geography mesh exists (rule 38), "pathing doesn't use a mesh at that distance / in that circumstance" (Kevin, 2026-11-12): how such a route is planned is WOR-776 (Nav: build a path toward a goal whose ground is not meshed yet), and as built such a goal reads "no known way" (4.10).
27. The meshed area is larger than the visible area. Some movers (approaching enemies) generate mesh without revealing anything, so the area the player sees is a subset of the meshed area. The player builds and interacts only on visible terrain, which a mover has seen and so meshed. "Has mesh" never stands for "the player can see or act here". Player visibility is built (WOR-754, Player visibility: unexplored areas (visible, seen, unseen); [player-visibility.md](./player-visibility.md)): the player places, draws, selects and orders only onto seen ground, a record of colonists' own sight kept apart from the mesh. (Restated for placement by rule 35.)
28. All movers use the same movement system, from enemies to small animals.
29. Mesh, nav, and collision code is generic. It never names one kind of obstacle.
30. Rules 22 and 23 replace the earlier model, in which the camera decided detail, small placed obstacles were ignored off camera, and a forward snap handled detail arriving with the view. The snap rule itself stands wherever an obstacle lands on a mover: the mover moves forward along its route, and never stalls or loses progress.
31. The nav mesh is one mesh at full detail. There is no coarse or structural layer, no second mesh, and no resolution levels.
32. Each mover adds to the mesh everything within its own sight distance plus a buffer: ground, water, walls, doors, and every object movers cannot path through. The distance is per mover, taken from its sight distance; it is not a fixed constant. Ground is patched in a whole chunk at a time, as today, and that stays as it is for now; sight distance plus a buffer governs everything else a mover adds. (The chunk-at-a-time half is replaced by rule 39: ground too is only what movers have seen.)
33. The mesh only grows. Meshed ground never leaves it: a chunk unloading does nothing to the mesh, and there is no cap and no trim. The one thing that changes what is in it is a change in the world (an obstacle felled, built, or moved), which patches the mesh in place (rule 24).
34. The camera has no effect on the mesh. (Restates rule 22.)
35. Placement needs no rule of its own. The player can only place in the viewable area, which is smaller than the meshed area. (Restates rule 27 for placement. Its premise holds now that player visibility is built: the player sees only ground a colonist has seen. Placement and drawing are refused on unseen ground, which the player can still pan to.)
36. Settled 2026-11-12 by the correction (entries 99 to 113), except the last item. It was open under WOR-799 (Nav: one full-detail mesh with no coarse layer):
    - How reach and "is there a way" are answered on one mesh: component labels (`connected`) answer whether two places are joined by meshed ground, and only a route request answers for a given mover's width and belief (4.6).
    - How large the search time budget of rule 18 is and how a search is resumed: a fixed count of search work a tick, sized from a time target, shared by every request; a search carries on across ticks with no limit of its own, and restarts when a patch changes what it read (4.6).
    - Whether answers depend on the machine: no; given the same chunk arrival ticks every answer lands on the same tick (4.8).
    - How large the buffer of rule 32 is: one 16 m cell (4.4). (Replaced by entry 116: `kSightBufferMeters` keeps its value, now set by what it must cover, 4.11; there are no cells.)
    - A target cut off from the start by ground no mover has meshed reads "no known way" until meshed ground joins them (4.10).
    - Mover footprints and labels: a footprint's tag reads open for labels, so a footprint never splits a component (WOR-710 (Step 7: Mover footprints in the mesh) builds the footprints).
    - Still open: how a route is planned to a goal beyond meshed ground. Rule 26 says the mover heads that way and meshes as it goes, not how the route is planned; WOR-776 (Nav: build a path toward a goal whose ground is not meshed yet).
37. Since the mesh only grows, its cost per object movers cannot path through is part of bringing the code to the design (section 6). Paging unused parts of a very large mesh out to disk is a possible later follow-up and not part of the design: WOR-800 (Nav: page unused parts of a very large mesh out to disk (later, maybe)). Mesh paged out would come back unchanged.
38. The geography mesh (also called the regional mesh) is Tier 1 of [pathfinding-architecture.md](./pathfinding-architecture.md), a separate mesh from the nav mesh, here called the real mesh where the two are contrasted. It is computed when the world loads and always exists. It is much less detailed than the real mesh. The real mesh is a detail view of the geography mesh, as game tiles are a detail view of world tiles. The two must be aligned. One is derived from the other and then filled in. What it is built from, its resolution, which mesh is derived from which in implementation terms, how alignment works, and what the geography mesh answers are to be designed under WOR-802 (Geography mesh (Tier 1): computed at load, always exists). It is not the coarse layer that rule 31 removes. As built it does not exist (1.1). It is built later, as its own work under WOR-802, after the nav correction, because long-distance travel is not being done yet; until then the real mesh answers only for ground movers have meshed.

### The seen area (rules 39 to 42, 2026-11-12)

39. The mesh is the union of what movers have seen, terrain included, and it ends at that union's edge. Outside it is impassable: no triangles, no links to chunk edges or far points. (Replaces the chunk-at-a-time half of rule 32.)
40. No arbitrary points. Every vertex comes from something in the game, of three kinds: a corner of a placed or built object (a placed obstacle, a wall, a door), a point of terrain (a water outline), or a corner of a visibility limit, plus the points where two such outlines cross. Chunk corners, chunk-side cut points, and kept loose points are gone.
41. A mover's visibility limit is a node type of its own: a polygon round where he stands, holding the disc of his sight distance plus a buffer. The objects in it are found by entity, with no concept of a cell. The step between limits is one tunable constant. Known-world nodes (the geography mesh) come later (4.12).
42. The landing meshes a large area round the drop before any colonist stands there, its radius one tunable constant: the crew saw it as they came down. It is the one exception to rule 22.

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
12. The static geography mesh (Tier 1 in the pathfinding doc) is a separate thing, and its less detailed mesh stays unbuilt. It is not the nav mesh. (Restated and extended by rule 38, which replaces "stays unbuilt": the geography mesh is computed when the world loads and always exists.)
13. Nav is expected to be the most expensive thing the game computes. Every step is measured against main. (Narrowed by Kevin, 2026-11-12: a step is measured, and compared against main only when its own numbers look bad; with good numbers for the new approach the old one needs no comparison.)

The rulings of the morning of 2026-11-11, which narrowed the rules above (section 10, entries 79 to 89):

14. Nav's job is to build a path to given coordinates: any place to any place, with no distance limit. Nav does not know who asks or why.
15. There is no colony ground and no owned area.
16. Mesh is made for movers (under them, along their routes, at their goals) and under the camera (for placement validity and other player interaction). It is small, and mesh a mover has caused is never unloaded. (Replaced by rules 22, 23, 24 and 27, and by 32 and 33: only movers make mesh, each within its sight distance plus a buffer, the player acts only on ground a mover has seen and so meshed, and the mesh only grows.)
17. Mesh existing is not ground being visible. Some movers mesh ground without revealing it, so "has mesh" never stands for "the player can see or act here". Player visibility (unexplored areas hidden from the player, revealed only by what colonists see) is built: WOR-754 (Player visibility: unexplored areas (visible, seen, unseen)). (Restated by rule 27.)
18. Compute sets limits, and some places cannot be reached, but running out of search budget means "didn't finish", never "can't reach". The bound on a search is a time budget, not a step limit: a search is given time per frame, and when a frame's time is spent it carries on later, on a later frame or on a nav worker. The budget decides how long a frame spends on a search and never what the answer is. The size of the time budget comes from measurement. This concerns searches; the per-tick patch budget (section 4.4) is a count, a different budget. (The form Kevin accepted on 2026-11-12, "The time budget above sounds fine to me": work per tick sized from a time target, the search carrying on next tick with no limit of its own, the same answer on any machine. How it is built: a search has no limit of its own and does a fixed amount of work per tick, `kRouteWorkPerSlice` (18,000) expansions a slice in `RouteQueue.h`, derived from 8 ms of route worker a tick at an estimated 0.45 us an expansion and shared `kRouteQuantum` (2,000) at a time, carrying on across ticks until it finds a goal or has explored all the mover can reach (4.6). Rule 36 records the rest as settled.)
19. A far route may stall the main thread for 19 to 34 ms once per far target, for now. Moving far requests to a worker is WOR-789 (Far route requests stall the main thread for 19 to 34 ms; run them on a worker). (Replaced by rules 18 and 31: there is no far route, and every route search runs on a worker under the time budget, 4.6.)
20. Nav and mesh code never names trees or flora. Obstacle kinds are data: "placed obstacle" (a placed entity's collider) and "detail" (once the class the view brought in and the search priced; both are gone). The "After" column in section 2 and sections 4.1, 4.2, 4.4 to 4.6, 4.8 and 4.10, rewritten on 2026-11-11, follow the same rule; older text, the measurements, and log entries 1 to 78 still say "tree" and "flora". (Restated by rule 29.)
21. Travelling toward a goal whose ground is not meshed is deferred: WOR-776 (Nav: build a path toward a goal whose ground is not meshed yet). (The design is rule 26, which that ticket builds.)

### 1.1 Where the code does not match the design yet

The correction of 2026-11-12 (section 10, entries 99 to 113) brought the code to rules 18 and 22
to 34, and the seen-area work of 2026-11-13 (entry 116) to rules 39 to 42. As built now:

- The camera has no effect on the mesh. It still loads chunks to draw them (3 by 3 around it in the game, kept to 5 by 5), but only a mover's visibility limit (or the landing's) brings ground and placed obstacles in, so a chunk only the camera loaded gets nothing (4.4). Collision and the forward snap act wherever a placed obstacle is in the mesh, on every mover.
- Ground is the union of visibility limits, each a polygon holding the disc of a mover's sight distance plus `kSightBufferMeters`, and the mesh ends at its edge. Placed obstacles are found by entity in each limit. Each mover holds chunks by the reach of his limit (4.4, 4.5).
- Nothing leaves the mesh but what leaves the world: a placed obstacle removed, a wall or door taken down, water re-derived. No chunk unload, cap, or trim takes anything out (4.1).
- Placed obstacles block. No search is given prices (4.2).
- There is one mesh at full detail and one patch job, with no coarse or structural layer (4.1, 4.8).
- Searches are bounded by work per tick, never a step limit. A route search has no limit of its own and carries on across ticks until it finds a goal or has explored all the mover can reach (rule 18, 4.6).

That is what WOR-796 (Nav: the camera must have no effect on the mesh), WOR-797 (Nav: movers
generate the full real mesh around them, placed obstacles included), WOR-798 (Nav: objects movers
cannot path through block in the mesh instead of being priced) and WOR-799 (Nav: one full-detail
mesh with no coarse layer) ask for. The code matches them now; the in-game pass on the correction
is what remains before they close.

Still open:

- **Memory per placed obstacle.** WOR-801 (Nav: cut the mesh cost of a placed obstacle from about 7.2 KB to about 1.25 KB) is not done. A placed obstacle cost about 5.8 KB in the mesh when last measured (5,803 B, 2026-11-12, by `BM_PlacedObstacleBytes`, a bench since deleted; the 7.2 KB below was measured earlier, entry 95), so meshed dense ground is about 435 to 490 MB per square kilometre, and the mesh only grows (rule 37, section 6).
- **The seen-area mesh: built, with known limits.** Chunk squares are gone, so chunk borders no longer exist in the mesh (entry 116). Its known limits are in 4.11: urgent work has no per-tick ceiling, a budgeted limit in dense ground is over the job budget, open ground with no placed objects has no interior vertices (an owner decision is open), water is still cut at chunk sides, walls do not clip a limit, and crafting stations and storage do not block movement.
- **Path query start edge.** The path query does not gate the edge between the start and goal triangles when they are adjacent, so a disc slightly wider than a waist can cross it when it starts in the triangle beside it. Found on the way (entry 116), on main before this work, filed as a bug, not fixed.
- **Bench fixes.** WOR-808 (bench fixes after the geometry clean-up) is not done.
- **The geography mesh.** WOR-802 (Geography mesh (Tier 1): computed at load, always exists) comes later: long-distance travel is not being done yet. Until it exists the real mesh answers only for ground movers have meshed (rule 38).
- **Goals beyond meshed ground.** WOR-776 (Nav: build a path toward a goal whose ground is not meshed yet). A route to a goal off meshed ground, or cut off from the start by ground no mover has meshed, answers "no known way" at once, unless the goal lies in a limit on its way in, which answers `AwaitingMesh` (4.6, 4.10).
- **Player visibility.** WOR-754 (Player visibility: unexplored areas (visible, seen, unseen)) is built. The player can still pan to ground no colonist has seen, where it is black. A move order onto unseen ground walks the colonist to the edge of seen ground on the way to the click, and a placement or drawing there is refused (rules 27 and 35).
- **Mover footprints.** WOR-710 (Step 7: Mover footprints in the mesh) is not built. Labels can already read a footprint tag as open (`MeshComponentsArgs::openTags`), so a mover in a doorway need never split a component; what a blocked mover does is question 2 (section 8).
- **Queries outside the sim tick wait on the route slice.** A nav query made outside the tick (the build tool's cursor, placement, the nav overlay) joins the patch job, which waits for the tick's route slice, so it can wait up to a slice (about 8 ms) longer while a search runs. WOR-804 (queries outside the tick waiting on the route slice).

**No longer arises: a route search that cannot finish.** Kevin allowed, for now, a route search at
its step limit to list its target as impossible. There is no step limit: a search ends only by
finding a goal or by exploring everything the mover can reach, which answers "no known way"
(4.6), so that edge case needs no mechanism.

Follow-ups filed before the correction: WOR-795 (A colonist on a far route walks straight through
placed obstacles on screen) and WOR-789 (Far route requests stall the main thread for 19 to 34 ms;
run them on a worker) concern the far route, which is deleted; WOR-794 (A colonist with a critical
need and a known fix is left idle when the route search to it gives up) concerns a search giving
up, which no search does now; WOR-791 (Nav: a goal on meshed ground cut off from the start by
unmeshed ground reads as a definite no) reads "no known way", never a statement about the world,
until WOR-776. None is closed by this document. When the way is blocked with no known way round,
WOR-775 (Exploring: a colonist blocked on the way to a remembered place looks for a way round)
takes over.

## 2. Current state

| Area | Before this epic | As built |
|---|---|---|
| Storage | A list of rectangles: a 60 m square per colonist, one for the viewport, overlapping ones merged, re-squared, clamped to 128 m. Each has its own complete mesh | One persistent mesh at full detail over the union of movers' visibility limits, ending at its edge; it only grows (rules 31 to 33, 39) |
| Change | Any trigger rebuilds a whole rectangle. A wall edit or a felled tree anywhere rebuilds every rectangle (global counters) | The triangles around a changed obstacle are patched, nothing else |
| Repeated work | Built rectangles overlap; a recenter re-meshes mostly unchanged ground; the placement mesh is a third copy | An obstacle enters the mesh once and leaves it once |
| Connectivity | A path needs both ends in one rectangle. Another rectangle reads unreachable, no rectangle holds | Paths cross all meshed ground; component labels answer "is there a known way" with no search (4.6) |
| Placed obstacles | Every tree, everywhere a rectangle exists | Every placed obstacle within a mover's sight distance plus a buffer enters the mesh, blocks, and stays (rules 23, 32 and 33) |
| Camera | Drives a rectangle like a colonist does, so it reshapes and can stall colonist coverage | Loads chunks to draw them; no effect on the mesh (rule 22) |
| Movers | Colonists only, and not in the mesh | Every `Position` + `AgentRadius` entity meshes, holds chunks, and is snapped and collided with. Footprints in the mesh are WOR-710 (Step 7: Mover footprints in the mesh) |
| Chunks | Loaded 1 chunk around the camera, unloaded at 2 | Also loaded and held around every mover, by its own range (4.5) |
| Threads | Input gathered on the main thread in the frame, mesh on one `std::async` thread per build | Edits gathered on the main thread under per-tick count budgets and applied on one patch worker after every system has run; route searches on a route worker, one slice a tick (4.8) |

### 2.1 Measured: what a build costs

`buildNavMesh` on real inputs dumped from the quickstart game (128 m square regions). Median of 5.

| Input | Trees | Triangles | Build |
|---|---|---|---|
| manyObstaclesDump | 512 | 4,418 | 152 ms |
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
the reachability forests (since deleted) together are 5 to 20 ms.

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
| manyObstaclesDump | 152 ms | 11.7 ms |
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
classification, widths, the locate grid, and the forests (since deleted). Area still scales worse than linear
(16 times the input takes 30 times as long), and that is in those later stages. It gets
profiled before step 4 relies on meshing a whole chunk at once.

### 2.2 Not measured yet

Historical: these were planned against the region system the epic replaced, and are kept for the
record. They needed the running game, one launch (section 6):

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
| Unreal navigation invokers ([docs](https://dev.epicgames.com/documentation/unreal-engine/using-navigation-invokers-in-unreal-engine)) | Detail exists only around agents, with a generation radius and a larger removal radius | Detail around agents with a generation radius: a mover's visibility limit (4.4). There is no removal radius; nothing leaves the mesh (rule 33). The view rects first built from this are deleted |
| Unreal per-area navmesh resolution ([docs](https://dev.epicgames.com/documentation/en-us/unreal-engine/navigation-mesh-resolutions-user-guide)) | Resolution there is voxel size, and low resolution can delete real paths | Not a precedent. Our detail is which obstacle classes block, never geometric precision |
| Godot region connection ([docs](https://docs.godotengine.org/en/4.2/tutorials/navigation/navigation_connecting_navmesh.html)) | Regions join cleanly only when edge vertices coincide exactly; margin heuristics fail on partial overlaps | Overlapping visibility limits are one ground in one mesh: a shared stretch dissolves, and the union's boundary is exact integer vertices (4.3.3) |
| Factorio FFF-317 ([post](https://cf-www.factorio.com/blog/post/fff-317)) | The reachability abstraction ignores entities, so placing buildings never touches it; only terrain changes do | First built as reachability on a structural layer, since deleted (4.2). Labels on the one mesh take the idea's other half: a placed obstacle landing in open ground moves no label and no epoch (4.6) |
| Kallmann, dynamic CDT and Local Clearance Triangulations ([TOG 2014](http://graphics.ucmerced.edu/papers/14-tog-lct.pdf)) | Local insert and remove with a list of obstacle ids per constrained edge. Floating point with an epsilon. Repeated small moves pile up stray points unless removal is done fully | The patch model, with our exact integer predicates. A move re-derives its hole from rings, which removes strays |
| Demyen and Buro ([AAAI 2006](https://cdn.aaai.org/AAAI/2006/AAAI06-148.pdf)); Kallmann on the same measure | A corridor width depends on the nearest obstacle in a wedge, which can sit outside the patched triangles. Kallmann reports the measure is wrong in some CDT cases | Width search is capped at a maximum clearance, and a patch invalidates widths within that distance of its hole |
| jdxdev RTS series ([part 2](https://www.jdxdev.com/blog/2021/07/06/rts-pathfinding-2-dynamic-navmesh-with-constrained-delaunay-triangles/), [part 3](https://www.jdxdev.com/blog/2021/12/07/rts-pathfinding-3-variable-agent-size-smoke-tests-navmesh-fixes/)) | A hobby project. Cuts out the triangles an obstacle touches and retriangulates. Updating subsets took longer to get right than the first mesh; overlapping obstacles were the trap | The cut-and-retriangulate shape. Overlaps go through the existing arrangement, on the hole only |
| CGAL, snap rounding ([docs](https://doc.cgal.org/latest/Snap_rounding_2/index.html)) | Exact predicates don't cover constraint crossings; a snapped intersection can land on the wrong side of a nearby edge | Crossing points are made only by the arrangement, which already iterates to a fixpoint on the integer grid |
| Shewchuk and Brown; Devillers | Segment insertion costs time linear in the edges it crosses. Vertex deletion is local to the triangles around the vertex | The two primitives a patch is built from |
| Dynamically Pruned A* ([abstract](https://webspace.science.uu.nl/~gerae101/pdf/DPAStar-Abstract-ICTOPEN-2016.pdf)), Detour `dtPathCorridor::isValid` | A path that doesn't cross the changed region stays valid | A patch replans only the movers whose remaining route crosses its hole |
| Holm, de Lichtenberg, Thorup, dynamic connectivity | Theory only. No incremental Kruskal reconstruction tree exists in the navmesh literature | Component labels kept per patch with plain local floods: a lockstep flood on a split, the smaller side relabelled on a merge (4.6) |
| S.T.A.L.K.E.R. A-Life ([interview](https://www.gamedeveloper.com/game-platforms/interview-inside-the-ai-of-i-s-t-a-l-k-e-r-i-)) | Offline agents route on a coarse graph, online ones on detail. Going online needs a valid nav vertex and has shipped stuck-in-transition bugs | The shape of the idea, as first planned. Rule 31 drops the coarse side: there is one mesh at full detail |
| Minecraft chunk tickets ([wiki](https://minecraft.wiki/w/Chunk)) | Chunks stay simulating while something holds a ticket on them | Chunk anchors |

No source documents what happens to a mover's position or route at a detail switch, and none
has a forward-only rule. Section 4.7 is new.

## 4. Design

Sections 4.1 to 4.12 describe the code as built after the seen-area work of 2026-11-13.

### 4.1 One mesh

The nav mesh is a single triangulation at full detail of the ground movers have meshed, each
within its sight distance plus a buffer (rules 31 and 32). There is no second mesh. It is created
once and lives as long as the game does, and it only grows (rule 33). Triangles and vertices have
stable ids; a patch frees some and allocates others, and every id it didn't touch still means
what it meant.

Each constrained edge carries the list of obstacle rings that put it there. The arrangement
already produces that list; today's mesh keeps one id per edge and throws the rest away. The
list is what makes removal safe, and it makes the mesh its own record of what is in it.

**Why there are no nav cells.** An earlier draft of this document stored the mesh in fixed
32 m or 64 m cells and re-derived a cell on any change. With patches there is nothing left for
them to do. A patch is already smaller than any cell. An obstacle enters the mesh once, so
there is nothing to key or cache. And a grid of cell borders would only add triangles, plus a
seam problem where a detailed cell meets a plain one.

**The mesh has no partition.** Ground is the union of visibility limits (4.4): each limit is a
ground outline, outlines may overlap, nest, touch or share sides, and only the union's boundary is
kept. Chunks are not in the mesh. Every vertex is a corner of a placed or built object, a point of
terrain, a corner of a visibility limit, or a point where two such outlines cross (rule 40); the
one exception left is water, still cut at chunk sides (4.11).

The triangulation's in-circle test is exact only while the points involved are within about
2^30 mm of each other. Nothing in the mesh bounds a triangle but the seen area it lies in, and in
open ground with no placed objects a triangle can span the whole open region (4.11). The library
refuses any ground outline that spans `kMaxGroundExtentMm` (2^30 mm) or more on an axis
(`PatchReport::refusedRings`, logged by the game; 4.8).

**A chunk unloading does nothing to the mesh.** Its water, walls, doors, placed obstacles, and the
seen ground over it stay with their ids, and so do the nav system's records of them (each chunk's
water, the placed obstacles in the mesh by key). A chunk that reloads patches nothing by itself. A
later limit over it adds any placed obstacle its placement data holds that the mesh lacks, which is
how one that came back with its chunk (below) re-enters. Ground no limit has reached has no
triangles.

One gap sits outside nav: placement data does not keep removals across an unload, so a felled
obstacle comes back in the placement data while the mesh rightly lacks it, WOR-717 (Felled trees
and harvested nodes come back when their chunk unloads and reloads).

### 4.2 Obstacles block

Every obstacle in the mesh blocks movement: water, walls (through the mover's belief), and placed
obstacles. Route requests pass no prices to the search, so a goal ringed by placed obstacles with
no gap the mover's disc fits through has no route, on camera or off.

A placed obstacle keeps its interior triangles, tagged as a placed obstacle. A mover standing
inside one reads on that face (`isOnMesh`), which is how the snap tells "inside a placed
obstacle" (snap forward, 4.7) from "in water" (the AI's off-mesh recovery).

Walls and doors are global: the nav update diffs them by content whenever
`ConstructionWorld::version` moves and patches them in at once. A door is a tag edit on the
triangles along its portal, applied in the patch job.

**The structural layer (deleted 2026-11-12).** Until the correction a second, structural mesh of
ground, water, walls and doors sat beside the nav mesh. Reach, the reach forests, the far route
and the step budgets were built on it, and placed obstacles were priced at 50 times open ground
instead of blocking, so they never sealed a goal. Entries 13, 26, 34, 35, 45, 50, 54 and 73 to 92
record what was built; entry 99 records its removal.

### 4.3 Patches

Every change is one of three operations. None of them regenerates anything outside its hole.

| Event | Operation |
|---|---|
| A placed entity with a collider goes away, a wall or door is removed | Remove (4.3.1) |
| A wall or door is built, placed obstacles first enter an area, a placed entity gains a collider | Add (4.3.2) |
| A mover's footprint or a large object moves (not built: WOR-710 (Step 7: Mover footprints in the mesh)) | Move (4.3.3) |
| A mover's visibility limit can go in (4.4) | Its placed obstacles, then its chunks' water not yet in, then the limit itself as a ground outline, added through the same patch job as every other edit |
| A loaded chunk's terrain changes | Its water is re-derived: the old water rings out, the new ones in |
| A chunk unloads | Nothing happens to the mesh (rule 33, 4.1) |

Doors are not rings: adding or removing one retags the triangles along its portal, in the patch
job after the tick's patches.

#### 4.3.1 One mechanism: re-derive a hole from the rings

Remove, add, and move are the same act. Pick the ring edges whose geometry is in question, cut
out the triangles along them, and rebuild that hole from the rings' original outlines. Remove
leaves a ring out, add puts one in, move does both in one hole. An earlier draft removed a tree
by deleting its vertices one at a time; one mechanism replaces that, so there is one code path
and the property in 4.3.2.

The mesh keeps three things to make it possible:

- **A ring registry.** Every ring in the mesh, under a stable id, with its original outline, its class, and its tags.
- **Sources on each constrained edge.** The list of (ring, which edge of that ring) that put the edge there. A piece of edge two rings share lists both.
- **A cover set on each triangle.** The set of rings the triangle lies inside, stored once and shared by every triangle with the same set. Floor, water, wall, tree, and door tags are computed from the set by the rule the builder uses today (the smallest solid ring wins; water goes by nesting parity), so that rule exists in one place. Ground outlines (visibility limits) do not appear by name: the cover holds one ground entry (`kGroundToken`), flipped by parity at each kept ground piece. That is exact because the kept ground pieces are the union's boundary, each kept once. Blocked rings such as water keep their ids, so an island in a lake is ground, lake and island, and floor by parity.
- **Boundary intervals on each ground edge.** A ground outline's edge is input only where ground differs across it: its boundary intervals, from `groundBoundaries` in `NavMesh.h`, which orders the edge's crossings with every nearby ground edge exactly (Int128) and reads each other outline's inside flag just off the edge's outer side. The rest of the edge is dissolved: no constraint, no mesh edge, no vertex. Where two ground edges run collinear the same way, the shared stretch is input once, owned by the lower `EdgeSource`, and passes to the other edge when its owner is removed; a shared side run the opposite way has ground on both sides and dissolves on both. An interval end is the crossing of two input segments rounded by the arrangement's own rounding, so the patch and a build from scratch make the same vertex. Blocked rings never dissolve. `buildNavMesh` and the patch path share this rule, so a patched mesh equals a build from scratch.

The patch:

1. **Start set.** The original edges of the ring being added, removed, or moved.
2. **Closure.** An original edge joins the set when an edge in the set crosses it, touches it, or shares a split point with it. Edges join whole, corner to corner, never as pieces.
3. **The hole.** The triangles along the current pieces of every edge in the set, plus the triangles under a ring being added. The mesh edges around that set are the boundary, held fixed.
4. **Re-derive.** The set's original edges, without the removed ring and with the new one, go through the arrangement and triangulation a first build uses, together with the fixed edges in and around the hole. If that splits a fixed edge, or an edge leaves the hole, the edge it hit joins the set, the hole grows, and this step repeats. The boundary is checked, never assumed.
5. **Splice and tag.** New triangles take the freed ids. Cover sets flood in, changing at each constrained edge by the rings on that edge. A group of new triangles that reaches a fixed hole-boundary edge takes the kept cover across it. Otherwise it is seeded from a triangle farther than three times each nearby ring's rounding stray from that ring's edges: ground when any ground outline holds the sample, blocked rings by exact containment. When no triangle is that clear, each nearby ring's containment is read along a ray against its outline as rounded. When nothing can be read, the patch fails loudly (`PatchReport::unreadCovers`, 4.8).
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
- **The builder's tags depend on which triangle it samples.** `buildNavMesh` tags a face by testing one point of its largest triangle against each ring's original outline. After rounding, a face can straddle an original outline by a fraction of a millimetre, so the tag can depend on the triangulation. It showed in a third of the adversarial states and in none of the game fixtures. The patched mesh doesn't have the problem: it gets tags by counting ring edges crossed, never by sampling. The builder is left as it is until step 4 decides what still uses it. (Since fixed: `buildNavMesh` classifies faces by a parity flood from the frame face, as the patch does, entry 116.)

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

**Queries on the patched mesh** (step 3). Path queries, the heuristic, and corridor widths
read a mesh through one view type, so each has one implementation whether
the mesh was built or patched.

- Paths through a patched mesh match the builder's in verdict and in length to the millimetre, on the fixtures and on wall-and-door inputs, at agent radii from 0 to 1.5 m. Query time and nodes expanded are the same (2.54 ms against 2.58 ms for 96 queries on one fixture).
- Corridor widths are capped at 4 m of clearance and recomputed on request, not inside each patch: computing them in the patch added 100 to 240% to a lone tree. A corner's width is recomputed only when a patch came within that width of the corner. One request serves any number of patches, so the game asks once per tick after its patches.
- Historical: a reachability forest rebuild was about 0.13 us per triangle: 0.03 to 0.1 ms for a 128 m region's structural layer, 0.67 ms for a 512 m one. The structural layer and the forests are deleted (4.2); component labels answer whether two places are joined.

**The cost of whole edges.** A long edge that many things cross pulls all of them into the
hole. Measured in step 3 on a 512 m chunk side: adding a placed obstacle across it with others
along it took about 1 ms and rebuilt about 200 triangles (*historical*: chunk squares and the
64 m pieces their sides were split into are gone, entry 116). A visibility limit's sides are a
`kLimitSides`-th of its perimeter and go in whole; nothing in the library splits a long edge. Only
an edge's boundary intervals are input at all, and a dissolved stretch pulls nothing into a hole.

#### 4.3.3 What each operation adds

**Remove.** The start set is the ring's edges. No new points are needed beyond re-rounding the
crossings of edges it had split. Removing a placed obstacle can only open ground: it moves the
label epoch, marks no route stale, and a path that would now be shorter is found at its next
replan.

**Add.** The start set is the new ring's edges, and the walk under them finds what they cross.
For a tree that touches nothing the hole is the few triangles its rect overlaps and the
arrangement has four segments in it. From the measured triangulation cost that is tens of
microseconds (*estimate*, confirmed or not in step 3). A limit's placed obstacles are many adds
in one patch, spread over ticks inside the patch budget when it is not urgent (4.4).

**Move.** One hole over the old footprint and the new one, re-derived with the object at its
new position.

**A visibility limit** is a ground outline, a ring of the class that makes ground exist: a
triangle is kept only if its cover set holds the ground entry. Adding one meshes the new ground
and joins it to the union: only its boundary intervals are arranged, every stretch of an older
limit's edge that it now covers dissolves, and those edges are re-derived in the same patch. A
limit wholly inside the union adds nothing; the game asks `groundCovers(outline)` first and skips
it. A blocked ring whose part hangs over no ground (water or a wall past the seen edge, a placed
obstacle straddling it) is registry only there, with no pieces and no triangles, and is completed
when ground arrives under it. The library also removes and moves ground outlines, re-deriving
the edges that get their constraint back; the game never does either.

**Mover footprints** (not built: WOR-710 (Step 7: Mover footprints in the mesh)). Every mover has
a footprint ring, sized from its clearance. It blocks every mover but its owner: the path query
already filters faces per agent for belief, and "this face is my own footprint" is one more case
of that filter. Its tag goes in the labels' open tags (4.6), so someone standing in a doorway
never changes what is connected, only what is passable right now, and removing or moving a
footprint must count as non-opening or the label epoch would move every tick. Footprint patches
would land every tick a mover moves, and a patch reuses freed triangle ids at once, so every
search that read them would restart (4.6). WOR-710 therefore needs footprints kept out of the
triangulation (an overlay the search and collision read), or search repair in place of restart;
the restart logic sits in `RouteQueue` alone, so either fits.

Motion adds what a one-off edit doesn't have:

- **Rate.** A footprint is patched when its mover has moved a set distance or come to rest, not every tick. Between patches, movers keep apart by the collision systems that already run. The distance is tuned from a measurement; with dozens of movers this is the patch that runs most.
- **Paths.** Each patch replans the movers whose remaining route crosses the hole, and only those.
- **Who is underneath.** A mover standing where a large object arrives is inside a blocked face and takes the snap in 4.7. Two movers' footprints don't overlap, because collision keeps them apart.
- **Large mobile objects** can open or close a gap. Their moves patch the one mesh like any other edit, and labels follow them; the same restart cost as footprints applies.

This reverses risk 7 in the pathfinding doc ("dynamic agents are never navmesh obstacles").
That rule came from StarCraft 2, which by secondhand accounts resets its mesh and reinserts
every obstacle on a change, so a unit in the mesh would mean a rebuild per step. With a patch
that costs what a move costs, the reason for it is gone. Steering and
crowd behaviour (P5 there) still stand, and still do the work between patches.

### 4.4 Who meshes what, and how far

**Movers.** A mover is any entity with `Position` and `AgentRadius`. Every mover meshes, holds
chunks, and is snapped and collided with. The camera does none of these (rule 22).

**Range.** A mover's range R is its sight distance (`Memory::sightRadius`, or
`kDefaultSightRadius` without `Memory`) plus `kSightBufferMeters`, unclamped
(`NavigationSystem::meshingRange`). What the buffer must cover is in 4.11.

**The visibility limit.** A mover takes a limit when he first appears, when he has moved
`kLimitRetakeStepMm` from where he last took one, or when his R changes (`trackMovers`). A limit
is a `kLimitSides`-sided polygon round where he stands, rounded to the millimetre, circumscribed so
the disc of R lies inside (`nav::visibilityLimit`, from `geometry::nav::limitPolygon` in
`libs/geometry/nav/LimitPolygon.h`). Its directions come from a table made at compile time with
+ - x / alone and pinned by a test, so every toolchain draws the same polygon from the same centre
and range. Vertex 0 points along +x and the winding is counter-clockwise. Its corners reach a
little past R, so the seen area is that much larger than the disc. A limit the meshed ground
already holds (`groundCovers`) is not queued: it is his served limit at once.

**The seen area is the union, and the mesh ends there.** Each limit is a ground outline (4.3.3).
Ground is wherever any limit reached, and nothing is ever taken out of it (rule 33). Outside it
there are no triangles, so `locate` gives -1 and every position check and route fails there
(4.6). The mesh edge counts as an obstacle for corridor widths, so a route keeps a mover's radius
off it. A chunk's water rings (`nav::appendChunkWater`) go in with the first limit over that chunk,
in the same patch as its ground, and hang over unseen ground, where they add nothing. Walls and
doors are global (4.2) and hang the same way.

**Placed obstacles by entity.** For a limit going in, each chunk its query box meets (the
polygon's box grown by `kFootprintReachMeters`) is asked for its placed entities in that box
(`SpatialIndex::queryRect`). An entity whose footprint box meets the disc the limit circumscribes,
and that is not in the mesh already (`obstacleInMesh`, by `ObstacleKey`: chunk, position, def),
goes in whole, in key order; its part outside the union is registry only. A footprint reaching
past `kFootprintReachMeters` from its entity's position is logged as an error once per def, since
where it pokes into a limit from outside the query box it is missed. A limit's placed obstacles go
in ahead of its ground or in the same patch, so seen ground is never without what stands on it. A
placed obstacle that lands over ground already meshed (one back in the world after it left the
mesh, such as one felled that came back with its chunk) marks the routes it crosses, as a wall
does (4.6).

**When a limit can go in.** Every chunk its query box meets is loaded and ready, has its
placement, and is held by a mover's anchor (4.5; `limitServable`). One whose chunks are not ready
waits without holding back those behind it. A queued limit with a chunk no anchor holds is
stranded and dropped, and so is one the meshed ground has come to hold; that check runs once per
queued limit each time the mesh changes.

**Order and budgets**, counts and never a time, so the tick a ring lands on does not depend on
frame time:

- the landing's limit first;
- urgent limits whole, with all their placed obstacles and water, outside every budget and through any hold;
- the rest first in, first out: `kLimitsPerTick` limits whose ground goes in a tick, and `kDetailRingsPerTick` placed obstacles; a limit with more obstacles than that brings them in over several ticks, hanging over unseen ground, and its ground goes with the last of them;
- removals, up to `kDetailRemovalsPerTick` a tick, each looked up by its key, in one patch. A removal not in the mesh changes nothing: a limit still waiting never finds it.

While a route search spans ticks, limits that are not urgent wait (4.6). A mover's first limit,
urgent limits, the landing, removals, walls, doors and re-derived water never do.

**Served, urgent, and stopping short.** A mover's served limit (`MoverDisc::served`) is his newest
limit known to be in the union: one whose ground patch was joined, or one the meshed ground already
held when he took it or when it came up in the queue. Ground never leaves, so what he sees stays
inside the union while he is within his served R minus his sight of its centre. His newest limit
is urgent when he has no served limit yet (a fresh mover's first limit), or when he stands
`kSightBufferMeters` minus `kLimitRetakeStepMm` or farther from the served centre. That leaves
exactly one retake step of margin, for any sight. A mover walking inside ground others meshed takes
covered limits a step apart, so `served` follows him and he is neither urgent nor stopped.

Movement takes each stride through `stepWithinSight`, so a mover never walks to where he would see
past his served limit. A mover with no served limit stands until his first one lands. One whose
urgent limit cannot go in yet (a chunk or its placement not ready) stops at that edge, his route
and task kept, and walks on when it lands, with no replan. A forward snap, a collision push, or a
teleport can still put him past it; then he may only step inward or stay level until his limit
lands.

**The landing.** `groupSpawnPoints` adds one limit of `kLandingSightMeters` round the drop before
the crew exists, and waits for it with its water and placed obstacles; it needs no anchor, and if
its chunks are not ready it goes in on the first tick they are. The landing search then picks the
crew's points, `clearLanding` takes placed entities out of the world round them, and their removal
is applied and waited for, so the crew stands on open ground before the first tick.

### 4.5 Chunks

`ChunkManager::update(cameraCenter, anchors)` takes one anchor per mover
(`NavigationSystem::appendAnchors`), each with its own range: the reach of his limit's obstacle
query (the limit's circumradius plus `kFootprintReachMeters`) plus `kLimitRetakeStepMm`, since his
newest limit was taken up to that far from where he stands. A mover loads every chunk within
`anchorLoadRange` of that range, the larger of 128 m and it, and keeps every chunk within
`anchorKeepRange`, the larger of 256 m and twice it. A colonist loads 128 m and keeps 256 m, as
before the correction; a mover that sees farther holds farther, so every chunk his newest limit
needs is held. The camera loads the
chunks within one chunk of its own and keeps those within two, to draw them. A chunk unloads when
it is outside the camera's keep radius and every anchor's keep range and isn't generating, which
does nothing to the mesh (4.1).

The hold range never shrinks below 128 m because chunks carry the placement data the nav system
scans for colliders. Routes, buildability, stand points and position checks read the mesh alone
(4.6), so they hold after a chunk unloads. The anchors of a live route's rest and of a goal
waiting for its ground are deleted (entry 99). Activity with no mover (construction, crops)
holds no chunk; that gap predates the correction.

Two groups of movers are joined by mesh when the ground between them has been meshed. Where it
never has, a route between them answers "no known way" (4.10).

**Mesh is not visibility (rule 27).** The meshed area is larger than the visible area, and
nothing may read "has mesh" as "the player can see or act here". Placement needs no rule of its
own: the player places only in the viewable area, which is smaller than the meshed area (rule
35). Player visibility is built (WOR-754, Player visibility: unexplored areas (visible, seen,
unseen)): the camera can still show ground no mover has meshed, as black; a move order there walks
to the edge of seen ground and a placement there is refused.

### 4.6 Queries

Every query reads the one mesh (rule 31) and first joins the patch job in flight.

**Component labels.** `geometry::nav::MeshComponents` keeps one label per triangle, owned by the
mesh and brought up to date on the patch worker after every patch from the patch's report (its
freed, new, and border triangles): a local check that the hole's boundary still meets, a
lockstep flood of both sides when it does not with the smaller side relabelled, and the smaller
side relabelled on a merge. For labels a triangle is passable when it is floor, any wall face
(walls read open, so labels hold for every belief), or a tag listed in `openTags`; water and
placed obstacles block. Labels have no width. So:

- different labels: no mover of any size or belief can cross between the two on meshed ground;
- the same label: joined by meshed ground for a mover of zero width with every wall open, and nothing more.

The label epoch (`connectivityEpoch()`) moves when labels join or split, when ground takes a
fresh label or a label empties, on any patch that may open a way (ground arriving, a placed
obstacle leaving, a wall or door removed, water re-derived), and when a door is added. A placed
obstacle landing in open ground leaves it. A closing patch that re-derives a small isolated
pocket can move it with no real change. Its values never repeat.

**`connected(a, b)`.** Two locates and a compare, with no per-tick work. False is "no known way":
not joined by meshed ground, a point off it, or a point inside a placed obstacle in the mesh. It
is never a statement about the world, since ground no mover has meshed may join them. True is
necessary, not sufficient: only a route request answers for a mover's width and belief. To ask
about a target that is a placed obstacle, ask from where a mover would stand to work it
(`standPointBeside`).

**Routes by ticket.** `requestRoute(start, goals, radius, belief)` returns a ticket,
`takeRouteAnswer(ticket)` returns the answer once it is takeable, and `cancelRoute(ticket)`
drops the request, its search and its answer. An answer is `Routed` (the goal nearest by walk
among those given, its index, the waypoints and the length), `NoKnownWay`, or `AwaitingMesh`
(the start, or a goal, is off the mesh inside a limit still on its way in: wait and ask again,
never a failure). It carries the label
epoch and the mesh's patch epoch it holds for, the restarts it took, and its expansions. Nav
never learns who asks (rule 14).

- *Answered at once.* The belief's sets are copied at the call. With no search, the request answers `AwaitingMesh` when the start or a goal locates on no triangle but lies inside a limit on its way in (the landing's, one queued and not stranded, or one whose ground is gathered and not yet joined: `groundComing`), and `NoKnownWay` when an end is off the mesh with no such limit, or no goal stands on a triangle with the start's label. A search that finds no goal while another goal waits for ground answers `AwaitingMesh` too. An `AwaitingMesh` answer is asked again once `awaitedGroundEpoch()` moves (`generation()` moved, or a queued limit was dropped and will never go in, so a waiter is never held for ground that is not coming), and no sooner than the AI's `kReEvalInterval` after the last ask; the epoch is stamped when the route is asked for, so ground landing before the answer counts. Each end is judged where a search would stand it (`standingTriangle`): a point on the edge of a placed obstacle or a wall stands on the free triangle beside it, so a goal deep inside a placed obstacle, or a start standing on nothing it can cross, answers "no known way" at once. Only the goals that share the start's label are queued.
- *The queue.* `nav::RouteQueue` runs the queued searches in slices, one a tick, on the route worker. A slice does up to `kRouteWorkPerSlice` (18,000) expansions shared by every running search. Requests admitted since the last slice take the first turns, in ticket order; then the running searches take turns in ticket order, cyclically, from where the last slice stopped. A turn grants at most `kRouteQuantum` (2,000), so a short request is never held behind a long one. A search has no limit of its own: it ends found, or exhausted when it has explored everything the mover can reach under its width and belief, which answers "no known way".
- *The time budget (rule 18).* The two counts are derived from a time target: 8 ms of route worker a tick, half a 60 Hz frame, at an estimated 0.45 us an expansion on the real mesh (not yet measured, section 6). They are counts so the tick an answer lands on is the same on any machine; a search that needs more work than a slice carries on in the next.
- *The search.* `geometry::nav::SearchState` is the path search made resumable: `step(n)` runs up to n expansions and returns Running, Found or Exhausted. With several goals the first goal reached is the nearest by walk; goals off the mesh or on ground the belief cannot cross are dropped. With one goal it first searches from the goal for up to `kGoalSidePhase` (2,000) expansions: a goal sealed in a pocket runs dry there, and meeting the start returns the walk. That is a phase length, never a limit on the answer. `pathThrough` is the same search run to the end with no goal-side phase.
- *Tick order.* The nav update joins the patch job (which waited for the last slice), hands every patch report since that slice to the queue (each patch's, the width refresh's, and one for the door edits), admits (delivers finished answers, takes in new requests), and starts this tick's slice. The rest of the tick, every query included, reads the mesh alongside the slice. `endTick` starts the patch job, which waits for the slice before it writes.
- *When an answer is takeable.* At once, from the call, for the answers above. Otherwise a request made during tick t is first searched in tick t + 1's slice, and an answer found in slice s is takeable from tick s + 1's update. A short search asked during tick t is takeable at tick t + 2; a long one takes as many slices as its work needs.
- *Restarts.* A patch report that changes a triangle a search has read sends it back to its start, its work discarded and a restart counted. That includes a search that has finished and is not yet delivered, so no answer is delivered over a patch it did not see. Restarts are counted on the answer and never limited.
- *The hold.* So that a search spanning ticks can finish, limits that are not urgent are held back while some search is inside its hold window. A search's window opens at the first update where it is still running after a slice it had a turn in, and lasts `kMaxHeldAdditionTicks` (240) updates over its whole life; a restart does not reopen it. With no search inside its window, held limits go in at the normal budget, tick after tick, until none wait. A mover's first limit, urgent limits, the landing, removals, walls, doors and water are never held, so a hold never lets a mover see past the mesh (4.4).
- *Untaken answers.* An answer not taken `kUntakenAnswerTicks` (600) ticks after it became takeable is dropped with its request, as a cancel would (a mover destroyed with a ticket out).
- *`routeTicketLive(ticket)`.* True while nav is searching the ticket or its answer waits to be taken; false once taken, cancelled, dropped, or never issued. When `takeRouteAnswer` returns nothing, live means keep waiting and not live means ask again.
- *Counts.* `routeStats()`: requests, answered at once, answered, outstanding, dropped, restarts, slices, expansions per slice, slice time, ticks held, and the longest hold.

**Position checks** read the mesh alone, never the placement data, so they hold after a chunk
unloads: the mesh keeps what a mover meshed, and the placement data does not. Off meshed ground
nothing is valid.

- `isOnMesh`: a face a route can cross, which is anything but water; a mover inside a placed obstacle stands on its face. Any wall face reads open.
- `isValidPosition`, the one predicate for spawns, drops, placement and teleports: a face that is neither water nor a placed obstacle in the mesh. A placed obstacle no mover has meshed does not block it.
- `isAreaBuildable` and `isPointBuildable`: placed obstacles do not block building (construction turns them into clear tasks); water under them does, and so does ground that is not meshed.
- `nearestPathablePoint` (both forms), `findValidPositionNear`, and the landing search: searched outward over the mesh. The form with a radius stands the mover's whole disc clear of water, placed obstacles, the mesh edge and built-wall collision bands, else nullopt; a stranded colonist with no such point goes to the colony-origin recovery.
- `standPointBeside(target, from, radius)`: where a mover stands to work a placed entity: the spot nearest `from`, on a circle the disc's radius out from the placed obstacle at the target, that is walkable in truth and in no placed obstacle; nullopt when there is none, and off meshed ground. Never a point inside a placed obstacle.

**Staleness.** `generation()` goes up by one at the join of any patch job in which ground arrived,
water was re-derived, walls changed, a door was added or removed, or a placed obstacle landed over
ground already meshed. At that join, routes that no closing box crosses (a wall, new water, a door
taken out, such a placed obstacle) are re-stamped current and those one crosses go stale. Ground
arriving closes nothing, so routes being walked are re-stamped, not asked again. A placed obstacle
that lands with or ahead of its ground closes no route, and one leaving only opens ground and
marks nobody. A
mover more than `kOffRouteMm` (2 m) off the leg of its route it is on has its route marked stale
(`markStrayedRoutes`). The AI asks again, by ticket, for a stale route.

**Path.** Triangle A* over the mesh scored by taut corridor length (entry 40), with the per-agent
filter deciding which tagged faces block: belief for walls, and later ownership for the agent's
own footprint. No heuristic beyond the score (entry 44). **Locate:** a spatial index over
triangles, updated by each patch for its hole. **Widths:** capped at a maximum clearance and
refreshed once per patch job, outward from the edges the job's patches changed (`refreshWidths`,
whose report lists the triangles whose widths changed). The mesh edge counts as an obstacle for
widths (`isObstacleEdge` in `NavMesh.cpp` reads outside the mesh as blocked), so a gap between a
placed obstacle and the edge of the seen area narrower than a mover's disc is shut to him. A
corner counts as an obstacle when any edge of its fan is one, so a waist between two facing
corners, of two limits or of two blocked rings, is its corner gap.

**The AI's side** (in `AIDecisionSystem`, 1.1). A colonist asks for a route by
ticket and stands in the awaiting-route state until it comes, or, replanning a route he is
walking, keeps walking it and joins the new one where he stands. A target inside a placed
obstacle is routed to its stand point. A "no known way" answer is a verdict on that target for
that colonist, remembered until the label epoch or his belief moves, and a target with a live
verdict is not asked about again. A need's source is chosen by one nearest-of route request over
the nearest few remembered sources (by straight line) that are connected and have no verdict;
the answer's goal is the choice, and a round that finds no way puts a verdict on all of them so
the next decision asks the next few. "Not connected" is not remembered: it costs a compare.

### 4.7 Snap forward

The rule (rule 30): wherever an obstacle lands on a mover, the mover moves forward along its
route, and never stalls or loses progress. The trigger is any placed obstacle meshed onto a
mover. Under visibility limits a placed obstacle lands with or ahead of its ground, so that is
rare: a mover put on unseen ground (a dev teleport) whose first limit lands an obstacle over him, a
placed obstacle back in the world over meshed ground, or a push that took him past his served
limit. The snap and collision with placed obstacles are one rule.

`snapMovers` runs in the nav update on every mover:

1. Not standing in a placed obstacle in the mesh: nothing happens.
2. Standing in one with a route: it moves forward along its remaining route to the first point walkable in truth with its clearance, never past a wall or water on the way. Points behind it are never candidates.
3. Standing in one with no route, or with a route still stale from an earlier tick (no new route came): it steps to the nearest spot within 4 m, ring by ring, where its disc is clear, the straight step crosses no wall or water, and the spot is not behind its heading (the leg it steers along, else its velocity).
4. Nowhere to go: it stays, its route goes stale, and the snap holds it (`snapHolds`).

`StaticRectCollisionSystem` pushes a mover out of a placed obstacle that is in the mesh, except
one the snap holds: for collision the snap's movers are the snap's, and collision only keeps a
mover that is outside from entering. Its push goes along the axis of least penetration, usually
backward, so leaving a held mover alone is what keeps it from undoing the snap. A held mover
walking on, or the next route starting where it stands, takes it out ahead.

The result is a function of the mover's position, its remaining waypoints and heading, and the
mesh. Frame time and thread timing are not inputs.

### 4.8 Threads, determinism, and correctness

Each tick's edits are gathered on the main thread in `NavigationSystem::update`, in a fixed
order and under per-tick count budgets (4.4). After every system has run, `endTick` hands them to
the patch worker as one job: the patches in order, the door edits, then one width refresh; labels
are brought up to date inside each patch. The next `update`, or the first nav query before it,
waits for the job, so a query always sees exactly the patches gathered through the previous tick,
and nothing reads the mesh while it is patched. There is no second copy of the mesh.

The route worker runs one slice a tick (4.6) alongside the rest of the frame, and the patch job
waits for it before writing, so a slice and a patch never overlap. Everything that reads the mesh
during a slice reads it const: the mesh keeps no lazily built state, each search keeps its own
scratch and its own star memo, and its belief is a copy the request owns. A query made outside the
sim tick joins the patch job and so can wait for the slice too (1.1).

The main thread pays only when a job outlasts the rest of the frame. The last measurement, taken
with two meshes and two workers, read up to 4.3 ms with a 2 ms gap between ticks when a shoreline
chunk's ground arrives (entry 67); WOR-721 (Shoreline chunk arrival can make the main thread wait
on the nav patch job) tracks it.

**Determinism.** Patch budgets and route work are counts, never clocks. Limits queue in the order
taken, a limit's placed obstacles go in key order, the limit polygon is the same on every
toolchain (4.4), labels follow patch order, searches break ties on coordinates, and route turns go in
ticket order. Given the same chunk arrival ticks, every route answer and the tick it lands on are
the same on any machine; thread scheduling changes only how long the main thread waits. Chunk
arrival itself is not deterministic (chunks generate on `std::async` and are taken when polled
ready), which predates the correction, so tests use synchronous chunk fixtures. The camera is not
an input.

A mesh that is never rebuilt has nothing to wash a bad patch out, so correctness is carried by
four things:

- **The oracle.** For any sequence of adds, removes, and moves, the mesh's constraints and tagged faces equal those of a mesh built from scratch from the same obstacles. Tested on every real fixture with obstacles added and removed one at a time in several orders, including overlapping obstacles, obstacles on waterlines, overlapping and nested ground outlines, and slanted limit polygons crossed at shallow angles. It is a face-level comparison because of the co-circular diagonals. The builder, the test model and the patch share one rule (parity over the arrangement, `groundBoundaries`), so the fuzzes also check an independent even-odd test against the outlines as given. Order independence is shown by fuzz, not by argument.
- **The validator.** `PatchableMesh::validate()` checks the whole mesh: free lists and counts, and per triangle its shape, links, ring edges, the Delaunay property, cover sets, tags and the locate index. The geometry and sim tests run it after their patches; the game does not.
- **Exact arithmetic throughout.** No epsilons enter. The one place new coordinates are made is the arrangement, which is the code that already handles it.
- **Loud failure.** Input the library cannot mesh exactly is never guessed at. A group of new triangles whose cover no sample can read counts in `PatchReport::unreadCovers`; a ground outline spanning `kMaxGroundExtentMm` or more is refused and counts in `PatchReport::refusedRings`, and validate fails from then on. The game logs either as an error once per patch, naming the outline, and keeps a refused placed obstacle out of its index. Neither was logged in the in-game runs of 2026-11-13, and no fuzz run has read a cover it could not.

`finishPendingBuilds` is the blocking settle for tests and the headless sim: it gathers every
queue with no budget, applies it and waits, until nothing is left. It reads only chunks that are
ready, so its caller settles chunk generation and placement first.

### 4.9 All movers

Meshing, chunk holding, the snap and collision key on one mover definition, `Position` +
`AgentRadius`. The route logic (requests by ticket, replans on staleness, off-mesh recovery)
still lives in `AIDecisionSystem` and reads colonist tasks and memory. It moves to a mover-generic
route system that the colonist AI sets goals on: WOR-709 (Step 6: Movers (route system out of
AIDecisionSystem, a wandering deer)). A wandering deer with no memory and no tasks is the second
client and the test that nothing colonist-shaped leaked in.

### 4.10 World scale

The nav mesh covers ground movers have meshed. In the design a mover whose goal has no mesh heads
that way and meshes the ground as it goes (rule 26). As built, a goal off meshed ground, or cut off
from the start by ground no mover has meshed, reads "no known way" at once (unless it lies in a
limit on its way in, 4.6) and is not remembered;
travelling toward it is WOR-776 (Nav: build a path toward a goal whose ground is not meshed yet).
Beyond the mesh are two things, and neither is this epic:

- The geography mesh (the regional mesh), Tier 1 of the pathfinding doc, much less detailed than the real mesh and aligned with it. The design has it computed when the world loads, so it always exists (rule 38); the code does not have it yet, WOR-802 (Geography mesh (Tier 1): computed at load, always exists).
- A landmass check so nothing ever tries to path off an island: flood the planet's land hexes from the landing site's hex once at load and keep the set. The single-hex-island test case falls out of it. At the quickstart resolution the planet has about 42 million hexes, so this is a bounded flood from the colony, not a table for the whole planet. It has no consumer until goals can lie outside loaded ground, so it is filed as a follow-up and built with that consumer.

### 4.11 Tunables, costs, and known limits

| Name | Value | Where | What it must cover |
|---|---|---|---|
| `kLimitRetakeStepMm` | 4000 mm | `NavigationSystem.h` | How far a mover walks before he takes a new limit. It is also the margin an urgent limit has to land in, so a mover's travel during one landing (a tick at the fastest game speed, plus any stall) must stay under it. A 16 m step measured dearer per patch in dense ground, so the step stays short |
| `kLimitSides` | 16 | `libs/geometry/nav/LimitPolygon.h` | The sides of a limit polygon. Fewer sides overshoot R farther at the corners; more add vertices to the seen edge |
| `kSightBufferMeters` | 16 m | `NavigationSystem.h` | Added to sight to make R. It covers the step to a mover's next limit, a step more for that limit to land in once urgent, and his travel during one landing; a `static_assert` holds it above twice the step. The polygon's overshoot only adds margin |
| `kLandingSightMeters` | 150 m | `NavigationSystem.h` | The radius of ground the landing meshes round the drop |
| `kFootprintReachMeters` | 8 m | `NavigationSystem.h` | The farthest a placed obstacle's footprint reaches from its entity's position: a limit's obstacle query box is grown by it, and a def found past it is logged as an error |
| `kLimitsPerTick` | 1 | `NavigationSystem.h` | Limits that are not urgent whose ground goes in a tick |
| `kDetailRingsPerTick` | 32 | `NavigationSystem.h` | Placed obstacles of limits that are not urgent a tick |
| `kDetailRemovalsPerTick` | 128 | `NavigationSystem.h` | Placed obstacles taken out a tick |
| `kJobBudgetMs` | 2.0 ms | `NavigationSystem.h` | The worker time a tick's job is sized for; a target, not enforced |
| `kMaxHeldAdditionTicks` | 240 | `NavigationSystem.h` | Ticks a search spanning ticks may hold limits that are not urgent, over its whole life |
| `kOffGroundReachMm` | 8 mm | `libs/geometry/nav/PatchableMesh.h` | How far snap rounding can move a piece of an edge; asserted to cover three times every ring's stray |
| `kMaxGroundExtentMm` | 2^30 mm | `libs/geometry/nav/NavMesh.h` | The widest a ground outline may be on an axis for crossings to order exactly in Int128; wider is refused |

**Measured costs.** Each is one run, RelWithDebInfo, headless, on the dev machine with other work
running, so none is a baseline. Dense ground is a field of 0.064 placed obstacles per square metre.

Per retake (`BM_LimitWalk`, a 300 m walk retaking at `kLimitRetakeStepMm`, the limit's patch plus
its width refresh, ms):

| Ground | Mean | p99 | Max |
|---|---|---|---|
| Open | 0.13 | 0.14 | 0.15 |
| Sparse | 0.41 | 0.48 | |
| Dense | 2.48 | 2.86 | |

In an earlier dense run (machine load 2 to 10%) the retake split as patch 1.50 / 1.87 and refresh
0.85 / 0.94 (mean / p99), with about 24 new placed obstacles, a hole of about 144 triangles and
about 330 created.

Many movers (`BM_WalkingMovers`: movers walking 60 m at 2 m/s in the dense field):

| Movers | Speed | Nav main thread, ms mean | p99 | Worst tick | Limits urgent / budgeted per s | Patch worker busy |
|---|---|---|---|---|---|---|
| 1 | 1x | 0.03 | 0.60 | 4.5 | 0 / 0.47 | 0.2% |
| 10 | 1x | 0.34 | 5.2 | 6.6 | 0 / 4.6 | 2% |
| 50 | 1x | 2.0 | 8.6 | 22.3 | 0 / 23 | 11% |
| 100 | 1x | 4.4 | 9.9 | 24.2 | 0 / 46 | 24% |
| 10 | 10x | 3.4 | 6.6 | 7.0 | 0 / 47 | 19% |
| 50 | 10x | 12.0 | 76 | 159 | 77 / 54 | 63% |
| 100 | 10x | 23.0 | 357 | 360 | 158 / 52 | 122% |

No stride stopped short on any row.

The landing (`BM_Landing`): 122.7 ms at load, of which the patch job is 118 (patch 88.9, widths
29.2); 5,622 placed obstacles and 44,150 triangles.

Hanging water (`BM_HangingWater`, per retake, mean / p99 ms, with water against without): beside a
lake 0.33 / 0.53 against 0.11 / 0.16; inside it 0.52 / 0.77 against 0.14 / 0.15.

Open ground with no placed objects (a swept square, machine load 2 to 10%): the limit patch is
0.22 / 0.30 ms (mean / p99) at 500 m across and 0.26 / 0.36 / 0.48 ms (mean / p99 / max) at
1 km, but what grows with the region is unbounded: the longest triangle edge is 552 m, then
1,049 m; `locate` is 385 / 780 ns, then 550 / 1,256 ns (mean / p99); a patch's report box reaches
the whole region, which takes the width refresh's p99 from 0.31 to 0.80 ms; a placed obstacle
dropped into it costs 0.35 ms p99 at 500 m and 0.89 ms at 1 km with its refresh. Routes stay cheap:
about 5 expansions for 40 m pairs and 26 to 33 for 400 m pairs.

Along the seen edge in dense ground (within 4 m of it): longest triangle edge 4.66 m, `locate`
114 / 162 ns, route expansions 78 mean. Registry memory per kilometre walked: 0.70 MB in open
ground, about 36 MB in dense ground, nearly all of it the placed obstacles.

**Known limits.**

- Urgent work has no per-tick ceiling. Urgent limits, their placed obstacles, and their chunks' water all go in the tick they are urgent, and the next update waits for the patch worker, so many movers urgent at once stall the main thread: 159 ms worst at 50 movers and 360 ms at 100 at 10x, and 22 to 24 ms worst at 50 to 100 movers at 1x.
- A budgeted limit in dense ground costs 4.4 to 6.6 ms on the worker against the 2 ms job budget, even with 1 to 10 movers.
- Open ground with no placed objects has no interior vertices, so its triangles span the open region and `locate` and the width refresh grow with it (above). An owner decision is open: points on a coarse lattice inside open ground only, a maximum triangle edge enforced by the triangulation's own points, or accept the cost.
- Water is still cut at chunk sides: a chunk's water rings end at its square, so each chunk line through water has cut points and a fixed edge across the water, until water outlines merge across chunks.
- Walls do not clip a limit. A limit is a polygon round the mover whatever stands between, so ground behind a wall is meshed; walls still block movement.
- Crafting stations and storage do not block movement: they are ECS entities that never reach the nav system. Built walls and doors do.
- The seen area reaches a little past R at a limit's corners, since the polygon holds the disc.
- A landing limit whose chunks unload before it lands is never dropped, so a goal inside it waits.
- A player move order onto unseen ground goes to the edge of seen ground only, one order to one point; the ground beyond opens as the colonist arrives and the player orders again. Planning on toward the click is route search past meshed ground, not built.
- The path query does not gate the edge between adjacent start and goal triangles (1.1).

### 4.12 Geography mesh (not built)

Kevin's idea: the known-world nodes of rule 41 would be the geography mesh's (rule 38, WOR-802
(Geography mesh (Tier 1): computed at load, always exists)), and a coarse known-world mesh could
later be what the seen-area mesh links out to at its edge, its join points geography nodes. His
words: "that was my thought, but I don't think we need to act on it now." So nothing is built for
it: the mesh ends at the seen edge with no links, and a goal past it reads "no known way" (4.10).

## 5. What gets deleted

No fallbacks. When the one mesh routes, these go in the same change:

- `SimulationRegion`, region clustering, recentering, the edge margin and size-change gates, `retiringBuilds`, the per-region `std::async` builds.
- `regionContaining` dispatch, the cross-region "unreachable" and off-area "hold" branches, `inSimArea`.
- The global `meshGeneration`, the global construction-version and removal-epoch rebuild triggers.
- `buildTerrainOnlyMesh` and the cached placement mesh.
- The per-build `extractWalls` over the whole construction world, and `nav::buildInput`'s area sweep.

The correction deleted, in the sim (entry 99): the structural mesh with its job, worker, ring
pairs and forest rebuild; reach (`walk`, `reaches`, `reachFrom`, `Reach`, `sharesStartRegion`,
`structuralWalk`, the stamps, `WalkAnswer.h`); `requestPath`, `PathAnswer` and its `gaveUp`, the
far route, the near search's cap and its fallback, the route registries, and the step budgets with
their setters (`setReachBudgets`, `setNavExpansionCap`); prices, and both exemptions from
collision and the snap; the camera's inputs (`setViewportRect`, the view rects, `detailOnAt`);
every removal path (ground shed after unloading, a chunk's placed obstacles leaving on unload,
the placed-obstacle cap and its trim); the anchors of a route's rest and of a goal waiting for
ground; and the settled-ground wake. The geometry library's tag prices and squeeze, the second corridor
width (`structuralWidthMm`, `detailTags`), the reachability forests and
`PatchReport::freedTriangles` and `newTriangles` are deleted too. `pathThrough` rejects a query
across components by their labels, and a path's cost is its length.

The seen-area work deleted (entry 116): in the library, kept points (`pins`, `vertexPinned`, the
Arrangement's point-site argument), partners (`RegisteredRing::partners`, `findPartners`,
`BoundsSeams`, `findBoundsSeams`, `sameSegment`), the overlap check, and long-edge splitting
(`splitLongEdges`, `maxBoundsEdgeMm`); in the sim, chunk squares and the ground lane
(`nav::chunkSquare`, `kMaxBoundsEdgeMm`, `kGroundVerticesPerTick`), 16 m cells and their budgets
(`kDetailCellMm`, `kDetailCellsPerTick`, `kDetailScansPerTick`, `wantDisc`, `wantNear`, the near
lane), `markCrossedRoutes`, `builtRegions`, and `nearestClearOnMesh`. `RouteOutcome::StartOffMesh`
became `AwaitingMesh`, and the AI's `deferredNavGeneration` became a stamp of
`awaitedGroundEpoch()` taken when the route is asked for.

## 6. Performance plan

Rule 13. Every step is measured headless and the game is launched as few times as the numbers
allow; a step is compared against main when its own numbers look bad. The table below is the plan
as first set for steps 1 to 5.

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

**Memory (historical estimate, from struct sizes and the measured triangle counts; the measured
figures below replace it)**

A triangle on the mesh is about 200 bytes: the 136-byte `NavTriangle`, plus its share of
vertices, the locate index, and the per-edge ring lists. A structural-layer triangle was about
500 bytes, because the two reachability forests rode on it; the structural layer is deleted.

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
chunks is about 20 MB. Flora is nearly all of it. The detail cap that once bounded it is deleted
(rule 33), which leaves the cost per placed obstacle. This estimate put 40,000 rings at 64 MB;
measured at main's density a ring is about 6.4 KB (entry 88). `NavTriangle` is mostly 64-bit fields holding a
sentinel, and packing it could halve the figure; measure before making that a step.

**Memory, measured 2026-11-11** (`BM_PlacedObstacleBytes`, `BM_PlacedObstacleSystemBytes` and
`BM_PlacedObstacleDensity` in `NavigationSystem.bench.cpp`; entry 95). This replaces the
estimate above wherever the two differ.

- On the real planet around the landing, placed obstacles run at 0.060 to 0.068 per square metre, 75 to 85% of the 0.0802 worst case.
- Now: about 5.8 KB a placed obstacle (`BM_PlacedObstacleBytes` 5,803 B, 2026-11-12). The 435 to 490 MB figure below was taken at the earlier 7.2 KB.
- A placed obstacle cost about 7.2 KB in the mesh as first measured: 8 triangles and 4 vertices. The triangle box grid is 29% of that, the triangle records 21%.
- So meshed ground costs about 435 to 490 MB per square kilometre with its placed obstacles, against about 1 MB for ground alone.
- A field-by-field accounting puts a placed obstacle at about 1.25 KB with mechanical changes to the records (no design change), about 75 to 85 MB per square kilometre.

The mesh only grows (rule 33), so nothing caps this: the meshed area times that cost is the
memory. Bringing the per-obstacle cost down is part of bringing the code to the design (rule 37):
WOR-801 (Nav: cut the mesh cost of a placed obstacle from about 7.2 KB to about 1.25 KB), not done
(1.1). Paging unused parts of a very large mesh out to disk is a possible later follow-up, not
part of the design: WOR-800 (Nav: page unused parts of a very large mesh out to disk (later,
maybe)). Mesh paged out would come back unchanged.

**Not measured yet, for the correction.** The expansion cost behind `kRouteWorkPerSlice` is the
0.45 us estimate; `BM_RouteSlice` (1, 20 and 200 requests pending) compiles and has not run. Label
upkeep per patch (`BM_LabelUpkeep`), and patch plus route load with 20 and 200 movers walking
through fresh dense ground, have not run either. They belong to the correction's final pass.

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

Steps 4 and 5 as written built the structural layers, reach on them, camera-driven detail,
pricing, the fixed anchor range and the detail cap. The correction of 2026-11-12 replaced all of
them (section 10, entries 99 to 113). Steps 6 and 7 are not built.

Step 6 doesn't depend on 3 to 5 and can move earlier. Step 3 is where the risk is: it is the
part every source says takes longest, and it is deliberately headless and finished before the
game depends on it.

## 8. Open questions

Questions 1, 3 and 4 are answered by rules 22 and 23: there is no camera-driven detail and no
off-camera tier, and an object movers cannot path through blocks. Question 5 is answered by rule
32 for the mesh and by the per-mover hold range for chunks (4.5). They stay as written for the
record. Question 2 is open and is WOR-710's (Step 7: Mover footprints in the mesh). Labels read a
footprint as open, so a mover in the only doorway never changes whether two places are connected,
only whether the mover behind it has a route right now.

1. **Flora that seals.** Trees can close a gap or ring a spot that the structural layer says is reachable. Options: the mover walks through the flora at a high path cost when there is no way around (visible, rare, and the same thing it would do off camera), or flora stays a hard wall and the answer differs on and off camera. Only the first satisfies rule 6. Confirm.
2. **A mover in the way.** A footprint is impassable, so a colonist standing in the only doorway blocks it. Labels read footprints as open, so the goal stays connected. What does the blocked mover do: wait and retry when the footprint moves, or ask the blocker to step aside? Waiting is the simple answer and can deadlock two movers in a corridor; the pathfinding doc's door queues and avoidance are the longer one. To be settled under WOR-710 (Step 7: Mover footprints in the mesh).
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
departures they add, and the memory measurements behind rule 37. Entry 97 records his answers
on the geography mesh, ground extent, and the step limit. Entry 98 records the search time
budget and the timing of the geography mesh. Entries 99 to 113 record the correction of
2026-11-12 that brought the code to the design: what was built, each place it departed from its
plan and why, and what four review rounds found. Entry 116 records the seen-area mesh of
2026-11-13 (rules 39 to 42).

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
96. **The geography mesh stays planned, and the real mesh is a detail view of it (Kevin's ruling; rule 38).** The geography mesh (also called the regional mesh), Tier 1 of the pathfinding doc, is much less detailed than the real mesh, the nav mesh this document specifies. The real mesh is a detail view of it, as game tiles are a detail view of world tiles; the two must be aligned; one is derived from the other and then filled in. Only the reachability layer over the real mesh's triangles is built (`buildReachabilityForests`), not the less detailed mesh, and it is not the coarse layer entry 93 removes. Which is derived from which in implementation terms, what alignment requires of vertices or edges, and what the geography mesh answers are not settled by the ruling. Rule 12 and section 4.10 stand, with the names.
97. **The geography mesh is computed at load and always exists; ground stays chunk by chunk for now; a route search at its step limit lists the target as impossible for now (Kevin's rulings).** Three answers:
    - The geography mesh is computed when the world loads and always exists (rule 38). This reverses entry 96's "stays planned" and rule 12's "stays unbuilt", and closes any reading of it as optional or as something reach waits for. What it is built from, its resolution, which mesh is derived from which, how alignment works, and what it answers are to be designed under WOR-802 (Geography mesh (Tier 1): computed at load, always exists). As built it does not exist, a fifth departure in section 1.1. Entry 96's account of what is built is corrected there: the reach forests are rebuilt in the game only on the structural mesh (`NavigationSystem::runJob`), not over the real mesh's triangles.
    - Ground is patched in a whole chunk at a time, as today, and stays so for now (rule 32); sight distance plus a buffer governs everything else a mover adds. This closes the question section 4.1 left open, whole chunks or the area itself.
    - A route search that reaches its step limit lists the target as impossible, for now. It is an extreme edge case and a temporary simplification, recorded in section 1.1 as accepted for now. It is not a design rule: rule 18 (running out of search budget means "didn't finish", never "can't reach") is the design and stands, and this departs from it. As built the code answers "search gave up" (entries 83 and 92).
98. **A search is bounded by a time budget, not a step limit; the geography mesh comes later (Kevin's rulings).** Two answers:
    - The step limit gives way to a time budget. This goes back to 2026-11-11, when the approach settled was to derive the bound on a search from a frame-time allowance instead of picking a count, to let a long search resume on later frames or run on the nav workers instead of being cut off, and never to record a capped search as a failure. Rule 18 now says so. As built the searches still stop at step counts sized from a time target (`kReachProbeExpansions` 2,000, `kReachExpansionCap` 5,000, `kFarRouteExpansions` 60,000), a sixth departure in section 1.1, which goes with WOR-799 (Nav: one full-detail mesh with no coarse layer) and WOR-789 (Far route requests stall the main thread for 19 to 34 ms; run them on a worker). The "accepted for now" note in section 1.1 now speaks of a search that cannot finish. Rule 36 keeps open the size of the time budget and how a search resumes. This narrows entry 81 (the budgets as step counts) and the "step limit" in entry 97. Entry 18 and section 4.8 stand: the per-tick patch budget is a count, a different budget from a search's.
    - The geography mesh comes later (2026-11-12). It is not part of the current nav correction: it is its own work under WOR-802 (Geography mesh (Tier 1): computed at load, always exists), after the correction, because long-distance travel is not being done yet. Until then the real mesh answers only for ground movers have meshed. Rule 38 and entry 97 stand as the design.

**2026-11-12**

99. **The correction is built: the code matches rules 18 and 22 to 34.** Built on the integration branch in this order: component labels on the mesh; a resumable path search; a chunk hold range per mover; the route queue; movers mesh and the camera does not; collision and the snap on every mover; the AI's tests off the camera; route requests by ticket; the AI switched to tickets; the synchronous route path deleted; label queries and position checks on the mesh; the AI's ticket handling; the AI's target choice; reach and the structural mesh deleted. Sections 1.1, 2 and 4 describe the result. This reverses, as built:
    - Entries 24 and 35 (patches on two workers, one per mesh): one patch worker, plus a route worker (4.8).
    - Entries 45, 50, 54, 55, 58 and 66 (placed obstacles priced, a route through them exempt from collision and the snap): they block, and nothing is passed a price.
    - Entries 47, 60, 77 and 88's cap (the view brings placed obstacles in, collision only where detail is on, the 40,000 cap and its trim): movers bring them in, nothing trims them.
    - Entries 14 and 15 as the area a mover meshes and the anchor set: a mover is `Position` + `AgentRadius` and holds the larger of 128 m and its range; entry 14's 128 m and 256 m stand as the floors.
    - Entries 26, 56 and 92's re-pricing, 73 to 76, and 79 to 87 (reach on the structural mesh, its memo and stamps, the step budgets, the far route, "search gave up", the gave-up record, ground shed when only the camera held it, the settled-ground wake): deleted. Entry 76's anchors on a route's rest and a waiting goal are deleted too.
    - Rule 19 and entry 82 (the far route's main-thread stall): there is no far route.
100. **Component labels answer "is there a known way" (4.6).** One label per triangle, owned by the mesh and kept per patch from its report. Water and placed obstacles block; walls read open; width is zero. A patch report now lists each changed triangle with why (freed, created, flipped, border, widths) and whether the patch can open a way. Widths are no longer refreshed inside a patch: `refreshWidths` is one batched call per job that returns its own report, so a tick's changes are its patch reports plus that one. The label epoch moves on a join, split, fresh or emptied label, an opening patch, and a door added; the emptied-label case was a bug found by an independent epoch test and fixed. `connected` and `connectivityEpoch` read them; no query reads anything else for reach.
101. **The path search is resumable (`SearchState`).** It runs in steps, holds any number of goals and answers the nearest by walk, and with one goal first searches from the goal for up to 2,000 expansions. `pathThrough` is the same search run to the end, and gives identical answers at the same speed on about 1,450 queries. Nothing in it is `thread_local`: the star memo became per search (about 7 to 10% slower a query, about 60 B per vertex asked about), and every fan walk covers the whole fan. The forest pre-check moved out of the search into `pathThrough`, so a queued request whose goals are all unreachable would flood the start's component; the caller rejects such a request by labels first.
102. **Routes run on a worker under work per tick, the form of rule 18 that Kevin accepted** ("The time budget above sounds fine to me", 2026-11-12, answering the choice between work per tick sized from a time target and a wall clock; the reading as the first form, not a wall clock, is an engineering call). `kRouteWorkPerSlice` (18,000) expansions a slice, shared `kRouteQuantum` (2,000) at a time, from 8 ms of route worker a tick at an estimated 0.45 us an expansion. There is no safety ceiling on a search, since a ceiling is a step limit; the mesh is finite, so every search ends. A request copies the belief's sets when it is made, so the worker never reads sets the main thread is inserting into. This replaces the step budgets of entry 81 and reverses entry 98's "as built" half.
103. **The route step went in as three: add, switch, delete.** The plan had one step add the ticket API and delete the synchronous `requestPath`, which could not compile until the AI stopped calling it. So the ticket API went in beside the old path, the AI switched to tickets, and the old path was deleted after, keeping every commit building and passing. The switch and the delete landed as a pair of commits, only the second green.
104. **A search restarts on every patch that changes what it read, with no deferral box.** The plan restarted a search once and then held back every patch meeting the box of what it had read. A patch's hole takes whole vertex stars and can reach far past its edit on open ground, so a box cannot stop a second hit, and predicting the triangles a patch will free (`PatchableMesh::holeOf`, built for this) caught only 37% of them on the fixtures, because the flips that re-legalize a hole's boundary cannot be predicted without doing the patch. So no prediction can bound a search's restarts. The deferral and `holeOf` are deleted; restarts are counted, never limited, never asserted; and a search that spans ticks is protected by holding additions (entry 105).
105. **The hold on additions, and its correction.** First built: while any search spanned ticks, every addition (new ground, newly meshed placed obstacles) was held, for at most 240 ticks, then released for one tick before holding again. That starved meshing: the hold was global, re-armed the tick after its release, held ground and the cells round movers too, and the released tick took one tick's budget, so while any search spanned ticks meshing ran at 1/241 of its rate, and a few same-component "no known way" searches in a row (each floods its component, about 30 ticks at a square kilometre of dense ground, an estimate) kept it on. Now: ground, a mover's own cell and the eight touching it, removals, walls, doors and water are never held; only placed obstacles in cells outside that near lane wait, and only while some search is inside its own window of `kMaxHeldAdditionTicks` (240), counted from the first update it is still running after a turn and never reopened by a restart. With no search in its window held cells go in at the normal budget until none wait. 240 ticks is half the 8 s a 16 m buffer gives a mover walking at 2 m/s.
106. **Movers mesh nearest first, and only chunks a mover holds are served.** As first built a mover's disc was taken from the point where it entered its cell, so coverage round it could fall to about 23 m, under its 30 m sight; cells were served first in, first out, so cells behind a mover went before those ahead, a spawned mover's own cell sat mid-queue, and a mover with a large range delayed everyone; and any wanted chunk that happened to be loaded was served, so a wanted chunk that unloaded before service waited for the camera to load it again. Now a disc is every cell within R of the mover's cell's square, taken nearest first; a mover's own cell and the eight touching it go to a lane ahead of the queue, with their chunks' ground; and only chunks a mover holds are served. The camera decides nothing about the mesh, including when.
107. **The snap and collision are one rule (4.7).** Collision pushed a mover out of a placed obstacle along the axis of least penetration, usually backward, in the same tick the snap had left it inside with nothing clear ahead, so the game reversed a mover the snap had kept. The tests ran nav alone and passed. Now the snap records the movers it leaves inside (`snapHolds`) and collision leaves them alone; a held mover whose route went stale with nothing new by the next tick steps to the nearest clear spot within 4 m that is not behind its heading.
108. **Position checks read the mesh.** `isValidPosition`, buildability, `nearestPathablePoint`, `findValidPositionNear`, the landing, `placedObstacleAt` and `standPointBeside` answer from the mesh alone. The placement data they read before disagreed with the mesh after a chunk unloads, since the mesh keeps what was meshed and placement data does not. `standPointBeside` answers nothing when no standable spot is beside the target (it used to return a spot inside a neighbour's collider), and `placedObstacleAt` lost its reach parameter.
109. **The route API's edges.** `requestRoute` judges both ends where a search would stand them (`standingTriangle`) before its at-once label check; with the raw located triangle, a goal just inside a placed obstacle's ring was refused where a search would have routed it. A start that locates nowhere is its own outcome, `StartOffMesh`, never "no known way". `routeTicketLive` tells a ticket still being searched from one nav has dropped or never knew, which `takeRouteAnswer` alone could not, so a caller is never left waiting on a dead ticket. An answer untaken for `kUntakenAnswerTicks` (600) is dropped. Answers carry the label and patch epochs they hold for, and a finished answer not yet delivered is checked against the patches before its delivery and restarted on a hit, so a stale "no known way" can never be delivered under a newer epoch.
110. **The AI asks by ticket, and its choice of target.** Built: a colonist stands in the awaiting-route state while his route is out, or keeps walking his old route while a replacement is out and joins the new one where he stands; the panel shows "Finding a way" (standing) or "Re-routing" (walking) after 10 ticks; a target inside a placed obstacle is routed to its stand point and judged arrived there; an answer for a goal the task has since left is dropped and asked again; "no known way" while the snap holds him waits for the snap. The target choice replaces the reach walk. Its rule: a route's answer is the truth about whether this mover can get there, so "no known way" is a verdict on that target for him, remembered until the label epoch or his belief moves, and a target with a live verdict is not asked about again; a need's source is chosen by one nearest-of route request over the nearest few connected candidates with no verdict, so a sealed nearest source loses to the next in the same request. This fixed a livelock in which a source sealed for his width but connected at zero width was chosen, answered "no known way", and chosen again forever. A verdict is recorded under the epoch as it stands and his belief version. A move of his belief clears every verdict at once; a move of the label epoch clears only verdicts recorded before it and at least `kVerdictMinTicks` (300) AI updates old, so a busy colony, where the epoch moves often, cannot keep wiping them, and a newly opened way can be noticed up to 300 updates (5 s at 60 updates a second) late. A need round is one nearest-of request over the nearest `kNearestOfCount` (8) connected sources with no verdict; a round that ends in "no known way" widens that need's next round to 16, then `kWidestRound` (32, the capacity of `SourceChoices`), and a routed answer resets it. A failed route decides once at the failure, then on the 0.5 s interval until a route is found. While the snap holds him his decision waits, one interval per wait, up to `kSnapWaitDecisions` (4); a snap wait that runs out fails the route with no verdict on the target. A nearest-of answer whose source another colonist has claimed meanwhile is not taken: his claims are retired, the task cleared, and he decides again at once. A decision locates him once and costs one component-label lookup (`NavigationSystem::componentAt`) and a compare per target; `standPointBeside` runs only for targets with no label that are on the mesh, and route ends are cached per decision. A wander point must share his component label and fit his disc (8 rim samples of `isValidPosition`); a wander under way keeps its point and probes nothing. A late route is joined at his projection onto its nearest leg (`joinRoute`). Shore points are generated every 1 m along shore edges, 300 mm onto land (`kShoreOffsetMm`), so they have a land face, pass the connected filter, and are their own route goal. The bench row `BM_NeedDecision` (1, 5 and 40 sources) is in `NavigationSystem.bench.cpp` and has not been run.
111. **`generation()` moves on ground, water, walls and doors only.** It goes up by one at the join of any patch job in which ground arrived, water was re-derived, walls changed, or a door was added or removed; placed obstacles landing or leaving never move it. A route a newly meshed placed obstacle crosses is marked stale one by one, and the AI asks again by ticket. `NavQueryStats::groundWaterWallPatches` counts ground, water and wall patches, and an AI test reads it.
112. **Not done in the correction (the geometry clean-up since landed, entry 114).** Record slimming (WOR-801, Nav: cut the mesh cost of a placed obstacle from about 7.2 KB to about 1.25 KB) was planned as part of it and was not done, so a placed obstacle still costs about 7.2 KB. The geometry clean-up was not done either: prices, the second corridor width (still configured on the nav mesh), the reachability forests and `pathThrough`'s pre-check on them, and the benches for the deleted reach budgets remain, unused by the sim. The route and walking-mover measurements planned for the end of the correction have not run; `BM_RouteSlice` (1, 20 and 200 requests pending) compiles.
113. **Four review rounds.** Every must-fix below is fixed, with a test.
    - Round one, of the labels, the search and the route queue. Findings: the deferral box could not stop a second hit; an answer finished in a slice was delivered after that tick's patches with no epoch on it, so a stale "no known way" could be stored under a new epoch; a door added did not move the label epoch. Fixes: entries 104 and 109; `addDoor` moves the epoch; a new request gets its first turn at the head of the next slice; the star memo became per search and the forest pre-check left the search (entry 101).
    - Round two, of the chunk hold, mover meshing, collision and the snap. Findings: the camera still decided when some wanted ground and placed obstacles were meshed; collision undid the forward snap; a mover's disc could cover less than its sight; first-in-first-out service ignored nearness; a goal off meshed ground read "maybe" while its chunk was loaded, so a chunk only the camera loaded could park a colonist waiting for good. Fixes: entries 106 and 107; the "maybe" went with reach (entry 99).
    - Round three, of routes by ticket. Findings: the hold starved meshing; the at-once label check refused goals on an obstacle's edge; a dropped ticket looked the same as one still searching; `standPointBeside` could return a spot inside a neighbour's collider; on the AI side, every replan stopped a walking colonist for at least two ticks, up to four times per 16 m cell in dense ground; a target at or inside a placed obstacle had no route, so a harvest dropped what could not be carried at the obstacle's centre, out of reach for good; a colonist the snap held asked for a route every frame; a dropped ticket was awaited forever; an answer was applied without checking it was for the task's current target. Fixes: entries 105, 108, 109 and 110.
    - Round four, of the AI's target choice. Findings: verdicts from searched answers rarely survived in a busy colony, because the label epoch moves on any merge, fresh label or opening anywhere, so the livelock could return (needs asked the same 8 sealed sources every round and never reached the 9th); a decision while the snap held him dropped his task; when the snap wait ran out a verdict was recorded on a target that was fine; a nearest-of answer could hand him a source another colonist claimed meanwhile; each candidate cost about four locates and a stand-point search, recomputed every frame for colonists deciding every frame; a wander point sealed for his width flooded the whole component with no verdict; a joined route could steer straight across the corner the new route bends round. Shore points pass the connected filter (entry 110). Entry 110 describes the fixes.

**Defaults for the open questions in section 8**, as first set. Questions 1, 3, 4 and 5 are
answered (section 8), so these defaults are the record of what was planned, not what is built.
Question 2 has no default here; it is settled under WOR-710 (Step 7: Mover footprints in the
mesh).

- **Flora that seals (question 1):** a mover with no way around walks through flora at a high path cost. The alternative makes answers differ on and off camera, which rule 6 forbids. Replaced by rule 23: placed obstacles block.
- **Footprints off camera (question 3):** they do not block where detail is off, the same as flora. Collision still separates movers. Replaced by rule 22.
- **Detail around off-camera working colonists (question 4):** none. Only the viewport requests detail in this epic. Replaced by rule 22.
- **Load range around a mover (question 5):** the chunks within 128 m of it. Now the larger of 128 m and the mover's range (4.5).
114. **The geometry clean-up and the disc rule landed after the correction.** The geometry library lost tag prices and the squeeze, the second corridor width (`structuralWidthMm`, `detailTags`), the reachability forests and `PatchReport::freedTriangles` and `newTriangles`. `pathThrough` rejects a query across components by their labels, and a path costs its length. `BM_SearchReach`, `BM_SearchDeadEnds`, `BM_ForestRebuild*` and `BM_SealedGoal` are deleted. A mover now wants every 16 m cell whose nearest point is within R of where he stands, retaken every 4 m (4.4); before, the distance was cell to cell over a 7 x 7 square of cells. Still open: record slimming (WOR-801), chunk-border slivers (WOR-809), bench fixes (WOR-808).
115. **Chunk borders are dissolved where two open squares meet (WOR-809, chunk-border slivers; 2026-11-12).** Measured with `BM_ChunkSeam` on two squares with 5,135 placed obstacles: within 16 m of the shared side the longest edge had mean 4.43 m, p99 22.1 m and max 64.0 m against 4.5 and 4.7 m in the interior; aspect p99 was 358 against 10; 14 triangles had an edge over 32 m. The cause was entry 51's constrained, 64 m split side: every constrained edge is skipped by the flip pass, and a triangle's cover named its square, so a triangle across the side would have lain in neither. Fixed in two moves. Covers carry one ground entry for all open-ground rings, with parity counted per entry. A bounds ring edge whose identical segment belongs to another live open-ground ring is dissolved: no constraint, no mesh edge, its end points kept as loose vertices (pins) so empty ground keeps its shape. Blocked rings such as water keep their edges; a side with ground on one side only stays the constrained boundary. `buildNavMesh` applies the same rule through one helper, so a patched mesh still equals a build from scratch. Long edges are cut from their lower end point so both neighbours cut alike, and a point on a shared side or corner belongs to exactly one square by a fixed perturbation. Rejected: finer pieces (only shortens the slivers, and costs the ground budget, entry 51); refinement points (break vertex equality with a build from scratch). After: the border band equals the interior (longest edge 3.71 / 4.53 / 4.72 m mean / p99 / max, aspect p99 10.3, no edge over 32 m); locate 120 to 102 ns; a 40 m route across the border 77 to 66 expansions; a placed obstacle on a shared side 180 to 22.6 us; an interior placed obstacle unchanged at about 31 us to add and 28 us to remove (a per-vertex pin flag keeps the pin lookup off the interior path). Memory per placed obstacle is unchanged by it (`sizeof(RegisteredRing)` 248 B). What remains is under "Chunk-border slivers" in 1.1.

**2026-11-12 and 2026-11-13**

116. **The mesh is the union of what movers have seen, and it ends there (rules 39 to 42).** Kevin, on seeing the overlay of entry 115's build, 2026-11-12: "we don't really know whats out there, so the current mesh with those arbitrary points has created a low res mesh that is wrong. I can clearly see trees inside those triangles that reach out to the arbitrary points." Then: "we should assume that outside the area that has been meshed, that its impassible. the way its built now its within the mesh. so the mesh needs to just end, not link out to chunk edges or anything like that." On the kept points: "these are still arbitrary points that are not associated with any object in the game. the chunk edges as you say, are an irrelevant place to path to." On what a vertex is: "there might need to be three types of nodes 1) nodes of a collideable object (the tree) 2) nodes of the known world (the geography mesh + the loaded chunk edges) 3) nodes of the visibility limit of the mover", and for objects, "this would require no concept of a 'cell'"; on the step, "4 is fine for now, but make sure is a single constant that we can tune later." On the landing: "landing should mesh a large area, they saw it as they were crashing!", and on 150 m, "That's fine. Make sure it's a tunable constant". This replaces his earlier answer the same day on terrain arriving a chunk at a time ("Leave as is for now."). Built as five stacked pull requests, in order: #369, slanted outlines (`buildNavMesh` classifies faces by a parity flood; the patch seeds covers exactly, reads them along a ray when no sample is clear, and fails loudly when it cannot); #370, no kept points; #371, overlapping ground outlines (`groundBoundaries`, `groundCovers`, the ground edge and ring grids); #372, the mesh edge as a wall for corridor widths; #373, the game side (limits as ground, placed obstacles by entity, urgency and stopping short, removal by key, `AwaitingMesh`, the landing, and long-edge splitting deleted). Decided along the way, at the plan's defaults: cut points where a placed-obstacle or water outline crosses the seen edge are kept; water stays cut at chunk sides; a limit has 16 sides; a mover whose next limit cannot go in stops short; a mover's first limit is urgent; a placed obstacle added over meshed ground marks the routes it crosses; the landing's cleared placed obstacles are out of the mesh before the crew stands. Found and fixed on the way: a waist between two facing corners narrower than a disc let the disc through, because a corner counted as an obstacle only when one of the triangle's own edges at it was one (on main before this work); and a patch could miss an edge whose input, not its rounded pieces, a new segment meets (on main before this work too). Reverses: entry 51 (64 m pieces of chunk sides), entry 106 and 114's disc rule (cells), entry 109's `StartOffMesh` (now `AwaitingMesh`, for start or goal), and entry 115 (pins, partners, and the long-edge cut). Known limits and costs are in 4.11. Undo: revert the five pull requests, which brings back chunk squares and the arbitrary points Kevin ruled out.
