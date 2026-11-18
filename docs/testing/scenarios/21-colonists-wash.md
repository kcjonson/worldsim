# Scenario 21: Colonists wash

**Status:** ready  
**Last verified:** 2026-11-17

A colonist low on Hygiene walks to water it knows and washes there, the way it drinks. Spec:
[needs.md](../../design/game-systems/colonists/needs.md#hygiene).

---

## Preconditions

Fresh launch (Quick Start): `world-sim --scene=game --background --no-settings` (RelWithDebInfo), dev
server on 127.0.0.1:8081. Quick Start lands by a river, so Bob has seen water within his first few
seconds. Bob's id is in `/api/state?what=colonists`.

---

## Steps

1. Wait 5 s after the game scene is up: Bob has looked around and knows the river.
2. Note the highest `id` in Bob's `thoughts.recent` (none when the list is missing).
3. Set Bob's Hygiene to 20 (`/api/dev/need?colonist=<id>&need=Hygiene&value=20`): ok.
4. Read the log (`/stream/logs`): an `[AI]` line for Bob starts "Hygiene at".
5. Read Bob in `what=colonists`: his target is at the river.
6. Read Bob each second: his `x` and `y` move toward that target.
7. Read Bob when he arrives: his `action` is `Wash`.
8. Read again after the wash: `needs.Hygiene` is 100.
9. Read Bob's `thoughts.recent` entries newer than step 2's id: exactly one `freshly_washed`.
10. Pause the game (`/api/control?action=pause`): ok.
11. Set Bob's Thirst to 30 (`/api/dev/need?colonist=<id>&need=Thirst&value=30`): ok.
12. Set Bob's Hygiene to 30 (`/api/dev/need?colonist=<id>&need=Hygiene&value=30`): ok.
13. Resume the game (`/api/control?action=resume`): ok.
14. Read Bob over the next game minutes: his `action` is `Drink` first.
15. Keep reading: his `action` turns `Wash` after the drink.
16. Read Bob after both: `needs.Thirst` and `needs.Hygiene` are both above 90.
17. Click Bob's card: his dossier opens.
18. Press the dossier's NEEDS tab: Hygiene is listed under Vital needs, at full brightness.

---

## Pass / fail

**Pass:** Bob walks to the river and washes, Hygiene rises to 100, one `freshly_washed` arrives, and with
Thirst and Hygiene low he does both.

**Fail:** Bob stands still or wanders with low Hygiene while he knows water; the action at the water is
`Drink` when he went to wash; Hygiene stays low after the wash; a second `freshly_washed` arrives for one
wash.
