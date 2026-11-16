# Scenario 17: Interface size

**Status:** draft  
**Last verified:** never

Settings' Interface size scales menus, panels, and text from 60 to 120%, applies at once, and is saved with the window's size. The world under the HUD keeps its size, and a click on it still lands where the pointer is.

---

## Steps

Build `world-sim` (RelWithDebInfo) and launch it with no flags; the dev server is on 127.0.0.1:8081. `/api/state?what=uiscale` reads `factor` and `layoutScale`. A position from `/api/ui/tree` is in ui px; times `layoutScale` it's the logical px `/api/input` takes. Read Bob's `x`, `y` from `/api/state?what=colonists` before a world click.

1. Main menu, Settings: Interface size shows 60% to 120%, 100% selected, Reset dimmed; `factor` is 1.
2. Click 120%: the screen grows at once, nothing restarts; `factor` is 1.2, Reset is lit.
3. Click 60%: everything shrinks at once; `factor` is 0.6.
4. Click Reset: back to 100%, Reset dimmed; `factor` is 1.
5. Click 80%, then `curl.exe "http://127.0.0.1:8081/api/control?action=exit"`. Relaunch: the same window size, and Settings shows 80%; `factor` is 0.8.
6. Click 60%, Back, Quick Start: the HUD is small but every panel is inside the window, nothing overlaps; `/api/ui/lint` answers `"count":0`.
7. `...camera&x=<BX>&y=<BY>&zoom=6`, click the window's centre: Bob is selected and his panel opens. Click Details: the dossier is inside the window, its tab scrolling if the tab is taller.
8. Escape, Main Menu, Settings, 120%, then Quick Start: the HUD is large, still inside the window; `"count":0`. Step 7 again: the click selects Bob, and the dossier is held to 92% x 86% of the window.
9. Settings, Reset (100%), then `curl.exe "http://127.0.0.1:8081/api/control?action=exit"`.
