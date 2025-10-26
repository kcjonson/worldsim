# Planet Parameters

Created: 2025-10-26
Status: Design

## Overview

Planet generation is controlled by a set of input parameters that define the star system, planet properties, orbital mechanics, and generation settings. These parameters work together to create scientifically plausible worlds with unique characteristics.

**Philosophy**: Parameters control the "laws of physics" for a world. The specific terrain, biomes, and features **emerge** from these fundamental properties.

## Parameter Categories

Parameters are organized into four categories:

1. **Star Properties** - The star the planet orbits
2. **Planet Properties** - Physical characteristics of the planet
3. **Orbital Parameters** - The planet's orbit around its star
4. **Generator Properties** - Technical generation settings

## 1. Star Properties

The star determines radiation, energy, and the habitable zone. Different star types create different planetary environments.

### Star Mass

**Range**: 0.1 - 50 solar masses
**Default**: 1.0 (Sun-like star)

**Effect on World**:
- **Higher mass**: Stronger radiation, shorter habitable zone, more extreme day/night
- **Lower mass**: Weaker radiation, longer orbital periods, dimmer light

**Gameplay Impact**:
- Affects temperature extremes
- Influences day/night cycle intensity
- Changes solar power potential

**Examples**:
- 0.5 solar masses: Red dwarf star (dim, long years)
- 1.0 solar masses: Sun-like star (Earth-like conditions)
- 2.0 solar masses: Bright blue star (intense radiation)

### Star Radius

**Range**: 0.1 - 1000 solar radii
**Default**: 1.0 (Sun-like star)

**Effect on World**:
- Determines apparent size in sky
- Influences total radiation received
- Affects visual appearance (huge red giant vs. small white dwarf)

**Gameplay Impact**:
- Visual atmosphere (giant sun dominating sky vs. small bright point)
- Affects tidal heating (if very large)

### Star Temperature

**Range**: 2000K - 50000K
**Default**: 5778K (Sun-like)

**Effect on World**:
- Determines light spectrum (red, yellow, blue-white)
- Affects photosynthesis potential
- Influences atmospheric heating patterns

**Gameplay Impact**:
- Changes visual appearance (red light vs. blue light)
- Affects plant growth if we model that
- Changes temperature baseline

### Star Age

**Range**: 1 million - 10 billion years
**Default**: 4.6 billion years (Sun's age)

**Effect on World**:
- Younger stars more variable and unstable
- Older stars more stable but approaching end of life
- Affects radiation patterns

**Gameplay Impact**:
- Stability of climate (young = more variable)
- Background lore (ancient star vs. young stellar nursery)

## 2. Planet Properties

Physical characteristics that define the planet itself.

### Planet Radius

**Range**: 0.1 - 10 Earth radii
**Default**: 1.0 (Earth-sized)

**Effect on World**:
- Determines surface area
- Affects gravity (with mass)
- Influences atmospheric retention

**Gameplay Impact**:
- Larger radius = more surface to explore
- Affects movement speed (gravity)
- Changes atmosphere density

**Examples**:
- 0.5 Earth radii: Small rocky world (like Mars)
- 1.0 Earth radii: Earth-sized
- 3.0 Earth radii: Super-Earth (high gravity)

### Planet Mass

**Range**: 0.1 - 10 Earth masses
**Default**: 1.0 (Earth mass)

**Effect on World**:
- Determines gravity (with radius)
- Affects atmospheric retention
- Influences geological activity (heavier = more internal heat)

**Gameplay Impact**:
- High gravity: Harder movement, thicker atmosphere, more volcanic activity
- Low gravity: Easier movement, thinner atmosphere, less geological activity

**Gravity Calculation**: g = M/R² (mass/radius²)

### Rotation Rate

**Range**: 0.1 - 100 Earth days per rotation
**Default**: 1.0 (24-hour day)

**Effect on World**:
- Determines day/night cycle length
- Affects Coriolis effect (wind patterns)
- Influences atmospheric circulation

**Gameplay Impact**:
- Slow rotation: Extreme day/night temperature swings
- Fast rotation: Moderate temperatures, stronger wind patterns
- Day length affects gameplay rhythm (if we model day/night survival)

**Examples**:
- 0.1 days: Fast rotation (2.4 hour days)
- 1.0 days: Earth-like (24 hour days)
- 100 days: Tidally locked or very slow rotation

### Number of Tectonic Plates

**Range**: 2 - 30 plates
**Default**: 12 (Earth has ~15 major plates)

**Effect on World**:
- **Few plates**: Large continents, fewer mountain ranges, bigger oceans
- **Many plates**: Fragmented landmasses, more mountains, archipelagos

**Gameplay Impact**:
- Determines continent size and shape
- Affects mountain distribution
- Influences exploration (connected continents vs. islands)

**Examples**:
- 4 plates: Pangaea-like supercontinent
- 12 plates: Earth-like continents
- 30 plates: Fragmented archipelago world

### Water Amount

**Range**: 0 - 100% of surface
**Default**: 70% (Earth-like)

**Effect on World**:
- Determines ocean vs. land ratio
- Affects climate (water moderates temperature)
- Influences precipitation and humidity

**Gameplay Impact**:
- 0-20%: Desert world (dry, extreme temperatures)
- 40-70%: Balanced continents and oceans
- 80-100%: Ocean world (scattered islands)

**Examples**:
- 10%: Dune-like desert planet
- 70%: Earth-like
- 90%: Waterworld (Kevin Costner's nightmare)

### Atmosphere Strength

**Range**: 0.1 - 10 Earth atmospheres
**Default**: 1.0 (Earth atmosphere)

**Effect on World**:
- Affects temperature regulation (greenhouse effect)
- Influences weather intensity
- Determines erosion rates

**Gameplay Impact**:
- Thin atmosphere: Temperature extremes, weak weather, less erosion
- Thick atmosphere: Moderate temperatures, intense storms, heavy erosion

**Examples**:
- 0.1 atm: Mars-like (thin, cold)
- 1.0 atm: Earth-like
- 3.0 atm: Venus-like (thick, hot, intense weather)

### Planet Age

**Range**: 10 million - 10 billion years
**Default**: 4.5 billion years (Earth's age)

**Effect on World**:
- **Young planets**: Jagged mountains, volcanic activity, rough terrain
- **Old planets**: Eroded mountains, smooth terrain, stable geology

**Gameplay Impact**:
- Young: Dramatic terrain, more resources from volcanic activity, unstable
- Old: Gentle terrain, deep soil, stable, easy building

**Examples**:
- 100 million years: Newly formed, volcanic, unstable
- 4.5 billion years: Earth-like maturity
- 10 billion years: Ancient, heavily eroded, geologically dead

## 3. Orbital Parameters

The planet's orbit around its star determines seasons, temperature variations, and year length.

### Semi-Major Axis

**Range**: 0.1 - 100 AU (Astronomical Units)
**Default**: 1.0 AU (Earth's distance from Sun)

**Effect on World**:
- Determines average distance from star
- Affects temperature (farther = colder)
- Controls orbital period (farther = longer years)

**Gameplay Impact**:
- Close orbits: Hot, short years
- Far orbits: Cold, long years
- Determines if planet is in habitable zone

**Habitable Zone**: Depends on star mass/temperature
- Sun-like star: ~0.8 - 1.5 AU
- Red dwarf: ~0.1 - 0.3 AU
- Blue giant: ~5 - 20 AU

### Eccentricity

**Range**: 0.0 - 0.95 (0 = perfect circle, higher = more elliptical)
**Default**: 0.017 (Earth's nearly circular orbit)

**Effect on World**:
- **Low eccentricity**: Stable temperature year-round
- **High eccentricity**: Extreme seasonal temperature swings

**Gameplay Impact**:
- 0.0 - 0.1: Minimal seasonal variation (circular orbit)
- 0.3 - 0.5: Significant seasons (hot summers, cold winters)
- 0.7 - 0.9: Extreme seasons (scorching vs. freezing)

**Examples**:
- 0.017: Earth (very circular)
- 0.2: Moderate ellipse (Mars-like)
- 0.6: Highly elliptical (comet-like orbit)

### Derived Orbital Values

These are **calculated automatically** from semi-major axis and eccentricity:

**Periapsis** (closest approach): `a × (1 - e)`
- Determines maximum temperature

**Apoapsis** (farthest distance): `a × (1 + e)`
- Determines minimum temperature

**Orbital Period** (year length): Kepler's third law `T² = (4π²/GM) × a³`
- Determines seasonal cycle duration

**Example**:
- Semi-major axis: 1.0 AU
- Eccentricity: 0.3
- Periapsis: 0.7 AU (hot season)
- Apoapsis: 1.3 AU (cold season)
- Orbital period: ~1 Earth year

## 4. Generator Properties

Technical settings that control generation detail and reproducibility.

### Resolution

**Range**: 100 - 10,000,000 tiles
**Default**: 100,000 tiles

**Effect on Generation**:
- Determines detail level of the world
- Higher = more detailed terrain, longer generation time
- Affects memory usage and 2D sampling performance

**Gameplay Impact**:
- Low resolution: Faster generation, chunkier terrain
- High resolution: Slower generation, smooth detailed terrain

**Recommended**:
- Quick test: 10,000 tiles
- Normal play: 100,000 tiles
- High detail: 1,000,000+ tiles

### Seed

**Range**: 32 or 64-bit integer
**Default**: Random

**Effect on Generation**:
- Ensures reproducibility
- Same seed + same parameters = identical world
- Different seeds = completely different worlds

**Gameplay Impact**:
- Players can share interesting seeds
- Can regenerate exact same world
- Essential for multiplayer (same world for all players)

**Usage**:
- Leave blank for random
- Enter specific number to recreate a world
- Share seeds in community

## Parameter Presets

To help players get started, provide preset parameter sets:

### "Earth-Like" (Default)
- Sun-like star (1.0 solar masses, 5778K)
- Earth-sized planet (1.0 radius, 1.0 mass)
- 24-hour rotation
- 12 tectonic plates
- 70% water
- 1.0 atmosphere
- 4.5 billion years old
- 1.0 AU, 0.017 eccentricity

**Result**: Balanced, familiar, good for first playthrough

### "Desert World"
- Sun-like star
- Smaller planet (0.7 radius, 0.5 mass)
- 1.2 AU (warmer)
- 15% water
- 0.5 atmosphere (thin)
- 3 billion years old

**Result**: Dune-like, challenging, resource-scarce

### "Ocean World"
- Sun-like star
- Larger planet (1.2 radius, 1.5 mass)
- 1.0 AU
- 90% water
- 1.2 atmosphere
- 30 tectonic plates (volcanic islands)

**Result**: Archipelago, seafaring culture, isolated colonies

### "Frozen World"
- Red dwarf star (0.3 solar masses)
- Small planet (0.8 radius, 0.6 mass)
- 1.5 AU (far from dim star)
- 40% water (mostly ice)
- 0.7 atmosphere
- 6 billion years old

**Result**: Ice caps, tundra, harsh survival

### "Volcanic World"
- Bright star (1.5 solar masses)
- Young planet (200 million years)
- 0.8 AU (close, hot)
- 30% water
- 1.5 atmosphere (thick, greenhouse effect)
- 20 tectonic plates (many boundaries)

**Result**: Active geology, mountains, lava, unstable

### "Ancient Garden"
- Old star (8 billion years)
- Old planet (8 billion years)
- Perfect orbit (0.0 eccentricity)
- 60% water
- 6 tectonic plates (large stable continents)
- 1.0 atmosphere

**Result**: Gentle terrain, stable climate, easy building

## Parameter Interactions

Parameters don't work in isolation - they interact to create emergent characteristics:

### Temperature
Determined by:
- Star temperature (baseline)
- Orbital distance (closer = hotter)
- Eccentricity (variation)
- Atmosphere strength (greenhouse/insulation)
- Rotation rate (day/night extremes)

### Weather Intensity
Determined by:
- Rotation rate (Coriolis effect)
- Atmosphere strength (more air = stronger storms)
- Water amount (evaporation and precipitation)
- Temperature (drives convection)

### Terrain Roughness
Determined by:
- Planet age (young = rough, old = smooth)
- Tectonic plates (more plates = more mountains)
- Water amount (erosion)
- Atmosphere strength (weathering)

### Habitability
Determined by:
- Temperature (livable range)
- Water availability (need some)
- Atmosphere (breathable if needed, protection from radiation)
- Stability (extreme eccentricity or young age = unstable)

## UI/UX Considerations

### Parameter Presentation

**Grouping**: Present parameters in logical groups (star, planet, orbit, generator)

**Tooltips**: Explain each parameter's effect in simple terms

**Warnings**: Indicate extreme/unusual values
- "Warning: High eccentricity will cause extreme seasons"
- "Warning: Low water may make world nearly uninhabitable"

**Visual Feedback**: Show real-time preview as parameters change (if feasible)

### Presets and Randomization

**Presets**: Dropdown with named presets (Earth-Like, Desert World, etc.)

**Randomize Button**: Generate random but plausible parameter set
- Ensures parameters are in reasonable ranges
- Avoids impossible combinations (hot ice world)

**Advanced Mode**: Hide complex parameters (star properties, orbital details) for beginners
- Simple mode: Just planet size, water, plates, age
- Advanced mode: Full control

## Validation and Constraints

Some parameter combinations are impossible or nonsensical:

**Physical Constraints**:
- Planet can't be more massive than its star
- Orbital period must match Kepler's laws
- Habitable zone depends on star properties

**Gameplay Constraints**:
- Very low resolution (<100 tiles) won't be playable
- Extreme values may cause generation to fail or produce uninteresting worlds

**Automatic Adjustments**:
- If parameters are invalid, suggest corrections
- Clamp values to valid ranges
- Warn if world will be extreme/unusual

## Next Steps

- **[generation-phases.md](./generation-phases.md)**: How these parameters drive world generation
- **[data-model.md](./data-model.md)**: What data the generation produces

## Revision History

- 2025-10-26: Initial parameter documentation created
