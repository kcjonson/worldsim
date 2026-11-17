# Scenario 19: Load errors on screen

**Status:** ready  
**Last verified:** 2026-11-17

A broken config file stops the game load with the error that names it on screen, in the loading scene and on the splash alike, so a modder sees what is wrong without the log. Spec: [config-loading.md](../../technical/config-loading.md#where-load-errors-surface).

---

## Steps

Edit only the staged copy beside the exe, `build/apps/world-sim/RelWithDebInfo/assets/config/`, never the source under `assets/config/`. Launch `world-sim --background --no-settings --http-port <PORT>` (RelWithDebInfo). `/api/state?what=scene` reads the top scene's `failed` and `errors`.

1. In the staged `colonists/needs/needs.xml`, set Hunger's `seek` to `"abc"`.
2. Launch the game.
3. Wait for the splash to finish: Enter Expedition is enabled.
4. Click Enter Expedition, then Quick Start: the loading scene shows a "Loading failed" panel in place of the bar.
5. Read the panel: `[needs] assets/config/colonists/needs/needs.xml: <Need name="Hunger"> seek must be a number, got "abc"` in red.
6. Read under it: "Full log:" and a path under `%TEMP%\world-sim\logs\`, which exists.
7. `/api/state?what=scene`: `failed` is true and `errors` holds the same message.
8. `/api/ui/lint`: `"count":0`.
9. Press Esc: the main menu.
10. Set Hunger's `urgent` and `critical` to `"xyz"` too.
11. Set the `urgent` of Thirst, Energy, Bladder, Digestion, Hygiene, Recreation, and Temperature to `"xyz"`.
12. Click Quick Start again: five messages, then "and 5 more", the messages scrolling inside the panel if they don't all fit.
13. `/api/ui/lint`: `"count":0`.
14. `/api/control?action=scene&scene=game`: the splash shows the "Loading failed" panel with only the colonist rules errors, Enter Expedition disabled and clear of the panel.
15. `/api/control?action=exit`.
16. Copy the source `assets/config` files back over the staged ones.
17. `diff -r assets/config build/apps/world-sim/RelWithDebInfo/assets/config` prints nothing.
18. Launch `world-sim --background --no-settings --scene=game`: the game scene loads.
