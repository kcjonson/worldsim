# 2026-11-11 - Nav mesh: one mesh, variable resolution (steps 1 to 5 of WOR-702)

## Summary

Navigation no longer builds a separate mesh per region. There is one persistent nav mesh,
patched where something changes, and a smaller structural mesh (ground, water, walls, doors)
that answers reach. Regions, their clustering, and their async rebuilds are deleted. Two points
on connected ground always route (WOR-644), and chunks stay loaded around every mover, so
panning away no longer unloads the colony (WOR-645).

Nav's job, by Kevin's ruling: given coordinates, build a path there, from any place to any
place, with no distance limit and no notion of who asks or why. There is no colony ground. A
reach check answers a walk length, a definite no, or "far", and far is never read as no; a route
search that runs out of steps says "search gave up", never "no route". The search budgets come
from measurements. Ground a mover has ever held stays meshed for the life of the world.

Steps 6 (the route system out of `AIDecisionSystem`) and 7 (mover footprints) are not built.

## Details

### Geometry (`libs/geometry`)

- **Arrangement broad phase.** The all-pairs segment loop became a uniform grid. A 128 m forest
  region builds in 14.5 ms where it took 234, byte-identical output.
- **`PatchableMesh`.** Add, remove and move are one operation: collect the changed rings' edges, cut
  a hole with a fixed boundary, re-derive it from the rings with the arrangement and the constrained
  Delaunay code, splice it into stable slots. After any sequence of edits the vertices and
  constrained edges equal a from-scratch build, tested on fixtures and 12,000 adversarial states.
  Storage is paged and never moves; hash tables grow incrementally; no patch pays for the size of
  the mesh.
- **Two stored widths per corner**: the full corridor width, and one with detail-class obstacles
  read as open. The width refresh starts from the edges a patch changed and walks outward.
- **Path search rewritten.** The old search scored a corridor by the distance between triangle
  centres, so it followed the triangulation. The new one carries each corridor's funnel and scores
  by its taut length. An exact visibility-graph referee checks it: 0 of 2,297 pairs on 300
  wall-and-post layouts are longer than the shortest path, where the old search was longer on 633,
  the worst by 350%. Ties break on coordinates, so the same rings reached through different edit
  orders give the same path. `PathLimits` caps a search by cost, by expansions, or to a box; the
  box is no longer used by the game.
- **Priced faces.** A query reads a table of obstacle kinds passable at a cost multiplier. A gap
  too narrow for the mover between priced obstacles is a squeeze, charged, and the result says it
  crosses priced ground.
- **The reverse-search heuristic (RRA) is deleted.** It no longer paid for itself against the new
  score.
- **Reach forests** rebuild 2 to 3 times faster and reuse their storage.

### World and sim

- **Chunk anchors.** `ChunkManager::update` takes anchors: every mover the AI drives, the rest of
  every live route, and the goal of every task waiting for ground. An anchor loads chunks within
  128 m and keeps them within 256 m.
- **`NavigationSystem`** holds the two meshes. Each tick's edits are gathered on the main thread,
  applied on two worker threads, one per mesh, after every system has run (a new
  `ISystem::endTick`), and joined by the next update or the first nav query, so a query always
  sees every edit gathered through the previous tick.
- **Reach and routes.** `walk()` searches the structural mesh under the colonist's belief over all
  meshed ground: from the goal first (2,000 steps), so a sealed pocket is a definite no at once,
  then from the start (5,000). When both run out the goal is far; a far candidate stays a
  candidate and is taken when nothing with a known walk is. `requestPath` runs the same walk, then
  the nav search with detail priced (falling back to the walk itself if that runs out). A far
  goal gets the structural search from the start to 60,000 steps instead, its walk taken as the
  route; past that the answer is "search gave up", which the decision trace and task list show as
  its own status. The 128 m reach square of the first merge is gone.
- **Ground.** Ground lands under a per-tick vertex budget, a chunk with water alone, ground an
  anchor holds before the camera's. A chunk an anchor has ever held keeps its ground forever;
  only ground the camera alone held is shed, past 16 unloaded chunks, never one the construction
  world touches. Mesh existing is not ground being visible: player visibility is planned
  (WOR-754 (Player visibility: unexplored areas (visible, seen, unseen))), not built.
- **Detail.** Placed obstacles enter the nav mesh only under the view, two 16 m cells a tick, capped
  at 40,000 rings, whole cells leaving least recently wanted first. They are priced, not walled,
  on camera and off, so a goal ringed by them is reached by walking through; this departs from the
  spec's section 4.4 as first written (decision 50). Detail being on decides only what collision
  and the forward snap do. A mover an obstacle lands on moves forward along its route and keeps
  its task.
- **Naming.** Nav, mesh and collision code never name trees or flora: "placed obstacle" and
  "detail".
- **Staleness is targeted.** The nav generation moves only on structural changes to ground an
  anchor holds, and routes the change doesn't close are re-stamped. Detail landing marks stale just
  the routes it crosses; a mover more than 2 m off its route is marked stale too. A colonist whose
  route failed while ground near it was still arriving is told to decide when that ground lands.
- **`MovementSystem`** spends a tick's stride along the route past every corner it reaches.
- A goal in ground that is neither loaded nor meshed reads unreachable, so a colonist takes its next
  choice instead of holding. Travelling toward it is deferred (WOR-776 (Nav: build a path toward a
  goal whose ground is not meshed yet)).

### Measured

Two headless runs, neither with a game or build running.

The first (commit 210b09a1, the density before the forest floor; the load check read busy, so not
a baseline; old figures from an earlier, busier run):

- Main-thread time in the nav update, worst tick: pan at 83 m/s 737 to 84 us, zoom sweep 381 to 98,
  a wall built 210 to 90, twenty colonists cold 783 to 512. The mean is lower in every row except
  twenty colonists cold, 13.7 to 18.1 us.
- A nav query waits for the patch job, which the old system never did. Worst wait with a 2 ms gap
  between ticks: none for slow pans, walls and walking; 1.2 ms on a long pan; 4.3 ms when a
  shoreline chunk's ground arrives.
- Path requests per 64 with detail priced: first time 1,519 to 3,143 us against 3,361; repeat 1,249
  to 2,153 against 516.

The second (commit 7e68a0e2, main's placement density, the Quick Start planet's chunks; CPU 2 to
5%, the GPU idling, which the load script misreads as busy (WOR-790 (machine-load.mjs reads an idle
GPU as busy)); ruled baselines by Kevin):

- A structural search step is 0.29 to 0.32 us, 0.56 at worst; a priced step in dense cover 0.45,
  1.48 at worst. So a reach walk is under 4 ms at worst and a far route request 19 ms, 34 ms at
  worst, once per far target, on the main thread. Kevin accepted that for now; WOR-789 (Far route
  requests stall the main thread for 19 to 34 ms; run them on a worker) is the follow-up.
- With the earlier budgets (a 20,000-step far nav search) 0 of 18 real far goals routed; the
  structural search found all 18 in 25,000 to 41,000 steps. Every real far goal to 10 km routes
  within 60,000.
- Detail costs about 6.4 KB a ring at main's density, so the 40,000-ring cap is about 250 MB.
- Both meshes over real ground: 29 MB at 121 chunks, 265 MB at 1,000.
- A wall edit's structural job passes the 2 ms job budget past about 121 meshed real chunks (2.2 ms
  at 121, 14.7 at 1,000), all of it the forest rebuild.
- Main's density doubles job and path cost against the old density: a priced `requestPath` is 133
  us against 40.

## Related Documentation

- [Nav mesh: one mesh, variable resolution](../../technical/nav-mesh-variable-resolution.md): the
  spec, with Kevin's rulings (section 1, rules 14 to 21), the decisions log (section 10, entries 1
  to 89), and every number above in context.

## Next Steps

- WOR-789 (Far route requests stall the main thread for 19 to 34 ms; run them on a worker).
- WOR-722 (Reach forests are rebuilt whole on every structural change): per-chunk forests, so a
  wall edit stops growing with meshed ground.
- WOR-721 (Shoreline chunk arrival can make the main thread wait on the nav patch job): split a
  patch into a read-only prepare and a short commit.
- WOR-725 (Priced path queries cost about twice unpriced ones).
- WOR-776 (Nav: build a path toward a goal whose ground is not meshed yet), and with it WOR-775
  (Exploring: a colonist blocked on the way to a remembered place looks for a way round).
- Keep or delete `PathLimits::within`, which the game no longer uses.
- WOR-709 (Step 6: Movers (route system out of AIDecisionSystem, a wandering deer)), and with it
  the removal of the region-era members of `NavigationSystem` that `AIDecisionSystem` still calls;
  WOR-710 (Step 7: Mover footprints in the mesh).
- Run the branch in the game; nothing since the reach rework has been.
