# Debug Log: Navmesh Zero Walkable

**Issue:** Navmesh builds with zero walkable faces; colonist freezes ("Waiting for the area to settle")
**Started:** 2026-06-26
**Status:** AWAITING_USER_CONFIRMATION

---

## Problem Summary

When the game loads the quickstart world, the navigation mesh builds successfully but classifies
EVERY face as a common-knowledge-terrain blocker (water/tree). Result: zero walkable faces, so no
colonist can path anywhere; the colonist sits in "Waiting for the area to settle" forever.

**Observed (in-game, fresh build 2026-06-26):**
```
[NavBuild] buildInput 29.15 ms: polys=639 walkableBorders=1 blockedRings=638
[NavBuild] buildNavMesh 12349.88 ms: tris=1848 verts=3127
[NavBuild] mesh swapped gen=1 tris=1848 walkable=0 floor=0 blocked=1848
```

**Expected:** The land (open grass with scattered trees) is walkable; only river water and
tree/wall colliders block.

## Key Facts Established (CONFIRMED only)

1. The build COMPLETES (no crash/hang). buildInput ~29 ms; buildNavMesh ~12.3 s (slow O(n^2)
   arrangement — a separate perf concern, not this bug).
2. The INPUT is correct: `walkableBorders=1` (the walkable border ring IS present) plus 638 blocked
   rings (a few water rings + ~630 tree colliders).
3. The OUTPUT is 100% wrong: all 1848 triangles tagged blocked (common-knowledge terrain).
   walkable=0, floor=0.
4. The real geometry is a Y-SHAPED RIVER CONFLUENCE through open forest: three river branches meet
   mid-area and each EXITS at a different area edge. (Quickstart, landing lat=43.88 lon=36.61; Bob
   spawns at (1.0,4.0).) NOT an island, NOT a closed pond.
5. A PRIOR fix (`representativeOutsideHoles` in `libs/geometry/nav/NavMesh.cpp`) is compiled in
   (build time rose 10.2 s -> 12.3 s) and passes 261 geometry unit tests on HAND-BUILT cases, but
   does NOT fix the real geometry — walkable=0 persists. So a hole-aware classification rep-point is
   not the (whole) cause.

## Evidence Artifacts
- `libs/geometry/nav/NavMeshRealRings.test.h` (755 lines) — the REAL water/border rings dumped from
  the live quickstart build by an earlier investigation. Use THIS for a faithful reproduction, not
  hand-built rings (hand-built is what produced false confidence in the prior fix).
- In-game verify loop: rebuild `world-sim`, launch `world-sim.exe --scene=game`, grep stdout for
  `[NavBuild]`. `walkable>0` + Bob moving = fixed.

## Scout Agent Findings
**Explored by:** 3 parallel scouts (data-flow, coordinate-math, edge-cases), all grounded in
`NavMeshRealRings.test.h`.
**Total hypotheses generated:** 9 — **after dedup:** 4 (3 substantive + 1 low).

Convergence: all three independently concluded the water ring (for a 3-edge-exit confluence) ends up
effectively CONTAINING the land — every face reads "inside water." Key discriminator (coordinate
scout): a simple parity flip would make everything WALKABLE (even depth), but the symptom is
everything BLOCKED, so the land is read as *contained by* water (odd depth), not merely flipped.

## Hypotheses (Ranked)

### H1: Water ring's even-odd interior is the LAND (every face reads inside-water -> blocked) [HIGH, 3/3 consensus]
- **Status:** untested
- **Suggested by:** all 3 scouts (pointInPolygon-all-inside / winding-inversion / water-runs-along-border).
- **Mechanism:** the marching-squares closure for water exiting at 3 edges ("out of bounds is land",
  `NavInputBuilder.cpp:113-176`) yields a water ring whose winding/shape makes the LAND its even-odd
  interior (or it encloses the whole area). So `pointInPolygon(grassRep, waterRing)=Inside` for every
  grass face -> waterDepth odd -> water blocker. Classification: `NavMesh.cpp:776-806`.
- **Why it fits all-blocked (not all-walkable):** everything reads CONTAINED by water, not a flip.
- **Decisive cheap test (NO build):** compute the signed area of `realWaterRing()` and test even-odd
  containment of a known grass point (spawn (1000,4000) / center (0,0)) against the water ring,
  straight from `NavMeshRealRings.test.h`. Inside -> H1 confirmed; Outside -> H1 refuted, see H2/H3.

### H2: Grass faces DISCARDED by the in-bounds border filter (false negatives) [MEDIUM, 1/3]
- **Status:** untested
- **Mechanism:** the face in-bounds filter `pointInPolygon(rep, border)` (`NavMesh.cpp:681-691`)
  returns Collinear/Outside for grass faces near where water touches the border, so grass faces are
  dropped; only water+tree faces survive -> all blocked. (Land faces don't EXIST, vs H1 mistag.)
- **Test:** log how many faces pass the in-bounds filter; check `pointInPolygon(spawn, border)`.

### H3: Big land-face-with-holes fails triangulation; only blocker interiors emit triangles [MEDIUM, 1/3 — carried lead]
- **Status:** untested
- **Suggested by:** edge-case scout + the earlier stopped investigation (WIP in `Triangulation.cpp`).
- **Mechanism:** the land face carries the water + ~630 trees as holes; the hole-bridge scan
  (`Triangulation.cpp` mergeHoles/bridgeIsClear ~275-415) fails (a hole touches the border / the
  confluence pinch) -> the land face emits 0 triangles -> walkable=0; the 1848 tris are blocker
  interiors. Consistent with tris=1848 all-blocked IF those are blocker interiors, not land.
- **Test:** log per-face emitted-triangle count — do floor faces emit 0?

### H4: default-blocker init / empty-or-wrong blockedRings [LOW]
- **Status:** largely self-refuted by the scouts (`WalkableFace.blocker` defaults to kNoBlocker; input
  has 638 rings). Recorded for completeness.

---

## Investigation Log

### Session 1 (2026-06-26)
**Goal:** Initialize formal debug session; capture established facts; spawn scouts.
**Tried:** Confirmed in-game walkable=0 on a fresh build; confirmed the prior `representativeOutsideHoles`
fix is compiled but insufficient; stopped an autonomous fix-agent in favor of the disciplined
protocol; preserved its real-ring capture (`NavMeshRealRings.test.h`) + WIP.
**Learned:** Input is correct (walkableBorders=1); the all-blocked output is a classification/
triangulation problem, not an input problem. Real geometry is a multi-edge-exit Y-confluence that
the hand-built tests never exercised.
**Debug code added:** `[NavBuild]` LOG_INFO instrumentation in `NavigationSystem.cpp` (buildInput /
buildNavMesh / mesh-swapped timing + walkable/floor/blocked counts). `NavOverlay.cpp` color-coded
(yellow=walkable, red=blocked). `[NavDiag]` LOG_DEBUG in `AIDecisionSystem.cpp`.
**Hypothesis status:** H1 (triangulation) untested; Phase 1 scouts pending.
**Next:** Spawn 3 parallel scouts; synthesize + present ranked hypotheses for user selection.

### Session 2 (2026-06-26)
**Goal:** Test H1 (water ring contains the land) — the build-free decisive check.
**Tried:** Parsed the real rings from `NavMeshRealRings.test.h`; computed signed areas (shoelace) +
ray-cast grass points.
**Learned — H1 INVALIDATED:** the water ring is a correct thin CCW loop (signed-area2 = +2.05e9,
~6% of area); pokes just past the border at the 3 exits. Sampled grass points (spawn (1000,4000),
corners +-40000, mid-edges, right grass) ALL test OUTSIDE the water ring; only confluence center
(0,0) tests inside (it IS water). So the classification would correctly leave grass as floor — the
bug is NOT a water mistag.
**Pivot — H3 PROMISING:** the 1848 all-blocked triangles ~= blocker interiors alone (637 trees ~2
tris ~1274 + water ~562 ~= 1836 ≈ 1848), implying the big grass face (border interior with water +
~637 tree HOLES) emits ZERO triangles. A faces-with-holes triangulation failure — matches the
carried `Triangulation.cpp` lead. (H2 border-filter-discard still possible; distinguish later by
whether the grass face EXISTS-but-emits-0 (H3) vs is-discarded (H2).)
**Hypothesis status:** H1 INVALIDATED (data); H3 PROMISING; H2 untested; H4 low.
**Next:** Confirm H3 by running buildNavMesh on the real rings on a CLEAN baseline (revert the
stopped agent's `Triangulation.cpp` WIP) + log per-face emitted-triangle counts; expect floor face
emits 0.

### Session 3 (2026-06-26)
**Goal:** Confirm H3 (baseline fails) + test the candidate `Triangulation.cpp` fix.
**Tried:** Reverted `Triangulation.cpp` to baseline -> built geometry-tests -> ran
`NavMesh.RealYConfluence_OpenGrassIsFloor`. Then restored the candidate fix -> rebuilt -> ran the
full NavMesh suite.
**Learned — H3 CONFIRMED:** on baseline the real-confluence test reproduces the EXACT in-game
numbers: `floor=0, terrain=1848 out of 1848` ("the walkable=0 navmesh symptom"). So the
classification is correct; the per-face triangulation of the land-with-holes drops every floor face.
ROOT CAUSE (per the fix's own analysis): `mergeHoles`' Eberly +x-ray hole-bridge picks a target
that, for a non-convex hole (the water ring) or after several holes merge, yields a SELF-CROSSING /
non-simple merged loop -> the land face triangulates to zero triangles.
**Candidate fix (`Triangulation.cpp`):** validate each bridge (`bridgeIsClear` / `bridgeHitsHole`)
and fall back to the nearest loop vertex reachable without crossing the loop, this hole, or any
unmerged hole. With it restored: `NavMesh.RealYConfluence_OpenGrassIsFloor` PASSES (floor>0); all
20 NavMesh tests green (no regression).
**Hypothesis status:** H1 INVALIDATED; **H3 CONFIRMED at unit level + fix passes**; H2 moot; H4 low.
**Next:** Build world-sim, launch --scene=game, confirm `[NavBuild] walkable>0` + Bob moves; then
request USER confirmation. Do NOT mark fixed or remove debug instrumentation until the user confirms.

### Session 4 (2026-06-26)
**Goal:** In-game verification + bisect the trigger (user: river vs entities?).
**In-game (world-sim, --scene=game, fix built):** `[NavBuild] mesh swapped gen=1 tris=6215
walkable=4367 floor=4367 blocked=1848` (was walkable=0). Bob WANDERS freely — destinations walk from
spawn ~(3,5) out to (24,60) over minutes; no more "Waiting for the area to settle". Verified in the
real engine, not just the unit test.
**Bisection (baseline vs fix; real rings stripped to subsets) — CORRECTS the river assumption:**
- border only (flat, 0 holes): baseline OK / fix OK
- border + water (1 non-convex hole): **baseline OK** / fix OK  -> the river alone is fine
- border + 637 trees (convex holes): **baseline FAILED floor=0/1275 (37.8s)** / fix OK (9.7s)
So the TRIGGER is the MANY TREE HOLES (entities), NOT the river. A single non-convex water hole
bridges fine; the multi-hole merge is what breaks (matches the fix note: "once several holes are
merged the chosen target sits OFF the +x ray, and that slanted bridge can slice through a hole").
The earlier "river winding" guess (H1) was wrong; the "is it entities?" instinct was right. Broken
bridging is also ~4x slower (bad bridges retried).
**Further bisection (user hypothesis: edge?):** on baseline, interior trees far from every border
(n=305) FAIL (floor=0/610) just like border-band trees (n=332, floor=0/665) and first-half-by-count
(n=318, floor=0/636). So it is NOT position/edge (screen or chunk) -- it is hole COUNT/density: a few
hundred holes ANYWHERE overwhelm the Eberly +x-ray bridge picker as the merged loop tangles. Water (1
hole) bridges fine; ~300+ holes anywhere do not. The fix passes all subsets.
**Hypothesis status:** **H3 CONFIRMED** — root cause = `mergeHoles` Eberly +x-ray bridge fouling on
multi-hole merges. Fix verified: 23 NavMesh tests (incl. 3 bisection) + in-game walkable=4367 + Bob
moves.
**Status:** AWAITING_USER_CONFIRMATION. Debug instrumentation (`[NavBuild]`/`[NavDiag]`) + WIP left
in place until the user confirms; then: clean up, run full engine suite, commit, PR.
