# Scenario 01: Craft axe and box

**Status:** ready  
**Last verified:** 2026-06-28

Verifies the full gather-and-craft loop: a colonist harvests raw materials from nearby world
entities (no freebies) and produces both a `Recipe_AxePrimitive` and a `Recipe_BasicBox` at a
`CraftingSpot`.

---

## Preconditions

Fresh launch. Bob starts with no axe, no inventory. Full launch and environment setup in
[../README.md](../README.md).

---

## Setup

Find Bob's position (needed for placing things near him):

```
curl.exe "http://127.0.0.1:<PORT>/api/state?what=colonists"
# -> colonists[0].x, colonists[0].y  (note these as BX, BY)
```

Spawn a `CraftingSpot` a few meters from Bob on valid land (not over the river):

```
curl.exe "http://127.0.0.1:<PORT>/api/dev/spawn?def=CraftingSpot&at=<BX+5>,<BY>&n=1&scatter=0"
```

Spawn harvestable resources near Bob. Keep scatter low so they land on valid land:

```
# Woody bushes -> Sticks (no tool required)
curl.exe "http://127.0.0.1:<PORT>/api/dev/spawn?def=Flora_WoodyBush&at=<BX+3>,<BY+3>&n=4&scatter=2"

# Reeds -> PlantFiber (no tool required)
curl.exe "http://127.0.0.1:<PORT>/api/dev/spawn?def=Flora_Reed&at=<BX-3>,<BY+3>&n=3&scatter=2"

# Small stones (loose items, picked up directly)
curl.exe "http://127.0.0.1:<PORT>/api/dev/spawn?def=SmallStone&at=<BX+2>,<BY-2>&n=4&scatter=1"
```

Queue both recipes at the crafting station (axe first, then box):

```
curl.exe "http://127.0.0.1:<PORT>/api/dev/craft?recipe=Recipe_AxePrimitive&n=1&at=<BX+5>,<BY>"
curl.exe "http://127.0.0.1:<PORT>/api/dev/craft?recipe=Recipe_BasicBox&n=1&at=<BX+5>,<BY>"
```

Accelerate time:

```
curl.exe "http://127.0.0.1:<PORT>/api/dev/time?speed=3"
```

---

## Steps

1. Confirm both recipes appear in the station job queue immediately after queueing:

```
curl.exe "http://127.0.0.1:<PORT>/api/state?what=stations"
# -> stations[0].jobs should list Recipe_AxePrimitive and Recipe_BasicBox
```

2. As Bob harvests, poll the station store to watch it fill to each recipe's BOM:

```
curl.exe "http://127.0.0.1:<PORT>/api/state?what=stations"
# -> stations[0].store fills toward:
#    Recipe_AxePrimitive BOM: SmallStone=2, Stick=1, PlantFiber=1
#    Recipe_BasicBox BOM: Stick=8, PlantFiber=3
```

3. Watch for colonist to pick up SmallStone (loose item) and harvest WoodyBush and Reed. The
   station store should fill to the **exact** BOM quantities — no excess. Bob is the one doing
   the work; materials are not given for free.

4. Keep Bob's needs up every ~2 real minutes:

```
curl.exe "http://127.0.0.1:<PORT>/api/dev/need?colonist=<BOB_ID>&need=Hunger&value=95"
curl.exe "http://127.0.0.1:<PORT>/api/dev/need?colonist=<BOB_ID>&need=Thirst&value=95"
curl.exe "http://127.0.0.1:<PORT>/api/dev/need?colonist=<BOB_ID>&need=Energy&value=95"
```

5. Poll colonist inventory until the axe appears:

```
curl.exe "http://127.0.0.1:<PORT>/api/state?what=colonists"
# -> colonists[0].inventory.AxePrimitive >= 1
```

6. Poll storage until a `BasicBox` appears (the crafted box is a container entity):

```
curl.exe "http://127.0.0.1:<PORT>/api/state?what=storage"
# -> at least one entry with defName=BasicBox
```

---

## Expected state

```
# Station jobs listed both recipes during the run
GET /api/state?what=stations
  -> stations[0].jobs contained both Recipe_AxePrimitive and Recipe_BasicBox

# Colonist produced the axe from gathered materials
GET /api/state?what=colonists
  -> colonists[0].inventory.AxePrimitive >= 1

# Box exists as a storage entity
GET /api/state?what=storage
  -> storage[].some(s => s.defName == "BasicBox")
```

---

## Pass / fail

**Pass:** axe appears in Bob's inventory; a `BasicBox` entry appears in `?what=storage`. Both
produced by gathering — no materials were given via `give`.

**Fail:** if Bob freezes without harvesting (check `/api/state?what=colonists` for `action`
field); or if after ~5 real minutes at speed 3 neither recipe completes.

---

## Notes

The station store fills to BOM-exact quantities. If you see the store exceed the BOM for one
recipe before the next is picked up, that's expected behavior between recipe completions.

Crafted `BasicBox` currently lands on the crafting station (cosmetic). The box is still
functional. See [../README.md#known-open-caveats](../README.md#known-open-caveats).
