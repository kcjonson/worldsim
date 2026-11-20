# Scenario 22: Landing sites

**Status:** draft  
**Last verified:** never

The world creator suggests good landing sites as markers on the globe, and a landing comes down beside the river or lake that made the spot good. Spec: [landing-sites.md](../../technical/landing-sites.md).

---

## Steps

Launch `world-sim --background --no-settings` (RelWithDebInfo); the dev server is on 127.0.0.1:8081. `/api/state?what=landingSites` reads the suggested sites (marker `x`, `y` in input space) and the selection; `/api/state?what=landing` reads the landing once in game.

1. Click Enter Expedition (`btn_enter_expedition`), then Settings (`menu_settings`): a Landing group with "Show suggested landing sites", off.
2. Click its toggle (`landing_suggested_sites_toggle`): on. Click Back (`settings_back`).
3. Click New Game (`menu_new_game`), Confirm Scenario (`btn_scenario_confirm`), the crew step's Generate (`btn_party_generate`), then Generate (`btn_wc_generate`): the globe, and the footer reads "Surveying landing sites..." until `landingSites` reads `surveying` false.
4. Read `landingSites`: 1 to 10 sites, both `river` and `lake` kinds if there are several; `selection.suggestedIndex` is 0 and `recommended` true; the Landing Zone pane shows Recommended.
5. Look at the globe: a small teal diamond (river) or circle (lake) at each site with `onScreen` true, none for the others, and the amber crosshair on site 0.
6. Click a shown marker at its `x`, `y`: `selection.suggestedIndex` is that index, and the pane shows Recommended and fresh water.
7. Click open land well away from every marker: the crosshair moves to the click, `suggestedIndex` is null, and `pickLatDeg`/`pickLonDeg` are where you clicked.
8. Drag the globe until a site's `onScreen` reads false: its marker is gone.
9. Click a shown marker again, then Confirm Landing Site (`btn_wc_accept`) and Begin Descent (`btn_begin_descent`): the game loads.
10. Read `landing`: `feature` is `river` or `lake`, `featureDistanceMeters` is between 27 and 33, and the water is in sight of the crew, who stand back from its bank on dry ground.
11. Exit, relaunch with `--no-settings`, and start Quick Start: the world creator is not involved, `landing` reads `biome` TemperateDeciduousForest, `feature` `river`, and `landingLatDeg`, `landingLonDeg`, `spawnXMeters`, `spawnYMeters`, and `featureDistanceMeters` match the table in the spec's [section 8](../../technical/landing-sites.md#8-quick-starts-landing).
12. Relaunch with `--no-settings` and generate a planet as in step 3: no markers on the globe (`markersShown` false), and site 0 is still selected.
