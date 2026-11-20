# Scenario 23: Colonists relieve themselves

**Status:** ready  
**Last verified:** 2026-11-20

A colonist low on Bladder or Digestion picks a spot on the ground off the water, relieves himself
there for 2, 4, or 5 s, a poop leaves a bio pile, and the next spot is drawn toward a pile he knows.
Spec: [needs.md](../../design/game-systems/colonists/needs.md#bladder--poop).

---

## Preconditions

Fresh launch (Quick Start): `world-sim --scene=game --background --no-settings` (RelWithDebInfo), dev
server on 127.0.0.1:<PORT>. Bob's id is in `/api/state?what=colonists`; copy it exactly. Keep time at
normal speed.

No shipped asset is a latrine (nothing has the `toilet` capability), so the ground is the only relief
a tester can make; a known latrine taken before the ground is covered by the sim unit tests
(`RelievingSpotTest`).

Read the log with a bounded request, `curl.exe -m 3 http://127.0.0.1:<PORT>/stream/logs`, soon after
the step that causes the line. The `[ToiletLocation]` lines are debug lines and show there too.

---

## Steps

1. Wait 5 s after the game scene is up, then note the highest `id` in Bob's `thoughts.recent` (none when the list is missing).
2. Set Bob's Bladder to 25 (`/api/dev/need?colonist=<id>&need=Bladder&value=25`): the log has an `[AI]` line for Bob, "Bladder at 24%", ending with the spot he chose; note it.
3. Read `/api/dev/ground?pts=<spot x>,<spot y>`: `valid` is true and `toWaterM` is above 1.5 (or null, water far off).
4. Read the log: "Started Toilet action (2.0s duration)", then "Completed Toilet action (restored 100.0%)", and no "Spawned Bio Pile" line.
5. Read Bob: `needs.Bladder` is 100 (or a hair under).
6. Set Bob's Digestion to 25: the log reads "Started Toilet action (4.0s duration)", then "Spawned Bio Pile at (x, y)"; note that point as `PX, PY`. Bob's `needs.Digestion` is 100.
7. Pause, teleport Bob 4 m east of the pile (`/api/dev/teleport?colonist=<id>&to=<PX+4>,<PY>`), set both his Bladder and Digestion to 25, and resume: the `[AI]` line's spot is about 1 m from `PX, PY` (the grid point 3 m from him toward the pile), not where he stands.
8. Read the log: "Started Toilet action (5.0s duration)", one "Spawned Bio Pile" within about 1.5 m of the first, and Bob's `needs.Bladder` and `needs.Digestion` both 100.
9. Read Bob's `thoughts.recent` entries newer than step 1's id: one `sweet_relief` for each need that rose back past 30, so four (one each for steps 2 and 6, two for step 7).
10. Set Bob's Thirst to 40 and read the log: the `[AI]` line "Thirst at" ends with a shore point; note it once he has drunk.
11. Pause, teleport Bob to that shore point, check `/api/dev/ground` there reads `toWaterM` below 1.5, set his Bladder to 25, and resume: the `[AI]` line's spot is a different point, and `/api/dev/ground` at it reads `toWaterM` above 1.5.

---

## Pass / fail

**Pass:** each relief picks valid ground more than 1.5 m from water, takes 2 s for Bladder, 4 s for
Digestion, and 5 s for both, refills what it served, and only a poop leaves a bio pile; a known pile
draws the next spot toward it.

**Fail:** a chosen spot within 1.5 m of water or in it; Bob stands still or wanders with Bladder or
Digestion below 30; a pee leaves a bio pile; a combined relief leaves two piles or takes other than
5 s; the step 7 spot ignores the pile.

---

## Notes

Bob arrives within about half a meter of the spot, short of it on the side he came from, and squats
where he stops, so in step 11 the squat point (his `x, y` during `Toilet`) can be nearer the water
than 1.5 m even though the spot he chose is not.
