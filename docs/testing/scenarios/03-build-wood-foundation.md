# Scenario 03: Build wood foundation over trees

**Status:** ready  
**Last verified:** 2026-11-13

Verifies the full foundation construction lifecycle: a colonist clears on-footprint trees, chops
for materials, and builds a `Wood` foundation to completion, unaided, without freezing.

---

## Preconditions

Colonist has an `AxePrimitive`. Either run [01-craft-axe-and-box.md](./01-craft-axe-and-box.md)
first, or give the axe directly:

```
curl.exe "http://127.0.0.1:<PORT>/api/dev/give?material=AxePrimitive&n=1&where=colonist&slot=hands"
```

---

## Setup

Find Bob's position:

```
curl.exe "http://127.0.0.1:<PORT>/api/state?what=colonists"
# -> colonists[0].x, colonists[0].y  (note as BX, BY)
```

Choose a ~3x3 area on valid land that has at least one or two trees and a berry bush
(`Flora_BerryBush`) standing on it (the forest around the Quick Start landing usually provides
this). A tree or bush placed with `/api/dev/spawn` blocks the footprint and is cleared the same way
as world-generated ones. Note the corner coordinates as `FX0,FY0` and `FX1,FY1`.

Blueprint the foundation (built=0 queues the colonist to build it):

```
curl.exe "http://127.0.0.1:<PORT>/api/dev/foundation?pts=<FX0>,<FY0>;<FX1>,<FY0>;<FX1>,<FY1>;<FX0>,<FY1>&material=Wood&built=0"
```

Note the foundation `id` from `?what=construction` for later assertions:

```
curl.exe "http://127.0.0.1:<PORT>/api/state?what=construction"
# -> construction.foundations[0].id  (note as FOUND_ID)
```

Accelerate time:

```
curl.exe "http://127.0.0.1:<PORT>/api/dev/time?speed=3"
```

---

## Steps

1. Poll construction until the phase moves through `Clearing`. Bob should fell any trees on the
   footprint; they disappear (one action each):

```
curl.exe "http://127.0.0.1:<PORT>/api/state?what=construction"
# -> foundations[0].phase == "Clearing"  (initial)
# -> wait until phase != "Clearing"
```

2. The berry bush on the footprint is cleared too: it disappears after one harvest (it doesn't
   regrow in place), even when Bob's arms are already full of wood, and no berries are delivered
   to the site. Use a bush the world generated, not a spawned one, and look at the screen
   (`control?action=camera&x=<bush x>&y=<bush y>&zoom=8`, then a screenshot): the bush is no
   longer drawn once the phase leaves `Clearing`, and none is drawn on the built foundation:

```
curl.exe "http://127.0.0.1:<PORT>/api/state?what=construction"
# -> foundations[0].delivered never has a "Berry" key
```

3. Phase moves to `AwaitingMaterials`. Bob chops nearby trees and provisions wood:

```
curl.exe "http://127.0.0.1:<PORT>/api/state?what=construction"
# -> foundations[0].phase == "AwaitingMaterials"
# -> foundations[0].delivered.Wood increases over time
```

4. Watch a fell during `AwaitingMaterials` that leaves a heap of logs at the stump: Bob's next trip is to that heap, not to another tree, and he carries it to the site (`delivered.Wood` rises) before he cuts again.

5. Phase moves to `UnderConstruction` once materials are delivered. Bob builds:

```
curl.exe "http://127.0.0.1:<PORT>/api/state?what=construction"
# -> foundations[0].phase == "UnderConstruction"
# -> foundations[0].progress increases toward 1.0
```

6. Keep Bob's needs up every ~2 real minutes:

```
curl.exe "http://127.0.0.1:<PORT>/api/dev/need?colonist=<BOB_ID>&need=Hunger&value=95"
curl.exe "http://127.0.0.1:<PORT>/api/dev/need?colonist=<BOB_ID>&need=Thirst&value=95"
curl.exe "http://127.0.0.1:<PORT>/api/dev/need?colonist=<BOB_ID>&need=Energy&value=95"
```

7. Poll until foundation is `Built` at `progress == 1.0`:

```
curl.exe "http://127.0.0.1:<PORT>/api/state?what=construction"
# -> foundations[0].state == "Built"
# -> foundations[0].progress == 1.0
```

   Screenshot the built foundation: it sits on bare ground, with no bush, grass tuft or stone
   drawn on it.

8. Blueprint an L-shaped foundation beside the first (six points: an 8 m square with its north-east
   quarter cut away, `pts=X,Y;X+8,Y;X+8,Y+4;X+4,Y+4;X+4,Y+8;X,Y+8`), placed so a tree stands in the
   notch (`X+6,Y+6`) and grass grows there. Bob does not clear the notch's tree (no "Clearing ...
   from build site" for it), the site leaves `Clearing` with it standing (he may still cut it later
   for Wood, as any tree), and once built the notch's grass is still drawn while the floor is bare. A packaged box (one not yet placed) lying on its floor before the blueprint was drawn sits
   just outside the floor once building starts; one lying in the notch has not moved.

9. With the site built, give Bob an armful of wood (`/api/dev/give?material=Wood&n=10&where=colonist&slot=hands`): if no box in the world still takes Wood, the next time he wanders he puts it down as a heap where he stands (`colonists[0].hands` empties, no deposit anywhere); if a box does take Wood, he carries it there instead.

10. Once the L-shaped foundation from step 8 is built, select it and press Demolish: the Tasks panel shows one "Deconstruct structure" row reading "Deconstructing - Bob (N%)", N rising, and Bob's dossier Tasks tab lists the same row under Known work.

11. When Bob finishes (the L-shaped foundation leaves `?what=construction`), the row leaves the Tasks panel and the Known work list.

12. Open the Build menu: it has one Foundation entry. Pick it: the Rectangle tool opens, the first pick this session. The config strip shows the tool picker with Rectangle picked, the material cards, and "Click or drag from a corner".

13. Move the pointer over open ground near Bob: the white snapped-point marker (a dot with four ticks) sits on the whole-metre grid point nearest the pointer.

14. Click once: a dot marks the first corner, and the strip reads "Click the opposite corner" in its usual colour, not red.

15. Move the pointer about 4 m right and 3 m up: a faint fill and outline follow it, labelled `4 m x 3 m` up and to the right of the marker.

16. Click: `?what=construction` lists another foundation with `"area":12`.

17. Press on open ground, drag from the grid point under the pointer to the grid point 5 m right and 4 m up (the size label reads `5 m x 4 m`), and release: `?what=construction` lists another foundation with `"area":20`, and the strip reads "Click or drag from a corner".

18. Move the pointer onto a corner of a new blueprint: an amber ring sits round the marker's dot. Along one of its edges, the ring is cyan.

19. Click open ground once, then move the pointer until the rectangle covers one of the new blueprints: the fill, the outline, and the marker's dot turn red, and the strip names the reason.

20. Click there: nothing is added, a toast says why, and the first corner keeps waiting.

21. Move the pointer to make a 1 m by 1 m rectangle: it previews red with a reason.

22. Press Esc: the first corner goes, and the tool stays on.

23. Press Esc again: the tool closes.

24. Pick Foundation again (Rectangle opens), press on open ground, drag onto the gameplay bar, and release there: nothing is added, and the first corner keeps waiting.

25. Click open ground about 3 m right and 3 m up of that corner: `?what=construction` lists another foundation with `"area":9`.

26. Pick Freeform in the strip's picker and click a first point: the strip shows no too-close refusal while the pointer has not moved since the click.

27. Click three more points round a square, then the first point again: it closes into another foundation.

28. Press Esc to leave the tool and pick Foundation in the Build menu: Freeform opens, picked in the strip.

29. Pick Rectangle in the picker: the Rectangle tool is on again.

30. Pick Freeform, click two points, and move the pointer out of the game's window (a person can also bring another window to the front; a `--background` launch ignores focus, so only leaving the window counts there): the rubber band, the marker, and its label go, the two points and their edge stay, and the strip shows no red reason.

31. Move the pointer back over the world: the rubber band follows it again from the second point, and a click places a third point.

32. Pick Rectangle, click a first corner, and move the pointer out of the window: the pending fill, outline, and size label go and the corner's dot stays; come back and click the opposite corner: the rectangle is added.

33. Press Esc: the Rectangle tool closes.

34. Open the top bar's Menu and pick Main Menu: the main menu shows.

35. Pick Settings: the Settings scene shows a Drawing Settings section.

36. Switch on Angle steps, Guides from the shape, Common lengths, and Angle readout: each reads on.

37. Pick Back, then Quick Start: a new game shows.

38. Pick Foundation in the Build menu, then Freeform in the strip's picker: the strip shows Freeform picked.

39. Click two points 4 m apart, left to right: a dot marks each, with an edge between them.

40. Move the pointer up and to the right at 45 degrees, about 2.5 m out: an arc sits in the corner at the second point with `135.0°` beside it (a real degree sign), clear of the arc.

41. Bring the pointer out along that line to 4 m: the segment label reads `4.00 m =` in bold amber.

42. Look at the first edge: it shows a faint band with a double tick at its middle.

43. Move the pointer straight up from the second point, about 2.5 m: the arc and degrees give way to the right-angle marker, and the `=` and the marks go.

44. Move the pointer back to the 45 degree position and hold Alt: no arc, degrees, marker, `=`, or marks.

45. Release Alt and press Esc twice: the tool closes.

46. Open the top bar's Menu, pick Main Menu, then Settings: the Drawing Settings section shows.

47. Switch Common lengths and Angle readout off: each reads off.

48. Pick Back, then Quick Start: a new game shows.

49. Repeat steps 38 to 41: no arc, no degrees, no `=`, and no marks.

---

## Expected state

```
GET /api/state?what=construction
  -> foundations[0].state == "Built"
  -> foundations[0].progress == 1.0
  -> foundations[0].material == "Wood"
  -> foundations[0].phase == "Complete"  (or equivalent terminal phase)
```

---

## Pass / fail

**Pass:** foundation reaches `state == "Built"` and `progress == 1.0` with no manual
intervention beyond need top-ups. All four phases completed: Clearing -> AwaitingMaterials ->
UnderConstruction -> Built.

**Fail:** Bob freezes in any phase without progressing. Note the phase and `colonists[0].action`
value at the time of freeze. Cross-check against the harvest-reachability regression in
[../regressions.md](../regressions.md).

---

## Notes

The footprint need not be a perfect rectangle; the `pts` param accepts any polygon as
semicolon-separated `x,y` pairs. A simple `FX0,FY0;FX1,FY0;FX1,FY1;FX0,FY1` rectangle works.

On-water footprints (or footprints that straddle water, a narrow stream included) are refused.
See the on-water placement regression in [../regressions.md](../regressions.md).
