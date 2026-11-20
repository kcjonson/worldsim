# Scenario 25: Colonists build openings

**Status:** ready  
**Last verified:** 2026-11-20

A door or window not yet built sits inside its wall, where no route ends. The colonist carries its
materials to beside the wall, on a side he can get to, delivers them there, builds it, and later
demolishes it from beside it too. A door in a sealed room's wall is supplied and built from outside.
Spec: [task-claims.md](../../technical/task-claims.md#build-and-deconstruct).

---

## Preconditions

Fresh launch (Quick Start): `world-sim --scene=game --background --no-settings`, dev server on
127.0.0.1:8081, or the port given with `--http-port`; `<PORT>` below is that port. Read every dev
reply: a refusal answers HTTP 400 with the reason.

---

## Setup

```
curl.exe "http://127.0.0.1:<PORT>/api/dev/time?speed=0"
curl.exe "http://127.0.0.1:<PORT>/api/dev/reveal?on=1"
curl.exe "http://127.0.0.1:<PORT>/api/dev/foundation?pts=18,8;32,8;32,18;18,18&material=Wood&built=1"
curl.exe "http://127.0.0.1:<PORT>/api/dev/walls?pts=20,15;30,15&material=Wood&thickness=Standard&host=1&built=1&close=0"
```

`what=construction` lists the wall as segment 1, `Built`.

---

## Steps

1. Blueprint a door (`/api/dev/opening?type=Door&seg=1&t=0.5&built=0`, at 25,15), give Wood (`/api/dev/give?material=Wood&n=20&where=loose&at=22,12`), and set `time?speed=3`: within about 30 s `what=construction` shows the opening `delivered` `Wood` 3, then `state` `Built`, `progress` 1.
2. Read the log (`curl.exe -m 3 http://127.0.0.1:<PORT>/stream/logs`): a route to `(25.00, 14.58)` or `(25.00, 15.42)` (beside the wall), "Deposit 3 x Wood", "Opening #1 built", and no "no known way to (25.00, 15.00)".
3. Set `time?speed=0`, build a closed room south of the wall (`/api/dev/walls?pts=19,9;31,9;31,13;19,13&material=Wood&thickness=Standard&host=1&built=1&close=1`), blueprint a door in its north wall (`/api/dev/opening?type=Door&seg=4&t=0.5&built=0`, at 25,13) and a window in its east wall (`/api/dev/opening?type=Window&seg=3&t=0.5&built=0`, at 31,11), give Wood outside (`/api/dev/give?material=Wood&n=20&where=loose&at=25,5`), and set `time?speed=3`.
4. Within about a minute both openings read `Built`; the log shows the door's delivery and build routed to `(25.00, 13.42)` and the window's to `(31.42, 11.00)`, both outside the room.
5. Watch Bob for a minute: he walks into the room through the new door and out again.
6. Pause, click the room's door, and press Demolish in its panel; resume: the log shows "Deconstructing structure", then "Opening #2 deconstructed", and the opening leaves `what=construction`.
7. Pause, click the room's west wall (19, 9 to 13), and press Demolish; resume: "Wall segment #5 deconstructed", and no "no known way" on the way.
8. Exit the game (`/api/control?action=exit`).

---

## Pass / fail

**Pass:** every opening is supplied and built by the colonist, from beside its wall and from outside
a sealed room, and a demolished door and wall are taken down, with no route to an opening's or wall's
own centre failing.

**Fail:** an opening stays at `progress` 0 with its Wood delivered or carried; the log shows "no known
way" to an opening or wall followed by "Put down ... nobody wants"; a sealed room's door is supplied
from inside; a demolish order is never worked.
