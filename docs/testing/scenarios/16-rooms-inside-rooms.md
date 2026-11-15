# Scenario 16: Rooms inside rooms

**Status:** draft  
**Last verified:** not yet run

A closed wall loop standing free inside another is its own room. Each keeps its own name, floor
tint, outline, label, and click target, whichever loop was built first.

---

## Preconditions

Fresh launch (Quick Start). Full launch and environment setup in [../README.md](../README.md).

---

## Setup

Turn reveal on (`/api/dev/reveal?on=1`) and frame the spot with
`/api/control?action=camera&x=<X0+2.5>&y=<Y0+2.5>&zoom=3`. Pick open ground clear of trees and
water, with room for 5 x 5 m twice (the second spot 10 m or more from the first). Call the first
spot's corner `X0,Y0` and the second's `X1,Y1`. No state view lists rooms; everything is read off
the toasts, the overlay, and the room panel.

---

## Steps

1. Stamp the outer loop (`/api/dev/walls?pts=<X0>,<Y0>;<X0+5>,<Y0>;<X0+5>,<Y0+5>;<X0>,<Y0+5>&material=Wood&thickness=Standard&built=1&host=0&close=1`): a "Room formed" toast names a room.

2. Stamp the inner loop (`pts=<X0+2>,<Y0+2>;<X0+3>,<Y0+2>;<X0+3>,<Y0+3>;<X0+2>,<Y0+3>`, same other parameters): the toast names only the new room, and the outer room keeps its name.

3. Turn the rooms overlay on (the ROOMS button or R): both floors are tinted the same strength and both rooms are outlined, each label inside its own room.

4. Click inside the inner room: its panel shows 1.0 m2.

5. Click the outer room's floor, away from the inner loop: its panel shows 24.0 m2.

6. Move the camera to the second spot, build the inner loop first and the outer loop second, same parameters: the toasts, names, and clicks come out as in steps 1 to 5, and the inner room keeps the name it got first.

---

## Pass / fail

**Pass:** two rooms, never one merged or renamed; both tinted equally, outlined, and labeled inside
their own walls; the clicks read 1.0 m2 and 24.0 m2, in either build order.

**Fail:** the second loop renames or replaces the first room, the outer tint covers or dims the
inner floor, a label sits in the wrong room, or a click inside the inner room opens the outer one.
