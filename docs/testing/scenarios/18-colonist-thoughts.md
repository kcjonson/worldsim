# Scenario 18: Colonist thoughts

**Status:** ready  
**Last verified:** 2026-11-17

A colonist's thoughts show in the info panel's and the dossier's Log tabs, and the held ones with a mood
effect in the dossier's mood breakdown. Spec: [thoughts.md](../../design/game-systems/colonists/thoughts.md#where-it-shows-in-the-ui).

---

## Steps

Launch `world-sim --scene=game --background --no-settings` (RelWithDebInfo); the dev server is on 127.0.0.1:8081. Bob's id is in `/api/state?what=colonists`. Give thoughts with `/api/dev/thought?colonist=<id>&type=<type>`.

1. Pause (`/api/dev/time?speed=0`): the HUD clock stops.
2. Give Bob `slept_on_ground`, `bottled_up` three times, `very_hungry` and `ate_good_meal`: each answers ok.
3. Click Bob's card: the info panel shows him.
4. Press the panel's LOG tab: Notable is picked, with one block per thought that has a mood effect, newest first, and none for very hungry.
5. Pick All: a very hungry block appears, with a feeling and no mood part.
6. Read the bottled up block: "Bottled up x3. Resigned. -6 mood, 1 day".
7. Read any block's first line: a "Day N, hh:mm" date.
8. Set the clock (`/api/dev/time?set=20:00`): every block keeps its date.
9. Press the panel's eye button: the dossier opens on Bio.
10. Press the dossier's LOG tab: Notable is picked, over the blocks step 4 showed.
11. Give Bob `very_hungry` 60 times: each answers ok.
12. Pick All: 50 blocks, and a "Show older" button under them.
13. Press "Show older": every thought shows, and the button is gone.
14. Open the NEEDS tab: the dialog keeps the size it opened at.
15. Read the Mood breakdown: Needs, a row per held thought with its amount and time left ("23 hours left"), and Target.
16. Add 100, Needs and every row: the Target within 0.1, or the sum on the "Clamped" note under it.
17. Close the dossier and open it on Bob again: it opens at the size it had in step 9, long log and all.
18. `/api/ui/lint` on each tab: `"count":0`.
