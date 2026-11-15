# Scenario 15: Builder work spots

**Status:** draft  
**Last verified:** not yet run

A foundation has 1 builder spot plus 1 per 10 m2 of area; each builder stands at its own spot just
inside the edge, and the colonists past the last spot do other work. A builder who leaves is
replaced.

---

## Preconditions

Fresh launch (Quick Start). Full launch and environment setup in [../README.md](../README.md).

---

## Setup

Pick open ground a few meters from the river bank, clear of trees, with room for 4 x 5 m. Note the
corners as `FX0,FY0` and `FX1,FY1` (`FX1 = FX0+4`, `FY1 = FY0+5`). Keep time at normal speed
(`/api/dev/time?speed=1`); faster, a foundation finishes before you can look.

Spawn four more colonists beside Bob (`/api/dev/colonist?at=<FX0-3>,<FY0>&n=4`), give each of the
five an axe, one call per colonist with its own position
(`/api/dev/give?material=AxePrimitive&n=1&where=colonist&slot=hands&at=<x>,<y>`, positions from
`what=colonists`; clearing a site needs one), and keep their needs up (`/api/dev/need`, as in
scenario 03).

---

## Steps

1. Blueprint the foundation (`/api/dev/foundation?pts=<FX0>,<FY0>;<FX1>,<FY0>;<FX1>,<FY1>;<FX0>,<FY1>&material=Wood&built=0`) and give the site its wood (`/api/dev/give?material=Wood&n=40&where=site`): `what=construction` shows `phase` `UnderConstruction`.

2. Look at `what=colonists`: three colonists have `action` Build, each at a different point just inside the foundation's edge (x, y); the other two are doing other work.

3. Set one builder's Hunger to 5 (`/api/dev/need?colonist=<id>&need=Hunger&value=5`): that colonist leaves to eat, and within a few seconds an idle colonist shows Build, standing just inside the edge; there are still three builders, at three different points.

---

## Expected state

```
GET /api/state?what=colonists
  -> exactly three have action Build while the foundation is UnderConstruction
  -> their x, y are three distinct points, each just inside the foundation's edge
```

---

## Pass / fail

**Pass:** never more than three builders, each at its own point just inside the edge; a builder who
leaves is replaced within a few seconds.

**Fail:** a fourth colonist joins, two builders share a point or stand more than about 1 m inside the edge,
or the spot stays empty after its builder leaves while someone is idle.
