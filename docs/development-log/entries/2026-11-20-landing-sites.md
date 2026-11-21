# 2026-11-20 - Landing sites: suggested spots on the globe, and a landing beside its water

## Summary

Most of the globe is a bad place to start a colony, and the world creator said nothing about where the good places were. A pick could also promise a river that was kilometres from where the crew came down, because a planet tile is about 3.5 km across and the landing reveals 150 m.

Now there is one definition of a good starting spot (fresh water, food, wood, a livable temperature through the year), the world creator finds up to ten such spots per planet and can mark them on the globe, and a pick, marker or free click, lands 30 m from the area's river or lake when the area has one with clear ground beside it; a pick with no such anchor stays where it was made, with no feature. Epic WOR-1110 (Landing sites: suggested habitable spots and landing beside the area's water).

## Details

**One bar.** `assessLandingTile` (`libs/worldgen/worldgen/sampling/LandingSite.cpp`) judges a tile, and the suggested sites, the default site, the Landing Zone pane's rating, and its Recommended badge all read it. It replaced `rateHabitability`, which ignored biome, and `findDefaultLandingSite`, which carried its own hand list of green biomes.

**Food and wood from the flora data.** `assets::biomeProvisionsFromDefinitions` builds a per-biome table from the asset registry: every harvestable definition, every biome placement block, food when the yield item is edible, wood when the yield item declares the `wood` group. Nothing is a fixed list, since far more assets and biome configuration are coming. Wood was first matched by the item name `"Wood"`; that constant is gone and `Wood.xml` declares the group. The flora group `wood_sources` could not be the test because it also holds bushes that yield sticks. `worldgen` cannot link `assets`, so the caller builds the table and passes it in, the split `canopyCoverFromDefinitions` already uses. With the shipped assets only the six forest biomes, Temperate Grassland, and Beach have both food and wood; wood is what separates biomes, and food rules out almost nothing (prickly pear yields berries).

**A pick is a rough area.** Kevin's call: "when you're looking at the globe you're not picking a precise coordinate, just a rough area to land. the ship is crashing, you don't have control." `resolveLandingSite` takes the picked tile and its neighbours and tries anchors in order: the tile's own river centre, a point just inside the tile from a neighbouring lake, then neighbouring river tiles. The landing's origin moves to the first anchor whose spawn search finds a clear drop beside fresh water. The crosshair stays where the player clicked and the interface never shows the move; the pane describes the tile the crew really lands on.

**The drop is 30 m from the water.** The first build landed the crew on the bank (0.07 m from the channel on Quick Start). Kevin: "not so close, lets go 30m from the water". `findWatersideSpawn` (was `findRiverbankSpawn`) now takes the dry point nearest the origin that is 30 m, within 3, from the nearest water of any kind: river distance from the channel segments, lake and ocean distance from an exact Euclidean distance transform over the sample grid. Measuring to the nearest water of any kind keeps the crew off the strip between two channels. Stopwatch readings of the spawn search, taken on a loaded machine, did not get slower; they are not a measured gain.

**The finder.** `findSuggestedLandingSites` scans every tile once, keeps the best candidates per (coarse bin, kind, biome) slot, then selects in rounds: at least 15 degrees apart, biomes spread, at most seven of one kind, each verified by resolving it. Every planet tried gave ten sites, seven river and three lake, across seven or eight biomes.

**Off the main thread.** `LandingSiteSurvey` owns one worker per planet. It runs the scan and resolves the player's picks, newest first. Quick Start's site choice runs there too; a stopwatch on a loaded machine read it no slower than the blocking call it replaced, which is not a measured gain.

**Markers behind a setting.** "Show suggested landing sites" in a new Landing group in Settings, off by default (`landing.showSuggestedSites`). On, the world creator draws a diamond for a river site and a circle for a lake site; a click selects the site. Markers only, no list.

**A pick is a click.** A press holds the globe still; it turns only once the pointer has travelled past salal's `ui::kDragThreshold`, the same travel past which the ui stops calling the press a click, so a click picks what was under the press on an unturned globe and a drag picks nothing. Picking on the press itself had made every drag a pick; picking on the ui's click while the globe turned from the first pixel had let a pixel of jitter move the pick a couple of tiles and slide the globe on afterwards. A press on the viz dock is the dock's, and a release outside the stage picks nothing.

**Quick Start.** Its origin is unchanged to the last digit (28.80951735938833, 24.502251073059021), checked against a recorded baseline after every slice, so terrain and flora coordinates hold. Its drop moved once, on purpose, with the 30 m rule: from (4, 4) to (-29, -17). The nearest point 30 m clear of all water is on the west bank (a feeder joins on the east side), so the crew now starts across the main channel from the old start. Scenario 14 had its points re-derived on the crew's side. Scenarios 03 and 04 teleport the crew to the east bank in setup; 11 gained a clearing step; 17's steps for the eye button and Escape were rewritten; 21's precondition was reworded. 10, 15, 16, and 20 ran as written. The steps of 03 and 20 that only a person can judge were not run.

**Fixed before the PR.**
- A landing could still come down on a narrow strip: the spawn search's narrow-land fallback was accepted as a fresh-water site. The search now reports whether the drop is clear and the resolution accepts only clear drops.
- The world creator and the resolver each had a water-tile test and they disagreed on land-flagged tiles below sea level. There is one now, `worldgen::isWaterTile`.
- The lake anchor was first placed at the two tile centres' midpoint, which is on the shared edge only at fine grids (13 m off at n=256, 209 m at n=64). It finds the edge by bisection on the grid's own lookup.
- Far-side markers and the crosshair drew through the globe; `projectLatLon` now answers false on the far side.
- A test that generates a planet sat in the fast bucket; it is in a Heavy suite.

**Things learned.**
- The globe turns on its own until first touched (a press, scroll, right-click, or key) (`OrbitCamera`, 0.05 rad/s after 3 s idle), which is why injected clicks seemed to land in different places. Scenario 27 says how to still it.
- An injected scroll goes to the last pointer position, not its own x,y (salal). Send a move first. Filed as SAL-184.
- The provisions measure counts placement tries: clumps now count their plants, spaced trees are still counted before grove scale and spacing, so wood densities overstate standing trees. Good enough for a presence floor, not for comparing biomes.

**Files.** New: `libs/assets/assets/placement/BiomeProvisions.*`, `apps/world-sim/scenes/shared/LandingSiteSurvey.*`, `libs/worldgen/worldgen/sampling/LandingSite.test.cpp`, `RiverWorld.test.h`, `libs/sim/ecs/systems/QuickStartLanding.bench.*`. Changed most: `LandingSite.*`, `SpawnSite.*`, `WorldCreatorScene.cpp`, `GameLoadingScene.cpp`, `LandingSiteDetailsModel.*`, `UserSettings.*`, `SettingsScene.cpp`.

## Related Documentation

- [Landing sites](../../technical/landing-sites.md): the spec
- [Scenario 27: Landing sites](../../testing/scenarios/27-landing-sites.md)
- [Landing site screen](../../design/ui/screens/landing-site.md)

## Next Steps

- WOR-1111 (Colonists drink from the ocean: shore points carry no fresh or salt distinction): the pane calls a coast salt while colonists drink from it.
- WOR-369 (Landing site local preview + difficulty rating): the 2D preview of a site is still open; the rating half is done here.
- The Settings list now scrolls at 1920x1080 at the default interface size; tightening it is a layout decision.
- The Landing Zone pane's right-aligned values end under its scrollbar thumb, and its Terrain row prints the raw enum name.
