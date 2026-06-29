# Testing

Manual and AI-automated scenario tests for worldsim, driven via the debug HTTP API. A human or
an agent can run these by issuing the curl commands listed in each scenario doc; no source changes
are needed.

See [TEMPLATE.md](./TEMPLATE.md) for the scenario format and [regressions.md](./regressions.md)
for the fixed-bug repro checklist.

---

## Scenarios

| # | Name | File |
|---|------|------|
| 01 | Craft axe and box | [scenarios/01-craft-axe-and-box.md](./scenarios/01-craft-axe-and-box.md) |
| 02 | Stock box with wood | [scenarios/02-stock-box-with-wood.md](./scenarios/02-stock-box-with-wood.md) |
| 03 | Build wood foundation | [scenarios/03-build-wood-foundation.md](./scenarios/03-build-wood-foundation.md) |
| 04 | Build walls | [scenarios/04-build-walls.md](./scenarios/04-build-walls.md) |

**Run each scenario in isolation** from a fresh launch, or use `give` / `need` dev verbs to
set preconditions from a running instance. The fully-combined four-scenario flow is not yet
reliable; see [Known-open caveats](#known-open-caveats) below.

---

## Environment

### Build

Config: **RelWithDebInfo**. Debug is too slow for a ready game scene; RelWithDebInfo keeps the
dev HTTP API and reaches the game scene in ~5 s.

```powershell
$env:VCPKG_ROOT = 'C:\vcpkg'
cmake --build build --config RelWithDebInfo --target world-sim -j8
```

Unit tests (independent of the scenario suite):

```powershell
cmake --build build --config Debug --target engine-tests
build\libs\engine\Debug\engine-tests.exe
# currently 872 green
```

### Pre-generated world

A worktree needs `planets/quickstart.wsplanet` (~1.3 GiB, gitignored). Junction it from the main
checkout; see the "Sharing the pre-generated world across worktrees" note in [../CLAUDE.md](../../CLAUDE.md).
Missing file causes an immediate `ConfigError` on launch.

### Launch

```
build\apps\world-sim\RelWithDebInfo\world-sim.exe --scene=game --http-port <PORT>
```

Use a **unique non-default port** (default 8081) to avoid collision with other worktrees or a
running game instance. Reaches the game scene in ~5 s; colonist "Bob" spawns in
TemperateDeciduousForest near a river, with no axe.

### Confirm ready

Poll until the response is valid JSON (not `{"error":"...unavailable..."}`):

```
GET /api/state?what=colonists
```

### Shut down

```
GET /api/control?action=exit
```

This call blocks and returns only after shutdown is complete; no sleep needed after it.

---

## Dev API reference

All calls below are GET requests. Substitute `<PORT>` with the port you launched on. Commands
shown as `curl.exe` (Windows); drop `.exe` on Linux/macOS.

### Write verbs — `GET /api/dev/<verb>?<params>`

Async, queued; only drained in the game scene. Place things on **valid land** away from water;
on-water placements are refused.

| Verb | Key params | Notes |
|------|-----------|-------|
| `spawn` | `def=<asset>&at=x,y&n=1&scatter=0` | Spawns a world entity (tree, station, container, loose item). |
| `colonist` | `at=x,y&n=1&name=` | Spawns a colonist at the given position. |
| `give` | `material=<defName>&n=<qty>&where=site\|loose\|colonist\|storage[&at=x,y]` | Gives material to the colonist, a site, or storage. |
| `need` | `colonist=<id>&need=Hunger\|Thirst\|Energy\|...&value=0..100` | Sets a colonist's need to the given value. |
| `time` | `speed=0..3` or `set=HH:MM` or `skip=Nh\|Nm` | Controls game time. `speed=3` accelerates. |
| `teleport` | `colonist=<id>&to=x,y` | Teleports a colonist. |
| `select` | `colonist=<id>` or `at=x,y` | Selects a colonist for the UI. |
| `kill` | `colonist=<id>` | Removes a colonist. |
| `complete` | `id=<entity>` | Instantly completes a task/entity. |
| `craft` | `recipe=<Recipe_*>&n=1&at=x,y` | Queues a recipe at the nearest crafting station. |
| `storage` | `at=x,y&item=<defName\|*>&category=RawMaterial\|Food\|Tool\|Furniture&priority=Low\|Medium\|High\|Critical&min=0&max=0` | Adds a StorageRule to the nearest container. |
| `foundation` | `pts=x0,y0;x1,y1;...&material=Wood&built=0\|1` | `built=0` = blueprint the colonist builds. Refuses if any part of the footprint is off the nav mesh or over water. |
| `walls` | `pts=...&material=Wood&thickness=Standard&host=<foundationId>&built=0\|1&close=1` | `built` **defaults to 1** — pass `built=0` for a blueprint. Same off-mesh/water refusal. |
| `opening` | `seg=<id>\|pt=x,y&type=Door\|Window&t=0.5&built=1` | Adds a door or window to a wall segment. |

### Readback — `GET /api/state?what=<x>`

Synchronous JSON.

| `what=` | Returns |
|---------|---------|
| `summary` | `{colonists, foundations, chunks}` |
| `colonists` | `[{id, name, x, y, needs{}, action, inventory{}, hands{}, belt{}, cargoKg, carryCapacityKg}]` |
| `stations` | `[{id, x, y, jobs:[{recipe, completed, quantity}], store{}}]` |
| `storage` | `[{id, defName, x, y, inventory{}, slots, maxSlots, rules:[{item, category, priority, min, max}]}]` |
| `construction` | `{foundations:[{id, material, state, entity, area, required{}, delivered{}, progress, phase}], walls:[{id, material, state, entity, x0, y0, x1, y1, required{}, delivered{}, progress}]}` |
| `landing` | `{landingLatDeg, landingLonDeg, biome, waterClass, riverInTile}` |
| `time` | `{...}` |

### Other endpoints

| Endpoint | Purpose |
|----------|---------|
| `GET /api/ui/screenshot` | Returns a PNG screenshot. |
| `GET /api/control?action=exit` | Shuts down the instance (blocking). |
| `GET /api/control?action=camera&x=&y=&zoom=` | Moves the camera. |
| `GET /api/input?ev=click,x,y` | Synthetic UI input (logical pixels). Event types: `move`/`down`/`up`/`click`/`scroll,x,y,delta`. |

---

## Def names and recipes

**Stations:** `CraftingSpot` (crafting), `BasicBox` (container/storage).

**Flora (yield Wood on fell, require an axe):** `Flora_TreeOak` (~20-30 wood), `Flora_TreeMaple`,
`Flora_TreePine`. Sticks from `Flora_WoodyBush` (harvest, no tool). Fiber from `Flora_Reed`.

**Loose items:** `SmallStone` (picked up directly).

**Item defNames:** `Wood` (two-hand, stack 40, 2.5 kg), `Stick`, `PlantFiber`, `SmallStone`,
`Berry`, `AxePrimitive`.

**Recipes:**

| Recipe defName | Inputs |
|---------------|--------|
| `Recipe_AxePrimitive` | 2 SmallStone + 1 Stick + 1 PlantFiber |
| `Recipe_BasicBox` | 8 Stick + 3 PlantFiber |

---

## Hygiene for every scenario

- Place everything on **valid land**, a few meters from the river bank (on-water is refused).
- Use `time?speed=3` to accelerate. At speed 3 a harvest-to-craft cycle takes ~1-2 real minutes.
- Keep needs topped every ~2 minutes of sped-up time or hunger/thirst will interrupt work:

```
curl.exe "http://127.0.0.1:<PORT>/api/dev/give?material=Berry&n=10&where=colonist"
curl.exe "http://127.0.0.1:<PORT>/api/dev/need?colonist=<id>&need=Hunger&value=95"
curl.exe "http://127.0.0.1:<PORT>/api/dev/need?colonist=<id>&need=Thirst&value=95"
curl.exe "http://127.0.0.1:<PORT>/api/dev/need?colonist=<id>&need=Energy&value=95"
```

---

## Known-open caveats

These are **open bugs**, not test failures. Do not flag them as regressions.

- **Combined flow unreliable.** Running all four scenarios back-to-back with one colonist is not
  yet reliable — blocked by a phantom harvest-loop, a harvest snap/find-tolerance mismatch on
  dense or large trees, and an AI-arbitration issue with queued jobs. Run scenarios individually
  (fresh launch per scenario, or set preconditions with `give`/`need`).
- **Crafted box placement.** A freshly crafted `BasicBox` lands on the crafting station
  (cosmetic; the box is still usable).
