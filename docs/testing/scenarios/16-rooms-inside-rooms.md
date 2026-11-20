# Scenario 16: Rooms inside rooms

**Status:** draft  
**Last verified:** 2026-11-20

A closed wall loop standing free inside another is its own room. Each keeps its own name, floor
tint, outline, label, and click target, whichever loop was built first. It also covers the builder of a
closing wall, who finishes it from outside the room it closes.

---

## Preconditions

Fresh launch (Quick Start). Full launch and environment setup in [../README.md](../README.md).

---

## Setup

Turn reveal on (`/api/dev/reveal?on=1`). The ground near the landing is dotted with trees, and a wall
loop is refused over one, so stand the loops on a built foundation, 20 x 7 m
(`/api/dev/foundation?pts=12,-7;32,-7;32,0;12,0&material=Wood&built=1`, no colonist needed), then wait
two seconds, since a loop stamped the instant it is built is refused. Its inside has room for 5 x 5 m
twice, 10 m or more apart: the first spot's corner is `X0,Y0` = 14,-6 and the second's `X1,Y1` = 26,-6.
Frame the spot with `/api/control?action=camera&x=<X0+2.5>&y=<Y0+2.5>&zoom=3`. No state view lists
rooms; everything is read off the toasts, the overlay, and the room panel.

---

## Steps

1. Stamp the outer loop (`/api/dev/walls?pts=<X0>,<Y0>;<X0+5>,<Y0>;<X0+5>,<Y0+5>;<X0>,<Y0+5>&material=Wood&thickness=Standard&built=1&host=0&close=1`): a "Room formed" toast names a room.

2. Stamp the inner loop (`pts=<X0+2>,<Y0+2>;<X0+3>,<Y0+2>;<X0+3>,<Y0+3>;<X0+2>,<Y0+3>`, same other parameters): the toast names only the new room, and the outer room keeps its name.

3. Turn the rooms overlay on (the ROOMS button or R): both floors are tinted the same strength, both rooms are outlined, and each room's name is drawn once (the two names overlap near the centre, since the inner room is 1 m wide).

4. Click inside the inner room (`/api/input?ev=click,x,y`, logical pixels; see the README's input row for reading them off a screenshot): its panel, bottom left, shows 1.0 m2.

5. Click the outer room's floor, away from the inner loop: its panel shows 24.0 m2.

6. Move the camera to the second spot, build the inner loop first and the outer loop second, same parameters: the toasts, names, and clicks come out as in steps 1 to 5, and the inner room keeps the name it got first.

The builder of a closing wall. The colonist's id and position come from `?what=colonists`, the foundations' ids (FOUND_ID) and the walls from `?what=construction`.

7. Stamp a second built foundation, north of Setup's, `foundation?pts=12,2;32,2;32,9;12,9&material=Wood&built=1`: it lists `Built`.
8. Set `time?speed=0`, then stamp three built walls of a square on it, `walls?pts=14,8;14,3;19,3;19,8&material=Wood&thickness=Standard&host=<FOUND_ID>&built=1&close=0`: three walls list `Built`.
9. Blueprint the fourth, `walls?pts=19,8;14,8&material=Wood&thickness=Standard&host=<FOUND_ID>&built=0&close=0`: it lists `Blueprint`.
10. Teleport the colonist inside the square, `teleport?colonist=<ID>&to=16.5,5.5`: they read (16.5, 5.5).
11. `give?material=Wood&n=2&where=site`, then `time?speed=1`: the colonist walks out of the square through the gap and builds the wall from its far side (y about 8.5).
12. Poll until the fourth wall lists `Built`: the colonist stands outside the square (y above 8.4, or x below 13.6 or above 19.4), and a "Room formed" toast shows.
13. Repeat steps 8 to 12 ten metres east (add 10 to every x) with a built door, `opening?type=Door&pt=26.5,3&built=1` after step 8: the colonist builds from the inside (y below 7.6, the side they are nearer), and once the wall lists `Built` they can leave through the door (y below 2.6 within a minute).
14. Stamp a third built foundation, `foundation?pts=34,-7;54,-7;54,0;34,0&material=Wood&built=1`, spawn a second colonist, `colonist?at=44,-3`, set `time?speed=0`, and on it stamp two built walls of a square, `walls?pts=36,-1;41,-1;41,-6&host=<FOUND_ID>&built=1&close=0`, and blueprint the other two, `walls?pts=41,-6;36,-6;36,-1&host=<FOUND_ID>&built=0&close=0` (both with `material=Wood&thickness=Standard`). Teleport the first colonist to (36.8, -3.5) and the second to (38.5, -5.2), both inside, then `give?material=Wood&n=4&where=site` and `time?speed=1`: both walls list `Built`, a "Room formed" toast shows, and neither colonist is inside the square (each has x below 35.6 or above 41.4, or y below -6.4 or above -0.6).
15. Blueprint one wall that closes nothing, `walls?pts=46,-3;51,-3&host=<FOUND_ID>&built=0&close=0` (same material and thickness), select the second colonist (`select?colonist=<ID>`), press Draft in their panel, teleport them onto its line, `teleport?colonist=<ID>&to=48.5,-3`, and `give?material=Wood&n=2&where=site`: the first colonist builds it, its progress reaches 1 and it stays `Blueprint`, and one "Wall waiting" toast names the second colonist. Teleport the second colonist to (48.5, -1.5): the wall lists `Built`.

---

## Pass / fail

**Pass:** two rooms, never one merged or renamed; both tinted equally, outlined, and named once each;
the clicks read 1.0 m2 and 24.0 m2, in either build order. Every closing wall completes with its
builders outside the room it closes (steps 12 and 14), a room with a door is built from the side the
colonist is nearer and left by its door (step 13), and the wall in step 15 waits, with one toast, until
the drafted colonist steps off it.

**Fail:** the second loop renames or replaces the first room, the outer tint covers or dims the
inner floor, a name is missing or names the wrong room, or a click inside the inner room opens the outer
one. A closing wall completes with a builder inside the room it closes, a wall completes with the
drafted colonist on its line, or the step 15 toast is missing or repeats.
