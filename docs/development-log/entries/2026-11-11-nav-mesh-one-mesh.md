# 2026-11-11 - Nav mesh: one mesh, variable resolution (steps 1 to 5 of WOR-702)

## Summary

Navigation no longer builds a separate mesh per region. There is one persistent nav mesh over every
loaded chunk, patched where something changes, and a smaller structural mesh (ground, water, walls,
doors) that answers reachability. Regions, their clustering, and their async rebuilds are deleted.
Two points on connected ground always route (WOR-644), and chunks stay loaded around every mover, so
panning away no longer unloads the colony (WOR-645). Steps 6 (the route system out of
`AIDecisionSystem`) and 7 (mover footprints) are not built.

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
  (trees) read as open. The width refresh starts from the edges a patch changed and walks outward.
- **Path search rewritten.** The old search scored a corridor by the distance between triangle
  centres, so it followed the triangulation. The new one carries each corridor's funnel and scores
  by its taut length. An exact visibility-graph referee checks it: 0 of 2,297 pairs on 300
  wall-and-post layouts are longer than the shortest path, where the old search was longer on 633,
  the worst by 350%. Ties break on coordinates, so the same rings reached through different edit
  orders give the same path.
- **Priced faces.** A query can list obstacle kinds passable at a cost multiplier. A gap too narrow
  for the mover between priced obstacles is a squeeze, charged, and the result says it crosses
  priced ground.
- **The reverse-search heuristic (RRA) is deleted.** It no longer paid for itself against the new
  score.
- **Reach forests** rebuild 2 to 3 times faster and reuse their storage.

### World and sim

- **Chunk anchors.** `ChunkManager::update` takes the movers' positions; a mover loads chunks within
  128 m and keeps them within 256 m.
- **`NavigationSystem`** holds the two meshes. Each tick's edits are gathered on the main thread,
  applied on two worker threads after every system has run (a new `ISystem::endTick`), and joined by
  the next update or the first nav query, so a query always sees every edit gathered through the
  previous tick.
- **Budgets.** Ground lands under a per-tick vertex budget, a chunk with water alone. Flora adds and
  removals each touch at most two 16 m cells a tick. Ground of unloaded chunks is kept for the 16
  most recently unloaded, never dropping one the construction world touches. Flora rings are capped
  at 40,000, whole cells leaving least recently wanted first.
- **Flora is priced, not walled.** A goal ringed by trees is reached by walking through. Flora is
  priced the same on camera and off, which departs from the spec's section 4.4 (decision 50).
- **Detail** (on near the view, with hysteresis) decides only what collision and the forward snap
  do. A mover a tree lands on moves forward along its route and keeps its task.
- **Staleness is targeted.** The nav generation moves only on structural changes. Flora landing
  marks stale just the routes it crosses; a mover more than 2 m off its route is marked stale too.
- **`MovementSystem`** spends a tick's stride along the route past every corner it reaches.
- A goal in ground that is neither loaded nor meshed reads unreachable, so a colonist takes its next
  choice instead of holding.

### Measured

Headless, 2026-11-11, no game or build running, GPU 30 to 40% from desktop apps (the load check read
busy before and mostly-idle after, so not a baseline). Old figures are from an earlier, busier run.

- Main-thread time in the nav update, worst tick: pan at 83 m/s 737 to 84 us, zoom sweep 381 to 98,
  a wall built 210 to 90, twenty colonists cold 783 to 512. The mean is lower in every row except
  twenty colonists cold, 13.7 to 18.1 us.
- A nav query waits for the patch job, which the old system never did. Worst wait with a 2 ms gap
  between ticks: none for slow pans, walls and walking; 1.2 ms on a long pan; 4.3 ms when a
  shoreline chunk's ground arrives.
- Path requests per 64 with flora priced: first time 1,519 to 3,143 us against 3,361; repeat 1,249
  to 2,153 against 516.

### Reviews

Ten independent review passes over the geometry and the game-side code. Every must-fix found
is fixed with a test that reaches it; what was filed instead of fixed is under the epic as WOR-721
to WOR-729.

## Related Documentation

- [Nav mesh: one mesh, variable resolution](../../technical/nav-mesh-variable-resolution.md): the
  spec, with the decisions log (section 10, entries 1 to 69) and every number above in context.

## Next Steps

- WOR-721: a shoreline chunk's ground is a 6 ms job the main thread can wait on. Split a patch into
  a read-only prepare and a short commit.
- WOR-725: priced path requests cost 1.5 to 2.8 times unpriced ones.
- Step 6 (WOR-709) and step 7 (WOR-710), and with step 6 the removal of the region-era members of
  `NavigationSystem` that `AIDecisionSystem` still calls.
