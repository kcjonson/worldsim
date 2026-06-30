# 2026-06-29 - Colonist gameplay stabilization: craft, harvest, construction loop

## Summary

End-to-end stabilization of the colonist craft → harvest → construction loop. Four gameplay
scenarios were driven to completion individually: crafting a primitive axe from gathered materials;
crafting a storage box; filling a box to ~200 wood via real chop-and-haul cycles; and building a
complete wood foundation, then adding walls on it. Delivered as PR #241 across four commits:
stabilize, testing suite, arbitration spec, review fixes.

~14 distinct fixes landed, reviewed across 6 dimensions, 872 engine tests green throughout.

## Details

### Keystone fix: harvest/build targets snap to a reachable adjacent point

The most broadly impactful change. Harvest targets and build-action targets now snap to a reachable
adjacent point on the navmesh rather than to the entity center. Tree centers are carved out of the
navmesh (a tree IS a hole in the floor), so a colonist trying to stand on a tree center was always
off-mesh. Targets that fall outside the colonist's current sim area are rejected outright
(off-area reject) rather than accepted and never reached.

### Destructive fell reclaims the navmesh hole

`PlacementExecutor` now bumps a `removalEpoch` counter when it removes a tree, which triggers a
regional navmesh rebuild. The felled-tree hole is reclaimed and the cleared ground becomes
walkable. Previously the navmesh hole persisted after felling, so a colonist could chop a tree and
then stand in a phantom blocked region the rest of the session.

### Belt stow-to-free-hands on armful pickup

When a colonist fells a tree and needs both hands for a wood armful, any one-hand tool in a hand
slot is moved belt → pack → drop before the armful is lifted. The axe stays tool-check-valid from
the belt. Spec: `docs/technical/bulk-materials-carry-and-felling.md`. Colonist state readback now
exposes `hands`/`belt` fields.

### Loose-pile haul: carrying-at-source deposit-redirect

A colonist who picks up from a loose pile and is already at the source is no longer stuck in a
deposit loop. The action reads carry-state-first (are we actually holding the item?) to decide
which haul leg to execute, and a carrying-at-source colonist is directed to the deposit leg
immediately.

### Wall-collision-aware off-mesh recovery snap

The recovery snap that fires when a colonist is off-mesh now checks that the snap target is
wall-collision-clear. Previously a colonist could snap into a position inside a built wall and
remain stuck there.

### Whole-footprint on-water placement refusal

`NavigationSystem::isAreaWalkable` now checks the full footprint of a blueprint, not just its
center. A foundation whose footprint overlaps water is refused at draw time (ghost goes red) and
via the dev verbs (`/api/dev` spawn/place/walls reject on water). Wired into `DrawingSystem`.

### Food-gather priority lowered + construction provisioning/build-action floors

Gather-food priority was lowered below active construction provisioning, so a colonist won't
abandon a half-provisioned foundation to eat a berry. Construction provisioning and the build
action received priority floors above Wander/idle, matching the craft-provisioning fix from #240.
These are explicitly interim: the arbitration epic (see below) restores the full documented
hierarchy.

### StorageRule.minAmount shortfall → harvest goals

When a storage box falls below its `minAmount` threshold for a material, the storage system now
emits harvest goals to top it up, not just haul goals. This enables the "chop wood and stock the
box" loop without the player manually queuing every chop.

### Forest-by-river quickstart landing + denser forest + fail-fast on missing prebuilt planet

`LandingSite` now biases toward biomes with tree cover near a river. The quickstart forest is
denser (more oak/maple spawns per cell). The loader fast-fails with a clear error message when the
prebuilt planet file is absent rather than hanging at the load screen.

### Construction readback: walls, delivered/required/progress, phase

`/api/state?what=construction` now includes walls (not just foundations), per-structure
delivered/required material counts, build progress percentage, and current phase. Foundation and
wall phases are distinct in the readback.

### Dev tooling additions

- `/api/dev/storage` verb: inspect or mutate storage box contents
- `/api/state?what=storage` readback: box-by-box inventory summary
- `/api/state?what=landing` readback: current landing site parameters
- Colonist state readback: `hands` and `belt` slot contents added

### Scenario testing suite

`docs/testing/` contains a manual + AI-automated regression guard: four scenario files (craft axe,
craft + stock box, build foundation, build walls), a regression log, and a template. The suite
documents the exact dev-tool command sequences needed to reproduce each scenario from a fresh
quickstart.

### Task-arbitration spec and epic

The recurring "colonist abandons the queued job" failures trace back to the priority system
drifting from its documented lexicographic (tier, score) design. The spec
`docs/technical/colonist-task-arbitration.md` restores that hierarchy (this is a restoration, not
a new architecture) and describes a job-lifecycle model that lets a colonist commit to an active
work order across distractions. A Specboard epic was filed for the implementation.

### Key decisions

- **(tier, score) is a restoration.** The code drifted from the documented architecture; the fix
  brings it back, not a new model.
- **Entity spawns deferred to post-ECS-update.** Mirroring the existing removal-deferral, this
  avoids a use-after-free when a harvest action completes and the harvested entity is destroyed in
  the same tick.
- **Use RelWithDebInfo for in-game testing.** The Debug build is too slow to reach a ready scene
  with the prebuilt planet and full flora density.
- **Pregen-world worktree junction.** The `pregenworld` test worktree junction is documented in
  `CLAUDE.md` for future sessions.

## Related Documentation

- `docs/technical/colonist-task-arbitration.md` — arbitration spec (lexicographic tier/score key,
  job-lifecycle model); the implementation is the planned next epic
- `docs/technical/bulk-materials-carry-and-felling.md` — belt-stow spec (already existed; the
  stow-to-free-hands fix brought the code into compliance)
- `docs/testing/` — scenario suite (four scenarios + regression log)
- `docs/known-issues-and-followups.md` — updated: items resolved this session marked, open
  follow-ups reconciled, new items added

## Next Steps

- **Colonist task-arbitration epic** (the big one): implement the `(tier, score)` lexicographic
  arbitrator from `docs/technical/colonist-task-arbitration.md`; restore the full documented
  priority hierarchy; job-lifecycle commit so a colonist finishes what it started.
- `findValidPositionNear` + ground-drops: items landing in rivers (`findValidPositionNear`'s first
  real consumer); also covers crafted-box placement out-of-station.
- **Crafted-box placement + packaged-furniture lifecycle**: the box appears on the station, not
  beside it; also the re-packaging workflow.
- **Mirrored colonist directional sprites**: directional sprite mapping is inverted (still open
  from the `<motion>` animation work).
- **Dev-tools spawn UX / unstacked items**: destination option labels, position arg on
  "colonist" dest, spawn-N-as-one-stack (the off-mesh refusal WAS added this session).
- **Stale-stocking-harvest retirement**: a haul job that was targeting stock that no longer exists
  should retire, not loop. Folded into the arbitration epic's job-lifecycle work.
