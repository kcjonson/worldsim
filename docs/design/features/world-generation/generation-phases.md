# Generation Phases

Created: 2025-10-26
Status: Design

## Overview

World generation follows a sequential pipeline of **eight conceptual phases**, each building on the previous to create a complete, scientifically plausible planet. This document describes **what each phase accomplishes** from a game design perspective, not the technical implementation.

**Philosophy**: Each phase models a real planetary process (tectonics, atmosphere, hydrology, biology). Features **emerge naturally** from these processes rather than being placed arbitrarily.

## Generation Pipeline

```
Parameters → Tectonic History → Crust → Terrain → Climate → Water → Biomes → Complete World
```

Each phase produces data that later phases consume:

1. **Tectonic History Simulation** - Run coarse time-stepped plate sim (~800 Myr)
2. **Crust Upsampling** - Upsample coarse history to full resolution; crisp organic coastlines
3. **Terrain Height Generation** - Elevation from isostasy, depth-age law, and orogeny history
4. **Atmospheric Circulation** - Model wind patterns
5. **Precipitation & River Formation** - Simulate rainfall and water flow
6. **Ocean & Sea Formation** - Fill low areas with water
7. **Biome Generation** - Assign ecosystems based on climate
8. **Snow & Glacier Formation** - Calculate ice coverage

## Phase 1: Tectonic History Simulation

**Purpose**: Run a coarse time-stepped tectonic simulation to produce geologically plausible crust state, boundary locations, and mountain-building history

*Revision note, 2026-06-13:* Phase 1 previously described a single-pass Voronoi plate generation. That was replaced by a time-stepped simulation (PR #136). Phase 2 (plate movement) was folded into Phase 1 as well; the pipeline now has TectonicHistoryStage + CrustStage in the first two slots.

**Conceptual Process**:

The planet runs ~800 Myr of tectonic history (scaled by planet age, ~160 time steps of 5 Myr each) on a coarse grid (~56 km/tile). That is roughly two Wilson cycles — enough for continents to rift, drift, collide, suture, and sometimes re-rift.

Each step:
- Plates move along Euler poles (oceanic 4-10 cm/yr, continental 1-4 cm/yr). Poles evolve slowly so plates don't move forever in straight lines.
- Each plate's crust is re-rasterized into the world grid each step. Ownership is resolved deterministically: continental crust wins over oceanic; between two oceanic cells, younger wins (older, denser crust subducts).
- **Subduction**: oceanic crust displaced by a higher-priority cell is erased. The subducting slab pulls its plate trenchward (slab pull), accelerating the recycling of old seafloor.
- **Ridge gap-fill**: cells left unoccupied after subduction get fresh age-0 oceanic crust (spreading ridges emerge where plates separate).
- **Collisions**: continental-continental convergence thickens crust and stamps orogeny records. Sustained fast collision (~150 Myr) sutures two plates into one.
- **Rifting**: when plate count falls below the target K, large plates preferentially split along old sutures, re-opening ancient mountain belts as new ocean basins (Wilson cycle).
- **Arc magmatism**: subduction zones generate volcanic arcs; mature arc crust converts to thin continental crust, balancing the area lost to collisional shortening. This keeps continental fraction near the target.
- **Hotspots**: fixed mantle plumes deposit volcanism as plates drift over them.
- Thickness-relaxation erosion proxy: over-thickened crust slowly relaxes toward the stable cratonic mean.

**Output** (coarse grid, ~56 km/tile):
- Per-tile: plate ID, crust type (continental/oceanic), crust age, thickness, orogeny age and intensity, volcanism, boundary type and convergence rate
- Plate list with Euler poles and cumulative rotation quaternions

**Visual Result**: Coarse crust map showing plate territories, age-striped seafloor, orogeny bands, and boundary classifications

**Gameplay Impact**:
- More plates → fragmented continents, island arcs, archipelagos
- Fewer plates → large unified landmasses, broader collisional belts
- Planet age → more Wilson cycles → richer suture networks, more eroded old belts

## Phase 2: Crust Upsampling

**Purpose**: Upsample the coarse tectonic history to the full-resolution world grid, producing crisp organic coastlines and smooth spatial fields

**Conceptual Process**:

Each full-resolution tile is assigned crust type by thresholding a signed-distance field (built from the coarse crust boundary) rather than nearest-neighbor sampling. The signed distance field is warped by fractal noise before thresholding, so the coastline meanders organically and breaks up any lingering hexagonal regularity from the coarse grid. A crenulation noise term adds further wiggle at the continental margin.

Once crust type is decided, per-tile crust age, orogeny age, and plate ID are sampled from the nearest coarse cell of the matching crust type (inverse-distance blend). This prevents coastline-transition artifacts where a continental tile would otherwise inherit oceanic ages.

**Output**:
- Per-tile: plate ID, crust flag (continental/oceanic), crust age (u16, Myr), orogeny age (u16, Myr)
- Boundary type and convergence rate carried from the coarse history

**Visual Result**: Full-resolution crust map with irregular coastlines, no hexagonal grid artifacts

**Gameplay Impact**:
- Coastline shape determines how much navigable coast, how many peninsulas and bays
- Crust age feeds the elevation calculation in Phase 3

## Phase 3: Terrain Height Generation

**Purpose**: Synthesize planetary elevation from crust physics and tectonic history

**Conceptual Process**:

### 3A: Continental elevation (Airy isostasy)
Thick continental crust floats high on the mantle. Elevation scales with how far the crust thickness departs from the cratonic equilibrium (~35 km): thin young continental crust sits near sea level, normal crust at ~+400 m, and collision-thickened crust (65-70 km, Tibet scale) reaches +5-6 km. Orogeny records from the tectonic history add ridged-noise mountain belts on top: active recent belts are tall and linear; ancient eroded sutures are low rolling hills.

### 3B: Oceanic elevation (seafloor depth-age law)
Oceanic crust cools and subsides as it ages, following an empirically calibrated depth-age relation (Parsons-Sclater / GDH1): ridge crests sit at ~-2500 m, abyssal plains at ~-5500 m. The bimodal hypsometry (land mode ~+400 m, ocean mode ~-5500 m) emerges from these two laws without any hand-tuning.

### 3C: Active-boundary features
Narrow kernels at active boundaries add:
- Ocean trenches (~-5500 m, 60 km wide) at subduction zones
- Volcanic arcs (+2500 m scaled by convergence rate, ~220 km inland)
- Rift valleys and flanking shoulders at divergent boundaries
- Transform fault roughness

### 3D: Hotspot volcanism
Mantle plume locations from the tectonic history get volcanic cones, capped at ~3500 m, with a ridged-noise texture.

### 3E: Sea level
Sea level is set by histogram quantile so the ocean fraction matches the player's water-amount parameter (±2%).

**Output**:
- Elevation map for the full planet
- Sea level value

**Visual Result**: 3D terrain with linear mountain ranges, ocean trenches, mid-ocean ridges, rift valleys, volcanic islands, and smooth abyssal plains

**Gameplay Impact**:
- Mountains are thin linear ridges (realistic), not smooth symmetric domes
- Ocean depth varies by seafloor age (ridges shallower than abyssal plains)
- Young planets: tall sharp mountain belts (recent collisions); old planets: eroded low sutures
- Planet age affects both how many Wilson cycles occurred and how eroded old belts are

**Design Notes**:
- Bimodal hypsometry (the defining feature of an Earth-like planet) is a consequence of physics, not parameter tuning
- Same seed produces a different world than before this overhaul (documented; analogous to the Goldberg hex conversion precedent)

## Phase 4: Atmospheric Circulation

**Purpose**: Model surface temperature and prevailing winds from planetary rotation, atmospheric pressure, and land/sea contrast

**Conceptual Process**:

### 4A: Global mean temperature
A Stefan-Boltzmann equilibrium temperature for the planet's orbit and star, plus a greenhouse correction that scales as the square root of atmospheric pressure (absorption bands saturate as column mass grows, so warming is sublinear). Earth at 1 atm: +33 C greenhouse, giving a ~15 C global mean.

### 4B: Latitude gradient and continentality
Temperature varies as cos²(latitude) around the global mean, with an equator-pole contrast A (~44 C Earth-like). The area-weighted sphere average of cos²(lat) is exactly 2/3, so the formula is zero-sum: the global mean is preserved by construction.

Land/sea contrast is applied on top using a distance-to-ocean field (multi-source BFS from every below-sea-level tile). Two corrections:
- Mean temperature: interiors warm slightly, coasts cool slightly, by an amount proportional to how far each land tile is from the sea. The nudge is zero-sum over land so the global mean is unchanged.
- Seasonal range: interiors swing far more than coasts at the same latitude (maritime vs. continental climate). This is the dominant continentality effect.

Elevation adds a lapse-rate cooling on land above sea level (~6.5 C/km). Below-sea-level depressions don't get the correction.

### 4C: Three-cell wind model with meridional tilt
Three atmospheric circulation cells per hemisphere, with cell boundaries shifted by rotation rate (faster rotation narrows the cells):
- **Hadley cell** (0° to ~30°): Trade winds, blowing westward and toward the equator (equatorward-west in both hemispheres). The ~30° meridional tilt means E-W mountain ranges cast rain shadows — wind approaches at an angle rather than dead-on.
- **Ferrel cell** (~30° to ~60°): Westerlies, blowing eastward and toward the pole.
- **Polar cell** (~60° to 90°): Polar easterlies, same equatorward tilt as the trades.

Wind speed peaks in the middle of each cell and drops near the cell boundaries (doldrums, subtropical high, subpolar low).

**Output**:
- Temperature map (annual mean and seasonal half-amplitude per tile)
- Wind direction and speed per tile

**Visual Result**: Temperature gradient from equator to poles, modified by elevation and land/sea position; wind arrows showing the three-cell structure with meridional tilt

**Gameplay Impact**:
- Wind direction determines which side of a mountain range is wet vs. dry
- Continental interiors are colder in winter, hotter in summer than coasts
- Elevation creates cold highland plateaus even at low latitudes

**Design Notes**:
- Faster rotation: narrower cells, winds stay more strictly zonal
- Slow rotation: wider cells, weather reaches higher latitudes
- The distance-to-ocean field is shared with PrecipitationStage so the two stages can never silently drift apart

## Phase 5: Precipitation & River Formation

**Purpose**: Simulate rainfall by advecting moisture parcels downwind, producing rain shadows behind mountain belts and a smooth wet-coast-to-dry-interior gradient

**Conceptual Process**:

### 5A: Base moisture per tile
A per-tile base precipitation is computed from three inputs: the latitude band (ITCZ near the equator peaks at ~2000 mm/yr, the subtropical highs dip to ~200, the midlatitudes recover to ~750, then decline to the poles), temperature-driven evaporation, and a seeded noise factor. The latitude band boundaries track the circulation cell edges from Phase 4 so precipitation bands stay physically coupled to winds.

### 5B: Downwind-ordered moisture sweep
Moisture advects downwind in a single sweep rather than a fixed-hop march. The key challenge is ordering: each tile must be processed after the tile upwind of it so the carried moisture is finalized before being read.

Each tile's upwind neighbor is the grid neighbor whose direction is most opposite the prevailing wind. These links form a tree (one parent each), so "depth along the upwind chain back to the coast" is a valid topological ordering key — a tile's depth always exceeds its parent's. Sorting by (depth, tile ID) gives a total order that is bit-identical at any thread count.

In sweep order, a moisture parcel M (normalized: 1.0 = fully saturated) is pulled downwind:
- **Ocean tile**: parcel recharges to a charge level set by sea-surface temperature; precipitation is the base ocean source term.
- **Land tile, in order**:
  1. Surface re-evaporation lifts M toward a latitude-dependent plateau cap, so flat interiors plateau at steppe/forest rather than collapsing to absolute desert (Congo/Amazon interiors stay forested because their local convective rainfall charges the surface even without direct ocean advection).
  2. Continentality loss: M drops by a fixed amount per km traveled inland, giving the smooth wet-coast-to-dry-interior gradient.
  3. Rain falls proportional to current M, with a windward orographic boost as the parcel climbs above its last-descended low.
  4. Orographic depletion: moisture is depleted only when the parcel climbs to a *new peak height*. Bumps below the running peak cost nothing; the total depletion over a windward face telescopes to (peak − base) regardless of how finely the slope is tiled, so rain shadows are resolution-independent and scale to any belt width.
  5. Once the parcel descends far enough past the peak into lowland, the peak resets so a second belt can cast its own shadow.

This approach produces rain shadows that are as wide as the mountain belt and survive even with 200-km ranges: the lee stays dry across the full downwind side.

### 5C: River formation
Downhill pointers are assigned per tile (the strictly lower neighbor, ties broken by tile ID). Flow accumulation is then computed in a single serial pass over land tiles sorted by decreasing elevation, so each tile's contribution flows into its downhill target before that target is processed. Tiles seeded with precipitation/1000 produce larger rivers in wet regions.

**Output**:
- Precipitation map (mm/yr per tile)
- Downhill pointers and flow accumulation (river network skeleton)

**Visual Result**: Wet windward coasts, dry lee sides behind mountain belts, broad interior drying gradient; river trunks visible in flow-accumulation maps

**Gameplay Impact**:
- Rain shadows create the most geographically interesting biome transitions (rainforest → mountain → desert in a compact strip)
- Continental interiors are substantially drier than coasts at the same latitude
- Rivers concentrate where precipitation and topography funnel water together

**Design Notes**:
- The upwind-chain-depth ordering trick is what makes the sweep resolution-independent; projecting tile positions onto the wind vector (the naive alternative) is geometrically wrong on a sphere because the wind is tangent, giving ~0 projection everywhere
- The latitude-dependent surface-recharge cap is what keeps equatorial interiors forested; without it the advection model would dry out the Congo/Amazon analogs the same way it dries the Sahara

## Phase 6: Ocean & Sea Formation

**Purpose**: Set sea level so the ocean fraction matches the player's water-amount parameter, and mark coastal geometry

**Conceptual Process**:
- Sea level is set by histogram quantile so the ocean fraction matches the player's water-amount parameter (±2%). Terrain Phase 3 already produces the bimodal hypsometry (abyssal ~-5500 m, land platform ~+400 m), so the sea level quantile falls cleanly in the gap between the two modes.
- The continental shelf profile, already embedded in the terrain by Phase 3, gives a shallow submerged rim around each continent. The shelf rises gently from the shelf break (~-140 m) across ~200 km to the inner edge, then blends into the platform. This puts roughly 5-7% of continental crust below sea level as shallow ocean — consistent with Earth's passive-margin shelves and visible as light-blue coastal rims in ocean maps.
- Coastlines, ocean basins, and seas are identified by flood-fill from the set sea level. Islands are land tiles entirely surrounded by ocean tiles.

**Output**:
- Sea level value
- Ocean/lake flags per tile
- Water depth (elevation relative to sea level, for below-sea tiles)
- Coastline positions

**Visual Result**: Blue oceans with a visible shallow coastal rim; deep abyssal plains much darker than the shelf shallows; islands small and distinct

**Gameplay Impact**:
- Water percentage determines land area available for colonization
- Shallow shelves are strategic (ports, fishing, coastal resources)
- Islands create isolated challenges
- Water depth varies systematically with seafloor age (ridges shallower than abyssal plains)

**Design Notes**:
- 0-30% water: Desert world, small isolated oceans
- 40-70% water: Earth-like, balanced continents and oceans
- 80-100% water: Ocean world, scattered islands
- Many plates + high water = archipelago world (volcanic island chains)
- The shelf width (~200 km) is a terrain constant, not a sea-level parameter; changing water-amount shifts which parts of the shelf are submerged rather than changing the shelf geometry

## Phase 7: Biome Generation

**Purpose**: Assign ecosystem types from the temperature and precipitation fields produced by Phases 4-5, with elevation zonation layered on top

**Conceptual Process**:

Each tile gets one biome. The decision runs top-down through these layers (first match wins):

**Water tiles**: ocean or lake from Phase 6.

**Wetlands**: warm, wet tiles with poor drainage (inland sinks with no downhill outlet, or flat low terrain where water pools). River trunks are excluded — high flow-accumulation means well-drained, not swampy. Tropical wetland above 18 C, temperate wetland otherwise.

**Beach**: any tile with an ocean neighbor, within 50 m of sea level, above freezing.

**Elevation zonation** (meters above sea level):
- Above 3500 m, or above 2500 m with mean annual temperature below -2 C → Alpine Tundra
- 2500-3500 m: wet enough (>250 mm/yr) → Alpine Grassland, else Cold Desert
- 1200-2500 m: Montane Forest if the *tile's own* temperature (>3 C) and precipitation (>400 mm/yr) support it, regardless of what the lowland below it is. A warm, wet mid-elevation slope grows forest even when the lowland is desert or tundra. Below the threshold, the Whittaker matrix applies.

**Whittaker base matrix** (all elevations below 1200 m, or mid-elevation tiles that don't qualify for montane forest):
- Hot (>20 C): tropical rainforest, seasonal forest, savanna, or hot desert depending on precipitation
- Temperate (5-20 C): temperate rainforest, deciduous forest, grassland, xeric shrubland, semi-desert, or desert depending on precipitation and temperature
- Cold (-10 to 5 C): boreal forest if precipitation exceeds 300 mm/yr, cold desert otherwise
- Arctic (≤-10 C): arctic tundra if precipitation exceeds 150 mm/yr, polar desert otherwise

The -10 C arctic/boreal cutoff (rather than -5 C) is what keeps the boreal band from collapsing into tundra at 55°. The 12 C hot-desert floor (rather than 18 C) is what lets subtropical dry interiors — the Sahara and Arabian/Australian analogs — land in hot desert rather than cold desert.

**Output**:
- Single biome type per tile

**Note on transitions**: Ecotones are not stored. They are generated during 2D sampling when tiles near spherical boundaries blend neighboring biomes. See [data-model.md](./data-model.md).

**Visual Result**: Color-coded biomes that track terrain, with biomes bending around mountain ranges (montane-forest flanks, rain-shadow deserts immediately behind belts) rather than running in rigid latitude stripes

**Gameplay Impact**:
- Biomes determine available resources and colonist comfort
- Rain shadows create the most interesting adjacencies (wet windward forest → mountain barrier → dry leeward desert)
- Montane forest flanks give mid-latitude belts a distinctive character
- Arctic tundra confined to genuinely high latitudes; temperate and boreal bands correctly sized

**Design Notes**:
- Biomes emerge from the climate model, not from placed distributions or noise
- The montane-forest decoupling is important: before this change, mid-elevation slopes in cold regions went straight from lowland tundra to alpine rock with no forest belt, which looked wrong and killed forest fraction totals
- Earth-like target fractions (seeds 42, 7, 1337): arctic tundra 8-16%, hot desert 7-10%, total forest 33-40%, no non-ocean biome above 35%

## Phase 8: Snow & Glacier Formation

> **Implemented** (and superseded in detail) by the real cryosphere model — see
> [Cryosphere: Sea Ice, Snow & Glaciers](/docs/technical/cryosphere-ice-and-glaciers.md)
> (PDD surface mass balance, perfect-plastic ice sheets, the ice→climate two-pass
> feedback, and the land/ocean thermal contrast). The conceptual notes below remain
> for design intent.

**Purpose**: Calculate snow coverage and glacier formation based on climate

**Conceptual Process**:

### 8A: Calculate Snow Potential
- For each region, simulate annual temperature cycle
- Determine if temperature drops below freezing
- Calculate snow accumulation vs. melt rate
- Identify regions with persistent snow

**Snow Categories**:
- **Seasonal Snow**: Melts each summer (mid-latitudes, moderate elevation)
- **Permanent Snowfields**: Year-round snow (high latitudes, high elevation)
- **Glaciers**: Compressed ice flows (highest latitudes, highest peaks)

### 8B: Model Glacier Formation
- Glaciers require:
  - Permanent snow accumulation (more snow than melts annually)
  - Sufficient time to form (planet age matters)
  - Appropriate topography (valleys for alpine glaciers, plateaus for ice caps)

- Calculate:
  - Glacier thickness (based on accumulation rate and time)
  - Flow direction (downhill from accumulation zone)
  - Ice cap coverage (polar regions)

**Output**:
- Snow coverage map (seasonal, permanent, glaciers)
- Glacier thickness and flow data
- Polar ice cap extent
- Snowline elevation (height where permanent snow begins)

**Visual Result**: White snow and ice, particularly at poles and high mountains

**Gameplay Impact**:
- Frozen regions are challenging to colonize (extreme cold)
- Glaciers provide water source when melted
- Ice caps affect global climate (reflect sunlight, cool planet)
- Seasonal snow affects agriculture and movement

**Design Notes**:
- Cold star + far orbit = extensive ice caps (ice world)
- Warm star + close orbit = minimal polar ice
- High mountains always snow-capped (elevation trumps latitude)
- Young planets: Less glacier formation (not enough time)
- Old planets: Extensive glaciers if climate is cold

## Phase Dependencies

Each phase depends on previous phases:

```
1. Tectonic History    →  (independent, just parameters)
2. Crust               →  Requires: Tectonic History
3. Terrain             →  Requires: Tectonic History, Crust
4. Atmosphere          →  Requires: Terrain (topography affects winds)
5. Precipitation       →  Requires: Atmosphere, Terrain
6. Oceans              →  Requires: Terrain (fill low areas)
7. Biomes              →  Requires: Terrain, Atmosphere, Precipitation, Oceans
8. Snow/Glaciers       →  Requires: Terrain, Atmosphere, Precipitation, Biomes
```

**Implication**: Generation must be sequential (can't skip ahead)

## Generation Time & Progress

**Expected Duration**: 30 seconds to 2 minutes
- Depends on resolution (more tiles = longer)
- Depends on hardware (faster CPU = quicker)

**Progress Tracking**: Each phase reports progress:
- Phase 1: "Generating tectonic plates... 20%"
- Phase 3: "Creating terrain from plate interactions... 50%"
- Phase 7: "Assigning biomes... 90%"

**Phase Weights** (approximate time):
- Tectonic History (coarse sim): 5%
- Crust Upsampling: 15%
- Terrain: 20%
- Atmosphere: 15%
- Precipitation: 20%
- Oceans: 5%
- Biomes: 15%
- Snow: 5%

## Visual Feedback During Generation

**Progressive Visualization**: Show planet updating as each phase completes
- After Plates: Colored regions (plate map)
- After Terrain: 3D bumpy sphere (mountains visible)
- After Oceans: Blue oceans fill basins
- After Biomes: Color-coded ecosystems
- After Snow: White polar caps

**Why**: Makes generation engaging, educational, and builds anticipation

## Error Handling & Edge Cases

**Impossible Parameters**: Some combinations won't work
- Very low water + very high eccentricity → planet freezes (all water becomes ice)
- Very high water + very few plates → single global ocean (no land)
- Very young planet + high erosion expectation → contradiction

**Handling**:
- Warn player before generation
- Proceed anyway (player's choice)
- May produce extreme but technically valid worlds

**Generation Failures**: Rare, but possible
- Math errors (division by zero, impossible geometry)
- Extreme parameter combinations
- Random seed produces degenerate case

**Handling**:
- Show error message
- Suggest parameter changes
- Offer "regenerate with different seed" option

## Next Steps

- **[biomes.md](./biomes.md)**: Detailed biome classification system
- **[data-model.md](./data-model.md)**: What data is produced and how it's structured
- **[user-experience.md](./user-experience.md)**: How the player experiences generation

## Revision History

- 2025-10-26: Initial generation phases documentation created
- 2026-06-13: Phases 1-3 rewritten to document the as-built tectonic history simulation, crust upsampling, and isostasy/depth-age terrain (PR #136 — tectonic history simulation). Phases 4-8 unchanged.
- 2026-06-14: Phases 4-7 rewritten to document the as-built climate/biome/shelf retune (feature/worldgen-climate-biome). Phase 4: meridional wind (~30° tilt), continentality temperature (zero-sum mean nudge + range boost), shared distance-to-ocean field. Phase 5: downwind-ordered moisture-advection sweep with upwind-chain-depth topological ordering, new-peak orographic ratchet (resolution-independent rain shadows), latitude-dependent surface-recharge cap. Phase 6: piecewise continental shelf profile (flat -120 m shelf → break at -140 m → steep slope). Phase 7: biome rebalance with -10 C arctic floor, 12 C hot-desert floor, montane-forest decoupled from lowland base, Beach cutoff 50 m, flag hygiene (kFlagCoast/kFlagGlacier removed). Phase 8 unchanged.
