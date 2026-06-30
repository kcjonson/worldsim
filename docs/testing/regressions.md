# Regressions

Fixed-bug repros that must keep passing. Run these against any branch that touches the
relevant systems. Each check is a minimal, targeted repro — not a full scenario.

See [README.md](./README.md) for the environment setup and dev API reference.

---

## Checklist

### On-water placement refused

- [ ] `foundation?pts=<coords that land over the river>&material=Wood&built=0` returns an error
  (message like "not on an active walkable nav mesh") and `?what=construction` stays empty
  (no foundations created).
- [ ] Whole-footprint check: a footprint whose corners are on land but which spans water is also
  refused. Place corners on land, center over the river; same result expected.
- [ ] Same refusal applies to `walls?...&built=0` on an off-mesh/over-water placement.

**Why this matters:** a silently accepted placement over water produces a construction site that
can never be completed and may leave the colonist stuck.

---

### Belt stow-on-fell

- [ ] Colonist has a crafted `AxePrimitive` seated in hand (from [scenarios/01](./scenarios/01-craft-axe-and-box.md)
  or via `give`). After a tree fell:
  - `?what=colonists` -> `colonists[0].hands.Wood >= 1` (carried armful, not 0)
  - `?what=colonists` -> `colonists[0].belt.AxePrimitive >= 1` (stowed to belt)
  - No log entry matching "Collected 0 ... carry-limited" (the zero-collect bug is fixed)
  - Wood is hauled to storage in subsequent trips.

**Why this matters:** the zero-collect + missing-stow bug caused all felled wood to be lost and
the axe to block hauling.

---

### Wall-trap recovery

- [ ] Blueprint a wall adjacent to a position the colonist can reach, then build it (`built=0`).
  While the colonist is inside or adjacent to the new wall, confirm:
  - Colonist does NOT loop on "Waiting for the area to settle".
  - Colonist recovers to a walkable position and resumes normal action within ~30 real seconds
    at speed 3.

**Why this matters:** colonists could get permanently stuck next to newly-built walls, requiring
a restart to recover.

---

### Harvest reachability

- [ ] Spawn a `Flora_TreeOak` 3-5 meters from Bob on reachable land. Give Bob an `AxePrimitive`.
  Without other tasks queued:
  - Colonist chops the nearby tree (not a distant or unreachable one).
  - No freeze on "Cutting tree" without the action completing.

**Why this matters:** a path-finding tolerance mismatch caused the colonist to freeze on harvest
targets it could see but not reach, especially on large or dense trees.

---

### Tree felled + navmesh reclaim

- [ ] Fell a tree via `give` + harvest (or `complete?id=<tree>`). After the tree entity is gone:
  - `foundation?pts=<stump area>&material=Wood&built=0` is **accepted** (no refusal).
  - `?what=construction` shows the new foundation; the formerly-blocked tile is now walkable.

**Why this matters:** a felled tree that stays on the navmesh as a non-walkable tile blocks
subsequent placement and colonist pathing to the stump.

---

### Forest landing

- [ ] Fresh launch, `?what=landing`:
  - `biome == "TemperateDeciduousForest"`
  - `waterClass == "River"` or `riverInTile == true`

**Why this matters:** the quick-start world is generated for a specific seed (424242) and should
always land at the same biome and water class. A mismatch indicates a world-gen or seed-loading
regression.

---

## Adding a new regression

When a bug is fixed, add a minimal repro here before closing the ticket:

1. Name it after the bug (not the fix).
2. Write the minimal API sequence that reproduces the original failure.
3. State what the **correct** outcome looks like (what the assertion checks).
4. Note which systems are involved so the checklist can be filtered by area.
