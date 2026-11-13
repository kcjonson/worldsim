# Scenario 02: Stock box with wood

**Status:** ready  
**Last verified:** 2026-11-02

Verifies that a colonist with an axe chops trees, hauls wood, and stocks a `BasicBox` to a
target quantity via a storage rule.

---

## Preconditions

Colonist has an `AxePrimitive`. Either run [01-craft-axe-and-box.md](./01-craft-axe-and-box.md)
first, or give the axe directly:

```
curl.exe "http://127.0.0.1:<PORT>/api/dev/give?material=AxePrimitive&n=1&where=colonist&slot=hands"
```

`slot=hands` seats the axe in hand, where a crafted one lands. The default (`slot=pack`) leaves
it in the backpack: Bob still chops with it, but there's nothing in hand to stow on a fell, so the
belt stays empty.

---

## Setup

Find Bob's position:

```
curl.exe "http://127.0.0.1:<PORT>/api/state?what=colonists"
# -> colonists[0].x, colonists[0].y  (note as BX, BY)
```

Spawn a `BasicBox` on valid land:

```
curl.exe "http://127.0.0.1:<PORT>/api/dev/spawn?def=BasicBox&at=<BX+8>,<BY>&n=1&scatter=0"
```

Note the box's position, then add a storage rule targeting 200 Wood:

```
curl.exe "http://127.0.0.1:<PORT>/api/dev/storage?at=<BX+8>,<BY>&item=Wood&priority=High&min=200&max=0"
```

`min=200` is how far Bob cuts trees for the box. `max=0` sets no limit, so past 200 he stops
cutting but still carries any wood he knows of lying loose (a fell's leftover heap) into the box,
until it is full. The verb removes the box's default "all raw materials" rule, as the storage dialog
does when a named rule is added.

The natural forest has trees. Optionally spawn a few `Flora_TreeOak` nearby if the nearest
natural trees are far away — but **do not cram many into a small area**, since that can trigger
harvest-tolerance issues (see [known-open caveats](../README.md#known-open-caveats)):

```
# Optional: extra trees, scattered
curl.exe "http://127.0.0.1:<PORT>/api/dev/spawn?def=Flora_TreeOak&at=<BX+15>,<BY>&n=3&scatter=8"
```

Accelerate time:

```
curl.exe "http://127.0.0.1:<PORT>/api/dev/time?speed=3"
```

---

## Steps

1. Confirm the storage rule is registered:

```
curl.exe "http://127.0.0.1:<PORT>/api/state?what=storage"
# -> storages[0].rules has an entry with item == "Wood", min == 200 (after the default "*" rules)
```

2. Watch Bob chop a tree. After a fell, poll colonist state:

```
curl.exe "http://127.0.0.1:<PORT>/api/state?what=colonists"
# -> colonists[0].hands should show Wood (armful)
# -> colonists[0].belt should show AxePrimitive (stowed on fell)
# -> colonists[0].cargoKg > 0
```

3. While Bob chops a tree, spawn Ann next to that tree (`/api/dev/colonist?at=<tree x+1>,<tree y>&name=Ann`), give her an axe (`/api/dev/give?material=AxePrimitive&n=1&where=colonist&slot=hands&at=<tree x+1>,<tree y>`), and select her (`/api/dev/select?colonist=<ANN_ID>`): her task list shows a "Cutting ... for Wood (reserved by Bob)" row for Bob's tree, and she cuts a different tree; the two never cut the same tree. `at=` picks the colonist nearest that point, so read the colonist's current position from `what=colonists` just before the call (colonists wander within seconds).

4. Open the Tasks panel while both cut: the "Cut for Wood" row reads "Working - Bob, Ann" (one name while only one of them cuts), and its detail goes on "needs N, M on the way": M appears as soon as a cutter starts toward his tree (a cut counts its expected wood as on the way from the moment he picks it), and N is the box's minimum less what it holds less M.

5. Open Ann's details (click her card, then the eye icon in the small left panel) and its TASKS tab: the placeholder "No known work nearby." shows only when the list is empty, never above rows. Known work lists the "Cut for Wood" row (the one her current task, shown under Currently, serves), and each of its rows matches the Tasks panel's row of the same name in status and detail, changing when the panel's does (except Available and Unassigned, which go by distance: from the camera on the panel, from Ann on the tab). It lists no row the panel doesn't (they refresh on separate 0.2 s timers, so a row can show on one a moment before the other; a mismatch that lasts is the failure).

6. Look at the stump after the fell: the remainder is one compact heap of logs lying bare, no crate under it. At speed 3 the heap and the Haul row are gone within seconds, so set speed 1 first (`/api/dev/time?speed=1`) or use a dev-dropped heap (step 9's `give` with `where=loose`).

7. Click the heap: one gold outline goes around the whole heap and the panel shows Wood, `Quantity: N` and `Mass: <N x 2.5> kg`.

8. Keep it selected while Bob hauls: the quantity drops each trip, and the panel closes when the heap is gone.

9. Drop a different item on a wood heap, `/api/dev/give?material=SmallStone&n=5&where=loose&at=<heap x>,<heap y>`: the stones land beside the heap, clear of it, not on top.

10. Spawn a second box 1 m from the stocked one (`/api/dev/spawn?def=BasicBox&at=<BX+9>,<BY>&n=1&scatter=0`), give Bob wood in hand (`/api/dev/give?material=Wood&n=10&where=colonist&slot=hands`), then click the new box, press its **Move** button, and click a spot a few meters away. When Bob lifts the crated box he puts the wood in the stocked box (`what=storage` Wood rises by 10) and no heap appears where he picked it up.

11. Poll the box inventory as Bob hauls armfuls:

```
curl.exe "http://127.0.0.1:<PORT>/api/state?what=storage"
# -> storages[0].inventory.Wood climbs with each deposit trip
```

12. Keep Bob's needs up every ~2 real minutes:

```
curl.exe "http://127.0.0.1:<PORT>/api/dev/need?colonist=<BOB_ID>&need=Hunger&value=95"
curl.exe "http://127.0.0.1:<PORT>/api/dev/need?colonist=<BOB_ID>&need=Thirst&value=95"
curl.exe "http://127.0.0.1:<PORT>/api/dev/need?colonist=<BOB_ID>&need=Energy&value=95"
```

13. Poll until box reaches or exceeds 200 Wood:

```
curl.exe "http://127.0.0.1:<PORT>/api/state?what=storage"
# -> storages[0].inventory.Wood >= 200
```

14. Add a berry rule with a cap to the box (`/api/dev/storage?at=<BX+8>,<BY>&item=Berry&min=0&max=5`) and drop 20 berries 3 m from it (`/api/dev/give?material=Berry&n=20&where=loose&at=<BX+11>,<BY>`): a colonist carries berries in, `what=storage` shows the box's `Berry` reach 5 and stay there, and the heap keeps the other 15.

15. Open the Tasks panel, then fill the box (`/api/dev/give?material=Wood&n=2000&where=storage&at=<BX+8>,<BY>`): within a second the box's "Haul Wood" row leaves the panel, though the box still has its rules.

---

## Expected state

```
# Box inventory reaches target
GET /api/state?what=storage
  -> storages[0].inventory.Wood >= 200

# Colonist used axe (stow-on-fell: axe on belt after a chop)
GET /api/state?what=colonists
  -> colonists[0].belt contains "AxePrimitive"  (at some point during the run)
```

---

## Pass / fail

**Pass:** box inventory reaches ~200 Wood (verified to 205 in testing) via Bob chopping trees
and hauling armfuls. The axe was stowed to belt after at least one fell.

**Fail:** Bob stops acting; box inventory does not increase over several real minutes at speed 3;
or the axe never appears in `belt` (indicating stow-on-fell is broken — cross-check against the
belt-stow regression in [../regressions.md](../regressions.md)).

---

## Notes

Wood is a two-hand bulk item (stack 40, 2.5 kg). An armful is weight-limited by Bob's strength;
expect several trips to reach 200. Each fell produces a loose ground stack at the stump (one
pile, drawn as 1 to 5 logs by how full it is; a second drop on the same spot tops it up);
subsequent haul trips deplete it before Bob moves to the next tree.
