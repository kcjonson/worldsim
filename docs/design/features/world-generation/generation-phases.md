# Generation Phases

Created: 2025-10-26
Status: Design

## Overview

World generation follows a sequential pipeline of **eight conceptual phases**, each building on the previous to create a complete, scientifically plausible planet. This document describes **what each phase accomplishes** from a game design perspective, not the technical implementation.

**Philosophy**: Each phase models a real planetary process (tectonics, atmosphere, hydrology, biology). Features **emerge naturally** from these processes rather than being placed arbitrarily.

## Generation Pipeline

```
Parameters → Plates → Terrain → Climate → Water → Biomes → Complete World
```

Each phase produces data that later phases consume:

1. **Tectonic Plate Generation** - Divide sphere into moving plates
2. **Plate Movement Simulation** - Simulate geological time
3. **Terrain Height Generation** - Create mountains and oceans from plate interactions
4. **Atmospheric Circulation** - Model wind patterns
5. **Precipitation & River Formation** - Simulate rainfall and water flow
6. **Ocean & Sea Formation** - Fill low areas with water
7. **Biome Generation** - Assign ecosystems based on climate
8. **Snow & Glacier Formation** - Calculate ice coverage

## Phase 1: Tectonic Plate Generation

**Purpose**: Divide the spherical planet into tectonic plates (continental and oceanic)

**Conceptual Process**:
- Generate specified number of plate "seed points" evenly distributed across sphere
- Divide sphere into regions (Voronoi diagram) around these points
- Classify each plate as **continental** (higher, thicker crust) or **oceanic** (lower, thinner crust)
- Ensure realistic distribution (Earth: ~30% continental, ~70% oceanic)

**Output**:
- Plate boundaries (where plates meet)
- Plate types (continental or oceanic)
- Base elevation for each plate region

**Visual Result**: Planet divided into colored regions representing different plates

**Gameplay Impact**:
- More plates → fragmented continents, archipelagos
- Fewer plates → large unified landmasses
- Plate boundaries become mountain ranges and ocean trenches

**Design Notes**:
- Plates aren't perfectly geometric - add natural variation to boundaries
- Plate sizes should vary (some large, some small) like real Earth
- Classification affects later terrain generation (continents sit higher than ocean floor)

## Phase 2: Plate Movement Simulation

**Purpose**: Simulate plate tectonics over geological time to determine plate interactions

**Conceptual Process**:
- Assign movement vector to each plate (direction and speed)
- Model rotational movement (plates can spin as they move)
- Ensure global momentum conservation (movement vectors balance out)
- Calculate steady-state positions based on planet age

**Output**:
- Plate movement directions and speeds
- Rotational components for each plate
- Historical context (how plates reached current positions)

**Visual Result**: Arrows showing plate movement directions

**Gameplay Impact**:
- Movement creates plate boundary interactions (collision, spreading, sliding)
- Planet age determines how far plates have moved
- Young planets: plates haven't moved much (less mountain building)
- Old planets: plates have collided and formed major mountain ranges

**Design Notes**:
- We don't literally simulate millions of years - we calculate what the result **would be**
- Movement is abstracted: just enough to determine boundary interactions
- Provides geological context (why are mountains here? Plates collided)

## Phase 3: Terrain Height Generation

**Purpose**: Create realistic terrain elevation based on plate boundaries and types

**Conceptual Process**:

### 3A: Analyze Plate Boundaries
Determine how plates interact at boundaries:

- **Convergent** (collision): Plates moving toward each other
  - Continental + Continental → Mountain ranges (Himalayas)
  - Oceanic + Continental → Subduction, coastal mountains (Andes)
  - Oceanic + Oceanic → Deep ocean trench (Mariana Trench)

- **Divergent** (spreading): Plates moving apart
  - Creates oceanic ridges (Mid-Atlantic Ridge)
  - Forms rift valleys on continents (East African Rift)

- **Transform** (sliding): Plates sliding past each other
  - Creates fault lines (San Andreas Fault)
  - Minor elevation changes, rough terrain

### 3B: Generate Elevation
- Set base elevation for each plate (continental higher, oceanic lower)
- Add elevation at convergent boundaries (mountains)
- Create ridges at divergent boundaries (underwater mountains or rift valleys)
- Add variation to transform boundaries (fault scarps)
- Apply noise for natural variation within each region

**Output**:
- Elevation map for entire planet
- Mountain ranges at plate collision zones
- Ocean ridges at spreading zones
- Variation within plates (not perfectly flat)

**Visual Result**: 3D terrain with visible mountains, ocean basins, highlands, lowlands

**Gameplay Impact**:
- Mountains affect movement, building difficulty, resources
- Elevation determines biomes (higher = colder)
- Creates strategic terrain (chokepoints, defensible positions, fertile valleys)

**Design Notes**:
- Young planets: Sharp, high mountains (recent formation, little erosion)
- Old planets: Smooth, gentle hills (heavily eroded over time)
- Planet age affects terrain roughness dramatically

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
1. Plates              →  (independent, just parameters)
2. Movement            →  Requires: Plates
3. Terrain             →  Requires: Plates, Movement
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
- Plates: 10%
- Movement: 5%
- Terrain: 25%
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
