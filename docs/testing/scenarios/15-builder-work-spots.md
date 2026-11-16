# Scenario 15: Builder work spots

**Status:** draft  
**Last verified:** 2026-11-16

A foundation has 1 builder spot plus 1 per 10 m2 of area; each builder stands at its own spot just
inside the edge, and the colonists past the last spot do other work. When a builder is removed, a
waiting colonist takes the spot.

---

## Preconditions

Fresh launch (Quick Start). Full launch and environment setup in [../README.md](../README.md).

---

## Setup

Pause (`/api/control?action=pause`; dev verbs still run) so nobody wanders off while you set up, and
read every dev reply: a refusal is HTTP 400 with an `error` body. Use the open ground near the
landing, which the colonists have meshed (the foundation verb refuses ground they have not): corners
`FX0,FY0` = 12,0 and `FX1,FY1` = 16,5, 4 x 5 m.

Spawn four more colonists (`/api/dev/colonist?at=10,2&n=4`), then teleport the five onto the west
side of the site, one each at `10,0` to `10,4` (`/api/dev/teleport?colonist=<id>&to=10,<y>`, ids from
`what=colonists`), and give each an axe at its own spot
(`/api/dev/give?material=AxePrimitive&n=1&where=colonist&slot=hands&at=10,<y>`; clearing a site needs
one).

---

## Steps

1. Blueprint the foundation (`/api/dev/foundation?pts=<FX0>,<FY0>;<FX1>,<FY0>;<FX1>,<FY1>;<FX0>,<FY1>&material=Wood&built=0`), give the site its wood (`/api/dev/give?material=Wood&n=40&where=site`), and resume (`/api/control?action=resume`): `what=construction` shows `phase` `Clearing` while a colonist fells any tree on the footprint, then `UnderConstruction`.

2. Poll `what=colonists` about twice a second: three colonists reach `action` Build, each at a different point just inside the foundation's edge (x, y); the other two are not building.

3. As soon as three are building, pause, then remove one builder (`/api/dev/kill?colonist=<id>`) and resume: within a few seconds one of the waiting colonists shows Build at the freed spot, so there are three builders again at three different points, never four, until the foundation is `Complete`. If it completes before you can pause, start over.

---

## Expected state

```
GET /api/state?what=colonists
  -> exactly three have action Build while the foundation is UnderConstruction
  -> their x, y are three distinct points, each just inside the foundation's edge
```

---

## Pass / fail

**Pass:** never more than three builders, each at its own point just inside the edge; when a
builder is removed, a waiting colonist takes the spot within a few seconds.

**Fail:** a fourth colonist joins, two builders share a point or stand more than about 1 m inside the edge,
or the spot stays empty after its builder is removed while someone is idle.
