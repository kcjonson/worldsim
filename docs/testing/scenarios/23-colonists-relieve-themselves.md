# Scenario 23: Colonists relieve themselves

**Status:** ready  
**Last verified:** 2026-11-20

A colonist low on Bladder or Digestion picks a spot on the ground off the water, relieves himself
there for 2, 4, or 5 s, a poop leaves a bio pile, and the next spot is drawn toward a pile he knows.
Spec: [needs.md](../../design/game-systems/colonists/needs.md#bladder--poop).

---

## Preconditions

Fresh launch (Quick Start): `world-sim --scene=game --background --no-settings` (RelWithDebInfo), dev
server on 127.0.0.1:8081, or the port given with `--http-port`; `<PORT>` below is that port. Bob's id
is in `/api/state?what=colonists`; copy it exactly. Keep time at normal speed. Run after scenario 22
in the same game is fine once his `action` is not `Sleep`.

No shipped asset is a latrine (nothing has the `toilet` capability), so the ground is the only relief
a tester can make; a latrine is covered by unit tests.

Read the log with a bounded request, `curl.exe -m 3 http://127.0.0.1:<PORT>/stream/logs`, soon after
the step that causes the line. An `[AI]` line ends with the chosen spot `(x, y)`; read that, not the
arrow or the `(tier N, score N)` part before it.

`thoughts.recent` shows only the 5 newest thoughts and others arrive on their own, so each step that
counts `sweet_relief` reads it straight after that relief, by `id` above the highest seen before.

---

## Steps

1. Wait 5 s after the game scene is up, then note the highest `id` in Bob's `thoughts.recent` (none when the list is missing).
2. Set Bob's Bladder to 25 (`/api/dev/need?colonist=<id>&need=Bladder&value=25`): the log has an `[AI]` line for Bob, "Bladder at 24%" or "Bladder at 25%"; note its final `(x, y)` as the spot.
3. Read `/api/dev/ground?pts=<spot x>,<spot y>`: `valid` is true and `toWaterM` is above 1.5 (or null, water far off).
4. Read the log: "Started Toilet action (2.0s duration)", then "Completed Toilet action (restored 100.0%)", and no "Spawned Bio Pile" line.
5. Read Bob: `needs.Bladder` is 100 (or a hair under) and `thoughts.recent` has exactly one new `sweet_relief`; note the highest `id`.
6. Set Bob's Digestion to 25: the log reads "Started Toilet action (4.0s duration)".
7. Read the log: "Spawned Bio Pile at (x, y)"; note that point as `PX, PY`.
8. Read Bob: `needs.Digestion` is 100 and `thoughts.recent` has exactly one new `sweet_relief`; note the highest `id`.
9. Pause and read `/api/dev/ground?pts=<PX+4>,<PY>;<PX+1>,<PY>`: both `valid`, `toWaterM` above 1.5. If not, use `PX-4` and `PX-1` in steps 10 and 11.
10. Teleport Bob to `PX+4, PY` (`/api/dev/teleport?colonist=<id>&to=<PX+4>,<PY>`), then set his Bladder and his Digestion to 25.
11. Resume: the `[AI]` line's spot is `PX+1, PY`. If it reads as his own position instead, a known Berry is near: rerun from a fresh launch.
12. Read the log: "Started Toilet action (5.0s duration)", then one "Spawned Bio Pile" within 2 m of `PX, PY`.
13. Read Bob: `needs.Bladder` and `needs.Digestion` both 100, and `thoughts.recent` has exactly two new `sweet_relief`.
14. Set Bob's Thirst to 40 and read the log until it shows "Started Drink action", then pause: he stays at the shore.
15. Read Bob's `x, y` and `/api/dev/ground` there: `toWaterM` below 1.5.
16. Set his Bladder to 25 and resume.
17. Read the log after "Completed Drink action": the `[AI]` "Bladder at" line's spot is not his `x, y`, and `/api/dev/ground` at it reads `toWaterM` above 1.5.
18. Exit the game (`/api/control?action=exit`).

---

## Pass / fail

**Pass:** each relief picks valid ground more than 1.5 m from water, takes 2 s for Bladder, 4 s for
Digestion, and 5 s for both, refills what it served, and only a poop leaves a bio pile; a known pile
draws the next spot toward it.

**Fail:** a chosen spot within 1.5 m of water or in it; Bob stands still or wanders with Bladder or
Digestion below 30; a pee leaves a bio pile; a combined relief leaves two piles or takes other than
5 s; the step 11 spot ignores the pile.

---

## Notes

Bob arrives within about half a meter of the spot, short of it on the side he came from, and squats
where he stops, so in step 17 the squat point (his `x, y` during `Toilet`) can be nearer the water
than 1.5 m even though the spot he chose is not.
