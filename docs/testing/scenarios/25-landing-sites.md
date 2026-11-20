# Scenario 25: Landing sites

**Status:** ready  
**Last verified:** 2026-11-20

The world creator suggests good landing sites as markers on the globe, and a landing comes down beside the river or lake that made the spot good. Spec: [landing-sites.md](../../technical/landing-sites.md).

---

## Steps

Launch `world-sim --background --no-settings` (RelWithDebInfo); the dev server is on 127.0.0.1:8081. `/api/state?what=landingSites` reads the suggested sites (marker `x`, `y` in input space) and the selection; `/api/state?what=landing` reads the landing once in game. The globe turns slowly on its own until it is first pressed, so read `landingSites` and click a marker in one go, or first click the stage's empty top-left corner (just inside `wc_stage`, on the starfield), which stops the turning and picks nothing.

1. Click Enter Expedition (`btn_enter_expedition`), then Settings (`menu_settings`): a Landing group with "Show suggested landing sites", off.
2. Click its toggle (`landing_suggested_sites_toggle`): on. Click Back (`settings_back`).
3. Click New Game (`menu_new_game`), Confirm Scenario (`btn_scenario_confirm`), the crew step's Generate (`btn_party_generate`), then Generate (`btn_wc_generate`): the globe, and the footer reads "Surveying landing sites..." (briefly, at the default resolution) until `landingSites` reads `surveying` false.
4. Read `landingSites`: 1 to 10 sites, both `river` and `lake` kinds if there are several; `selection.suggestedIndex` is 0 and `recommended` true; the Landing Zone pane shows Recommended.
5. Look at the globe: a small teal diamond (river) or circle (lake) at each site with `onScreen` true, none for the others, and the amber crosshair on site 0.
6. Click a shown marker at its `x`, `y`: `selection.suggestedIndex` is that index, and the pane shows Recommended and fresh water. Scroll the pane (`ev=move,x,y&ev=scroll,x,y,-3` at a point inside `landing_details`; a scroll goes where the pointer last moved, so move there first): Provisions shows Food and Wood, each Yes.
7. Click open land well away from every marker: the crosshair moves to the click, `suggestedIndex` is null, and `pickLatDeg`/`pickLonDeg` are where you clicked. A click on water leaves the selection and shows "Select a land tile to start the colony".
8. Drag the globe (`ev=drag,x0,y0,x1,y1`) until a site's `onScreen` reads false: its marker is gone, and its `x`, `y` read null.
9. Click a shown marker again, then Confirm Landing Site (`btn_wc_accept`) and Begin Descent (`btn_begin_descent`): the game loads.
10. Read `landing`: `feature` is the marker's kind, `river` or `lake`, `featureDistanceMeters` is between 27 and 33, and the water is in sight of the crew, who stand back from its bank on dry ground.
11. Exit, relaunch with `--no-settings`, and click Quick Start (`btn_enter_expedition`, then `menu_quick_start`): the world creator is not involved, `landing` reads `biome` TemperateDeciduousForest, `feature` `river`, and `landingLatDeg`, `landingLonDeg`, `spawnXMeters`, `spawnYMeters`, and `featureDistanceMeters` match the table in the spec's [section 8](../../technical/landing-sites.md#8-quick-starts-landing).
12. Relaunch with `--no-settings` and generate a planet as in step 3: no markers on the globe (`markersShown` false), and site 0 is still selected. A click at a site's `x`, `y` is an ordinary pick: `pickLatDeg`/`pickLonDeg` are the click's, not the site's.
