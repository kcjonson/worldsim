# Debug Log: colonist-wall-trap-offmesh-oscillation

**Issue:** After a colonist builds walls it can end up trapped OFF-MESH against the wall band, frozen forever (nav reconcile snap vs WallCollisionSystem push-out oscillation).
**Started:** 2026-06-29
**Status:** FIXED (verified live + unit test; engine-tests 865/865)

---

## Problem Summary

Observed (live, bug #17): after a colonist builds walls it ends up trapped off-mesh against the wall band. Per-frame oscillation:
1. NavigationSystem reconcile sees the colonist off-mesh, snaps it to nearest pathable floor: `[NavDiag] reconcile offMesh at (X,Y): nearestFloor=1 (X2,Y2)`.
2. WallCollisionSystem pushes it back out INTO the wall band -> off-mesh again.
3. AI sees off-mesh, picks no option: `[AI] Waiting for the area to settle (priority 0)` forever.

Nav mesh is settled (no NavBuild activity). Teleport refused (position off-mesh / requireValidPosition false). Permanent stuck colonist.

Expected: a colonist at/near a built wall recovers to a STABLE walkable point OUTSIDE the wall collision band and resumes normal AI.

## Key Facts Established (CONFIRMED by reading code)

1. System order (priority): NavigationSystem(51) -> AIDecisionSystem(60, does the off-mesh snap) -> PhysicsSystem(200) -> CollisionSystem(250) -> WallCollisionSystem(260, pushes out). Both AI snap and wall push mutate the SAME `Position::value` in the same frame; wall push is the LAST mover.
2. Off-mesh snap: `AIDecisionSystem.cpp:858-891` calls `m_navSystem->nearestPathablePoint(position.value)` when `inSimArea && !isOnMesh`, writes `position.value = *snapped`, invalidates NavPath, clears task. Emits the `[NavDiag] reconcile` log at :863.
3. `nearestPathableOnMesh` (NavigationSystem.cpp:49-82) snaps to the nearest point on a WALKABLE navmesh triangle edge, then nudges 5% toward that triangle's centroid (`best = cand + (centroid - cand) * 0.05F`, :76). It is NOT agent-radius aware.
4. Navmesh wall carve-out uses ONLY geometric half-thickness: `extractWalls` reads `preset->halfThicknessMm` (NavInputBuilder.cpp:273) and bands at centerline +/- halfThickness (`geometry::band(... gs.halfThicknessMm)`, :379/:411). NO agent-radius buffer. So the walkable face edge sits at ~halfThickness from the wall centerline.
5. WallCollisionSystem clearance = `halfThicknessMeters + r` (WallCollisionSystem.cpp:130), r = AgentRadius.radiusMeters (default 0.3m, AgentRadius.h:8). Push-out sets dist-to-centerline to exactly `halfThickness + r` (:164).
6. THE ASYMMETRY: nav floor edge is at ~halfThickness; collision clearance is halfThickness + 0.3m. So the snapped "floor" point is ~0.3m INSIDE the collision band. Wall-collision pushes it out by ~0.3m, past the walkable face edge -> off-mesh -> reconcile snaps it back -> oscillation forever.

## Scout Agent Findings

**Explored by:** 3 parallel agents (data-flow, geometry/math, AI-gating/edge-cases)
**Consensus:** data-flow scout and geometry scout INDEPENDENTLY identified the same root cause: navmesh carve-out (halfThickness only) is narrower than the wall collision band (halfThickness + agentRadius), so the nearest walkable point is inside the collision band.

## Hypotheses (Ranked)

### H1: Collision band wider than navmesh carve-out (agent-radius asymmetry)  [PROMISING - consensus 2/2 geometry+dataflow scouts, confirmed by direct code read]
- **Status:** promising
- **Evidence for:** Facts 3-6 above. Nav snap lands at ~halfThickness; collision clearance = halfThickness + 0.3m; gap ~= agent radius.
- **Test approach:** repro in-game, log distance from snapped point to wall centerline vs (halfThickness + r). If snapped dist < clearance, confirmed.

### H2: Snap lands on a thin sliver triangle edge; 5% centroid nudge insufficient  [secondary]
- **Status:** untested (likely subsumed by H1 -- even a perfect interior snap at <clearance from the wall oscillates)

### H3: AI does nothing ("Waiting for the area to settle") because off-mesh => no option; teleport refused => no escape  [downstream symptom, not root cause]
- **Status:** this is the FROZEN symptom; fixing H1 (snap lands outside band) removes it.

## Candidate fixes (consistent with nav-mesh authority)

- **Fix A (preferred):** make the off-mesh snap collision-aware -- after `nearestPathablePoint`, project the point out of every nearby wall collision band by (halfThickness + r + margin), so the recovered point is BOTH on a walkable face AND outside the band. Keeps one navmesh for all agent sizes. Risk: snap could land off-mesh again if pushed too far; clamp/re-validate.
- **Fix B:** widen the navmesh wall carve-out by the agent radius so the walkable face never sits inside the collision band. Risk: bakes a single agent radius into a shared mesh; changes pathing clearance globally; bigger blast radius.
- **Fix C:** WallCollisionSystem must not push an agent that is mid off-mesh-recovery. Risk: leaves the agent clipping the wall; fights the "safety net" purpose; needs a per-entity recovery flag.

Leaning Fix A: it puts the correction where the disagreement is (the snap), respects nav-mesh authority, and is per-agent-radius correct.

---

## Investigation Log

### Session 1 (2026-06-29)
**Goal:** Reproduce on fresh instance (port 8108), capture oscillation log, root-cause the nav-snap vs wall-collision boundary disagreement.
**Tried:**
- Built Wood/Standard walls (halfThickness 0.10m; agent radius 0.30m -> collision clearance 0.40m) via /api/dev/walls and teleported colonist into/near the bands. Also blueprint->complete to flip a wall solid under the colonist.
- KEY GOTCHA: Engine log category defaults to LogLevel::Info (Log.cpp:76), so LOG_DEBUG(Engine,...) -- both the existing [NavDiag] reconcile AND my [WallColDiag] -- are filtered from stdout. They ARE captured by the DebugServer and visible via the SSE endpoint `GET /stream/logs` (DebugServer.cpp:884; capture happens at Log.cpp:121 BEFORE the console filter). Read logs from /stream/logs, NOT stdout.
**Learned (CONFIRMED live):**
- WallColDiag at WallCollisionSystem.cpp shows the colonist standing at (-6.100,-5.500): `nearestDist=0.100 clearance=0.400 inBand=1`. So a point is simultaneously ON-MESH (nav isOnMesh true at the carve boundary 0.10m from centerline) AND INSIDE the wall collision band (0.10 < 0.40). This DIRECTLY confirms H1: the navmesh carve (halfThickness) and the collision band (halfThickness + agentRadius) disagree by the agent radius.
- In a symmetric 2-wall corridor the collision relaxation ping-pongs and settles back to its start each frame (start==end), so the colonist clips the wall but stays on-mesh -> no reconcile, no freeze. The TRAP (#17) is the asymmetric case where the 0.40m push lands the colonist on the NON-walkable side (another carve / sim edge), flipping isOnMesh false -> reconcile snaps back to the carve boundary (0.10m, inside band) -> push again -> oscillation + AI "Waiting for the area to settle".
**Debug code added:** WallCollisionSystem.cpp -- `[WallColDiag]` LOG_DEBUG(Engine) reporting per-agent settled pos, nearestDist, clearance, inBand (within 1.5m of any band). REMOVE on confirmation.
**Hypothesis status:** H1 CONFIRMED live. Full oscillation reproduced + captured.

**REPRO (deterministic):** built Wood wall at x=-3.3 (y=-5.5..-4.5) just inside the sim region's east walkable edge (~x=-2.9), colonist anchored near (-5,-5). Teleport colonist to the thin walkable sliver east of the wall (-3.1,-5.0). Result, ~191 cycles/sec, every frame:
```
[NavDiag] reconcile 4294967296 offMesh at (-2.90, -5.16): nearestFloor=1 (-3.00, -5.15)
[WallColDiag] 4294967296 start(-2.900,-5.163) end(-2.900,-5.163) nearestDist=0.400 clearance=0.400 inBand=0
```
1147 reconciles in 6s. The colonist sits OFF-MESH at (-2.90,-5.16); reconcile snaps to nearestFloor (-3.00,-5.15) = 0.30m from the x=-3.3 wall centerline = INSIDE the 0.40m collision band; WallCollisionSystem pushes out to 0.40m = (-2.90) = past the walkable edge = OFF-MESH; repeat forever.

**ROOT CAUSE (file:line):** `nearestPathableOnMesh` (NavigationSystem.cpp:49-82, esp. the candidate = closest-point-on-walkable-triangle-edge at :60-71 + 5% centroid nudge at :76) returns a point on the walkable-face boundary, which is only `halfThickness` (0.10m) from the wall centerline -- INSIDE the collision band. WallCollisionSystem.cpp:130 `clearance = halfThickness + r` (:164 pushes to exactly that). The snap target is inside the band by ~agentRadius, so collision ejects it; if the ejection lands off-mesh (another carve / sim edge / opposite wall), reconcile (AIDecisionSystem.cpp:858-891) re-snaps it back inside the band -> 2-cycle. The AI "Waiting for the area to settle" (AIDecisionSystem.cpp:1575) is the downstream freeze when the colonist also holds no task.

**FIX (chosen): Fix A -- collision-aware snap.** After nav finds the nearest walkable point, push it OUT of every nearby BUILT wall collision band (to halfThickness + r + small margin), then re-validate it is still on walkable mesh; so the recovered point is BOTH on a walkable face AND outside the band -> WallCollisionSystem has nothing to push -> no oscillation. Per-agent-radius correct, single shared navmesh, respects nav-mesh authority. Implemented in NavigationSystem (it owns both the mesh and the ConstructionWorld, already wired via setConstructionWorld).

### CLARIFICATION on the off-mesh trigger (from unit-test failure)
`isOnMesh` uses `terrainTraversable`, which treats WALL faces as walkable (only water/tree common-knowledge terrain is off-mesh). So a point inside a wall CARVE is still "on mesh". In the repro the colonist is off-mesh because the collision push ejects it past the SIM REGION EDGE (the walkable area ended ~x=-2.9), not because of the carve. The wall band is still what the nearest-walkable SNAP lands inside; the region edge is what makes the pushed point off-mesh. The fix (snap clears the band) breaks the cycle regardless of which boundary triggers the off-mesh state.

### IMPLEMENTATION (files)
- `libs/engine/ecs/systems/NavigationSystem.h`: new overload `nearestPathablePoint(meters, agentRadiusMeters)`.
- `libs/engine/ecs/systems/NavigationSystem.cpp`: anon-namespace band helpers (`resolveBuiltBands`, `clearOfBands`, `ejectFromBands`, `nearestClearOnMesh`) mirroring WallCollisionSystem's band model exactly (Built-only, halfThickness+r clearance, pathable door-gap exemption, 2-iteration relaxation, +2cm margin); the new overload: eject the bare nearest point out of the bands (fast path), else scan walkable triangles for the nearest band-clear interior/centroid point (slow path -- frees a colonist sealed in a sub-clearance sliver to the wall's open side), else fall back to the bare point.
- `libs/engine/ecs/systems/AIDecisionSystem.cpp` (~:859): recovery snap now passes the colonist's AgentRadius to the band-aware overload.
- TEST: `libs/engine/ecs/systems/NavigationSystem.test.cpp` `RecoverySnapClearsWallCollisionBand` -- a built wall, a query point inside its band, asserts the radius overload returns an on-mesh point >= halfThickness+r from the centerline.

### VERIFICATION
- engine-tests Debug: 865/865 pass (was 864; +1 new test). No regressions; no test asserted the old in-band-snap behavior.
- In-game (8108, RelWithDebInfo, clean build, NO instrumentation): exact trap repro (wall x=-3.3 at the east sim edge, teleport colonist to the thin sliver (-3.1,-5.0)).
  - BEFORE: 1147 `[NavDiag] reconcile` in 6s, perpetual oscillation, frozen.
  - AFTER: exactly 1 reconcile `offMesh at (-2.90,-5.00): nearestFloor=1 (-2.71,-6.23)` (snap now 1.3m clear of the wall band), settle=0, colonist resumes normal movement (walks freely across the map). Build-under-colonist (blueprint box completed under the colonist) also leaves it stable/mobile, not trapped. Normal wall collision preserved (colonist still settles at the band boundary nearestDist==clearance and paths around walls).

### DEBUG CODE
- Added `[WallColDiag]` LOG_DEBUG to WallCollisionSystem during investigation; REMOVED after verification. WallCollisionSystem.cpp is back to its original state (no instrumentation). The pre-existing `[NavDiag] reconcile` log (not added by this work) is left in place.
- NOTE for future debugging: Engine log category defaults to Info (Log.cpp:76), so LOG_DEBUG(Engine,...) is filtered from stdout but captured by the DebugServer -- read it from the SSE endpoint `GET /stream/logs`, not the console/stdout log.
