# Landing sites

**Created:** 2026-11-20
**Companions:** [3D to 2D Sampling](./3d-to-2d-sampling.md) (the landing projection), [Player Visibility](./player-visibility.md) (the landing reveal), [Landing site screen](../design/ui/screens/landing-site.md), [World generation UX](../design/features/world-generation/user-experience.md#landing-site-selection)

What makes a good place to start a colony, how the game finds about ten of them per planet and shows them on the world creator's globe, and how a pick on the globe (a rough area, never a coordinate) becomes the point the crew lands on.

Terms: a **pick** is the lat/lon the player clicked or the marker they chose. The **landing** is the resolved point: the 2D world's origin (`landingLatDeg`/`landingLonDeg` everywhere downstream) plus the **drop**, the spawn offset from it that `PlayerVisibilitySystem::revealLanding` centres on. A **feature** is the fresh water the landing is beside, a river or a lake. **Fresh water** is a river or a lake; a coast is salt and never counts, and ponds are not counted. A drop is **clear** when it is dry land 30 m from the edge of the nearest water of any kind, within 3 m.

---

## 1. Rules

1. One definition of a good starting spot: fresh water, food, wood, and a livable temperature. The suggested sites, the default site, the Landing Zone pane's rating, and its Recommended badge all read it (`assessLandingTile`). There is no second scorer.
2. Food and wood come from the flora asset data. A biome has food when flora whose yield item is edible places in it densely enough, and wood when flora whose yield item declares the `wood` group does. No hand list of biomes exists anywhere.
3. Temperature reads both the mean and the seasonal half-amplitude: the mean, the coldest season, and the hottest season must each be livable.
4. A site is called fresh-water, rated as such, recommended, or suggested only when the resolution found a fresh bank or shore for it with a clear drop. Tile flags alone never promise water.
5. A suggested site and a free pick come from the same anchor steps: a suggested site is the landing its own tile's anchors give, and a free click is resolved by `resolveLandingSite`, which runs those steps on the picked tile and then on its river neighbours. A marker click takes its site as found and resolves nothing. The interface never shows the adjustment: the player's crosshair stays where they clicked, the coordinates shown are the pick's, and nothing snaps or moves the pointer.
6. A landing with a feature has a clear drop: dry land 30 m from the edge of the nearest water of any kind, within 3 m, so the crew lands in sight of the water and back from its bank, never on an island or between two channels with less than that on either side. The feature lies inside the area revealed at landing (150 m round the drop). Land too narrow for a clear drop is never a feature's landing; the resolution tries the next anchor, and a landing that finds none has no feature.
7. The Landing Zone pane describes the landing tile, which can be a neighbour of the picked tile, so its biome, climate, and water are where the crew really lands.
8. Up to ten suggested sites per planet, all meeting rule 1, at least 15 degrees of arc apart, varied by kind (river or lake) and by biome. Fewer is fine; none is fine and breaks nothing.
9. The same planet always gives the same sites in the same order. Ties break to the lowest `TileId`; nothing is random.
10. The default site is the first suggested site. Quick Start takes the first site of the same finder with its biome preference; the world creator takes the first without one.
11. Suggested sites are markers on the globe in the Reviewing state, nothing else: no list, no panel entries. A marker shows its kind at a glance. Clicking one selects it exactly as a click there would. A marker is drawn and can be clicked only on screen: on the near side of the globe and inside the stage.
12. Markers show only when the Settings switch "Show suggested landing sites" is on. It is off by default, and a `--no-settings` launch has it off.
13. Sites are computed after generation or load and never stored. The planet file format does not change.
14. Quick Start's landing is fixed for the shipped planet (seed 424242, n=2048): the first suggested site with the Temperate Deciduous Forest preference, a river site. Its origin and drop are recorded in section 8. The regression scenarios run on Quick Start, and several depend on where the crew starts.
15. `revealLanding` is centred on the raw drop, the spawn point before the landing clearing is made.
16. Neither the scan nor a resolution runs on the main thread. A resolution runs a spawn search for each anchor it tries, so the world creator hands both to a worker and stays interactive.

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
- Its density is `spawnChance / (samplePitch * samplePitch) * 1e6` rolls a square kilometer, times the plants a successful roll places. A `Clumped` placement places a clump of `clumpSizeMin` to `clumpSizeMax`, uniformly, as `PlacementExecutor` draws it, so it counts the mean, `(clumpSizeMin + clumpSizeMax) / 2`; any other placement counts one. Spaced flora (trees) are counted as candidates before the grove field's scale and the spacing thin them, the measure `canopyCoverFromDefinitions` uses.
- The density adds to food when the yield item (`registry.getDefinition(harvestable.yieldDefName)`) `isEdible()`, and to wood when the yield item's definition declares the group `kWoodItemGroup` (`"wood"`) in its XML. Both answers come from the item's own definition, never from its name, so a new edible item or a second kind of timber counts by declaring itself. (The flora group `wood_sources` is not the test: it also holds bushes that yield sticks, which can't be built with.)

Both callers build the table only once the asset definitions have loaded. The splash scene gates the main menu on `AssetRegistry::isLoadComplete()` and a `--scene` launch loads them synchronously before the first scene, but a dev switch from the splash can reach either scene while they still load on their worker. The world creator builds the table when it starts generating a planet, and its Generate button stays disabled until the load is complete; `GameLoadingScene` builds it when it chooses Quick Start's site, after its ground bake has waited for the load. Building it is a loop over the definitions, so neither caches it.

### 2.2 Assessing a tile

```cpp
struct TileAssessment {
    WaterClass   water{WaterClass::RainFed}; // verified: River or Lake only when the resolution found it
    bool         hasFood{};
    bool         hasWood{};
    bool         livableTemperature{};
    int          habitabilityScore{}; // the sum habitability is bucketed from
    Habitability habitability{Habitability::Harsh};

    [[nodiscard]] bool meetsBar() const { return isFreshwater(water) && hasFood && hasWood && livableTemperature; }
};

[[nodiscard]] TileAssessment assessLandingTile(const GeneratedWorld& world, const BiomeProvisionTable& provisions,
                                               TileId tile, WaterClass verifiedWater);
```

- **Food, wood:** the tile biome's densities against `kMinimumFoodSourcesPerSquareKilometer` and `kMinimumWoodSourcesPerSquareKilometer`.
- **Temperature:** with mean `m` and half-amplitude `r` in degrees Celsius (the stored tenths divided by 10), livable is `kLivableMeanMinimumCelsius <= m <= kLivableMeanMaximumCelsius`, `m - r >= kLivableColdestSeasonCelsius`, and `m + r <= kLivableHottestSeasonCelsius`. A missing field fails the test.
- **Habitability** is a sum, `habitabilityScore`, and `habitability` is its bucket (4 or more Easy, 2 or more Moderate, 0 or more Hard, below 0 Harsh):
  - water: fresh +3, coastal +1, rain-fed 0;
  - rainfall: at least `kAmpleRainfallMillimeters` +2, at least `kModerateRainfallMillimeters` +1, below `kDesertRainfallMillimeters` on a rain-fed tile -1;
  - temperature: livable 0; mean outside the brutal band, or coldest season below `kBrutalColdestSeasonCelsius`, or hottest above `kBrutalHottestSeasonCelsius`, -2; otherwise -1;
  - no food -1, no wood -1;
  - ample provisions +1: the biome's food at least `kAmpleFoodSourcesPerSquareKilometer` and its wood at least `kAmpleWoodSourcesPerSquareKilometer`.

A tile that meets the bar scores 3 to 6: fresh water's 3, up to 2 for rainfall, and 1 for ample provisions. It is Easy unless it gets under 250 mm of rain a year and lacks ample provisions, when it scores 3 and is Moderate. With the shipped flora the six forest biomes (Tropical Rainforest, Tropical Seasonal Forest, Temperate Deciduous Forest, Temperate Rainforest, Boreal Forest, Montane Forest) have ample provisions; Temperate Grassland (wood 625 a square kilometer) and Beach (food 2,187.5, wood 937.5) do not. The pane's skulls come from `habitability`, and its Recommended badge from `LandingSite::recommended()` (2.3).

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

`SpawnSite` (`SpawnSite.h`) carries `SpawnWater water` (`None`, `River`, `Lake`, `Ocean`), `float waterDistanceMeters`, and `bool clear`. A landing has a feature only when its drop is clear and its water is a river or a lake (section 3), so every recommended landing has a clear drop.

```cpp
SpawnSite findWatersideSpawn(const std::shared_ptr<const GeneratedWorld>& world, double landingLatDeg,
                             double landingLonDeg, double searchRadiusMeters = 200.0);
```

The drop is found on a 3 m grid over the square of half-side `searchRadiusMeters` round the origin. A point qualifies when it is dry (not lake or ocean, not inside a river channel) and its distance to the edge of the nearest water, of any kind, is `kLandingWaterDistanceMeters` within `kLandingWaterDistanceToleranceMeters`. The nearest water's kind is the point's kind. A river point comes before a lake point before an ocean point, and within a kind the point nearest the origin wins, ties to the first in scan order (rows of increasing y, each row by increasing x); that drop is `clear`. When no dry point qualifies, the land is too narrow: the drop is the dry point farthest from water among those within the distance plus the tolerance of it, in the same order, ties to the nearer to the origin, and `clear` is false. When no dry point is that near any water, the drop is the origin, `water` is `None`, and `clear` is false.

- **River distance** is exact: the distance to a channel segment's centreline less its half-width there, the least over the segments `RiverNetwork2D::gatherSegments` returns for the square widened by the distance plus the tolerance.
- **Lake and ocean distance**: `PlanetSampler` classifies every grid sample once (land, lake, or ocean) over the square widened by the distance, the tolerance, and one step, and an exact Euclidean distance transform (Felzenszwalb and Huttenlocher, rows then columns) gives every sample its distance to the nearest lake sample and to the nearest ocean sample. The shore lies between that water sample and the dry one before it, so its edge is taken half a step short of the sample.

Because the samples and segments reach past the search square, a point at the square's edge is never judged far from water it could not see. `waterDistanceMeters` is the drop's distance to its nearest water's edge, and zero for `None`.

## 3. Resolving a pick

```cpp
[[nodiscard]] LandingSite resolveLandingSite(const std::shared_ptr<const GeneratedWorld>& world,
                                             const BiomeProvisionTable& provisions, LatLon pick);
[[nodiscard]] bool isWaterTile(const GeneratedWorld& world, TileId tile);
```

A planet tile is about 3.5 km across at n=2048, and the area a pick names is the picked tile `T = grid.fromLatLon(pick)` and its six neighbours. The resolution tries anchors in this order and takes the first whose spawn search gives a clear drop beside fresh water:

1. `T` is a land river tile: its centre.
2. `T` borders lake tiles: a lake anchor toward each lake neighbour, nearest neighbour centre to the pick first.
3. `T`'s land river neighbours: each one's centre, nearest to the pick first.

Ties break to the lowest `TileId`. For each anchor, `findWatersideSpawn(world, anchor)` runs with the anchor as origin; the anchor is accepted when `spawn.clear` holds and `spawn.water` is `River` or `Lake`, and the site is `{anchor, fromLatLon(anchor), feature, spawn, assessLandingTile(..., River or Lake)}`. A search that finds fresh water but only the narrow-land drop does not accept its anchor, and the next anchor is tried. When no anchor passes, the landing stays at the pick: origin is the pick, the spawn search runs there and its drop is taken as it comes (clear or not), `feature` is `None`, and the water class is `Coastal` when `T` has an ocean neighbour (a water tile that is not a lake), otherwise `RainFed`. Such a landing is never recommended. The anchors and the spawn search read Elevation, Biome, and Flags; on a world missing any of them no anchor is tried and the drop is the pick itself, with water `None`.

Water tiles are `isWaterTile`: a lake or ocean flag, or an elevation below sea level, so a tile below sea level with no flag is ocean. A water pick resolves the same way with no anchors; the world creator rejects water picks with the same test before resolving, and the finder skips water tiles with it.

Why the anchors land on water:

- **River.** `RiverNetwork2D` joins its channel segments at tile centres and tapers the meander and the width variation to zero there, so a river tile's centre lies exactly on its channel's centreline. A landing origin there puts the channel through the 2D origin, and the bank is within half the channel width (at most `RiverNetwork2D::kMaxHalfWidthMeters`, 103.5 m) of it. The drop is 30 m (at most 33 m) back from the bank, so within 136.5 m of the origin, inside the 200 m search. A river tile with no segment of its own and none arriving (an isolated flag) fails the search and the next anchor is tried.
- **Lake.** `PlanetSampler` decides water by the containing tile alone (no blend), and the chunk layer draws a tile's surface from its primary biome, the containing tile's (weight at least 0.5). So the 2D lake shore is the Voronoi edge between a lake tile and a land tile, moved at most `TerrainPolygonBuilder::kMaxWarpMm` (1.45 m) by the shore warp, plus the 1 m tile grid. The grid decides the containing tile in its lattice chart, so that edge is not the centres' great-circle midpoint: the two are 0.2 m apart at n=2048 and more than a kilometer apart at n=24. The lake anchor therefore finds the edge itself. On the arc from `T`'s centre to the lake tile's centre, a bisection on `grid.fromUnitVector` finds the point where the containing tile stops being `T`, to within `kLakeEdgeToleranceMeters`; the anchor is `kLakeShoreInsetMeters` back from it toward `T`'s centre. It is on land, inside `T`, at any grid resolution, the shore is about 20 m off, and the drop is about 10 m behind the anchor, 30 m from the shore and well inside the search.

The anchor's lat/lon comes from `SphereGrid::latLonFromUnitVector`, and the pick's direction from `SphereGrid::unitVectorFromLatLon`, the conversions the grid's own lookups make.

The origin moves to the anchor, so every `RiverNetwork2D`, `PlanetSampler`, and `GeneratedWorldSampler` downstream is built at the anchor, and the anchor's tile is the landing tile.

**Idempotent.** Resolving a landing's origin gives the same landing: a river anchor is its own tile's centre (step 1 on that tile), and a lake anchor lies inside `T`, nearest to the lake tile it was built toward. Anchors are computed from tile centres, never from the pick, so the float lat/lon the globe holds loses nothing. So a free click anywhere on a suggested site's own tile lands on that site.

## 4. Suggested sites

```cpp
struct SuggestedSiteQuery {
    std::optional<Biome> preferredBiome;
    std::size_t          maximumSites{kSuggestedSiteCount};
    std::stop_token      stopToken;
    std::function<void()> checkpoint; // called on the finder's thread wherever it checks stopToken
};

[[nodiscard]] SuggestedSiteQuery quickStartSiteQuery(); // Temperate Deciduous Forest preferred, one site

[[nodiscard]] std::vector<LandingSite> findSuggestedLandingSites(const std::shared_ptr<const GeneratedWorld>& world,
                                                                 const BiomeProvisionTable& provisions,
                                                                 const SuggestedSiteQuery& query);
```

### 4.1 Candidates

A candidate is a tile whose own resolution anchors on itself, so the scanned tile is the landing tile: a land river tile (kind River), or a land tile bordering a lake tile (kind Lake). It must also pass the rest of the bar on its own data (food, wood, temperature), and its centre must lie within `kMaximumSuggestedLatitudeDegrees` of the equator. The latitude cut belongs to the finder, not the bar: a free pick nearer a pole that meets the bar is still Recommended. One pass over every tile.

Candidates are kept per slot, a slot being (bin, kind, biome). The bin is a tile of a coarse `SphereGrid(kSiteBinSubdivision)` (642 bins, about 8 degrees across), found from the candidate's centre. Each slot keeps its best `kCandidatesPerSlot` by rank. That bounds memory on a lush planet and keeps candidates everywhere for the spacing and variety rules; the keep is order-independent, so the scan may split across workers without changing the result. The global best candidate is always kept.

**Rank**, best first: preferred biome (when the query has one), then kind (River before Lake), then `habitabilityScore` (higher first), then the lowest `TileId`.

### 4.2 Selection

Candidates are sorted by rank. Selection runs in rounds `r = 1, 2, ...` up to `kSuggestedSiteCount`, then one final pass. A candidate is taken when all hold:

1. it is at least `kMinimumSiteSeparationDegrees` of arc from every taken site's tile centre;
2. fewer than `r` taken sites share its biome (no limit in the final pass);
3. fewer than `kMaximumSitesOfOneKind` taken sites share its kind (no limit in the final pass);
4. its own tile's anchors (section 3's steps 1 and 2, nearest to its centre first) give a landing that is `recommended()` and lies on the candidate's tile.

Check 4 runs last and at most once per candidate (a failure is remembered). Check 3 reads the kind the scan gave a candidate until check 4 has run, and the kind its landing resolved to after: a site's kind is its resolved `feature`, so a lake candidate with a river inside the spawn search resolves as a river site (the search prefers a river) and counts as one. An unverified candidate whose scanned kind is at its limit is therefore skipped in every round before the final pass, even one that would resolve to the other kind (Decision 21). Selection stops at `query.maximumSites`. The first site taken is the best-ranked candidate whose resolution passes, so `sites[0]` is the default site. The scan checks `stopToken` every 65,536 tiles and before each resolution, calling `query.checkpoint` (when set) each time. A stop requested during the tile scan returns no sites; one requested during selection returns the sites taken so far.

### 4.3 Where it runs

`LandingSiteSurvey` (`apps/world-sim/scenes/shared/LandingSiteSurvey.h`) owns one `std::jthread` for a planet. It runs `findSuggestedLandingSites` and hands that result back once, and it resolves picks, including at the finder's `checkpoint`, so a pick waits at most one step of the scan: the newest pick waiting is resolved next (ahead of the rest of the scan's verification step when both wait), and a pick replaced before it was resolved is dropped.

```cpp
class LandingSiteSurvey {
  public:
    // The survey sets the query's stopToken and checkpoint.
    void start(std::shared_ptr<const worldgen::GeneratedWorld> world, worldgen::BiomeProvisionTable provisions,
               worldgen::SuggestedSiteQuery query);
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

- **World creator.** Started when Reviewing begins, with the default query. While it runs the globe is shown and usable, the footer status reads "Surveying landing sites...", no site is selected, and Confirm Landing Site is disabled until one is. The player may click the globe meanwhile; a click resolves and selects as usual. When the survey finishes, if the player has picked nothing, `sites[0]`'s origin becomes the pick. The status then reads "World ready - suitable for colonization", or "World ready - no recommended landing sites" when the list is empty (no site is then selected until the player clicks). Regenerate, Escape, and `onExit` cancel it.
- **Quick Start.** `GameLoadingScene`'s `ChoosingSite` phase follows `PreparingPlanet`, with the status "Choosing a landing site...". It runs the survey with `quickStartSiteQuery()` and takes `sites[0]`. An empty list fails the load with "The quickstart planet has no landing site with fresh water, food, wood, and a livable climate.", as a missing planet does: the shipped planet is a packaging fact, not a player state.
- **A click** in the world creator moves the crosshair and the coordinates at once and calls `resolve(pick)`. Until `takeResolved` returns that pick, the pane keeps the coordinates and shows "Surveying..." in place of the rest, and Confirm Landing Site is disabled. A click on a suggested marker needs no resolution: its `LandingSite` is already in `suggestedSites`.

## 5. The world creator

- **State.** `bool pickMade` (the player or the survey has picked), `planetview::LatLon selectedPick` (where the crosshair is), `std::optional<worldgen::LandingSite> selectedLanding` (empty while its resolution is pending), and `std::optional<std::size_t> selectedSuggestion`. `suggestedSites` holds the survey's result.
- **Picking.** A left press on the stage (`stagePress`) holds the globe and picks nothing: `GlobeView` stops any inertia (`OrbitCamera::hold`) and the stage captures the pointer. A press whose target, or any component between it and the stage, answers the pointer itself (a viz dock segment) is that component's, and the stage ignores it. `GlobeView` owns whether a press is a drag: the globe turns only once the pointer has travelled more than `ui::kDragThreshold` ui px from where it went down, the travel past which the ui stops calling the press a click, and turns from the point where it crossed the threshold, so it starts without a jump. A pick is the ui's click (`stageClick`): a press that never turned the globe (`GlobeView::pressTurnedGlobe`), released within the threshold of where it went down and inside the stage, so a click leaves the camera exactly where it was, with no inertia, and a drag picks nothing even when it ends back within the threshold. The pick is made at the press point (`GlobeView::lastPressPoint`): `stageClick` first hit-tests the suggested markers on screen (`kSuggestedMarkerHitRadius` ui px from the press, nearest wins); a hit makes that site's origin the pick. Otherwise `globe.pick` at the press point gives it. Right-click cycles the colour mode and picks nothing; the wheel zooms. A marker hit sets `selectedPick` to the site's origin and `selectedLanding` to the site. A globe pick goes to `trySelectSite(pick)`, which rejects water tiles (`isWaterTile`), sets `selectedPick`, clears `selectedLanding` and `selectedSuggestion`, and calls `survey.resolve(pick)`; when the result for that pick arrives it becomes `selectedLanding`. `selectedSuggestion` is the index of the site whose `origin` equals the shown landing's origin, if any, set by one function (`matchSuggestion`) whenever a landing is shown and again when the survey's sites arrive, so a free pick that resolved first is matched too.
- **Pane.** `buildLandingSiteDetails(const worldgen::GeneratedWorld&, const worldgen::LandingSite&, double pickLatDeg, double pickLonDeg)`. Coordinates are the pick's; biome, temperatures, rainfall, terrain, ice, and water come from `site.tile` and `site.assessment`; `recommended` is `site.recommended()`. A Provisions section shows Food and Wood, each Yes or None. The Commit to Descent dialog reads the same `details`.
- **Landing.** `land()` puts `selectedLanding` in `GameStartConfig::landing`.
- **Markers.** `renderSuggestedSites(rect, scale, theme)` draws before `renderMarker`, so the selection crosshair sits on top. A site's marker is on screen when `projectLatLon` is true for it (false on the far side: the camera must lie above the point's tangent plane) and its point lies inside the stage; one predicate (`placeMarker`) decides it for drawing, hit testing, and the `landingSites` view. A click selects a marker only when its release is inside the stage too. Each marker on screen is a ring of radius `kSuggestedMarkerRadius` ui px with a `kSuggestedMarkerStroke` px stroke in the `data` token, and a dot of radius `kSuggestedMarkerDotRadius` px at its centre in `dataBright`, every size scaled by the interface scale as the crosshair's are. A river site's ring is a diamond (a square turned 45 degrees), a lake site's a circle. No pulse; the pulse belongs to the selection. Drawn only while `UserSettings::Get().landing.showSuggestedSites` is on, read each frame.

## 6. The setting

- `struct LandingSettings { bool showSuggestedSites = false; bool operator==(const LandingSettings&) const = default; };` in `UserSettings.h`, held as `UserSettings::landing`. Its default is this constant, not construction XML.
- JSON key `landing.showSuggestedSites`, in a `kLandingSwitches` table beside `kSwitches`, read by `load` (after resetting `landing` to `LandingSettings{}`) and written by `store`.
- `SettingsScene` has a `// LANDING` group after `// INTERFACE`, with one switch: id `landing_suggested_sites` (its toggle `landing_suggested_sites_toggle`), label "Show suggested landing sites", line "Mark good places to start a colony on the globe: fresh water, food, wood, and a livable climate." `saveSettings` writes the settings.

## 7. Handing the landing on

- `GameStartConfig::landing` (`worldgen::LandingSite`) is the landing the game starts at.
- `GameLoadingScene::initializeWorldSystems` builds the sampler and chunk manager at `landing.origin` and takes the drop from `landing.spawn`; it does not run the spawn search itself.
- `GameWorldState::landing` holds it for the game. `GameScene` reads its origin (minimap context) and its drop (`prepareLandingClearing`, `revealLanding`) from it; `revealLanding` is centred on that raw drop.
- `DevCommandHandler`'s context carries `const worldgen::LandingSite* landing`.
- The benches that need the Quick Start landing (`NavScale.bench.cpp`, `NavigationSystem.bench.cpp`) get it from `bench::quickStartLanding` (`QuickStartLanding.bench.h`), which loads the planet (the file `WORLDSIM_PLANET` names, else `planets/quickstart.wsplanet` under the project root, the checkout its source file lies in) and the world asset definitions from that root, once, and runs the finder with `quickStartSiteQuery()`. A missing planet skips the bench; a planet or asset definitions that won't load, or no site, fail it with the reason.

## 8. Quick Start's landing

On the shipped planet (seed 424242, n=2048), Quick Start lands on the first suggested site with the Temperate Deciduous Forest preference: a river tile in Temperate Deciduous Forest, its resolution anchored on the tile's own centre. The `landing` view must read:

| Field | Value |
|---|---|
| `landingLatDeg` | 28.80951735938833 |
| `landingLonDeg` | 24.502251073059021 |
| `feature` | `river` |
| `spawnXMeters`, `spawnYMeters` | -29, -17 |
| `featureDistanceMeters` | 27.950307846069336 |
| `biome` | `TemperateDeciduousForest` |
| `waterClass` | `River` |
| `riverInTile` | true |

The regression scenarios run on Quick Start, and several depend on where the crew starts. A change that moves the origin or the drop is a deliberate one: it records the new numbers here and re-derives those scenarios.

## 9. Dev views

**`landing`** (game scene, `/api/state?what=landing`):

```json
{"landingLatDeg":0.0,"landingLonDeg":0.0,"feature":"river","spawnXMeters":0.0,"spawnYMeters":0.0,
 "featureDistanceMeters":0.0,"biome":"TemperateDeciduousForest","waterClass":"River","riverInTile":true}
```

`landingLatDeg`/`landingLonDeg` are the origin, `biome` and `waterClass` the landing tile's (the verified class), `feature` is `river`, `lake`, or `none`, `spawnXMeters`/`spawnYMeters` the raw drop, and `featureDistanceMeters` the drop's `waterDistanceMeters`, its distance to the nearest water's edge: 30 within 3 when `feature` is `river` or `lake`, null when it is `none`.

**`landingSites`** (world creator, registered with `devServer().stateProvider` in `onEnter`, released in `onExit`):

```json
{"surveying":false,"resolving":false,"markersShown":true,"cameraMoving":false,
 "sites":[{"index":0,"latDeg":0.0,"lonDeg":0.0,"kind":"river","biome":"TemperateDeciduousForest","onScreen":true,"x":0.0,"y":0.0}],
 "selection":{"pickLatDeg":0.0,"pickLonDeg":0.0,"suggestedIndex":0,"landingLatDeg":0.0,"landingLonDeg":0.0,
              "biome":"TemperateDeciduousForest","feature":"river","recommended":true}}
```

`sites` lists every suggested site whether or not markers are shown. `x`/`y` are the marker's position in the window's logical px (the input space, as `renderMarker` draws), null on the far side, and `onScreen` is true when the site is on the near side and inside the stage, the markers that are drawn and can be clicked. `resolving` is true while a free pick waits for its landing. `cameraMoving` is `GlobeView::isCameraMoving`: true during a drag, while a zoom eases to its target, while a drag's inertia lasts, and until the globe is first touched by a press, a scroll, a right-click, or a key (its reveal spin can start on its own); marker positions read while it is true may be stale by the time a click lands. A click leaves it false. `selection` is null until a site is selected; `suggestedIndex` is null for a free pick, and the landing's fields (`landingLatDeg` to `recommended`) are null while it resolves.

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
| `kAmpleFoodSourcesPerSquareKilometer` | 700.0 | food for the ample provisions term of the score |
| `kAmpleWoodSourcesPerSquareKilometer` | 1000.0 | wood for the same |
| `kFreshWaterScore`, `kCoastalScore` (`LandingSite.cpp`) | 3, 1 | the score's water term; rain-fed adds nothing |
| `kAmpleRainfallScore`, `kModerateRainfallScore`, `kDesertRainfallScore` (`LandingSite.cpp`) | 2, 1, -1 | the score's rainfall term; the desert term on a rain-fed tile only |
| `kUnlivableTemperatureScore`, `kBrutalTemperatureScore` (`LandingSite.cpp`) | -1, -2 | the score's temperature term when not livable, and when brutal |
| `kNoFoodScore`, `kNoWoodScore` (`LandingSite.cpp`) | -1, -1 | a biome without food, without wood |
| `kAmpleProvisionsScore` (`LandingSite.cpp`) | 1 | ample food and wood |
| `kEasyScoreMinimum`, `kModerateScoreMinimum`, `kHardScoreMinimum` (`LandingSite.cpp`) | 4, 2, 0 | the least score of each bucket; below the last is Harsh |
| `kLakeShoreInsetMeters` | 20.0 | lake anchor's distance inside the land tile from the lake edge |
| `kLandingWaterDistanceMeters` (`SpawnSite.h`) | 30.0 | the drop's distance from the nearest water's edge |
| `kLandingWaterDistanceToleranceMeters` (`SpawnSite.h`) | 3.0 | how far off that distance a clear drop may be: one step of the search grid |
| `kLakeEdgeToleranceMeters` | 0.5 | how closely the bisection finds the edge between a land tile and a lake tile |
| `kWoodItemGroup` (`BiomeProvisions.h`) | `"wood"` | the group an item declares to count as wood |
| `kSuggestedMarkerRadius` (`WorldCreatorScene.cpp`) | 6 ui px | marker size |
| `kSuggestedMarkerStroke` (`WorldCreatorScene.cpp`) | 1.5 ui px | marker outline width |
| `kSuggestedMarkerDotRadius` (`WorldCreatorScene.cpp`) | 2 ui px | marker centre dot |
| `kSuggestedMarkerHitRadius` (`WorldCreatorScene.cpp`) | 10 ui px | how near a click's press must be to a marker to take it |
| `ui::kDragThreshold` (salal, `ui/Drag.h`) | 6 ui px | travel from the press past which the globe turns and the press is no longer a click |

The landing reveal radius is `NavigationSystem::kLandingSightMeters` (150 m); a drop 30 m from its water keeps every fresh feature well inside it.

## 11. Tests

The synthetic worlds (a block of grassland in an ocean, rivers carved across it, lakes) come from `libs/worldgen/worldgen/sampling/RiverWorld.test.h`, which `worldgen-tests` and `world-sim-tests` both use.

- **Bar** (`LandingSite.test.cpp`). A river tile passes with food, wood, and livable temperatures, and fails with each one taken away in turn (no wood; no food; a mild mean with a coldest season below -15 degrees Celsius). A dry, cold, rain-fed tile rates Harsh.
- **Provisions** (`BiomeProvisions.test.cpp`, in `assets-tests`). `biomeProvisionsFromDefinitions` on registered test definitions: an edible-yield clumped bush counts its mean clump in its biome's food, a tree whose yield item declares the `wood` group counts as wood, an item merely named Wood and a stick yield do not, and a `near="Mud"` entry counts not at all.
- **Spawn search** (`SpawnSite.test.cpp`). At a river's source, where the trunk and two springs meet at the origin, the drop is dry and clear, its reported distance is 30 m within the tolerance and matches the nearest channel read independently, and no point of the same grid nearer the origin is dry and in the band; with a search too narrow for any point to reach 30 m, the drop is the farthest dry point, on the square's edge, and not clear; 20 m in from an ocean shore the drop is an ocean drop 30 m back, the shore read independently; where a clear ocean drop lies nearer the origin than a river's, shown first, the drop still takes the river; the same with a lake in place of the ocean; where a clear ocean drop lies nearer than a lake's, the drop takes the lake; a world with no water gives the origin and `None`.
- **Resolution** (`LandingSite.test.cpp`). A pick on a river tile resolves to that tile's centre with its drop 30 m from the river; a pick beside a lake to a drop 30 m from the lake, the lake read independently round it; a pick one tile from a river to the river tile, with `tile` the river tile; a river tile past 45 degrees of latitude that meets the bar is recommended when picked and is never a suggested site; a dry pick resolves to itself with `feature` `None`, and is `Coastal` once a neighbour lies below sea level with no flag, which `isWaterTile` calls water; on a narrow strip, where the search finds fresh water but no clear drop, the anchor is not accepted and the resolution lands on the river neighbour, whose drop is clear. Each resolved landing resolves to itself.
- **Finder** (`LandingSite.test.cpp`). Every site meets the bar and has a clear drop; sites are at least the separation apart; on a fixture with river and lake candidates in two biomes, both kinds and both biomes appear, a forest river with ample provisions outranks a grassland river with a lower `TileId`, and with `quickStartSiteQuery()` a forest river outranks a grassland river with a lower `TileId` and the same score; with more river sites to be had than the cap and lakes elsewhere, the rivers stop at `kMaximumSitesOfOneKind` and the rest are lakes; two runs give identical lists; an all-desert world and an all-ocean world give an empty list; a stop during the tile scan returns nothing, and a stop before the second resolution returns the first site.
- **Generated planet** (`LandingSiteFinderHeavy`, the heavy bucket). On a small generated planet (n=64, as `WorldStats.test.cpp` generates), every suggested site's drop is clear, its `waterDistanceMeters` is 27 to 33 m, and the distance read independently on rings round the drop agrees: to the channel as `RiverNetwork2D::sampleAt` reports it for a river site, to the lake's samples for a lake site.
- **Survey** (`LandingSiteSurvey.test.cpp`, in `world-sim-tests`). The sites are handed back once, then the newest of two picks is the one resolved; a cancel leaves no survey running, no pick waiting, and no sites to take.
- **Setting** (`UserSettings.test.cpp`, in `world-sim-tests`). `landing.showSuggestedSites` comes back as it was stored, and an empty file (what a `--no-settings` launch loads) or a malformed value reads as off whatever was on before.

Happy path: [scenario 25](../testing/scenarios/25-landing-sites.md).

---

## Decisions

1. The bar is fresh water, food, wood, and livable temperature, one definition for everything that judges a site. (Kevin.)
2. Food and wood are derived from flora placement and yields so a new plant changes the answer without code. Conditional (`near`) placements don't count because the planet can't see mud. Nothing is a fixed list: far more assets and biome configuration are coming (Kevin), so an item says it is food or wood in its own XML and no item name appears in code.
3. Ponds are not rated water: the planet-level code can't see them. Coast is salt and never counts.
4. Suggested sites keep a 45 degree latitude cut on top of the bar. It holds Quick Start's landing (section 8). It is the finder's filter, not part of the bar, so the pane's Recommended badge means the four things in rule 1 and nothing about latitude.
5. A globe pick names a rough area: "the ship is crashing, you don't have control. so the exact coordinates can get moved." The game resolves the landing for every pick and never shows it. (Kevin.)
6. The landing origin moves to the anchor rather than the drop moving kilometres from the origin. The projection is exact at its origin, and every 2D consumer keys off the origin already.
7. A site is fresh-water only when the spawn search found the water. Tile flags promise a river on the tile or a neighbour, which can be kilometres off.
8. A pick resolves on the worker, not inside the click: a spawn search is long enough to drop frames on its own, and a resolution runs one for each anchor it tries.
9. The lake anchor finds the tile edge by bisection on the grid's own lookup. The centres' midpoint is on the edge only at fine resolutions (13 m off at n=256, 209 m at n=64).
10. Candidates are kept per (bin, kind, biome) slot instead of in one sorted list, so memory is bounded and spacing and variety have candidates everywhere.
11. The default site is the first suggested site: one finder, no second scorer.
12. Markers sit behind a Settings switch, off by default. (Kevin.)
13. Sites are computed, never stored: a format change forces a re-bake of the 1.3 GiB shared planet.
14. From Dwarf Fortress's embark screen, sites found by criteria and marked on the map: the suggested sites. From RimWorld's world map, a side panel of the picked tile's terrain, temperature, and growing season: the Landing Zone pane.
15. The drop is 30 m from the nearest water: "not so close, lets go 30m from the water" (Kevin, on a drop 0.15 m from a river). The distance is to the nearest water of any kind, not to the water the site is rated for, so the crew never lands on an island or between two channels with less than 30 m on either side. Lakes and ocean are measured by a distance transform over one classification of the grid rather than probes per point, which is exact on the grid and cheaper.
16. Suggested sites rank by `habitabilityScore`, not its bucket, and the score counts ample provisions. Nearly every good site rates Easy, so a rank by bucket would leave the order to `TileId`, and with no biome preference the default site would often be a beach river site: a beach passes the bar by its data (palms, sea grape) but is a poor place to start. Food and wood well past the bar are what set a forest apart from grassland and beach, so they earn the point.
17. A landing needs a clear drop. When no dry point is 30 m from water, the spawn search still returns its best point, so a caller always has a drop, but that point can stand a few meters from two channels; it is a landing's drop only when no anchor gives a clear one, and that landing has no feature and is never recommended.
18. Quick Start's landing is known good from play, so the livable limits and the bar answer to it: a limit that would exclude it is wrong and is the thing to change. Any change to the bar, the rank, the resolution, or the spawn search is checked by reading the `landing` view on Quick Start against section 8, and an unplanned difference stops the change.
19. A clumped placement counts every plant of its clump, since the food a colony finds is plants, not rolls. Spaced trees count candidates before thinning, as canopy cover does, so the two measures of a forest agree.
20. A click and a drag agree on one number, the ui's `kDragThreshold`: the globe does not turn inside it, so a click picks what was under the press on the globe as it stood, with no inertia, and the pick is made at the press because that is where the player aimed. The drag begins from the point it crossed the threshold rather than taking the travel so far at once, which would jump. `GlobeView` owns the decision, as it owns the press and the camera; the scene only asks whether the press turned the globe.
21. During selection the kind limit reads an unverified candidate's scanned kind. Verifying every such candidate to learn its resolved kind would run a spawn search for each, and the final pass still takes the ones the limit held back.
22. Out of scope here: the 2D local preview and difficulty rating of a site (Specboard WOR-369, "Landing site local preview + difficulty rating"), the loading screen backdrop (WOR-378, "Improve Landing Screen"), ponds as rated water, the sim treating ocean shore as drinkable (its own bug), a wider area cursor (only if playtests ask for one), and macOS and Linux specifics.
