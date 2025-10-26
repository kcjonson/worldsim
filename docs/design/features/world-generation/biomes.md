# Biome Types and Classification

Created: 2025-10-26
Status: Design

## Overview

Biomes are large-scale ecosystem types defined by temperature, precipitation, and other environmental factors. The world generation system assigns biomes based on climatic conditions that emerge from the planet's physical properties, creating scientifically plausible ecosystem distribution.

**Classification Approach**: Modified Whittaker diagram (temperature + precipitation as primary axes, with elevation and latitude as modifiers)

## Biome Classification System

### Primary Factors

**Temperature** (Annual Mean):
- Very Cold: < -5°C (Alpine tundra, polar desert)
- Cold: -5 to 5°C (Tundra, boreal forest)
- Cool: 5 to 15°C (Temperate forests, grasslands)
- Warm: 15 to 25°C (Temperate/subtropical transitions)
- Hot: > 25°C (Tropical forests, hot deserts)

**Precipitation** (Annual Total):
- Very Dry: < 250mm/year (Deserts)
- Dry: 250-500mm/year (Semi-deserts, dry grasslands)
- Moderate: 500-1000mm/year (Grasslands, woodlands)
- Wet: 1000-2000mm/year (Forests)
- Very Wet: > 2000mm/year (Rainforests, wetlands)

### Modifying Factors

**Elevation**:
- Creates vertical biome zones on mountains
- Higher elevation = colder (lapse rate ~6.5°C per 1000m)
- Affects precipitation (orographic lift)

**Latitude**:
- Determines base temperature (equator hot, poles cold)
- Affects seasonality (mid-latitudes have strong seasons)
- Influences day length variation

**Seasonality**:
- Temperature variation (moderate vs. extreme seasons)
- Precipitation timing (wet/dry seasons vs. year-round)

**Soil Type**:
- Derived from underlying geology and erosion
- Affects vegetation types and density

## Terrestrial Biome Types

### Forest Biomes

#### 1. Tropical Rainforest
**Temperature**: > 20°C year-round
**Precipitation**: > 2000mm/year, consistent year-round
**Characteristics**:
- Highest biodiversity of any biome
- Multi-layered canopy (emergent, canopy, understory, floor)
- Year-round growing season
- High humidity

**Locations**:
- Equatorial lowlands (0-10° latitude)
- Windward side of mountains in tropics
- Coastal areas with warm ocean currents

**Gameplay Implications**:
- Dense vegetation (movement obstacles)
- Abundant plant resources
- High disease/pest pressure
- Difficult building (must clear dense jungle)
- Heavy rainfall (flooding potential)

---

#### 2. Tropical Seasonal Forest (Monsoon Forest)
**Temperature**: > 20°C year-round
**Precipitation**: 1000-2000mm/year, distinct wet/dry seasons
**Characteristics**:
- Deciduous or semi-deciduous trees (shed leaves in dry season)
- Less dense than rainforest
- Pronounced seasonal changes

**Locations**:
- Tropical regions with monsoon patterns
- 10-25° latitude
- Areas affected by seasonal wind shifts

**Gameplay Implications**:
- Resource availability varies by season
- Dry season makes movement easier
- Wet season brings floods
- Agriculture possible (seasonal crops)

---

#### 3. Temperate Deciduous Forest
**Temperature**: 5-20°C, distinct seasons
**Precipitation**: 750-1500mm/year, evenly distributed
**Characteristics**:
- Broad-leaved trees (oak, maple, beech)
- Leaves change color and fall in autumn
- Cold winters (trees dormant), warm summers

**Locations**:
- Mid-latitudes (30-50°)
- Eastern sides of continents
- Moderate climates

**Gameplay Implications**:
- Four distinct seasons (affects gameplay rhythm)
- Rich soil (good for agriculture)
- Moderate resource availability
- Winter survival challenges
- Beautiful seasonal visual changes

---

#### 4. Temperate Rainforest
**Temperature**: 5-15°C, mild year-round
**Precipitation**: > 1400mm/year
**Characteristics**:
- Dominated by conifers (evergreens)
- Mosses, ferns, dense understory
- High humidity, frequent fog
- Coastal locations

**Locations**:
- Western coasts at mid-latitudes (40-60°)
- Facing prevailing westerly winds
- Ocean moderated climate

**Gameplay Implications**:
- Dense vegetation (like tropical rainforest)
- Mild climate (easier survival)
- Abundant water
- Good timber resources
- Persistent moisture (building challenges)

---

#### 5. Boreal Forest (Taiga)
**Temperature**: -5 to 5°C, long cold winters
**Precipitation**: 300-850mm/year, mostly summer
**Characteristics**:
- Dominated by conifers (spruce, fir, pine)
- Short growing season
- Permafrost in northern areas
- Slow tree growth

**Locations**:
- High latitudes (50-70° North, primarily Northern Hemisphere)
- Sub-arctic regions
- Between tundra and temperate forests

**Gameplay Implications**:
- Long harsh winters (survival challenge)
- Short growing season (limited agriculture)
- Timber resources (but slow regrowth)
- Low biodiversity (fewer resource types)
- Cold climate penalties

---

#### 6. Montane Forest
**Temperature**: Varies with elevation
**Precipitation**: Varies with elevation and aspect (slope direction)
**Characteristics**:
- Vertical zonation (different forests at different elevations)
- Cooler than lowland forests at same latitude
- Often transitions to alpine meadows above treeline

**Locations**:
- Mountain slopes
- Elevation determines type (subtropical → temperate → boreal)

**Gameplay Implications**:
- Slopes affect building and movement
- Varied resources at different elevations
- Cooler climate than surroundings
- Strategic defensive positions

---

### Grassland Biomes

#### 7. Tropical Savanna
**Temperature**: > 20°C year-round
**Precipitation**: 500-1300mm/year, pronounced wet/dry seasons
**Characteristics**:
- Grasses dominate, scattered trees
- Fires common in dry season
- Large grazing animals (not modeled initially)

**Locations**:
- Tropical latitudes (15-30°)
- Between rainforests and deserts
- Seasonal rainfall patterns

**Gameplay Implications**:
- Open terrain (easy movement)
- Grazing animals (food source)
- Dry season fire risk
- Seasonal agriculture possible
- Limited timber

---

#### 8. Temperate Grassland (Prairie/Steppe)
**Temperature**: 0-20°C, hot summers, cold winters
**Precipitation**: 250-750mm/year
**Characteristics**:
- Dominated by grasses (few or no trees)
- Deep fertile soil
- Periodic fires
- Strong seasonal variation

**Locations**:
- Interior continents at mid-latitudes
- Rain shadows or far from oceans
- Between forests and deserts

**Gameplay Implications**:
- Excellent agriculture (rich soil)
- Open terrain (strategic considerations)
- Extreme weather (hot/cold)
- Limited building materials (no trees)
- High winds

---

#### 9. Alpine Grassland (Alpine Meadow)
**Temperature**: < 10°C, cool year-round
**Precipitation**: Varies
**Characteristics**:
- Above treeline on mountains
- Short growing season
- Grasses, wildflowers, low shrubs
- Harsh conditions

**Locations**:
- High mountains above ~3000m elevation
- Between montane forests and permanent snow

**Gameplay Implications**:
- Difficult access (steep slopes)
- Cold climate
- Limited resources
- Strategic observation points
- Grazing potential

---

### Desert and Xeric Biomes

#### 10. Hot Desert
**Temperature**: > 18°C annual average, extreme daily variation
**Precipitation**: < 250mm/year
**Characteristics**:
- Sparse vegetation
- Extreme temperature swings (hot days, cold nights)
- Specialized drought-adapted plants

**Locations**:
- 20-30° latitude (subtropical high pressure)
- Rain shadows behind mountains
- Far from moisture sources

**Gameplay Implications**:
- Water scarcity (critical challenge)
- Extreme temperatures (survival difficulty)
- Limited resources
- Open visibility (strategic)
- Sandstorms

---

#### 11. Cold Desert
**Temperature**: < 18°C annual average
**Precipitation**: < 250mm/year
**Characteristics**:
- Cold winters, variable summers
- Sparse vegetation adapted to cold and drought
- Often sandy or rocky

**Locations**:
- Interior continents at mid-latitudes
- Rain shadows in temperate zones
- High elevation plateaus

**Gameplay Implications**:
- Combined challenges (cold AND dry)
- Very limited resources
- Extreme difficulty survival
- Strategic value (empty buffer zones)

---

#### 12. Semi-Desert (Semi-Arid)
**Temperature**: Variable
**Precipitation**: 250-500mm/year
**Characteristics**:
- More vegetation than true deserts
- Shrubs, scattered grasses
- Transitional between desert and grassland

**Locations**:
- Fringes of deserts
- Rain shadows with some moisture
- Degraded grasslands

**Gameplay Implications**:
- Marginal habitability
- Limited but present resources
- Grazing potential
- Agriculture difficult but possible

---

#### 13. Xeric Shrubland (Mediterranean Scrub)
**Temperature**: 10-20°C, hot dry summers, mild wet winters
**Precipitation**: 250-600mm/year, winter rainfall
**Characteristics**:
- Drought-resistant shrubs
- Mediterranean climate pattern
- Chaparral, maquis, fynbos types

**Locations**:
- Western coasts at 30-40° latitude
- Mediterranean climate zones

**Gameplay Implications**:
- Fire-prone (summer droughts)
- Moderate habitability
- Unique resources (herbs, olives, etc.)
- Beautiful landscapes

---

### Tundra and Cold Biomes

#### 14. Arctic Tundra
**Temperature**: Annual average < 0°C
**Precipitation**: 150-250mm/year
**Characteristics**:
- No trees (permafrost prevents deep roots)
- Mosses, lichens, low shrubs
- Permafrost layer
- Short summer growing season

**Locations**:
- High latitudes (60-75°N/S)
- Arctic and sub-Arctic regions

**Gameplay Implications**:
- Extreme cold (survival challenge)
- Permafrost (building challenges)
- Very limited resources
- Short summer (brief agriculture window)
- Ice roads in winter

---

#### 15. Alpine Tundra
**Temperature**: Cold due to elevation
**Precipitation**: Variable
**Characteristics**:
- Similar to arctic tundra but at high elevation
- No permafrost (no trees due to cold/wind)
- Harsh conditions

**Locations**:
- High mountains above treeline
- Below permanent snow line

**Gameplay Implications**:
- Difficult access (steep slopes + cold)
- Similar to arctic tundra challenges
- Strategic value (observation posts)

---

#### 16. Polar Desert
**Temperature**: Extremely cold (< -20°C average)
**Precipitation**: < 100mm/year (very dry)
**Characteristics**:
- Almost no vegetation
- Permanent ice and snow
- Driest places on Earth (despite ice)

**Locations**:
- Polar ice caps
- High latitude interiors (Antarctica, Greenland)

**Gameplay Implications**:
- Virtually uninhabitable
- Extreme survival challenge
- Scientific outpost potential
- Ice mining?

---

### Wetland Biomes (Terrestrial)

#### 17. Temperate Wetland
**Temperature**: 5-20°C
**Precipitation**: > 750mm/year, saturated soils
**Characteristics**:
- Marshes, swamps, bogs
- Water-tolerant vegetation
- Rich biodiversity

**Locations**:
- River deltas, lake shores
- Low-lying areas with poor drainage
- Coastal zones

**Gameplay Implications**:
- Difficult terrain (movement penalties)
- Disease risk (stagnant water)
- Rich resources (fish, plants)
- Unique building challenges (stilts, drainage)

---

#### 18. Tropical Wetland
**Temperature**: > 20°C
**Precipitation**: > 1500mm/year, saturated soils
**Characteristics**:
- Mangroves (coastal), swamps
- Extremely high biodiversity
- Dense vegetation

**Locations**:
- Tropical river deltas
- Coastal zones in tropics
- Seasonally flooded areas

**Gameplay Implications**:
- Very difficult terrain
- Disease and pest pressure
- Abundant resources (if manageable)
- Coastal defense value (mangroves)

---

## Biome Transition Zones (Ecotones)

**Ecological Reality**: In nature, biomes blend gradually through transition zones called ecotones:

**Forest → Grassland**:
- Woodland (scattered trees)
- Savanna (mixed grass and trees)

**Grassland → Desert**:
- Semi-desert
- Shrubland

**Forest → Tundra**:
- Taiga (sparse boreal forest)
- Forest-tundra (scattered trees)

**Altitude Transitions** (on mountains):
- Lowland forest → Montane forest → Alpine meadow → Tundra → Snow/ice

**World Data Storage**: Spherical tiles store **single definitive biomes** (sharp boundaries)
- Each tile IS one biome type (Forest, Grassland, Desert, etc.)
- No transition percentages stored in world data
- Simple, clean data structure

**2D Rendering**: Blending happens during sampling for 2D gameplay
- When 2D game queries position near spherical tile boundary
- Calculates blend based on distance to boundary (~500m transition width)
- Example: 2D tile 200m from Forest/Grassland boundary → 60% Grassland, 40% Forest
- Uses existing biome influence system to render smooth transitions
- See [data-model.md](./data-model.md) for complete explanation

**Result**: Large pure regions (realistic) with narrow smooth transitions at boundaries

## Biome Distribution Patterns

### Latitude-Based (North-South)

**Equator → Poles** (typical pattern if no mountains):
1. Tropical Rainforest (0-10°)
2. Tropical Seasonal Forest (10-20°)
3. Savanna or Desert (20-30°, depends on moisture)
4. Temperate Forest or Grassland (30-50°)
5. Boreal Forest (50-65°)
6. Tundra (65-75°)
7. Polar Desert (75-90°)

**But Modified By**:
- Mountains (create vertical zones)
- Oceans (coastal areas wetter)
- Rain shadows (deserts on leeward side)
- Continental interiors (drier)

### Elevation-Based (Mountain Zones)

**Sea Level → Peak** (example at 30° latitude):
1. Temperate Forest (0-1000m)
2. Montane Forest (1000-2500m)
3. Alpine Meadow (2500-3500m)
4. Alpine Tundra (3500-4500m)
5. Permanent Snow (4500m+)

**Elevation Lapse**: ~6.5°C cooler per 1000m elevation gain

### Continental Position

**West Coast** (at mid-latitudes):
- Temperate Rainforest (prevailing winds from ocean)

**East Coast** (at mid-latitudes):
- Temperate Deciduous Forest (continental climate)

**Interior**:
- Grasslands or deserts (far from moisture)

**Rain Shadow**:
- Desert on leeward side of mountains (wind drops moisture on windward side)

## Biome Data in Generation

For each spherical tile, the generation system stores:

**Biome Type**: Single, definitive ecosystem type (no transitions)

**Environmental Data**:
- Temperature (annual mean, seasonal variation)
- Precipitation (annual total, seasonal pattern)
- Elevation
- Moisture/humidity
- Vegetation density (0-1)

**Example Spherical Tile Data**:
```
Tile #52341:
- Location: 35°N, 120°W
- Elevation: 450m
- Temperature: 18°C average (8°C winter, 28°C summer)
- Precipitation: 600mm/year (winter rainfall)
- Biome: Xeric Shrubland  ← Single definitive biome
- Vegetation Density: 0.4
```

**Note**: No secondary biome or transition percentages stored. The tile IS Xeric Shrubland. When the 2D game samples near this tile's boundaries, it may blend with neighboring tiles (e.g., if neighbor is Temperate Grassland), but that blending happens during sampling, not in this stored data.

## Visual Representation

**Color Schemes** (for biome visualization mode):
- Tropical Rainforest: Deep green
- Deciduous Forest: Medium green
- Boreal Forest: Dark blue-green
- Savanna: Yellow-green
- Grassland: Golden yellow
- Desert: Tan/beige
- Tundra: Gray-green
- Ice/Snow: White
- Ocean: Blue (dark = deep, light = shallow)

**Transitions**: Blend colors smoothly between biomes

## Gameplay Impact Summary

| Biome Type | Habitability | Resources | Challenges | Strategic Value |
|------------|--------------|-----------|------------|-----------------|
| Tropical Rainforest | Medium | Abundant | Dense jungle, disease | Hidden colonies |
| Temperate Forest | High | Good | Seasonal | Balanced, versatile |
| Boreal Forest | Low | Timber only | Extreme cold | Frontier, isolation |
| Savanna | Medium | Moderate | Seasonal, fire | Open combat |
| Grassland | High | Agriculture | Weather extremes | Farming, trade |
| Desert | Very Low | Scarce | Water, temperature | Buffer zones |
| Tundra | Very Low | Minimal | Extreme cold | Isolation |
| Wetland | Low | Rich but hard to access | Disease, movement | Defensible, unique |

## Next Steps

- **[data-model.md](./data-model.md)**: How biome data is stored and accessed
- **[user-experience.md](./user-experience.md)**: How biomes are visualized in world gen UI

## Related Documentation

**Visual Appearance in 2D Game**:
- [Game View Overview](../game-view/README.md) - How 2D rendering works
- [biome-ground-covers.md](../game-view/biome-ground-covers.md) - How biomes appear visually
- [biome-influence-system.md](../game-view/biome-influence-system.md) - Blending biomes in tiles
- [tile-transitions.md](../game-view/tile-transitions.md) - Visual transitions

## Revision History

- 2025-10-26: Initial biome classification documentation created
