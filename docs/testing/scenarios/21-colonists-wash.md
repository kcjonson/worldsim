# Scenario 21: Colonists wash

**Status:** ready  
**Last verified:** 2026-11-20

A colonist low on Hygiene walks to water it knows and washes there, the way it drinks. Spec:
[needs.md](../../design/game-systems/colonists/needs.md#hygiene).

---

## Preconditions

Fresh launch (Quick Start): `world-sim --scene=game --background --no-settings` (RelWithDebInfo), dev
server on 127.0.0.1:8081. Quick Start lands about 28 m from a river and gives the crew memory of the landing area, so Bob
knows the water at once. Bob's id is in `/api/state?what=colonists`; ids are large numbers (for example 4294967296), so
copy it exactly.

`/stream/logs` never ends, so read it with a bounded request: `curl.exe -m 3 http://127.0.0.1:8081/stream/logs`.
It starts a ring of 1000 entries back, so read it soon after the step that causes the line. The run's log
file under `%TEMP%\world-sim\logs` has the same lines.

---

## Steps

1. Wait 5 s after the game scene is up: Bob has looked around and knows the river.
2. Note the highest `id` in Bob's `thoughts.recent` (none when the list is missing).
3. Set Bob's Hygiene to 20 (`/api/dev/need?colonist=<id>&need=Hygiene&value=20`): ok.
4. Read the log (`curl.exe -m 3 http://127.0.0.1:8081/stream/logs`): an `[AI]` line for Bob starts "Hygiene at" and ends `-> (x, y)`; note that point.
5. Read `/api/dev/ground?pts=<x>,<y>` with step 4's point: `inWater` is true, or `toWaterM` is near 0, so the target is at the river.
6. Read the log again: an `[Action]` line for Bob reads "Started Wash action", so he walked there and began washing.
7. Read the log again: an `[Action]` line for Bob reads "Completed Wash action", about 3 s after the start.
8. Read Bob in `what=colonists`: `needs.Hygiene` is above 99.
9. Read Bob's `thoughts.recent` entries newer than step 2's id: a hygiene-falls thought such as `filthy` (expected, since Hygiene was set to 20), then exactly one `freshly_washed`.
10. Pause the game (`/api/control?action=pause`): ok.
11. Set Bob's Thirst to 30 (`/api/dev/need?colonist=<id>&need=Thirst&value=30`): ok.
12. Set Bob's Hygiene to 30 (`/api/dev/need?colonist=<id>&need=Hygiene&value=30`): ok.
13. Resume the game (`/api/control?action=resume`): ok.
14. Read Bob over the next game minutes: his `action` is `Drink` first.
15. Keep reading: his `action` turns `Wash` after the drink.
16. Read Bob after both: `needs.Thirst` and `needs.Hygiene` are both above 90.
17. Read Bob's card in `/api/ui/tree` (the colonist list, top-left) and `layoutScale` in `/api/state?what=uiscale`: its ui px position times `layoutScale` is the point for `/api/input`.
18. Click Bob's card: the info panel opens at the bottom-left with tabs NEEDS, BIO, GEAR, LOG.
19. Press the panel's eye button: the colonist details dialog opens.
20. Press the dialog's NEEDS tab: Hygiene is listed under "Vital needs" with Hunger, Thirst, Energy, Bladder, and Digestion, at full brightness.
21. Read the same tab below that group: Recreation and Temperature are under "Comfort", dimmed.

---

## Pass / fail

**Pass:** Bob walks to the river and washes, Hygiene rises to full, one `freshly_washed` arrives, and with
Thirst and Hygiene low he does both.

**Fail:** Bob stands still or wanders with low Hygiene while he knows water; the action at the water is
`Drink` when he went to wash; Hygiene stays low after the wash; a second `freshly_washed` arrives for one
wash.
