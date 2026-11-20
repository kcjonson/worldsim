# Landing sites

**Created:** 2026-11-20
**Companions:** [3D to 2D Sampling](./3d-to-2d-sampling.md) (the landing projection), [Player Visibility](./player-visibility.md) (the landing reveal), [Landing site screen](../design/ui/screens/landing-site.md), [World generation UX](../design/features/world-generation/user-experience.md#landing-site-selection)

What makes a good place to start a colony, how the game finds about ten of them per planet and shows them on the world creator's globe, and how a pick on the globe (a rough area, never a coordinate) becomes the point the crew lands on.

Terms: a **pick** is the lat/lon the player clicked or the marker they chose. The **landing** is the resolved point: the 2D world's origin (`landingLatDeg`/`landingLonDeg` everywhere downstream) plus the **drop**, the spawn offset from it that `PlayerVisibilitySystem::revealLanding` centres on. A **feature** is the fresh water the landing is beside, a river or a lake. **Fresh water** is a river or a lake; a coast is salt and never counts, and ponds are not counted.

---

## 1. Rules

1. One definition of a good starting spot: fresh water, food, wood, and a livable temperature. The suggested sites, the default site, the Landing Zone pane's rating, and its Recommended badge all read it (`assessLandingTile`). There is no second scorer.
2. Food and wood come from the flora asset data. A biome has food when edible-yield flora places in it densely enough, and wood when `Wood`-yield flora does. No hand list of biomes exists anywhere.
3. Temperature reads both the mean and the seasonal half-amplitude: the mean, the coldest season, and the hottest season must each be livable.
4. A site is called fresh-water, rated as such, recommended, or suggested only when the resolution found a fresh bank or shore for it. Tile flags alone never promise water.
5. Every pick is resolved, suggested marker or free click, by the same function (`resolveLandingSite`). The interface never shows the adjustment: the player's crosshair stays where they clicked, the coordinates shown are the pick's, and nothing snaps or moves the pointer.
6. When the resolution finds fresh water, the feature lies inside the area revealed at landing (150 m round the drop). The drop is a bank or shore point, at most 6 m from the water's edge.
7. The Landing Zone pane describes the landing tile, which can be a neighbour of the picked tile, so its biome, climate, and water are where the crew really lands.
8. Up to ten suggested sites per planet, all meeting rule 1, at least 15 degrees of arc apart, varied by kind (river or lake) and by biome. Fewer is fine; none is fine and breaks nothing.
9. The same planet always gives the same sites in the same order. Ties break to the lowest `TileId`; nothing is random.
10. The default site is the first suggested site. Quick Start takes the first site of the same finder with its biome preference; the world creator takes the first without one.
11. Suggested sites are markers on the globe in the Reviewing state, nothing else: no list, no panel entries. A marker shows its kind at a glance. Clicking one selects it exactly as a click there would. A marker on the far side of the globe is hidden.
12. Markers show only when the Settings switch "Show suggested landing sites" is on. It is off by default, and a `--no-settings` launch has it off.
13. Sites are computed after generation or load and never stored. The planet file format does not change.
14. Quick Start's landing does not move: the same origin and the same drop as before this system, on the shipped planet (seed 424242, n=2048, Temperate Deciduous Forest by a river). The regression scenarios are written on those coordinates.
15. `revealLanding` stays centred on the raw drop, the spawn point before the landing clearing is made.
16. Neither the scan nor a resolution runs on the main thread. A spawn search costs 50 to 160 ms, so the world creator hands both to a worker and stays interactive.

## 2. The bar

### 2.1 Biome provisions

```cpp
// libs/worldgen/worldgen/sampling/LandingSite.h
struct BiomeProvisions {
    float foodSourcesPerSquareKilometer{};
    float woodSourcesPerSquareKilometer{};
};
using BiomeProvisionTable = std::array<BiomeProvisions, static_cast<std::size_t>(Biome::Count)>;

// libs/assets/assets/placement/BiomeProvisions.h
[[nodiscard]] worldgen::BiomeProvisionTable biomeProvisionsFromDefinitions(const AssetRegistry& registry);
```

`worldgen` cannot link `assets`, so the table is built by the caller and passed in, the same split as `canopyCoverFromDefinitions` (`CanopyCover.h`). For every definition with a `placement` and a `harvestable` capability, and every biome `b`:

- `const BiomePlacement* placement = definition.placement.findBiome(worldgen::biomeToString(b))`. Skip it when null, and when `placement->nearTileType` is set (a `near="Mud"` entry): the planet can't see mud, so a conditional placement is not counted.
- Its density is `spawnChance / (samplePitch * samplePitch) * 1e6` sources per square kilometer, the measure `canopyCoverFromDefinitions` uses.
- The density adds to food when the yield item (`registry.getDefinition(harvestable.yieldDefName)`) `isEdible()`, and to wood when the yield item's definition declares the group `kWoodItemGroup` (`"wood"`) in its XML. Both answers come from the item's own definition, never from its name, so a new edible item or a second kind of timber counts by declaring itself. (The flora group `wood_sources` is not the test: it also holds bushes that yield sticks, which can't be built with.)

Asset definitions are loaded before either caller runs: the splash scene gates the main menu on `AssetRegistry::isLoadComplete()`, and a `--scene` launch loads them synchronously before the first scene. The world creator builds the table in `onEnter`; `GameLoadingScene` builds it when it chooses Quick Start's site. Building it is a loop over the definitions, so neither caches it.

### 2.2 Assessing a tile

```cpp
struct TileAssessment {
    WaterClass   water{WaterClass::RainFed}; // verified: River or Lake only when the resolution found it
    bool         hasFood{};
    bool         hasWood{};
    bool         livableTemperature{};
    Habitability habitability{Habitability::Harsh};

    [[nodiscard]] bool meetsBar() const { return isFreshwater(water) && hasFood && hasWood && livableTemperature; }
};

[[nodiscard]] TileAssessment assessLandingTile(const GeneratedWorld& world, const BiomeProvisionTable& provisions,
                                               TileId tile, WaterClass verifiedWater);
```

- **Food, wood:** the tile biome's densities against `kMinimumFoodSourcesPerSquareKilometer` and `kMinimumWoodSourcesPerSquareKilometer`.
- **Temperature:** with mean `m` and half-amplitude `r` in degrees Celsius (the stored tenths divided by 10), livable is `kLivableMeanMinimumCelsius <= m <= kLivableMeanMaximumCelsius`, `m - r >= kLivableColdestSeasonCelsius`, and `m + r <= kLivableHottestSeasonCelsius`. A missing field fails the test.
- **Habitability** is a sum, bucketed as before (4 or more Easy, 2 or more Moderate, 0 or more Hard, below 0 Harsh):
  - water: fresh +3, coastal +1, rain-fed 0;
  - rainfall: at least `kAmpleRainfallMillimeters` +2, at least `kModerateRainfallMillimeters` +1, below `kDesertRainfallMillimeters` on a rain-fed tile -1;
  - temperature: livable 0; mean outside the brutal band, or coldest season below `kBrutalColdestSeasonCelsius`, or hottest above `kBrutalHottestSeasonCelsius`, -2; otherwise -1;
  - no food -1, no wood -1.

A tile that meets the bar is Easy (rainfall at least 250 mm a year) or Moderate. The pane's skulls come from `habitability`, and its Recommended badge from `LandingSite::recommended()` (2.3).

`classifyWater(world, tile)` stays as the tile-level screen: it says where to look for fresh water (the tile or a neighbour carries `kFlagRiver` or `kFlagLake`). Only the resolution turns that into a verified `WaterClass`.

### 2.3 The landing site

```cpp
enum class FreshWaterFeature : uint8_t { None, River, Lake };

struct LandingSite {
    LatLon            origin;         // the 2D world origin
    TileId            tile{kInvalidTile}; // tile containing origin: what the pane describes
    FreshWaterFeature feature{FreshWaterFeature::None};
    SpawnSite         spawn;          // the drop, meters from origin
    TileAssessment    assessment;

    [[nodiscard]] bool recommended() const { return feature != FreshWaterFeature::None && assessment.meetsBar(); }
};
```

`SpawnSite` (`SpawnSite.h`) carries `SpawnWater water` (`None`, `River`, `Lake`, `Ocean`) and `float waterDistanceMeters` in place of its two booleans. `findRiverbankSpawn` keeps its search unchanged (200 m radius, 3 m step, 6 m bank distance, riverbank before lake shore before coast, nearest to the origin); only what it reports grows. `waterDistanceMeters` is the chosen point's distance to the channel edge for a river, and for a lake or the ocean the shortest of 1 m probes out to the bank distance along the four axes that lands in water.

## 3. Resolving a pick

```cpp
[[nodiscard]] LandingSite resolveLandingSite(const std::shared_ptr<const GeneratedWorld>& world,
                                             const BiomeProvisionTable& provisions, LatLon pick);
```

A planet tile is about 3.5 km across at n=2048, and `classifyWater` looks at a tile and its six neighbours, so the area a pick names is the picked tile `T = grid.fromLatLon(pick)` and its neighbours. The resolution tries anchors in this order and takes the first whose spawn search finds fresh water:

1. `T` is a land river tile: its centre.
2. `T` borders lake tiles: a lake anchor toward each lake neighbour, nearest neighbour centre to the pick first.
3. `T`'s land river neighbours: each one's centre, nearest to the pick first.

Ties break to the lowest `TileId`. For each anchor, `findRiverbankSpawn(world, anchor)` runs with the anchor as origin; the anchor is accepted when `spawn.water` is `River` or `Lake`, and the site is `{anchor, fromLatLon(anchor), feature, spawn, assessLandingTile(..., River or Lake)}`. When no anchor passes, the landing stays at the pick: origin is the pick, the spawn search runs there, `feature` is `None`, and the water class is `Coastal` when `T` has an ocean neighbour, otherwise `RainFed`. A water pick resolves the same way with no anchors; the world creator rejects water picks before resolving.

Why the anchors land on water:

- **River.** `RiverNetwork2D` joins its channel segments at tile centres and tapers the meander and the width variation to zero there, so a river tile's centre lies exactly on its channel's centreline. A landing origin there puts the channel through the 2D origin, and the bank is within half the channel width (at most `RiverNetwork2D::kMaxHalfWidthMeters`, 103.5 m) plus 6 m, inside the 200 m search. A river tile with no segment of its own and none arriving (an isolated flag) fails the search and the next anchor is tried.
- **Lake.** `PlanetSampler` decides water by the containing tile alone (no blend), and the chunk layer draws a tile's surface from its primary biome, the containing tile's (weight at least 0.5). So the 2D lake shore is the Voronoi edge between a lake tile and a land tile, moved at most `TerrainPolygonBuilder::kMaxWarpMm` (1.45 m) by the shore warp, plus the 1 m tile grid. The grid decides the containing tile in its lattice chart, so that edge is not the centres' great-circle midpoint: the two are 0.2 m apart at n=2048 and more than a kilometer apart at n=24. The lake anchor therefore finds the edge itself. On the arc from `T`'s centre to the lake tile's centre, a bisection on `grid.fromUnitVector` finds the point where the containing tile stops being `T`, to within `kLakeEdgeToleranceMeters`; the anchor is `kLakeShoreInsetMeters` back from it toward `T`'s centre. It is on land, inside `T`, at any grid resolution, and the shore search finds the water about 20 m off.

The origin moves to the anchor, so every `RiverNetwork2D`, `PlanetSampler`, and `GeneratedWorldSampler` downstream is built at the anchor, and the anchor's tile is the landing tile.

**Idempotent.** Resolving a landing's origin gives the same landing: a river anchor is its own tile's centre (step 1 on that tile), and a lake anchor lies inside `T`, nearest to the lake tile it was built toward. Anchors are computed from tile centres, never from the pick, so the float lat/lon the globe holds loses nothing. This is what lets a marker click (a pick at a site's origin) select that site exactly.

## 4. Suggested sites

```cpp
struct SuggestedSiteQuery {
    std::optional<Biome> preferredBiome;
    std::size_t          maximumSites{kSuggestedSiteCount};
    std::stop_token      stopToken;
};

[[nodiscard]] std::vector<LandingSite> findSuggestedLandingSites(const std::shared_ptr<const GeneratedWorld>& world,
                                                                 const BiomeProvisionTable& provisions,
                                                                 const SuggestedSiteQuery& query);
```

### 4.1 Candidates

A candidate is a tile whose own resolution anchors on itself, so the scanned tile is the landing tile: a land river tile (kind River), or a land tile bordering a lake tile (kind Lake). It must also pass the rest of the bar on its own data (food, wood, temperature), and its centre must lie within `kMaximumSuggestedLatitudeDegrees` of the equator. The latitude cut belongs to the finder, not the bar: a free pick nearer a pole that meets the bar is still Recommended. One pass over every tile.

Candidates are kept per slot, a slot being (bin, kind, biome). The bin is a tile of a coarse `SphereGrid(kSiteBinSubdivision)` (642 bins, about 8 degrees across), found from the candidate's centre. Each slot keeps its best `kCandidatesPerSlot` by rank. That bounds memory on a lush planet and keeps candidates everywhere for the spacing and variety rules; the keep is order-independent, so the scan may split across workers without changing the result. The global best candidate is always kept.

**Rank**, best first: preferred biome (when the query has one), then kind (River before Lake), then habitability (Easy before Moderate), then the lowest `TileId`.

### 4.2 Selection

Candidates are sorted by rank. Selection runs in rounds `r = 1, 2, ...` up to `kSuggestedSiteCount`, then one final pass. A candidate is taken when all hold:

1. it is at least `kMinimumSiteSeparationDegrees` of arc from every taken site's tile centre;
2. fewer than `r` taken sites share its biome (no limit in the final pass);
3. fewer than `kMaximumSitesOfOneKind` taken sites share its kind (no limit in the final pass);
4. `resolveLandingSite` at its centre returns a site that is `recommended()` and whose `tile` is the candidate.

Check 4 runs last and at most once per candidate (a failure is remembered). A site's kind is its resolved `feature`: a lake candidate with a riverbank inside the spawn search resolves as a river site (the search prefers a riverbank), is counted and marked as one, and is skipped when the river count is already at its limit. Selection stops at `query.maximumSites`. The first site taken is the best-ranked candidate whose resolution passes, so `sites[0]` is the default site. The scan checks `stopToken` every 65,536 tiles and before each resolution, and returns what it has when stopped.

### 4.3 Where it runs

`LandingSiteSurvey` (`apps/world-sim/scenes/shared/LandingSiteSurvey.h`) owns one `std::jthread` for a planet. It runs `findSuggestedLandingSites` and hands that result back once, and it resolves picks: the newest pick waiting is resolved next (ahead of the rest of the scan's verification step when both wait), and a pick replaced before it was resolved is dropped.

```cpp
class LandingSiteSurvey {
  public:
    void start(std::shared_ptr<const worldgen::GeneratedWorld> world, worldgen::BiomeProvisionTable provisions,
               std::optional<worldgen::Biome> preferredBiome, std::size_t maximumSites);
    [[nodiscard]] bool surveying() const;
    [[nodiscard]] std::optional<std::vector<worldgen::LandingSite>> takeSites();

    void resolve(worldgen::LatLon pick); // replaces any pick not yet resolved
    [[nodiscard]] bool resolving() const;
    struct ResolvedPick {
        worldgen::LatLon      pick;
        worldgen::LandingSite landing;
    };
    [[nodiscard]] std::optional<ResolvedPick> takeResolved();

    void cancel(); // requests stop and joins
};
```

- **World creator.** Started when Reviewing begins. While it runs the globe is shown and usable, the footer status reads "Surveying landing sites...", no site is selected, and Confirm Landing Site is disabled until one is. The player may click the globe meanwhile; a click resolves and selects as usual. When the survey finishes, if the player has picked nothing, `sites[0]`'s origin becomes the pick. The status then reads "World ready - suitable for colonization", or "World ready - no recommended landing sites" when the list is empty (no site is then selected until the player clicks). Regenerate, Escape, and `onExit` cancel it.
- **Quick Start.** `GameLoadingScene` gains a `ChoosingSite` phase after `PreparingPlanet`, status "Choosing a landing site...", which runs the survey with `preferredBiome = TemperateDeciduousForest` and `maximumSites = 1` and takes `sites[0]`. An empty list fails the load with that message, as a missing planet does (the shipped planet is a packaging fact, not a player state).
- **A click** in the world creator moves the crosshair and the coordinates at once and calls `resolve(pick)`. Until `takeResolved` returns that pick, the pane keeps the coordinates and shows "Surveying..." in place of the rest, and Confirm Landing Site is disabled. A click on a suggested marker needs no resolution: its `LandingSite` is already in `suggestedSites`.

## 5. The world creator

- **State.** `selectedSite` gives way to `planetview::LatLon selectedPick` (where the crosshair is), `std::optional<worldgen::LandingSite> selectedLanding` (empty while its resolution is pending), and `std::optional<std::size_t> selectedSuggestion`. `suggestedSites` holds the survey's result.
- **Picking.** `stagePress` first hit-tests the shown suggested markers (`kSuggestedMarkerHitRadius` ui px from the press, nearest wins); a hit makes that site's origin the pick. Otherwise `globe.pick` gives it. A marker hit sets `selectedPick` to the site's origin, `selectedLanding` to the site, and `selectedSuggestion` to its index. A globe pick goes to `trySelectSite(pick)`, which rejects water tiles as now, sets `selectedPick`, clears `selectedLanding` and `selectedSuggestion`, and calls `survey.resolve(pick)`; when the result for that pick arrives it becomes `selectedLanding`, and `selectedSuggestion` is the index of the site whose `origin` equals its origin, if any.
- **Pane.** `buildLandingSiteDetails(const worldgen::GeneratedWorld&, const worldgen::LandingSite&, double pickLatDeg, double pickLonDeg)`. Coordinates are the pick's; biome, temperatures, rainfall, terrain, ice, and water come from `site.tile` and `site.assessment`; `recommended` is `site.recommended()`. A Provisions section shows Food and Wood, each Yes or None. The Commit to Descent dialog reads the same `details`.
- **Landing.** `land()` puts `selectedLanding` in `GameStartConfig::landing`.
- **Markers.** `renderSuggestedSites(rect, scale, theme)` draws before `renderMarker`, so the selection crosshair sits on top. For each site with `projectLatLon` true: a ring of radius `kSuggestedMarkerRadius` ui px, 1.5 px stroke, in the `data` token, with a 2 px dot at its centre in `dataBright`. A river site's ring is a diamond (a square turned 45 degrees), a lake site's a circle. No pulse; the pulse belongs to the selection. Drawn only while `UserSettings::Get().landing.showSuggestedSites` is on, read each frame.

## 6. The setting

- `struct LandingSettings { bool showSuggestedSites = false; bool operator==(const LandingSettings&) const = default; };` in `UserSettings.h`, held as `UserSettings::landing`. Its default is this constant, not construction XML.
- JSON key `landing.showSuggestedSites`, in a `kLandingSwitches` table beside `kSwitches`, read by `load` (after resetting `landing` to `LandingSettings{}`) and written by `store`.
- `SettingsScene` adds a `// LANDING` group after `// INTERFACE`, with one switch: id `landing_suggested_sites` (its toggle `landing_suggested_sites_toggle`), label "Show suggested landing sites", line "Mark good places to start a colony on the globe: fresh water, food, wood, and a livable climate." `saveDrawingSettings` is renamed `saveSettings`.

## 7. Handing the landing on

- `GameStartConfig::landing` (`worldgen::LandingSite`) replaces `landingLatDeg` and `landingLonDeg`.
- `GameLoadingScene::initializeWorldSystems` builds the sampler and chunk manager at `landing.origin` and takes the drop from `landing.spawn`; it no longer calls `findRiverbankSpawn`.
- `GameWorldState::landing` replaces `landingLatDeg`, `landingLonDeg`, and `spawnPosition`. `GameScene` reads its origin (minimap context) and its drop (`prepareLandingClearing`, `revealLanding`) from it; `revealLanding` stays centred on that raw drop.
- `DevCommandHandler`'s context carries `const worldgen::LandingSite* landing` in place of the two lat/lon fields.

### 7.1 Deleted

`findDefaultLandingSite`, `isPreferredLandingBiome`, `waterScore`, `habitabilityScore`, and the public `rateHabitability` (folded into `assessLandingTile`) in `LandingSite.cpp`; `SpawnSite::nearWater` and `SpawnSite::freshWater`; the pane's own recommended rule (`isFreshwater && (Easy || Moderate)`); the `findRiverbankSpawn` calls in `GameLoadingScene` and the two benches (`NavScale.bench.cpp`, `NavigationSystem.bench.cpp`), which take `findSuggestedLandingSites(...)[0]` and its `spawn`, loading asset definitions first; the `LandingSite.*` tests of `findDefaultLandingSite` in `PlanetSampler.test.cpp`. `habitabilityWeight` in `PlanetGenerator.cpp` and the saved `WorldSummary::habitability` are a planet-wide summary, not a site rating, and are untouched.

## 8. Quick Start holds still

The old default was the lowest `TileId` among land tiles within 45 degrees of the equator, in Temperate Deciduous Forest, with `kFlagRiver` on the tile, rated Easy. The new first site with that preference is the lowest `TileId` among such river tiles that meet the bar and rate Easy. Every term of the new set implies the old one (the latitude cut is the same, a livable mean of 0 to 28 degrees Celsius with at least 250 mm of rain is what the old rule rated Easy for a river tile, and Temperate Deciduous Forest has food and wood), so the new set is a subset of the old and the old winner stays first if it is in it. It is in it when its seasons are livable and its spawn search finds the river, which it does today. Its resolution anchors on its own centre, the old site exactly, and the spawn search is unchanged, so the drop is the same.

The livable limits give way to the site, never the reverse: the Quick Start site is known good from play, so limits that would exclude it are wrong and are the thing to change.

Held by check, not by trust: the `landing` view's `landingLatDeg`, `landingLonDeg`, `spawnXMeters`, and `spawnYMeters` on Quick Start are recorded before the scorer changes and compared after; any difference stops the change. Scenario 14's setup (a BigRock at (0.4, 48.3), 48 m south of the drop) is the human check of the same thing.

## 9. Dev views

**`landing`** (game scene, `/api/state?what=landing`), extended:

```json
{"landingLatDeg":0.0,"landingLonDeg":0.0,"biome":"TemperateDeciduousForest","waterClass":"River","riverInTile":true,
 "feature":"river","spawnXMeters":0.0,"spawnYMeters":0.0,"featureDistanceMeters":0.0}
```

`landingLatDeg`/`landingLonDeg` are the origin, `biome` and `waterClass` the landing tile's (the verified class), `feature` is `river`, `lake`, or `none`, `spawnXMeters`/`spawnYMeters` the raw drop, and `featureDistanceMeters` the drop's `waterDistanceMeters` (null when `feature` is `none`).

**`landingSites`** (world creator, registered with `devServer().stateProvider` in `onEnter`, released in `onExit`):

```json
{"surveying":false,"markersShown":true,
 "sites":[{"index":0,"latDeg":0.0,"lonDeg":0.0,"kind":"river","biome":"TemperateDeciduousForest","onScreen":true,"x":0.0,"y":0.0}],
 "selection":{"pickLatDeg":0.0,"pickLonDeg":0.0,"suggestedIndex":0,"landingLatDeg":0.0,"landingLonDeg":0.0,
              "biome":"TemperateDeciduousForest","feature":"river","recommended":true}}
```

`sites` lists every suggested site whether or not markers are shown. `x`/`y` are the marker's position in the window's logical px (the input space, as `renderMarker` draws), and `onScreen` is true when the site is on the near side and inside the stage. `selection` is null until a site is selected; `suggestedIndex` is null for a free pick.

## 10. Constants

In `LandingSite.h` unless named otherwise.

| Constant | Value | Meaning |
|---|---|---|
| `kSuggestedSiteCount` | 10 | most suggested sites per planet |
| `kMinimumSiteSeparationDegrees` | 15.0 | least great-circle arc between two sites' tile centres (1,668 km on an Earth-sized planet) |
| `kMaximumSitesOfOneKind` | 7 | most river (or lake) sites before the final pass |
| `kSiteBinSubdivision` | 8 | coarse grid for keeping candidates (642 bins) |
| `kCandidatesPerSlot` | 2 | candidates kept per (bin, kind, biome) |
| `kMaximumSuggestedLatitudeDegrees` | 45.0 | farthest a suggested site's tile centre lies from the equator |
| `kLivableMeanMinimumCelsius` | 0.0 | coldest livable annual mean |
| `kLivableMeanMaximumCelsius` | 28.0 | hottest livable annual mean |
| `kLivableColdestSeasonCelsius` | -15.0 | lowest livable mean minus half-amplitude |
| `kLivableHottestSeasonCelsius` | 38.0 | highest livable mean plus half-amplitude |
| `kBrutalMeanMinimumCelsius`, `kBrutalMeanMaximumCelsius` | -10.0, 35.0 | the mean band outside which the rating takes -2 |
| `kBrutalColdestSeasonCelsius`, `kBrutalHottestSeasonCelsius` | -30.0, 45.0 | season extremes past which the rating takes -2 |
| `kAmpleRainfallMillimeters`, `kModerateRainfallMillimeters`, `kDesertRainfallMillimeters` | 600, 250, 100 | yearly rainfall steps of the rating |
| `kMinimumFoodSourcesPerSquareKilometer` | 100.0 | food: about 7 sources in the 150 m landing area |
| `kMinimumWoodSourcesPerSquareKilometer` | 100.0 | wood: the same |
| `kLakeShoreInsetMeters` | 20.0 | lake anchor's distance inside the land tile from the lake edge |
| `kLakeEdgeToleranceMeters` | 0.5 | how closely the bisection finds the edge between a land tile and a lake tile |
| `kWoodItemGroup` (`BiomeProvisions.h`) | `"wood"` | the group an item declares to count as wood |
| `kSuggestedMarkerRadius` (`WorldCreatorScene.cpp`) | 6 ui px | marker size |
| `kSuggestedMarkerHitRadius` (`WorldCreatorScene.cpp`) | 10 ui px | how near a press must be to take a marker |

The landing reveal radius is `NavigationSystem::kLandingSightMeters` (150 m); `findRiverbankSpawn`'s 6 m bank distance keeps every fresh feature well inside it.

## 11. Tests

Unit tests, in `libs/worldgen/worldgen/sampling/LandingSite.test.cpp` (the landing tests move out of `PlanetSampler.test.cpp`; its `classifyWater` tests stay), on the synthetic fixtures the sampling tests already build unless stated:

- **Bar.** A river tile passes with food, wood, and livable temperatures, and fails with each one taken away in turn (no wood; no food; a mild mean with a coldest season below -15 degrees Celsius). A tile past 45 degrees of latitude that meets the bar is recommended when picked and is never a suggested site.
- **Provisions.** `biomeProvisionsFromDefinitions` on registered test definitions: an edible-yield bush counts as food in its biome, a `Wood` tree as wood, a `near="Mud"` entry not at all (in `assets-tests`).
- **Finder.** Every site meets the bar; sites are at least the separation apart; on a fixture with river and lake candidates in two biomes, both kinds and both biomes appear; two runs give identical lists; an all-desert world and an all-ocean world give an empty list.
- **Resolution.** A pick on a river tile resolves to that tile's centre; a pick beside a lake to a shore within the bank distance; a pick one tile from a river to the river tile, with `tile` the river tile; resolving a landing's origin returns the same landing; a dry pick resolves to itself with `feature` `None`.
- **Generated planet.** On a small generated planet (n=64, as `WorldStats.test.cpp` generates), every suggested site's drop has fresh water within `kLandingSightMeters`, checked with `RiverNetwork2D` and `PlanetSampler` at the site's origin.

Happy path: [scenario 22](../testing/scenarios/22-landing-sites.md).

---

## Decisions

1. The bar is fresh water, food, wood, and livable temperature, one definition for everything that judges a site; it replaced a scorer that ignored biome and a hand list of green biomes. (Kevin.)
2. Food and wood are derived from flora placement and yields so a new plant changes the answer without code. Conditional (`near`) placements don't count because the planet can't see mud. Nothing is a fixed list: far more assets and biome configuration are coming (Kevin), so an item says it is food or wood in its own XML and no item name appears in code.
3. Ponds are not rated water: the planet-level code can't see them. Coast is salt and never counts.
4. Suggested sites keep a 45 degree latitude cut on top of the bar. It holds Quick Start's landing (section 8). It is the finder's filter, not part of the bar, so the pane's Recommended badge means the four things in rule 1 and nothing about latitude.
5. A globe pick names a rough area: "the ship is crashing, you don't have control. so the exact coordinates can get moved." The game resolves the landing for every pick and never shows it. (Kevin.)
6. The landing origin moves to the anchor rather than the drop moving kilometres from the origin. The projection is exact at its origin, and every 2D consumer keys off the origin already.
7. A site is fresh-water only when the spawn search found the water. Tile flags promise a river on the tile or a neighbour, which can be kilometres off.
8. A pick resolves on the worker, not inside the click: a spawn search measured 52 to 159 ms, a visible hitch on every click. The search itself is left alone because Quick Start's drop depends on it point for point.
9. The lake anchor finds the tile edge by bisection on the grid's own lookup. The centres' midpoint is on the edge only at fine resolutions (13 m off at n=256, 209 m at n=64).
10. Candidates are kept per (bin, kind, biome) slot instead of in one sorted list, so memory is bounded and spacing and variety have candidates everywhere.
11. The default site is the first suggested site: one finder, no second scorer.
12. Markers sit behind a Settings switch, off by default. (Kevin.)
13. Sites are computed, never stored: a format change forces a re-bake of the 1.3 GiB shared planet.
14. RimWorld's world map gives a tile's terrain, temperature, and growing period in a side panel and has a "select random site" button; it has no suggested sites. Dwarf Fortress's embark screen has a site finder that searches by criteria (temperature, water, trees, and so on) and highlights matches on the map.
15. Out of scope here: the 2D local preview and difficulty rating of a site (Specboard WOR-369, "Landing site local preview + difficulty rating"), the loading screen backdrop (WOR-378, "Improve Landing Screen"), ponds as rated water, the sim treating ocean shore as drinkable (its own bug), a wider area cursor (only if playtests ask for one), and macOS and Linux specifics.
