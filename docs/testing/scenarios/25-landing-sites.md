# Scenario 25: Landing sites

**Status:** ready  
**Last verified:** 2026-11-20, every step as now written on RelWithDebInfo with injected input (step 7's and 9's open land and water clicks taken after step 13's zoom, once the first land point chosen turned out to be water)

The world creator suggests good landing sites as markers on the globe, and a landing comes down beside the river or lake that made the spot good. Spec: [landing-sites.md](../../technical/landing-sites.md).

---

## Steps

Launch `world-sim --background --no-settings` (RelWithDebInfo); the dev server is on 127.0.0.1:8081. `/api/state?what=landingSites` reads the suggested sites (marker `x`, `y` in input space), the selection, and `cameraMoving`; `/api/state?what=landing` reads the landing once in game. A pick is a click, a press released within 6 ui px of where it went down, and it picks what was under the press; the globe turns only past that travel, and a drag picks nothing. Marker positions hold only while the camera is still: after a zoom or a drag, read `landingSites` until `cameraMoving` is false, then click. The globe turns on its own until it is first touched by a press, a scroll, a right-click, or a key (`cameraMoving` stays true until then); a click on the stage's empty top-left corner (just inside `wc_stage`, on the starfield) stops it and picks nothing. Input that moves the pointer goes where the last `ev=move` put it, so a scroll or a drag starts with one.

1. Click Enter Expedition (`btn_enter_expedition`), then Settings (`menu_settings`): a Landing group with "Show suggested landing sites", off, and `/api/ui/lint` reads `"count":0`.
2. Click its toggle (`landing_suggested_sites_toggle`): on. Click Back (`settings_back`).
3. Click New Game (`menu_new_game`), Confirm Scenario (`btn_scenario_confirm`), the crew step's Generate (`btn_party_generate`), then Generate (`btn_wc_generate`): the globe. While the planet generates `landingSites` reads `surveying` false with no `sites`, so wait until `sites` is not empty (the footer reads "Surveying landing sites..." briefly in between; a planet with no sites says so in the footer instead).
4. Read `landingSites`: 1 to 10 sites, both `river` and `lake` kinds if there are several; `selection.suggestedIndex` is 0 and `recommended` true; the Landing Zone pane shows Recommended; `/api/ui/lint` reads `"count":0`.
5. Look at the globe: a small teal diamond (river) or circle (lake) at each site with `onScreen` true, none for the others, and the amber crosshair on site 0.
6. Stop the turning (the empty corner), read until `cameraMoving` is false, and click a shown marker at its `x`, `y`: `selection.suggestedIndex` is that index, and the pane shows Recommended and fresh water. Scroll the pane (`ev=move,x,y&ev=scroll,x,y,-3` at a point on one of `landing_details`'s rows; a gap between rows doesn't take the wheel): Provisions shows Food and Wood, each Yes.
7. Click open land well away from every marker: the crosshair moves to the click, `suggestedIndex` is null, and `pickLatDeg`/`pickLonDeg` are where you clicked. A click on water leaves the selection and shows "Select a land tile to start the colony".
8. Click a shown marker with a little travel (`ev=move,x,y&ev=down,x,y&ev=move,x+2,y+1&ev=up,x+2,y+1` at its `x`, `y`): `suggestedIndex` is that marker's index, `cameraMoving` reads false at once, and every marker's `x`, `y` read the same as before.
9. Do the same on open land well away from every marker: `pickLatDeg`/`pickLonDeg` equal what a plain click at that point gives, `suggestedIndex` is null, and the markers have not moved.
10. Click the Biomes segment of the dock under the globe (`wc_viz`): the globe recolours by biome and the selection reads the same. Click Terrain to go back.
11. Note `pickLatDeg`, `pickLonDeg`, and `suggestedIndex`, then drag the globe from a point on water (`ev=move,x,y&ev=down,x,y&ev=move,x+20,y&ev=move,x+50,y&ev=up,x+50,y`): the globe turns, the three read the same as before, and no "Select a land tile" hint appears.
12. Scroll over the globe (`ev=move,x,y&ev=scroll,x,y,3`): `cameraMoving` reads true at once and false again within about a second.
13. Drag the globe until a site's `onScreen` reads false: no marker is drawn for it, and its `x`, `y` read null if it went round to the far side.
14. Read until `cameraMoving` is false, click a shown marker, then Confirm Landing Site (`btn_wc_accept`) and Begin Descent (`btn_begin_descent`): the game loads.
15. Read `landing`: `feature` is the marker's kind, `river` or `lake`, `featureDistanceMeters` is between 27 and 33, and the water is in sight of the crew, who stand back from its bank on dry ground.
16. Exit, relaunch with `--no-settings`, and click Quick Start (`btn_enter_expedition`, then `menu_quick_start`): the world creator is not involved, `landing` reads `biome` TemperateDeciduousForest, `feature` `river`, and `landingLatDeg`, `landingLonDeg`, `spawnXMeters`, `spawnYMeters`, and `featureDistanceMeters` match the table in the spec's [section 8](../../technical/landing-sites.md#8-quick-starts-landing).
17. Relaunch with `--no-settings` and generate a planet as in step 3: no markers on the globe (`markersShown` false), and site 0 is still selected. Stop the turning, zoom in on a site so its own tile is wide under the pointer (scroll in over it, then read until `cameraMoving` is false), and click at its `x`, `y`: an ordinary pick, which lands on that site, so `suggestedIndex` is the site's index and `landingLatDeg`/`landingLonDeg` are its `latDeg`/`lonDeg`, while `pickLatDeg`/`pickLonDeg` are the click's.
