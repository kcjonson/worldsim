# Scenario 24: Box-to-box pull

**Status:** ready  
**Last verified:** 2026-11-20

A colonist moves stored goods from a box to one that ranks the item at a strictly higher priority,
up to the higher box's limit, never below the lower box's own minimum, and never sideways or down.
Spec: [storage-system.md](../../design/features/storage-system.md#priority-based-movement).

---

## Preconditions

Fresh launch (Quick Start): `world-sim --scene=game --background --no-settings` (RelWithDebInfo), dev
server on 127.0.0.1:<PORT>. Keep time at normal speed. Bob starts with no axe, so nothing here makes
him cut trees, and the item, Wood, lies nowhere loose on a fresh map.

`/api/dev/storage` sets one rule on the box nearest `at`, replacing that box's rule for the same item.

---

## Setup

Pause (`/api/control?action=pause`), read Bob's `x, y` from `/api/state?what=colonists` (note as
`BX, BY`), and check `/api/dev/ground?pts=<BX+4>,<BY>;<BX+8>,<BY>` reads `buildableNow` true at both
(`spawn` answers ok even when it refuses a spot); else pick another open spot near him. Then:

```
curl.exe "http://127.0.0.1:<PORT>/api/dev/spawn?def=BasicBox&at=<BX+4>,<BY>&n=1&scatter=0"
curl.exe "http://127.0.0.1:<PORT>/api/dev/spawn?def=BasicBox&at=<BX+8>,<BY>&n=1&scatter=0"
curl.exe "http://127.0.0.1:<PORT>/api/dev/storage?at=<BX+4>,<BY>&item=Wood&priority=Low&min=0&max=0"
curl.exe "http://127.0.0.1:<PORT>/api/dev/storage?at=<BX+8>,<BY>&item=Wood&priority=Low&min=0&max=10"
curl.exe "http://127.0.0.1:<PORT>/api/dev/give?material=Wood&n=20&where=storage&at=<BX+4>,<BY>"
```

Box A is the one at `BX+4`, box B the one at `BX+8`.

---

## Steps

1. Read `/api/state?what=storage`: A holds `Wood` 20 with a `Wood` rule at `Low`, B holds nothing with a `Wood` rule at `Low`, max 10.
2. Resume (`/api/control?action=resume`) and read `what=storage` after 10 s: A still 20, B still empty (same priority, no pull).
3. Raise B (`/api/dev/storage?at=<BX+8>,<BY>&item=Wood&priority=High&min=0&max=10`): within about 10 s `what=storage` reads A 10 and B 10.
4. Read the log (`curl.exe -m 3 http://127.0.0.1:<PORT>/stream/logs`): an `[AI]` line for Bob reads "Pulling Wood from lower-priority storage", then "Started Withdraw action" and "Deposited 10 x Wood into storage <B's id>" (B's `id` is in `what=storage`).
5. Read `what=storage` 10 s later: still A 10, B 10 (B is at its max).
6. Give A a minimum and B more room (`/api/dev/storage?at=<BX+4>,<BY>&item=Wood&priority=Low&min=8&max=0`, then `/api/dev/storage?at=<BX+8>,<BY>&item=Wood&priority=High&min=0&max=20`): Bob carries 2, A reads 8 and B 12, and they stay so.
7. Raise A above B (`/api/dev/storage?at=<BX+4>,<BY>&item=Wood&priority=Critical&min=0&max=0`): Bob carries B's 12 into A in one or two trips, A reads 20 and B is empty, and nothing goes back to B over the next 10 s.

---

## Pass / fail

**Pass:** wood moves only from the lower-priority box to the higher one, stops at the higher box's
max, leaves the lower box its minimum, and reverses direction only when the priorities do.

**Fail:** wood moves between same-priority boxes; the higher box goes past its max; the lower box
drops below its minimum; wood goes from the higher box to the lower one; Bob stands idle with a pull
open.
