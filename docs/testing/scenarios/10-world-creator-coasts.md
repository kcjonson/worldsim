# Scenario 10: World creator coasts

**Status:** ready  
**Last verified:** 2026-11-06

New Game generates a random planet, and its coasts read right from orbit. Quick Start's planet is pre-generated, so it doesn't exercise worldgen changes; use this.

---

## Steps

Launch `world-sim` (RelWithDebInfo); the dev server is on 127.0.0.1:8081. `/api/state?what=scene` reads `state` (1 generating, 2 ready) and `globe`.

1. Click Enter Expedition (`btn_enter_expedition`): the main menu.
2. Click New Game (`menu_new_game`), then Confirm Scenario (`btn_scenario_confirm`), then the crew step's Generate (`btn_party_generate`): Generate Planet, step 03 / 03, Earth-Like selected, a random seed filled in.
3. Click Generate (`btn_wc_generate`): a progress strip, then `state` 2 and `globe` true; the Terrain tab shows the globe and the footer reads World ready.
4. Zoom onto a coast (pointer `move` over the globe first, then `scroll`): land, then a light-blue shallow band, then deep blue. No thin green ring in the water outside the band, and no hexagonal outlines on coasts or inland seas.
5. Click Advanced (`advanced_toggle`), pick Default 1024 in the resolution select, then Random (`btn_randomize`) and Regenerate (`btn_wc_regenerate`): a new planet; step 4 holds on it too.
