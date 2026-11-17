# Scenario 12: Colonists eat

**Status:** draft  
**Last verified:** not yet run

Two hungry colonists never cut one bush at once, and a hungry colonist eats one meal from a storage
box or a loose pile. Spec: [task-claims.md](../../technical/task-claims.md#need-tasks).

---

## Preconditions

Fresh launch (Quick Start). Full launch and environment setup in [../README.md](../README.md). Every
colonist here is spawned for the step, so its pack holds no food; a colonist with food in its pack
eats that first and searches for nothing. If `what=colonists` shows `Berry` in a new colonist's
`inventory` before its step starts (it gathered while idle), spawn another. Other colonists (Bob, the
starting colonist, and any earlier spawns) keep gathering and wandering throughout and don't count.

A colonist with an empty pack starts gathering within about a second and finishes gather and eat
quickly, so each step sets up with the game paused: pause (`/api/control?action=pause`), set up, then resume
(`/api/control?action=resume`). Dev verbs run while paused, and nothing moves until the resume.
Steps 3 and 4 are the exception on purpose: Eve is left to start her gathering walk before her
Hunger is set, because turning hungry on the way to a bush is the case they check. Step 4 sets her
Hunger straight after the resume, with no wait: a colonist with no task decides on the first frame
after the resume and again every 0.5 s, and her meal goes to the nearest food she knows, so a wait
lets her walk nearer a bush than the box, and eating at that bush is then correct, not a failure.
On a retry, spawn only a new colonist beside the existing box: don't spawn a second box or give more
berries. Identify her by id (the name stays `Eve`), note the box's current `Berry` count, and expect it
to drop by 1.

The Quick Start map has a berry bush within about 13 m of any spot, and no dev verb clears bushes, so
a colonist can always go to another bush: a second colonist waiting on a reserved or regrowing bush
(the "reserved by" and "source regrowing" task rows) is not checkable here.

---

## Setup

Pick a spot on open ground, one the `ground` state view (`/api/dev/ground?pts=<x>,<y>`) reads `valid` at, with room for the 20 m to the box. Note it as `FX, FY`. Keep time at
normal speed, and don't skip time (`ate_good_meal` is held for 6 game hours, which steps 4 to 7
rely on).

---

## Steps

1. Pause, spawn one bush (`/api/dev/spawn?def=Flora_BerryBush&at=<FX>,<FY>&n=1&scatter=0`) and two colonists 2 m from it (`/api/dev/colonist?at=<FX+2>,<FY>&n=2&name=Hungry`), read their ids from `what=colonists` (it also lists Bob and any earlier spawns: ignore them), set both Hunger to 40 (`/api/dev/need?colonist=<id>&need=Hunger&value=40`), then resume: each cuts a bush and eats, and at no time are both cutting the same bush (with other bushes around, one cuts the placed bush and the other walks to a different one).

2. Read `what=colonists` a little later: both Hungry colonists have eaten, each with `needs.Hunger` above 50.

3. Pause, spawn a `BasicBox` 20 m from the bush (`/api/dev/spawn?def=BasicBox&at=<FX+20>,<FY>&n=1&scatter=0`), put 10 berries in it (`/api/dev/give?material=Berry&n=10&where=storage&at=<FX+20>,<FY>`), spawn Eve 1 m from it (`/api/dev/colonist?at=<FX+21>,<FY>&name=Eve`), and read her `id` from `what=colonists`. Note it. On a retry, spawn only a new Eve (see Preconditions).

4. Resume and set her Hunger straight after, as one command line with no wait: `curl.exe "http://127.0.0.1:<PORT>/api/control?action=resume" && curl.exe "http://127.0.0.1:<PORT>/api/dev/need?colonist=<Eve id>&need=Hunger&value=40"`. She had already started the gather walk when Hunger was set, about 1 m from the box, and still eats from the box, the nearer food: `what=storage` shows its `Berry` drop by 1 (10 to 9). Pause as soon as her Hunger passes 50.

5. Check Eve's `inventory` has no `Berry` (if it has, spawn only another Eve beside the box and repeat steps 3 and 4; steps 5 to 7 only mean something for an Eve who ate the step 4 box meal, so `ate_good_meal` is held and `thoughts.recent` has entries to take an id from). Read her position, then drop 10 berries 1 m from her on the side away from the box (`/api/dev/give?material=Berry&n=10&where=loose&at=<her x+1>,<her y>` if her x is above the box's `FX+20`, else `<her x-1>,<her y>`), note the highest `id` in her `thoughts.recent` (`what=colonists`), set her Hunger to 5, then resume: she takes 2 berries from the heap or the box, whichever is nearer, never more, and her Hunger rises past 50.

6. Read Eve in `what=colonists`, looking only at the `thoughts.recent` entries with an `id` above the one noted in step 5: among them is exactly one `starving`, and no `getting_peckish`, `very_hungry` or `ate_good_meal` (it is non-stacking and already held from step 4, so this meal folds into the held one and records nothing new); `thoughts.held` still holds `ate_good_meal`. Thoughts about other needs may be there too (eating fills the gut, so a Digestion one can land) and don't count.

7. Read Eve again a few seconds later, with the same id cut-off: no new `starving`, `getting_peckish`, `very_hungry` or `ate_good_meal` has arrived, and `ate_good_meal` is still held. Her Hunger drifts down on its own and may be anywhere above 5 and below its post-meal value; only those thought kinds count.

---

## Expected state

```
GET /api/state?what=storage
  -> the box's inventory.Berry dropped by the meal size each time (1 at Hunger 40, 2 at Hunger 5)

GET /api/state?what=colonists
  -> each colonist that ate has needs.Hunger > 50 afterwards
  -> Eve's thoughts.recent entries newer than step 5's noted id: exactly one starving; no
     getting_peckish, very_hungry or ate_good_meal; thoughts.held still holds ate_good_meal; other
     needs' thoughts may sit among them
```

---

## Pass / fail

**Pass:** two colonists never cut one bush at once and each eats; each meal from a box or heap takes
1 or 2 berries, never a pack-full.

**Fail:** both colonists cut the same bush in one go; a hungry colonist with food in reach stands
frozen; a colonist empties the box or heap into its pack; a repeat meal records a second
`ate_good_meal`.
