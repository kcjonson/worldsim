# Scenario 18: Colonist thoughts

**Status:** ready  
**Last verified:** 2026-11-17

A colonist's thoughts show in the info panel's and the dossier's Log tabs, and the held ones with a mood
effect in the dossier's mood breakdown. Spec: [thoughts.md](../../design/game-systems/colonists/thoughts.md#where-it-shows-in-the-ui).

---

## Steps

Launch `world-sim --scene=game --background --no-settings` (RelWithDebInfo); the dev server is on 127.0.0.1:8081. Bob's id is in `/api/state?what=colonists`. Give thoughts with `/api/dev/thought?colonist=<id>&type=<type>`.

1. Pause (`/api/dev/time?speed=0`): the HUD clock stops.
2. Give Bob `slept_on_ground`: ok.
3. Give Bob `bottled_up` three times: each answers ok.
4. Give Bob `very_hungry`: ok.
5. Give Bob `ate_good_meal`: ok.
6. Click Bob's card: the info panel shows him.
7. Press the panel's LOG tab: Notable is picked, with one block per thought that has a mood effect, newest first, and none for very hungry.
8. Pick All: a very hungry block appears, with a feeling and no mood part.
9. Read the bottled up block: "Bottled up x3. Resigned. -6 mood, 1 day".
10. Read any block's first line: a "Day N, hh:mm" date.
11. Set the clock (`/api/dev/time?set=20:00`): every block keeps its date.
12. Press the panel's eye button: the dossier opens on Bio.
13. Press the dossier's LOG tab: Notable is picked, over the blocks step 7 showed.
14. Give Bob `very_hungry` 60 times: each answers ok.
15. Pick All: 50 blocks, and a "Show older" button under them.
16. Press "Show older": every thought shows, and the button is gone.
17. Open the NEEDS tab: the dialog keeps the size it opened at.
18. Read the Mood breakdown: Needs, a row per held thought with its amount and time left ("24 hours left", the game being paused), largest amount first, and Target.
19. Read the Target row: it reads 100 plus Needs plus the rows' amounts (-10, -6 and +5 for the scripted thoughts, so 89 plus Needs), within 0.1, or the sum on the "Clamped" note under it.
20. Close the dossier and open it on Bob again: it opens at the size it had in step 12, long log and all.
21. `/api/ui/lint` on each tab: `"count":0`.

### Pruning

Before launching, stage a mod beside the exe, `assets/config/thoughts/mods/prune-test.xml`, holding `<thoughts><tuning maxLineLength="48" maxSubjectNameLength="16" whileCheckMinutes="15"><kept limit="60" pruneStartsAt="0.9" pruneStopsAt="0.8" recentDays="0.5" trivialWeight="1" normalWeight="3"/></tuning></thoughts>`, and delete it when done (the staged config then matches the source).

1. Pause, then give Bob `accident_in_public`, `idle_musing`, and `very_hungry`.
2. Forty times, skip an hour (`/api/dev/time?skip=1h`) and give Bob `very_hungry`.
3. Skip 20 hours, give `slept_on_ground`, and skip 20 hours more.
4. Give `quenched` until `thoughts.recorded` in `what=colonists` drops: it drops from 54 to 48, and `merged` is 1.
5. Open the panel's LOG tab and pick All: the oldest run of very hungry is one entry, "Very hungry xN. Annoyed." under a range such as "Day 1, 07:07 to Day 1, 13:07", and the accident, the first very hungry, the first idle musing, and slept on the ground are all there.
6. Open the dossier's NEEDS tab: the breakdown has the slept on the ground row, and Target matches `mood.target`.
