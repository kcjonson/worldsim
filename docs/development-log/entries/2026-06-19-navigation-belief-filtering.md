# 2026-06-19 - Navigation P4: belief-filtered navigation

## Summary

Colonists now plan paths against what they personally believe the world looks like, not the live truth of every wall that's been built. A colonist that has never seen a room walks straight at where its walls sit (an unseen wall is treated as absent, the freespace assumption from the D* family); vision reveals the wall; the path replans. This is the game's stated differentiator made real: nobody magically knows your floor plan, and a future raider can't route to a storeroom it's never scouted. Truth-based routing still exists as the default (an empty belief filter) and reproduces navigation v1 exactly, so single-colonist play is unchanged today.

The prerequisite was the Vision System (occlusion + honest per-colonist memory), which shipped just before this. Belief is hollow without an honest write path into memory; vision is that path.

## Details

Landed as three phased PRs off main, each a tested checkpoint. The architecture (locked with Kevin up front) is the spec's full-triangulation, per-agent belief filter: one shared navmesh that triangulates wall interiors and tags every face with its blocker, plus a per-colonist predicate consulted at query time. The rejected alternative, a per-group "colony-union" believed mesh, was faster but reintroduced colony-wide omniscience, the exact thing the game avoids.

### P4.1 - Belief-ready navmesh (#189)
The geometry rework. `NavTriangle` gained `faceBlocker` (int64: `kNoBlocker` for floor; `+segmentId` for a wall-band interior; a negative sentinel for terrain that always blocks as common knowledge) and `faceOpening` (a pathable door's id, else `kNoOpening`). `buildNavMesh` stopped discarding blocked faces: it now keeps them, triangulates the wall interiors, and tags each resulting triangle from the smallest containing blocked ring (ring `|2*area|` cached once at push so classification stays O(faces*rings), not O(faces*rings*ringSize)). `NavInputBuilder` emits each built wall as its full band footprint tagged `+segmentId` plus door-span sub-polygons tagged with the opening id, instead of cutting the door gap out of the band. `PathQuery::pathThrough` took a `BeliefFilter` (two `const std::unordered_set<uint64_t>*`, known segments and known openings, or null for truth) and applies the predicate per neighbor and at the start/goal locate: floor walkable; terrain/junction always blocks; a wall segment is walkable when the agent doesn't know it (unseen, so absent), or knows it and knows a door on that face; otherwise blocked.

### P4.2 - Belief-aware service + replan loop (#191)
`NavigationSystem::requestPath`/`isReachable` took an optional `BeliefFilter` (default truth) and a `generation()` counter that bumps on each async mesh swap. `Memory` gained `beliefVersion`, a monotonic counter bumped whenever a remember/forget reveals something new, the cheap "my belief changed" signal. `NavPath` stores `builtBeliefVersion` + `builtNavVersion` stamps. `AIDecisionSystem::requestNavPath` builds the filter from the colonist's `Memory` and stamps the path. The replan loop re-requests the same goal when either version drifts, coalesced against the ~12 Hz vision throttle so a fast-forwarded sim doesn't churn. On a believed-route denial the colonist stops (clears the movement target, zeroes velocity) rather than dishonestly beelining through a wall it believes is there; the wall-collision safety net from P-phase C2 still backstops physical clipping.

### P4.3 - Nav-state readability (#194)
Belief behavior has to read as intent, not a bug. `Task` gained `NavState { Traveling, Rerouting, SearchingLKP, LookingForWayIn, CantFindWayTo }` plus a `navStateHold` countdown so the momentary "Re-routing" beat stays on screen long enough to see. The load-bearing change: `requestNavPath` returns `NavRequestOutcome { Routed, Beelined, Blocked }` instead of a bool, so callers separate a benign no-mesh beeline (startup/outdoor, still ordinary travel) from a genuine belief denial (stuck). Only the latter becomes `CantFindWayTo`. The colonist info panel maps the state to the `memory.md` vocabulary ("Going to", "Re-routing", "Can't find a way to") with status colors; the same pass fixed the Build/Deconstruct/Haul/PlacePackaged task types that previously fell through to "Idle", and the panel's change detection now diffs the rendered task line so a task-type swap refreshes it. The believed path is already visible through the existing NavOverlay (it draws the colonist's `NavPath`, which is now belief-planned).

## Files

- `libs/geometry/nav/NavMesh.{h,cpp}` (face-blocker/opening tags, keep+triangulate blocked faces), `libs/geometry/nav/PathQuery.{h,cpp}` (`BeliefFilter` + predicate)
- `libs/engine/nav/NavInputBuilder.cpp` (walls as tagged footprints + door sub-regions)
- `libs/engine/ecs/systems/NavigationSystem.{h,cpp}` (belief arg, generation counter)
- `libs/engine/ecs/components/Memory.h` (`beliefVersion`), `NavPath.h` (version stamps), `Task.h` (`NavState`)
- `libs/engine/ecs/systems/AIDecisionSystem.{h,cpp}` (belief injection, replan loop, `NavRequestOutcome`, nav state)
- `apps/world-sim/scenes/game/ui/dialogs/ColonistDetailsModel.{h,cpp}` + `tabs/BioTabView.{h,cpp}` (nav-state line + color)

## Testing

geometry-tests cover the truth query reproducing v1 routing (door passes, wall/window/water block) and the belief cases: empty memory routes through an unseen wall, a known wall blocks, known wall + known door passes, known wall + unknown door blocks, terrain always blocks. engine-tests cover belief `requestPath` differing by memory and a `beliefVersion` bump flipping a stored path to stale. Both suites stayed green through all three PRs; build/test/test-windows green on each.

## Related Documentation

- `/docs/technical/pathfinding-architecture.md` section 5 (belief filtering, freespace assumption, search behaviors)
- `/docs/design/game-systems/colonists/memory.md` (the nav-state vocabulary)
- `/docs/technical/vision-architecture.md` (the honest write path into memory)

## Next Steps

Deferred out of P4 v1, noted here so the gaps are explicit:
- Active search behaviors: door-discovery wall-follow and last-known-position expanding-ring search. The `NavState` enum reserves `SearchingLKP`/`LookingForWayIn` for them; the behaviors are AI work for a follow-up.
- Stale-demolished-wall pessimism (remembering and planning around a wall that's been torn down). The single-truth-mesh design only does the optimistic direction; v1 self-corrects through vision reconciliation and `forgetSegment`.
- Tier-1 O(1) reachability (P3): "no believed route" stays a full `pathThrough` returning none for now.
- Raider belief seeding + scouting arrives with P6 (global tier + raids).
