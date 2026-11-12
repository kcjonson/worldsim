# Scenario 12: Colonists eat

**Status:** draft  
**Last verified:** not yet run

Two hungry colonists don't converge on one bush, a colonist waits on a regrowing bush, and a hungry
colonist eats one meal from a storage box or a loose pile. Spec:
[task-claims.md](../../technical/task-claims.md#need-tasks).

---

## Preconditions

Fresh launch (Quick Start). Full launch and environment setup in [../README.md](../README.md). Every
colonist here is spawned for the step, so its pack holds no food; a colonist with food in its pack
eats that first and searches for nothing. If `what=colonists` shows `Berry` in a new colonist's
`inventory` before its step starts (it gathered while idle), spawn another.

---

## Setup

Pick a spot on open ground, a few meters from the river bank, with no berry bush within about 30 m
(colonists see 30 m; check a screenshot). Note it as `FX, FY`. Keep time at normal speed.

---

## Steps

1. Spawn one bush (`/api/dev/spawn?def=Flora_BerryBush&at=<FX>,<FY>&n=1&scatter=0`) and two colonists 2 m from it (`/api/dev/colonist?at=<FX+2>,<FY>&n=2&name=Hungry`), then at once (an idle colonist with an empty pack goes to gather the bush) set both Hunger to 40 (`/api/dev/need?colonist=<id>&need=Hunger&value=40`): one walks to the bush; select the other (`/api/dev/select?colonist=<id>`) and its task list shows "Hunger at 40% (reserved by <the first>)"; they never both cut it.

2. After the first cuts the bush, the second's row reads "Hunger at ... (source regrowing)" and it does something else; about a game minute later it walks to the bush and cuts it.

3. Spawn a `BasicBox` 20 m from the bush (`/api/dev/spawn?def=BasicBox&at=<FX+20>,<FY>&n=1&scatter=0`), put 10 berries in it (`/api/dev/give?material=Berry&n=10&where=storage&at=<FX+20>,<FY>`), spawn Eve 1 m from it (`/api/dev/colonist?at=<FX+21>,<FY>&name=Eve`), and set her Hunger to 40: she walks to the box, `what=storage` shows its `Berry` drop to 9, and her Hunger rises past 50.

4. Drop 10 berries 1 m from Eve on the side away from the box (`/api/dev/give?material=Berry&n=10&where=loose&at=<her x+1>,<her y>`) and set her Hunger to 5: she takes 2 berries from the heap or the box, whichever is nearer, never more, and her Hunger rises past 50.

---

## Expected state

```
GET /api/state?what=storage
  -> the box's inventory.Berry dropped by the meal size each time (1 at Hunger 40, 2 at Hunger 5)

GET /api/state?what=colonists
  -> each colonist that ate has needs.Hunger > 50 afterwards
```

---

## Pass / fail

**Pass:** two colonists never cut one bush at once; the waiting colonist cuts the bush once it has
regrown; each meal from a box or heap takes 1 or 2 berries, never a pack-full.

**Fail:** both colonists walk to and cut the same bush in one go; the waiting colonist stands frozen
after the bush has regrown, or loops on it while it is regrowing; a colonist empties the box or heap
into its pack.
