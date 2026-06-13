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

**Purpose**: Model global wind patterns based on planetary rotation and temperature

**Conceptual Process**:
- Calculate temperature gradient (hot equator, cold poles)
- Model atmospheric circulation cells (Hadley, Ferrel, Polar)
- Apply Coriolis effect (wind deflection from rotation)
- Modify patterns based on topography (mountains block/redirect wind)

**Atmospheric Cells**:
- **Hadley Cell** (0-30° latitude): Warm air rises at equator, flows poleward, sinks at 30°
- **Ferrel Cell** (30-60° latitude): Mid-latitude circulation
- **Polar Cell** (60-90° latitude): Cold air circulation at poles

**Output**:
- Wind direction map (prevailing winds for each region)
- Wind speed/intensity data
- Air pressure patterns

**Visual Result**: Arrows showing wind direction, potentially heat map of temperature

**Gameplay Impact**:
- Wind patterns determine precipitation (affects biomes)
- Creates rain shadows (dry side of mountains)
- Affects weather intensity in gameplay
- Determines where moisture comes from

**Design Notes**:
- Fast rotation: Stronger Coriolis effect, more distinct wind bands
- Slow rotation: Weaker patterns, simpler circulation
- Mountains disrupt wind flow (create rain shadows, local weather)

## Phase 5: Precipitation & River Formation

**Purpose**: Simulate rainfall and water flow to create rivers and determine moisture distribution

**Conceptual Process**:

### 5A: Calculate Precipitation
- Wind picks up moisture from oceans
- Moisture condenses as rain based on:
  - Temperature (warmer = more evaporation/precipitation)
  - Wind patterns (prevailing winds bring moisture)
  - Elevation (air cools rising over mountains → rain)
  - Distance from water (farther = drier)

- **Rain Shadows**: Dry region on leeward side of mountains
  - Wind rises over mountain (cools, rains)
  - Wind descends other side (warms, no rain → desert)

### 5B: Simulate Water Flow
- Water flows downhill from high elevation to low
- Accumulate flow to form rivers where enough water concentrates
- Rivers cut paths toward oceans or inland seas
- Create lakes in depressions where water accumulates

**Output**:
- Precipitation map (rainfall per region)
- River networks (major rivers and tributaries)
- Lake locations
- Moisture/humidity map

**Visual Result**: Blue lines showing rivers, shaded regions showing wet/dry areas

**Gameplay Impact**:
- Rivers provide water for colonies
- Wet regions support forests, dry regions become deserts
- Rivers are strategic features (defense, trade, agriculture)
- Rain shadows create interesting geography (lush forest → mountain → desert)

**Design Notes**:
- More water = more rainfall = bigger rivers
- Atmospheric strength affects weather intensity
- Old planets: Well-developed river networks (time to form)
- Young planets: Fewer rivers (terrain still rough, rivers just forming)

## Phase 6: Ocean & Sea Formation

**Purpose**: Fill low-lying areas with water based on water percentage parameter

**Conceptual Process**:
- Sort all regions by elevation
- Fill from lowest to highest until water percentage is reached
- Define ocean basins (large connected water bodies)
- Define seas (smaller or partially enclosed)
- Calculate water depth (difference between elevation and water level)
- Define coastlines (boundary between land and water)

**Output**:
- Water level (global "sea level")
- Ocean and sea boundaries
- Water depth map
- Coastline positions
- Island identification (small land completely surrounded by water)

**Visual Result**: Blue oceans and seas, clearly defined coastlines

**Gameplay Impact**:
- Water percentage determines land area available for colonization
- Coastlines are strategic (ports, fishing, defense)
- Islands create isolated challenges
- Water depth affects naval gameplay (if we have that)

**Design Notes**:
- 0-30% water: Desert world, small isolated oceans
- 40-70% water: Earth-like, balanced continents and oceans
- 80-100% water: Ocean world, scattered islands
- Many plates + high water = archipelago world (volcanic island chains)

## Phase 7: Biome Generation

**Purpose**: Assign ecosystem types based on environmental factors

**Conceptual Process**:
- For each region, calculate:
  - Temperature (based on latitude, elevation, proximity to water)
  - Precipitation (from Phase 5)
  - Seasonality (from orbital eccentricity)
  - Soil type (from underlying geology)

- Use **Whittaker diagram** approach:
  - High temp + high precipitation → Tropical Rainforest
  - High temp + low precipitation → Desert
  - Mid temp + mid precipitation → Temperate Forest or Grassland
  - Low temp + low precipitation → Tundra
  - (See [biomes.md](./biomes.md) for complete classification)

- Assign **single definitive biome** to each tile
  - Based purely on local climate conditions
  - No neighbor checking or transition calculation
  - Sharp boundaries between tiles (blending happens later during 2D sampling)

**Output**:
- Biome type for each region (single, definitive)
- Temperature and moisture data
- Vegetation density estimates

**Note on Transitions**: Ecotones (transition zones) are NOT stored in world data. They are generated during 2D sampling when tiles near spherical boundaries blend neighboring biomes. See [data-model.md](./data-model.md) for details.

**Visual Result**: Color-coded biomes (green forests, yellow deserts, white ice, etc.)

**Gameplay Impact**:
- Biomes determine available resources
- Affect colonist comfort and survival
- Create visual variety
- Strategic choices (settle in forest vs. desert)

**Design Notes**:
- Elevation creates biome bands on mountains (forest → alpine → snow)
- Latitude creates climate zones (tropical → temperate → polar)
- Rain shadows create desert next to wet forest
- Biomes emerge from climate, not placed manually
- Each tile gets single biome - simple, fast assignment
- Visual transitions (ecotones) rendered during 2D sampling, not stored
- Most of each spherical tile (99%+) will be pure biome in 2D view

## Phase 8: Snow & Glacier Formation

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
- Tectonic History (coarse sim): 10%
- Crust Upsampling: 15%
- Terrain: 20%
- Atmosphere: 15%
- Precipitation: 20%
- Oceans: 5%
- Biomes: 10%
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
- 2026-06-13: Phases 1-3 rewritten to document the as-built tectonic history simulation, crust upsampling, and isostasy/depth-age terrain (PR #136 — tectonic history simulation). Phase 4-8 unchanged.
