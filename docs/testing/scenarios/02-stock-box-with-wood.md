# Scenario 02: Stock box with wood

**Status:** ready  
**Last verified:** 2026-06-28

Verifies that a colonist with an axe chops trees, hauls wood, and stocks a `BasicBox` to a
target quantity via a storage rule.

---

## Preconditions

Colonist has an `AxePrimitive`. Either run [01-craft-axe-and-box.md](./01-craft-axe-and-box.md)
first, or give the axe directly:

```
curl.exe "http://127.0.0.1:<PORT>/api/dev/give?material=AxePrimitive&n=1&where=colonist"
```

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
curl.exe "http://127.0.0.1:<PORT>/api/dev/storage?at=<BX+8>,<BY>&item=Wood&category=RawMaterial&priority=High&min=200&max=0"
```

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
# -> storage[0].rules[0].item == "Wood", rules[0].min == 200
```

2. Watch Bob chop a tree. After a fell, poll colonist state:

```
curl.exe "http://127.0.0.1:<PORT>/api/state?what=colonists"
# -> colonists[0].hands should show Wood (armful)
# -> colonists[0].belt should show AxePrimitive (stowed on fell)
# -> colonists[0].cargoKg > 0
```

3. Poll the box inventory as Bob hauls armfuls:

```
curl.exe "http://127.0.0.1:<PORT>/api/state?what=storage"
# -> storage[0].inventory.Wood climbs with each deposit trip
```

4. Keep Bob's needs up every ~2 real minutes:

```
curl.exe "http://127.0.0.1:<PORT>/api/dev/need?colonist=<BOB_ID>&need=Hunger&value=95"
curl.exe "http://127.0.0.1:<PORT>/api/dev/need?colonist=<BOB_ID>&need=Thirst&value=95"
curl.exe "http://127.0.0.1:<PORT>/api/dev/need?colonist=<BOB_ID>&need=Energy&value=95"
```

5. Poll until box reaches or exceeds 200 Wood:

```
curl.exe "http://127.0.0.1:<PORT>/api/state?what=storage"
# -> storage[0].inventory.Wood >= 200
```

---

## Expected state

```
# Box inventory reaches target
GET /api/state?what=storage
  -> storage[0].inventory.Wood >= 200

# Colonist used axe (stow-on-fell: axe on belt after a chop)
GET /api/state?what=colonists
  -> colonists[0].belt.AxePrimitive >= 1  (at some point during the run)
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
expect several trips to reach 200. Each fell produces a loose ground stack at the stump;
subsequent haul trips deplete it before Bob moves to the next tree.
