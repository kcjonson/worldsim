# Scenario 16: Rooms inside rooms

**Status:** draft  
**Last verified:** 2026-11-20

A closed wall loop standing free inside another is its own room. Each keeps its own name, floor
tint, outline, label, and click target, whichever loop was built first.

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

A doorless room whose last wall the colonist builds from inside. Bob's id and position come from `?what=colonists`, the foundation's id (FOUND_ID) and the walls from `?what=construction`.

7. Stamp a second built foundation, `foundation?pts=12,2;32,2;32,9;12,9&material=Wood&built=1`: it lists `Built`.
8. Set `time?speed=0`, then stamp three built walls of a square on it, `walls?pts=14,8;14,3;19,3;19,8&material=Wood&thickness=Standard&host=<FOUND_ID>&built=1&close=0`: three walls list `Built`.
9. Blueprint the fourth, `walls?pts=19,8;14,8&material=Wood&thickness=Standard&host=<FOUND_ID>&built=0&close=0`: it lists `Blueprint`.
10. Teleport Bob inside the square, `teleport?colonist=<BOB_ID>&to=16.5,5.5`: he reads (16.5, 5.5).
11. `give?material=Wood&n=2&where=site`, then `time?speed=1`: Bob walks out of the square through the gap and builds the wall from its far side (y about 8.5).
12. Poll until the fourth wall lists `Built`: Bob stands outside the square (y above 8.4, or x below 13.6 or above 19.4), and a "Room formed" toast shows.
13. Repeat steps 8 to 12 ten metres east (add 10 to every x) with a built door, `opening?type=Door&pt=26.5,3&built=1` after step 8: Bob may build from either side, and he can walk out through the door after.

---

## Pass / fail

**Pass:** two rooms, never one merged or renamed; both tinted equally, outlined, and named once each;
the clicks read 1.0 m2 and 24.0 m2, in either build order.

**Fail:** the second loop renames or replaces the first room, the outer tint covers or dims the
inner floor, a name is missing or names the wrong room, or a click inside the inner room opens the outer
one.
