# Scenario 17: Interface size

**Status:** draft  
**Last verified:** 2026-11-16 (steps 1 to 6, 9 and 10 as written; 7 and 8 with Bob selected through `/api/dev/select`, before the pause was added to step 7)

Settings' Interface size scales menus, panels, and text from 60 to 120%, applies at once, and is saved with the window's size, as the drawing settings are. The world under the HUD keeps its size, and a click on it still lands where the pointer is.

---

## Steps

This one runs on the real settings file, since it tests that the size is saved: launch `world-sim` (RelWithDebInfo) with `--background` only, no `--no-settings`. The dev server is on 127.0.0.1:8081. `/api/state?what=uiscale` reads `factor` and `layoutScale`. Read `factor` first and note it as START (it should be 1; if it isn't, step 10 puts it back). A position from `/api/ui/tree` is in ui px; times `layoutScale` it's the logical px `/api/input` takes. Read Bob's `x`, `y` from `/api/state?what=colonists` before a world click.

1. Splash: click Enter Expedition. Main menu: click Settings. Interface size shows 60% to 120% with START selected and Reset disabled when START is 1, and the row's description says the smaller sizes stop where text would get too small to read on this screen.
2. Click 120%: the screen grows at once, nothing restarts; `factor` is 1.2, `layoutScale` rises, Reset is enabled.
3. Click 100%, read `layoutScale`, then click 60%: if it was above 0.8, the screen shrinks at once and `layoutScale` falls; `factor` is 0.6. On a window where it was 0.8, nothing moves, since the floor holds.
4. Click Reset: back to 100%, Reset disabled; `factor` is 1.
5. Click 80% and switch Angle steps on (note whether it was on before; it should be off), then `curl.exe "http://127.0.0.1:8081/api/control?action=exit"`. Relaunch, again with `--background` only, and click through to Settings: 80% is selected, Angle steps is on; `factor` is 0.8.
6. Click 60%, Back (the main menu), Quick Start: the HUD is small but every panel is inside the window, nothing overlaps; `/api/ui/lint` answers `"count":0`.
7. Space to pause (Bob wanders), read his `x`, `y`, `...camera&x=<BX>&y=<BY>&zoom=6`, click the window's centre: Bob is selected and his panel opens. Click Details: the dossier is inside the window, its tab scrolling if the tab is taller.
8. Escape, Main Menu, Settings, 120%, Back, Quick Start: the HUD is large, still inside the window; `"count":0`. Step 7 again: the click selects Bob, and the dossier is held to 92% x 86% of the window.
9. Escape, Settings (from the pause menu), click the START stop (Reset if START is 1) and put Angle steps back as step 5 found it: `factor` is START. Back: the paused game, its menu open.
10. `curl.exe "http://127.0.0.1:8081/api/control?action=exit"`: the exit saves START. The window's size is saved on that exit too, so it needs no step.
