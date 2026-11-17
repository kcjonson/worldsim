# Scenario 18: Colonist thoughts

**Status:** ready  
**Last verified:** 2026-11-17

A colonist's thoughts show in the info panel's and the dossier's Log tabs, and the held ones with a mood
effect in the dossier's mood breakdown. Spec: [thoughts.md](../../design/game-systems/colonists/thoughts.md#where-it-shows-in-the-ui).

---

## Steps

Launch `world-sim --scene=game --background --no-settings` (RelWithDebInfo); the dev server is on 127.0.0.1:8081. Bob's id is in `/api/state?what=colonists`.

1. Give Bob `slept_on_ground`, `bottled_up` three times, `very_hungry` and `ate_good_meal` (`/api/dev/thought?colonist=<id>&type=<type>`), then pause (`/api/dev/time?speed=0`).
2. Click Bob's card, then the panel's LOG tab: one block per thought, newest first, each a "Day 1, hh:mm" line, a line like "Slept on the ground. Miserable. -10 mood, 1 day", and the quip in quotes; the list scrolls inside the panel.
3. The bottled up block reads "Bottled up x3. Resigned. -6 mood, 1 day"; the very hungry block has no mood part ("Very hungry. Annoyed.").
4. Press the panel's eye button and open the dossier's LOG tab: the same blocks, in the same order.
5. Open the NEEDS tab: the dialog keeps its size; Mood breakdown lists Needs, a row per held thought with its amount and time left (such as "23 hours left"), and Target. Needs plus every row plus 100 is the Target, and it matches `mood.target` in `what=colonists`.
6. `/api/ui/lint` on each tab: `"count":0`.
