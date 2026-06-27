# Debug Log: Navmesh Zero Walkable

**Issue:** Navmesh builds with zero walkable faces; colonist freezes ("Waiting for the area to settle")
**Started:** 2026-06-26
**Status:** INVESTIGATING

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
_(pending Phase 1)_

## Hypotheses (Ranked)
_(pending Phase 1 synthesis)_

### H1 (carried lead): per-face constrained triangulation mis-handles faces with holes
- **Status:** untested
- **Source:** earlier autonomous investigation (stopped mid-fix) localized a candidate to
  `libs/geometry/triangulation/Triangulation.cpp` (it was adding a hole-"bridge" scan). UNTESTED,
  possibly non-compiling — treat as a lead, not a fix.
- **Evidence for:** the prior CLASSIFICATION fix (NavMesh.cpp) did not help, shifting suspicion
  downstream to the triangulation stage.
- **Evidence against:** none yet; not built/tested.
- **Test approach:** reproduce from `NavMeshRealRings.test.h`; build + assert floor faces > 0;
  in-game `walkable>0`.

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
