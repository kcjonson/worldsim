# Scenario 22: Colonists sleep

**Status:** ready  
**Last verified:** 2026-11-20

A tired colonist with no bed sleeps on the ground where he stands, the sleep pays its Energy at
its end, and a critical need wakes him. Spec:
[needs.md](../../design/game-systems/colonists/needs.md#energy-sleep).

---

## Preconditions

Fresh launch (Quick Start): `world-sim --scene=game --background --no-settings` (RelWithDebInfo), dev
server on 127.0.0.1:8081, or the port given with `--http-port`; `<PORT>` below is that port. Bob's id
is in `/api/state?what=colonists`; ids are large numbers (for example 4294967296), so copy it exactly.
Keep time at normal speed.

No shipped asset is a bed (nothing has the `sleepable` capability), so the ground is the only sleep a
tester can make; a bed is covered by unit tests.

Read the log with a bounded request, `curl.exe -m 3 http://127.0.0.1:<PORT>/stream/logs`, soon after
the step that causes the line (it starts 1000 entries back). An `[AI]` line ends with the target
point `(x, y)`; read that, not the arrow or the `(tier N, score N)` part before it.

---

## Steps

1. Wait 5 s after the game scene is up, then note the highest `id` in Bob's `thoughts.recent` (none when the list is missing).
2. Pause (`/api/control?action=pause`) and set Bob's Energy to 25 (`/api/dev/need?colonist=<id>&need=Energy&value=25`).
3. Resume (`/api/control?action=resume`): Bob's `action` turns `Sleep`, at once or when the action he was in ends (a fresh Bob is often mid-`Harvest`, which a need that isn't critical doesn't interrupt).
4. Read the log: an `[AI]` line for Bob reads "Energy at 24% (using ground)" or "Energy at 25% (using ground)", and its final `(x, y)` is his own position.
5. Read the log again: an `[Action]` line reads "Started Sleep action (8.0s duration)".
6. Read Bob about 4 s after that line: `action` still `Sleep`, `x, y` unchanged, `needs.Energy` still about 25.
7. Read the log after the 8 s are up: "Completed Sleep action (restored 30.0%)".
8. Read Bob: `action` is not `Sleep` and `needs.Energy` is about 55.
9. Straight away read Bob's `thoughts.recent` (it holds only the 5 newest): newer than step 1's id are a `getting_tired`, then exactly one `slept_on_ground`.
10. Read `thoughts.held`: it holds `slept_on_ground`.
11. Set Bob's Energy to 25 again and read until his `action` is `Sleep`; note his `needs.Energy`.
12. Set Bob's Thirst to 5 (`need=Thirst&value=5`): on the next read his `action` is no longer `Sleep`.
13. Read the log until it shows "Completed Drink action": he walked to the river and drank.
14. Read Bob: `needs.Thirst` above 90, `needs.Energy` about 25 and no higher than step 11's value, and `action` is `Sleep` again.
15. Set Bob's Energy to 100.
16. Exit the game (`/api/control?action=exit`).

---

## Pass / fail

**Pass:** Bob lies down where he stands, stays still for 8 s, gains 30 Energy at the end and not
before, records one `slept_on_ground`, and a critical thirst wakes him to drink without paying the
sleep.

**Fail:** Bob walks somewhere to sleep with no bed known; Energy rises during the sleep; the sleep
pays a value other than 30 on the ground; critical thirst leaves him asleep; a woken sleep pays out.
