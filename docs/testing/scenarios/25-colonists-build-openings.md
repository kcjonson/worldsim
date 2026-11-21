# Scenario 25: Colonists build openings

**Status:** ready  
**Last verified:** 2026-11-20

A door or window not yet built sits inside its wall, where no route ends. The colonist carries its
materials to beside the wall, on a side he can get to, delivers them there, builds it, and later
demolishes it. A door and a window in a sealed room's walls are supplied and built from outside.
Spec: [task-claims.md](../../technical/task-claims.md#build-and-deconstruct).

---

## Preconditions

Fresh launch (Quick Start): `world-sim --scene=game --background --no-settings`, dev server on
127.0.0.1:8081, or the port given with `--http-port`; `<PORT>` below is that port. Bob, the one
colonist a Quick Start lands, does all the work; read his `id` from `/api/state?what=colonists` and
note it as `<BOB>`. Read every dev reply: a refusal answers HTTP 400 with
the reason. Nothing moves at `time?speed=0`, though dev verbs still run; the waits below are wall-clock
seconds at `time?speed=3`. The log is read with `curl.exe -m 3 http://127.0.0.1:<PORT>/stream/logs`.

---

## Setup

The site is the foundation of [scenario 04](./04-build-walls.md)'s corner, (14, 0), grown to 16 m by
12 m. Every coordinate below is written as an offset from it, `FX = 14`, `FY = 0`.

1. `/api/dev/time?speed=0`, then `/api/dev/reveal?on=1`.
2. Check the ground: `/api/dev/ground?pts=14,0;30,0;30,12;14,12` reads `buildableNow` true at all four.
   If any is false, add the same amount to every x (or y) below until all four are true, and use the
   shifted values for `FX`, `FY` from then on.
3. Build the foundation: `/api/dev/foundation?pts=<FX>,<FY>;<FX+16>,<FY>;<FX+16>,<FY+12>;<FX>,<FY+12>&material=Wood&built=1`,
   and note its `id` in `/api/state?what=construction` (`foundations`) as `<FOUND>`.
4. Build an open wall: `/api/dev/walls?pts=<FX+2>,<FY+10>;<FX+12>,<FY+10>&material=Wood&thickness=Standard&host=<FOUND>&built=1&close=0`.
   In `/api/state?what=construction` it is the wall with `x0` `FX+2`, `y0` `FY+10`; note its `id` as
   `<SEG_OPEN>`.
5. Give the Wood, outside every room this scenario builds: `/api/dev/give?material=Wood&n=20&where=loose&at=<FX+7>,<FY+8>`.

---

## Steps

1. Blueprint a door in the open wall (`/api/dev/opening?type=Door&seg=<SEG_OPEN>&t=0.5&built=0`); in
   `what=construction` it is the opening at `x` `FX+7`, `y` `FY+10`; note its `id` as `<DOOR_OPEN>`. Set
   `time?speed=3`: within about 30 s that opening reads `delivered` `{"Wood":3}`, then `state` `Built`,
   `progress` 1.
2. Set `time?speed=0`. Build a closed room south of the pile, its north wall 2 m below the open wall:
   `/api/dev/walls?pts=<FX+2>,<FY+2>;<FX+12>,<FY+2>;<FX+12>,<FY+6>;<FX+2>,<FY+6>&material=Wood&thickness=Standard&host=<FOUND>&built=1&close=1`.
   Read the four new walls; note as `<SEG_ROOM_LOW>` the one from (`FX+12`, `FY+6`) to (`FX+2`, `FY+6`)
   and as `<SEG_ROOM_EAST>` the one from (`FX+12`, `FY+2`) to (`FX+12`, `FY+6`).
3. Blueprint a door and a window in it: `/api/dev/opening?type=Door&seg=<SEG_ROOM_LOW>&t=0.5&built=0`
   (at `FX+7`, `FY+6`; note `<DOOR_ROOM>`) and `/api/dev/opening?type=Window&seg=<SEG_ROOM_EAST>&t=0.5&built=0`
   (at `FX+12`, `FY+4`; note `<WINDOW_ROOM>`). Set `time?speed=3`: within about a minute both read
   `delivered` in full, `state` `Built`. Whichever opening he builds first,
   the room is shut until its door is built, so the door is supplied and built from outside.
4. Demolish the open wall's door from its panel, with `time?speed=0`:
   - Point the camera at it (`/api/control?action=camera&x=<FX+7>&y=<FY+10>`), read `what=camera`, and
     turn the door's position into a window pixel with the formulas in
     [the camera view](../README.md#the-camera-view).
   - Click it (`/api/input?ev=click,<px>,<py>`). In `/api/ui/tree` find the `Text` node whose content is
     `Demolish` and click the middle of the `Button` that holds it, its `screenBounds` times `layoutScale`
     from `/api/state?what=uiscale`. Don't send Escape with no tool active: it opens the pause menu.
   - Set `time?speed=3`: within about 30 s `<DOOR_OPEN>` is gone from `what=construction`.
5. Demolish the open wall the same way, clicking it at (`FX+4`, `FY+10`): within about 30 s
   `<SEG_OPEN>` is gone from `what=construction`. This shows a built wall taken down from beside it;
   both its sides are open ground.
6. Exit the game (`/api/control?action=exit`).

---

## Pass / fail

**Pass:** both doors and the window end `Built` with their Wood delivered and no dev completion; the
room's door is built while the room is still shut, from outside; the demolished door and wall leave
`what=construction`; the log has no `[Nav] Entity <BOB>: no known way to` line at all from step 1 to
step 5 (the line names where he was to stand, beside the wall, not the opening, so any such line
counts).

**Fail:** an opening stays at `progress` 0 with its Wood delivered or carried; a
`[Nav] Entity <BOB>: no known way to` line appears during steps 1 to 5; a demolish order is never worked.

Last verified 2026-11-20: setup and steps 1 to 6, word for word from a fresh Quick Start, on commit
ac6093f8 of `fix/WOR-1139-openings-reachable` (Debug).
