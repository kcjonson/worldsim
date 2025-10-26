# World Data Model

Created: 2025-10-26
Status: Design

## Overview

The world generation system produces a **spherical planet data model** that serves as the "source of truth" for all terrain, climate, and biome information. The 2D gameplay samples this model to provide infinite, consistent terrain as the player explores.

**Key Architectural Decision**: Spherical tiles store **single definitive biomes** (sharp boundaries). Blending happens during 2D sampling, not in the world data. This makes world generation simpler, data smaller, and sampling faster.

This document describes **what data is produced** (conceptually) and **how the 2D game uses it**, without specifying implementation details.

## Core Concept

**The Spherical World as Source of Truth**:
- World generation creates a complete 3D sphere subdivided into tiles
- Each tile has properties (elevation, biome, climate, etc.)
- Each tile stores ONE definitive biome (no transition data)
- The 2D game "samples" this sphere at specific coordinates
- Blending between biomes happens during 2D sampling at tile boundaries
- Walking in 2D = moving along the sphere's surface
- The world wraps (walk far enough east → return to starting point)

**Analogy**: The spherical world is like a globe with colored regions. The 2D game is like looking through a magnifying glass that smoothly blends colors where regions meet.

## Spherical World Structure

### Sphere Subdivision

**Conceptual Model**: The sphere is divided into many small regions (tiles)

**Properties**:
- Each tile has a position on the sphere (latitude/longitude)
- Tiles form a mesh covering the entire planet surface
- Tiles are roughly equal in area (not perfectly geometric)
- Higher resolution = more tiles = finer detail

**Example Resolutions**:
- Low: 10,000 tiles (~100km per tile on Earth-sized planet)
- Medium: 100,000 tiles (~30km per tile)
- High: 1,000,000 tiles (~10km per tile)

**Scale Perspective**:
- Medium resolution spherical tile: ~30km across
- 2D gameplay tile: ~10m across
- **One spherical tile contains ~9 million 2D tiles!**

**Technical Note**: Actual implementation may use geodesic subdivision (icosahedron → subdivide → project to sphere) or other methods. From game design perspective, what matters is: "The sphere is divided into many tiles."

### Tile as Fundamental Unit

Each tile on the sphere represents a region of the planet and stores:

**Geographic Data**:
- Position (latitude, longitude)
- Elevation (height above/below sea level)
- Water depth (if underwater, depth of ocean)

**Climate Data**:
- Temperature (annual mean, seasonal variation)
- Precipitation (annual total, seasonal pattern)
- Wind patterns (prevailing wind direction and speed)
- Moisture/humidity levels

**Ecological Data**:
- **Biome type** (single, definitive - NO transition data)
- Vegetation density

**Geological Data**:
- Parent tectonic plate
- Proximity to plate boundaries
- Soil type (derived from geology)

**Hydrological Data**:
- River presence (is there a river here?)
- River flow direction
- Water accumulation (watershed size)
- Lake presence

**Snow/Ice Data**:
- Seasonal snow (months per year with snow)
- Permanent snow/ice
- Glacier presence and thickness

**Example Tile**:
```
Tile #52341 on sphere:
  Position: 35.2°N, 118.5°W
  Elevation: 420m above sea level
  Temperature: 18°C average (8°C winter, 28°C summer)
  Precipitation: 350mm/year (winter rain, summer dry)
  Biome: Xeric Shrubland  ← Single definitive biome
  Vegetation Density: 0.4
  Tectonic Plate: Plate #4 (Oceanic)
  Plate Boundary Distance: 50km
  River: No
  Snow: None (too warm)
```

**Note**: No "secondary biome" or "transition factor" - the tile IS shrubland. Adjacent tiles might be different biomes, and blending happens when sampling for 2D rendering.

## Global Data Structures

In addition to per-tile data, the world model stores global information:

### Tectonic Plates
- List of all plates
- Plate boundaries
- Plate types (continental vs. oceanic)
- Movement vectors
- Historical collision zones

**Use**: Understanding geological context, resource distribution

### Water Bodies
- List of oceans, seas, lakes
- Size, depth, connectivity
- Shoreline definition

**Use**: Navigation, fishing, strategic planning

### River Networks
- Major rivers and tributaries
- Flow paths (which tiles the river crosses)
- River size (flow rate)
- Source and mouth locations

**Use**: Water access, agriculture, trade routes

### Mountain Ranges
- Named/identified mountain chains
- Peak locations and elevations
- Range extents

**Use**: Strategic features, resources (mining), challenge areas

### Climate Zones
- Identified regions (tropical, temperate, polar)
- Climate zone boundaries

**Use**: High-level understanding of planet structure

## Data Resolution vs. Detail

**Conceptual Trade-off**:
- **Higher resolution** = More tiles = Finer detail = Longer generation + more storage
- **Lower resolution** = Fewer tiles = Coarser detail = Faster generation + less storage

**Implications for 2D Gameplay**:
- High resolution world: More frequent biome changes, finer terrain detail
- Low resolution world: Larger uniform regions, broader features

**Recommended Approach**:
- Generate high-level structure at moderate resolution (100k tiles)
- Use procedural detail generation for fine features in 2D view
  - Example: World tile says "forest", 2D game places individual trees procedurally
  - Example: World tile says "river", 2D game draws meandering river path

**Hybrid Model**:
- World model: Macro-scale (continents, mountains, biomes)
- 2D game: Micro-scale (individual trees, rocks, buildings)
- Both use same seed → consistent detail

## Sampling the Spherical World for 2D Gameplay

### The Sampling Problem

**Challenge**: The spherical world is 3D with sharp biome boundaries, but gameplay needs smooth 2D terrain with natural transitions

**Solution**: Sample the sphere at player's current location and apply blending at tile boundaries

### Coordinate Mapping

**Player Position in 2D** → **Coordinates on Sphere**

**Conceptual Process**:
1. Player has 2D coordinates (x, y) in game world
2. Convert to spherical coordinates (latitude, longitude)
3. Query sphere for tile at that position
4. Retrieve tile data (elevation, biome, etc.)
5. Check proximity to tile boundaries
6. If near boundary, query neighbors and blend
7. Use data to generate 2D terrain appearance

**Example**:
```
Player at 2D position (1000, 500):
  → Maps to spherical coordinates (35.2°N, 118.5°W)
  → Finds Tile #52341 (Xeric Shrubland)
  → Distance to nearest boundary: 12km (far from edge)
  → Return: 100% Xeric Shrubland (no blending needed)
```

### Query Performance: Fast Path vs. Slow Path

**Most Queries Are Trivial** (99%+ of cases):

```
Query position (x, y):
  1. Get spherical tile → Tile #52341 (Xeric Shrubland)
  2. Calculate distance to nearest boundary → 12km
  3. Is distance > blend threshold (e.g., 500m)? YES
  4. Return: 100% Shrubland (FAST PATH - no neighbor queries needed)
```

**Only Boundary Queries Need Blending** (<1% of cases):

```
Query position (x, y):
  1. Get spherical tile → Tile #52341 (Xeric Shrubland)
  2. Calculate distance to nearest boundary → 180m
  3. Is distance > blend threshold (500m)? NO
  4. Query neighboring tiles → Tile #52342 (Temperate Grassland)
  5. Calculate blend based on distance → 70% Shrubland, 30% Grassland
  6. Return: Blended percentages (SLOW PATH - requires neighbor queries)
```

**Performance Implications**:
- **Spherical tile radius**: ~15km
- **Blend distance**: ~500m at boundaries
- **Pure region**: ~99.3% of tile area
- **Blend region**: ~0.7% of tile area
- **Result**: 99%+ of queries take fast path (no neighbor lookups)

### Chunk Loading and Optimization

**2D Game View**: Player sees a limited area (viewport)

**Chunks**: Divide 2D space into manageable regions (e.g., 1km × 1km chunks)

**Chunk Classification**:

**Pure Chunks** (99% of chunks):
- Entire chunk is >1km from any spherical tile boundary
- All 2D tiles in chunk: 100% same biome
- **Optimization**: Render entire chunk with single biome, no per-tile blend calculations

**Boundary Chunks** (1% of chunks):
- Chunk crosses a spherical tile boundary
- Some tiles pure, some blended
- **Requires**: Per-tile blend calculation for tiles near boundary

**Loading Strategy**:
1. Calculate which spherical tiles are visible in current view
2. Load data for those tiles (typically 1-4 tiles for normal viewport)
3. Classify chunks as pure or boundary
4. Generate 2D terrain using fast path for pure chunks, slow path for boundary chunks
5. As player moves, load new chunks, unload distant chunks

**Infinite World**:
- Chunks load on-demand from spherical data
- Walking east/west: Eventually wrap around planet (return to start)
- Walking north/south: Reach poles and can't go farther
- World is finite (planet has limited surface area) but feels infinite

### Biome Blending at Boundaries

**Spherical Data** (sharp boundaries):
```
Tile A: Temperate Forest
Tile B: Temperate Grassland (neighbor to A)
```

**2D Sampling at Boundary**:
```
Position 100m from boundary (inside Tile A):
  → 100% Temperate Forest (pure, fast path)

Position 400m from boundary (inside Tile A):
  → Distance 400m < blend threshold 500m
  → Calculate blend: 80% Forest, 20% Grassland

Position 200m from boundary (inside Tile A):
  → Distance 200m < blend threshold 500m
  → Calculate blend: 60% Forest, 40% Grassland

Position 0m (exactly at boundary):
  → Calculate blend: 50% Forest, 50% Grassland

Position 200m from boundary (inside Tile B):
  → Distance 200m < blend threshold 500m
  → Calculate blend: 40% Forest, 60% Grassland

Position 600m from boundary (inside Tile B):
  → 100% Temperate Grassland (pure, fast path)
```

**Blend Width**: Configurable (e.g., 500m, 1000m)
- Wider blend: More gradual ecotones, softer transitions
- Narrower blend: Sharper boundaries, more distinct regions

**Different Blends for Different Biome Pairs** (future):
- Forest → Grassland: Wide gradual transition (500m)
- Desert → Ocean: Sharp coastline (50m)
- Mountain → Alpine: Elevation-based (varies with slope)

**2D Rendering**:
- Use existing biome influence system to render blended tiles
- Percentages come from sampling calculation
- Gradual ecotone (transition zone) between biomes
- Matches real-world ecology (biomes don't have hard lines)

### Elevation and Terrain

**Spherical Data**: Tile elevation (e.g., 420m)

**2D Rendering**:
- If elevation-based gameplay: Slopes, cliffs, building restrictions
- If visual only: Shading/coloring to indicate height
- Rivers flow downhill (based on elevation data)

**Slope Calculation**: Compare elevation of neighboring tiles
- Flat if similar elevation
- Slope if gradual change
- Cliff if sharp change

**Elevation can also be interpolated at boundaries** (similar to biome blending):
- Smooth elevation transitions between spherical tiles
- Prevents abrupt elevation changes

### Climate and Weather

**Spherical Data**:
- Temperature: 18°C average
- Precipitation: 350mm/year
- Wind: Westerly 15km/h

**2D Gameplay Effects**:
- Temperature affects colonist comfort, crop growth
- Precipitation determines water availability
- Wind affects weather events, windmill efficiency

**Seasonal Variation**:
- Spherical data includes seasonal patterns
- 2D game shows current season (based on in-game date)
- Temperature/precipitation vary seasonally

### Rivers and Water

**Spherical Data**: River network (which tiles have rivers, flow directions)

**2D Rendering**:
- Render river as blue path through tiles
- River width based on flow rate (larger rivers wider)
- Meanders and details added procedurally (consistent with seed)

**Lakes and Oceans**:
- Tiles marked as water (ocean, lake)
- 2D game renders as blue regions
- Coastlines defined by land/water boundary

### Poles and Extreme Latitudes

**Challenge**: Spherical projection to 2D distorts near poles

**Handling**:
- Tiles near poles are smaller in spherical terms (latitude lines converge)
- 2D game may adjust tile size to compensate
- Polar regions playable but may feel different (intentional - they ARE different)

**Example**: Landing at 89°N (near North Pole)
- Tile data: Arctic tundra, -30°C, permanent ice
- 2D game: Renders icy, barren landscape
- If player walks in circle, returns to start quickly (near pole, circumference small)

## World Wrapping

**East-West Wrapping**:
- Walking east continuously → eventually wrap around planet
- Spherical longitude wraps: 179.9°E → 180° → -179.9°W → ... → 0° → back to start
- 2D game: Seamless transition (player doesn't notice teleport)

**North-South Boundaries**:
- Walking north → reach North Pole (90°N), can't go farther
- Walking south → reach South Pole (90°S), can't go farther
- Poles are boundaries, not wrappable

**Implications**:
- Planet feels infinite when traveling along latitude
- Planet has clear "top" and "bottom" (poles)
- Realistic for spherical world

## Data Persistence

**Save Game**: Stores player's position on sphere (lat/lon) and game state

**World Data**: Stored separately from save game
- Can generate new colony on same world (different landing site)
- Can share worlds between players (send world file or seed+parameters)

**Regeneration**: Same seed + same parameters = identical world
- Useful for multiplayer (all players generate same world)
- Useful for sharing interesting worlds

## Performance Considerations

**Data Size**:
- 100,000 tiles × ~150 bytes per tile = ~15MB world data (simplified vs. transition data)
- Reasonable for modern systems
- Higher resolution = more data (linear growth)

**Query Speed**:
- Must quickly find tiles at given lat/lon → Spatial indexing (quadtree, grid, etc.)
- Target: < 1ms to query tile data
- **Fast path** (99% of queries): Single tile lookup + distance check
- **Slow path** (1% of queries): Tile lookup + neighbor queries + blend calculation

**Chunk Generation**:
- Convert tile data → 2D terrain representation
- Pure chunks: Very fast (single biome for entire chunk)
- Boundary chunks: Moderate (per-tile blend for edge tiles only)
- Target: < 100ms per chunk
- Can pre-generate nearby chunks (prediction)

**Caching Opportunities**:
```
Cached chunk metadata:
- Source spherical tile ID
- isPure flag (true if >1km from boundaries)
- If pure: render entire chunk with single biome (fast)
- If boundary: perform per-tile blending (slower but rare)
```

## Procedural Detail Generation

**Key Insight**: Spherical world provides macro-scale data, 2D game adds micro-scale detail

**Macro (from spherical world)**:
- This region is "temperate deciduous forest"
- Elevation ~300m
- Moderate precipitation
- No rivers

**Micro (generated in 2D game)**:
- Place individual trees (oak, maple)
- Add rocks, bushes, flowers
- Create terrain noise (small bumps, hollows)
- All using same seed → consistent if revisited

**Benefits**:
- Spherical world stays manageable size
- 2D view has rich detail
- Detail is consistent (seed-based)

**Example**:
```
Spherical tile #52341: "Deciduous forest, 320m elevation"
2D game when player visits:
  - Samples noise at position (using seed + tile ID)
  - Places 50 trees based on noise pattern
  - Adds 20 bushes, 100 flowers
  - Creates small elevation variations (±5m)
  - If player returns later: same trees, same rocks (seed-based)
```

## World Data Access (Conceptual API)

From a game design perspective, the 2D game needs these queries:

**GetTileAtPosition(latitude, longitude)**:
- Returns tile data for given coordinates
- Includes elevation, biome, climate, etc.

**GetTilesInBounds(north, south, east, west)**:
- Returns all tiles within bounding box
- Used for loading visible area

**SampleBiomeAtPosition(lat, lon, blendDistance)**:
- Returns biome percentages (with blending if near boundary)
- **Fast path**: Returns 100% single biome if >blendDistance from edges
- **Slow path**: Queries neighbors and calculates blend
- Primary sampling function for 2D rendering

**GetElevationAtPosition(lat, lon)**:
- Returns interpolated elevation (between tiles if needed)
- Used for smooth terrain

**GetClimateAtPosition(lat, lon, season)**:
- Returns temperature, precipitation for given location and season
- Used for weather and gameplay effects

**GetRiverAtPosition(lat, lon)**:
- Returns river data if present (flow direction, size)
- Used for rendering rivers

**Example Usage**:
```
Player moves to new area:
→ Game calls GetTilesInBounds(visible area)
→ Receives 1-4 spherical tiles
→ For each 2D tile in view:
    → Call SampleBiomeAtPosition(tile center, blend=500m)
    → Receives biome percentages (mostly 100% single biome, fast path)
    → Render using biome influence system
→ Result: Most tiles render instantly (pure), boundary tiles blend smoothly
```

## Example: Walking Across a Continent

**Player Journey**: Walk from coast → mountain → desert (200km)

**Spherical Tiles Crossed**: ~7 tiles (at 30km resolution)

**Tile Sequence** (sharp boundaries in world data):
1. Tile #1001: Coastal, temperate rainforest, 50m elevation, high precipitation
2. Tile #1002: Inland, temperate forest, 150m elevation, moderate precipitation
3. Tile #1003: Foothills, montane forest, 600m elevation, moderate precipitation
4. Tile #1004: Mountain, alpine tundra, 2400m elevation, low temp, snow
5. Tile #1005: Mountain descent, montane forest, 900m elevation (leeward side)
6. Tile #1006: Rain shadow, semi-desert, 400m elevation, low precipitation
7. Tile #1007: Desert, hot desert, 250m elevation, very low precipitation

**2D Gameplay Experience** (smooth transitions via sampling):
- **Km 0-29**: Pure temperate rainforest (Tile #1001, far from boundaries)
- **Km 29-30**: Rainforest → Temperate forest transition (boundary blending)
- **Km 30-59**: Pure temperate forest (Tile #1002)
- **Km 59-60**: Temperate → Montane forest transition
- **Km 60-89**: Pure montane forest (Tile #1003), elevation increasing
- **Km 89-90**: Montane forest → Alpine tundra transition, getting cold
- **Km 90-119**: Pure alpine tundra (Tile #1004), snowy peak
- **Km 119-120**: Alpine → Montane transition, descending
- **Km 120-149**: Pure montane forest (Tile #1005), leeward side
- **Km 149-150**: Montane → Semi-desert transition (rain shadow effect!)
- **Km 150-179**: Pure semi-desert (Tile #1006), getting dry
- **Km 179-180**: Semi-desert → Hot desert transition
- **Km 180-200**: Pure hot desert (Tile #1007)

**Data Driving Experience**:
- Sharp boundaries in spherical data (~1km transitions during sampling)
- 99% of journey is "pure" biome (fast rendering)
- Smooth, realistic transitions at boundaries (blending)
- All consistent with spherical world model
- Player experiences continuous, believable world

## Architectural Benefits Summary

**Compared to storing transitions in spherical data:**

✅ **Simpler World Generation**:
- No transition calculation during generation
- Each tile: "What biome IS here based on climate?" (simple)
- No neighbor checking, no percentage calculation
- Faster generation

✅ **Smaller Data Footprint**:
- ~15MB vs. ~20MB (no transition data stored)
- Simpler data structure
- Easier serialization

✅ **Faster Sampling**:
- 99%+ of queries: Fast path (single tile lookup)
- <1% of queries: Slow path (neighbor queries + blend)
- Average query time: Much faster

✅ **More Realistic**:
- Large pure regions (matches real ecology - forests are mostly forest)
- Narrow ecotones at boundaries (matches real transition zones)
- Scale-appropriate (transitions ~500m, not 30km)

✅ **More Flexible**:
- Adjust blend width without regenerating world
- Different blend distances for different biome pairs
- Different blending algorithms possible
- Can vary by zoom level

✅ **Clean Architecture**:
- World gen: "Define what IS at each location"
- 2D sampler: "How do I present this visually?"
- Clear separation of concerns

## Edge Cases

### Landing Near Pole

**Scenario**: Player lands at 85°N

**Implications**:
- Very small circumference (short distance to wrap around)
- Extreme cold (Arctic climate)
- Polar day/night (sun may not set in summer, not rise in winter - if modeled)

**2D Gameplay**: Works normally, but feels different (intentional)

### Landing on Small Island

**Scenario**: Player lands on 5km diameter island (archipelago world)

**Implications**:
- Limited land area from spherical data
- Ocean in all directions beyond island
- Expansion requires boats (if implemented) or island-hopping

**2D Gameplay**: Island boundaries clear (ocean tiles surround land tiles)

### Crossing the Date Line

**Scenario**: Player crosses 180° longitude (International Date Line equivalent)

**Implications**:
- Longitude wraps: 179.9°E → -179.9°W
- Should be seamless in 2D view

**2D Gameplay**: No visible discontinuity (chunk loading handles wrap)

## Future Enhancements

**Dynamic World Changes**:
- If seasons modeled: Snow coverage changes over time
- If terraforming: Player actions modify spherical data (drain lake, plant forest)
- If climate shifts: Temperature/precipitation change gradually

**Multi-Resolution Data**:
- Store world at multiple resolutions (Level-of-Detail)
- Coarse data for global view, fine data for local play
- Generate fine detail on-demand

**Streaming**:
- For very large worlds, don't load entire sphere into memory
- Stream tiles from disk as needed
- Similar to modern open-world games

**Adaptive Blending**:
- Biome pair-specific blend distances (forest-grass: 500m, desert-ocean: 50m)
- Elevation-influenced blending (steep mountains: sharper transitions)
- Seasonal variation in blend appearance

## Summary

**Key Takeaways**:

1. **Spherical tiles store single definitive biomes** - No transition data in world model
2. **Blending happens during 2D sampling** - At spherical tile boundaries only
3. **Most queries are trivial (99%+)** - Fast path returns pure biome instantly
4. **Boundary queries blend neighbors** - Slow path for <1% of queries
5. **Large pure regions** - Realistic (forests are mostly forest, not constantly transitioning)
6. **Scale-appropriate transitions** - ~500m ecotones at ~30km tile boundaries
7. **Simpler, faster, more flexible** - Than storing transitions in world data

**This architecture enables**:
- Smaller world data (no transition storage)
- Faster generation (no transition calculation)
- Faster sampling (99% fast path)
- More realistic ecology (large pure regions, narrow ecotones)
- Flexible presentation (adjust blending without regenerating)
- Clean separation (world defines truth, 2D renders visuals)

## Related Documentation

**Generation**:
- [generation-phases.md](./generation-phases.md) - How the spherical world is generated

**User Experience**:
- [user-experience.md](./user-experience.md) - How players interact with world generation

**Visual Appearance in 2D**:
- [biome-ground-covers.md](../../biome-ground-covers.md) - How biomes appear in 2D
- [biome-influence-system.md](../../biome-influence-system.md) - Blending biomes at boundaries

## Revision History

- 2025-10-26: Initial data model documentation created
- 2025-10-26: Updated to reflect single-biome architecture with boundary-based blending
