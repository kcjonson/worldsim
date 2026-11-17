# Scenario 19: Load errors on screen

**Status:** ready  
**Last verified:** 2026-11-17

A broken config file stops the game load with the error that names it on screen, in the loading scene and on the splash alike, so a modder sees what is wrong without the log. Spec: [config-loading.md](../../technical/config-loading.md#where-load-errors-surface).

---

## Steps

Edit only the staged copy beside the exe, `build/apps/world-sim/RelWithDebInfo/assets/config/`, never the source under `assets/config/`. Launch `world-sim --background --no-settings --http-port <PORT>` (RelWithDebInfo). `/api/state?what=scene` reads the top scene's `failed` and `errors`.

1. In the staged `colonists/needs/needs.xml`, set Hunger's `seek` to `"abc"`, then launch: the splash finishes and Enter Expedition is enabled.
2. Click Enter Expedition, then Quick Start: the loading scene shows a "Loading failed" panel in place of the bar.
3. Read the panel: `[needs] assets/config/colonists/needs/needs.xml: <Need name="Hunger"> seek must be a number, got "abc"` in red.
4. Read under it: "Full log:" and a path under `%TEMP%\world-sim\logs\`, which exists.
5. `/api/state?what=scene`: `failed` is true and `errors` holds the same message.
6. `/api/ui/lint`: `"count":0`.
7. Press Esc: the main menu.
8. Set Hunger's `urgent` and `critical` to `"xyz"` too, and every Thirst to Temperature `urgent` to `"xyz"`, then Quick Start again: five messages, then "and 5 more"; lint is still `"count":0`.
9. `/api/control?action=scene&scene=game`: the splash shows the same panel and messages, Enter Expedition disabled.
10. `/api/control?action=exit`, then copy the source `assets/config` files back over the staged ones: `diff -r assets/config build/apps/world-sim/RelWithDebInfo/assets/config` prints nothing.
11. Launch with `--scene=game`: the game scene loads.
