# Scenario 22: Colonists sleep

**Status:** ready  
**Last verified:** 2026-11-20

A tired colonist with no bed sleeps on the ground where he stands, the sleep pays its Energy at
its end, and a critical need wakes him. Spec:
[needs.md](../../design/game-systems/colonists/needs.md#energy-sleep).

---

## Preconditions

Fresh launch (Quick Start): `world-sim --scene=game --background --no-settings` (RelWithDebInfo), dev
server on 127.0.0.1:<PORT>. Bob's id is in `/api/state?what=colonists`; ids are large numbers (for
example 4294967296), so copy it exactly. Keep time at normal speed.

No shipped asset is a bed (nothing has the `sleepable` capability), so the ground is the only sleep a
tester can make; a known bed taken before the ground is covered by the sim unit tests.

Read the log with a bounded request, `curl.exe -m 3 http://127.0.0.1:<PORT>/stream/logs`, soon after
the step that causes the line (it starts 1000 entries back).

---

## Steps

1. Wait 5 s after the game scene is up, then note the highest `id` in Bob's `thoughts.recent` (none when the list is missing).
2. Pause (`/api/control?action=pause`), set Bob's Energy to 25 (`/api/dev/need?colonist=<id>&need=Energy&value=25`), then resume (`/api/control?action=resume`): within a second Bob's `action` is `Sleep`.
3. Read the log: an `[AI]` line for Bob reads "Energy at 24% (using ground)" and ends with a point at his own `x, y`, then an `[Action]` line reads "Started Sleep action (8.0s duration)".
4. Read Bob 4 s later: `action` still `Sleep`, `x, y` unchanged, `needs.Energy` still about 25 (the sleep pays at its end, not along the way).
5. Read Bob after the 8 s are up: `action` is no longer `Sleep` and `needs.Energy` is about 55 (25 plus the ground sleep's 30); the log reads "Completed Sleep action (restored 30.0%)".
6. Read Bob's `thoughts.recent` entries newer than step 1's id: a `getting_tired` (Energy fell below 30), then exactly one `slept_on_ground`; `thoughts.held` holds `slept_on_ground`.
7. Set Bob's Energy to 25 again and read until his `action` is `Sleep`.
8. Set Bob's Thirst to 5 (`need=Thirst&value=5`): on the next read his `action` is no longer `Sleep` and he walks to the river, where his `action` turns `Drink`.
9. Read Bob after the drink: `needs.Thirst` above 90, `needs.Energy` still below 30 (the broken sleep paid nothing), and his `action` is `Sleep` again.
10. Set Bob's Energy to 100 to end.

---

## Pass / fail

**Pass:** Bob lies down where he stands, stays still for 8 s, gains 30 Energy at the end and not
before, records one `slept_on_ground`, and a critical thirst wakes him to drink without paying the
sleep.

**Fail:** Bob walks somewhere to sleep with no bed known; Energy rises during the sleep; the sleep
pays a value other than 30 on the ground; critical thirst leaves him asleep; a woken sleep pays out.
