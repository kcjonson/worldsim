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
13. Read the dossier's bounds in `/api/ui/tree`: note its width and height.
14. Press the dossier's LOG tab: Notable is picked, over the blocks step 7 showed.
15. Give Bob `very_hungry` 60 times: each answers ok.
16. Pick All: 50 blocks, and a "Show older" button under them.
17. Press "Show older": every thought shows, and the button is gone.
18. Open the NEEDS tab: the dialog keeps the size it opened at.
19. Set Bob's Hunger to 20 (`/api/dev/need?colonist=<id>&need=Hunger&value=20`): the breakdown shows a "Hunger" row of about -13.3.
20. Read the breakdown's need rows: one per need that is costing something, its label and cost, largest first, and no row for a need that costs nothing.
21. Read the breakdown's thought rows: one per held thought, its amount and time left ("24 hours left", the game being paused), largest amount first.
22. Read the Target row: 100 plus the need rows (or 0, when "Needs take mood to 0 at most" shows under them) plus the thought rows (-10, -6 and +5 for the scripted thoughts), within 0.1 for each row, or that sum on the "Clamped" note under it.
23. Close the dossier and open it on Bob again: it opens at the width and height step 13 noted, long log and all.
24. Open the developer client (`build/developer-client/index.html`, as a `file://` page) and its Dev Tools tab. The client talks to http://localhost:8081, so the tab steps apply to this game on its default port.
25. Press Refresh: the colonist list and the thought types fill in.
26. Pick Bob in the colonist list: Give thought and Set attribute turn on.
27. Pick `Idle musing` in the thought list: it shows as picked.
28. Press Give thought: the feedback line reads `thought -> ` with `"created":true`.
29. In the panel's LOG tab pick All: an idle musing block is the newest.
30. Pick `strength` in the attribute list and set the value to 20: both show as picked.
31. Press Set attribute: the feedback line reads ok.
32. Read `/api/state?what=colonists`: Bob's `attributes.strength` is 20.
33. `/api/ui/lint` on each tab: `"count":0`.

### Pruning

A separate run on a fresh launch with a small record: a limit of 60 prunes at 54 thoughts (0.9) and stops at 48 (0.8), and a thought is recent for 12 game hours (`recentDays="0.5"`). `ID` is Bob's id from `/api/state?what=colonists`, and giving a thought means `curl "http://127.0.0.1:8081/api/dev/thought?colonist=ID&type=<type>"`.

1. Exit the game: `curl "http://127.0.0.1:8081/api/control?action=exit"` answers `"shutdown":"complete"`.
2. Write `build/apps/world-sim/RelWithDebInfo/assets/config/thoughts/mods/prune-test.xml` holding `<thoughts><tuning maxLineLength="48" maxSubjectNameLength="16" whileCheckMinutes="15"><kept limit="60" pruneStartsAt="0.9" pruneStopsAt="0.8" recentDays="0.5" trivialWeight="1" normalWeight="3"/></tuning></thoughts>`.
3. Launch `world-sim --scene=game --background --no-settings`: the game starts.
4. Pause (`/api/dev/time?speed=0`): the HUD clock stops.
5. Read Bob in `/api/state?what=colonists`: he has no `thoughts` field, which a colonist only has once he has had a thought.
6. Give Bob `accident_in_public`, `idle_musing`, and `very_hungry`: each answers ok, and `thoughts.recorded` is 3.
7. Run `for i in $(seq 40); do curl -s "http://127.0.0.1:8081/api/dev/time?skip=1h"; curl -s "http://127.0.0.1:8081/api/dev/thought?colonist=ID&type=very_hungry"; done`: `thoughts.recorded` is 43.
8. Skip 20 hours (`/api/dev/time?skip=20h`): the HUD clock moves on 20 hours.
9. Give Bob `slept_on_ground`: `thoughts.recorded` is 44.
10. Skip 20 hours again: the HUD clock moves on 20 hours.
11. Run `for i in $(seq 9); do curl -s "http://127.0.0.1:8081/api/dev/thought?colonist=ID&type=quenched"; done`: `thoughts.recorded` is 53, below the start size.
12. Give Bob `quenched` once more: `thoughts.recorded` is 48 and `merged` is 1.
13. Open Bob's info panel LOG tab and pick All: the oldest run of very hungry is one entry, "Very hungry x7. Annoyed." under a range of six hours such as "Day 1, 07:07 to Day 1, 13:07".
14. Scroll the same list: the accident, the first very hungry, the first idle musing, slept on the ground, and ten quenched are all there, with 33 other very hungry blocks.
15. Open the dossier's NEEDS tab: the breakdown has the slept on the ground row, and Target matches `mood.target`.
16. Exit the game and delete `prune-test.xml`: the staged config folder matches the source again.
