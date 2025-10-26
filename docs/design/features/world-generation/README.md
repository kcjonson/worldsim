# World Generation System - Overview

Created: 2025-10-26
Status: Design

## Overview

The World Generation System creates procedural 3D spherical planets with realistic geological, atmospheric, and biological features. Players customize planet parameters, observe the generation process from a space view, and then play on the generated world using a 2D tile-based interface that samples the spherical surface.

**Why a 3D Spherical World?**:
- **Realism**: Authentic planetary features (poles, equator, climate zones)
- **Variety**: Every world is unique based on parameters and seed
- **Scientific Grounding**: Based on real geological and atmospheric processes
- **Exploration**: Players discover a believable alien world
- **Replayability**: Infinite variety of planets to colonize

## Core Concept

World-sim generates worlds in **two distinct phases**:

### Phase 1: World Generation (Standalone 3D View)
- Player adjusts planet parameters (size, plates, water, atmosphere, etc.)
- System generates a complete 3D spherical planet
- Player views planet from space with different visualization modes
- Planet displays global features: continents, oceans, mountains, climate zones
- Player can regenerate with different parameters or save when satisfied

### Phase 2: 2D Gameplay (Tile-Based Colony Sim)
- Game transitions to top-down 2D view
- World data is sampled at player's landing coordinates
- Existing tile-based rendering system displays local terrain
- Infinite world loads chunks by sampling the spherical data model
- Player builds colony on the procedurally generated terrain

**Key Insight**: The spherical world is the "source of truth" - the 2D game samples this 3D data model to provide infinite, consistent terrain.

## Generation Process (High-Level)

The world generation follows these conceptual phases:

1. **Tectonic Plate Generation** - Divide sphere into plates (continental/oceanic)
2. **Plate Movement Simulation** - Simulate geological time and plate interactions
3. **Terrain Height Generation** - Create mountains, ocean floors, continents from plate boundaries
4. **Atmospheric Circulation** - Model wind patterns based on rotation and temperature
5. **Precipitation & Rivers** - Simulate rainfall and water flow from high to low elevation
6. **Ocean & Sea Formation** - Fill low areas with water based on water percentage
7. **Biome Generation** - Assign biomes based on temperature, precipitation, elevation
8. **Snow & Glacier Formation** - Calculate permanent snow and glaciers based on climate

Each phase builds on the previous, creating a scientifically plausible world.

## Input & Output

**Inputs**: Planet parameters (star properties, planet size/mass/rotation, number of plates, water amount, atmosphere density, age, seed)

**Output**: Complete spherical planet data model containing:
- Elevation map (terrain height)
- Water bodies (oceans, seas, lakes, rivers)
- Climate data (temperature, precipitation, wind patterns)
- Biome distribution
- Snow and glacier coverage
- Tectonic plate information

## Document Organization

### Game Design Documentation

**This Folder** (`/docs/design/features/world-generation/`):
- **[README.md](./README.md)** (this document) - Overview and index
- **[concept.md](./concept.md)** - Core concept, motivations, and game experience goals
- **[planet-parameters.md](./planet-parameters.md)** - Input parameters (star, planet, orbital, generator)
- **[generation-phases.md](./generation-phases.md)** - Conceptual phases of world generation
- **[biomes.md](./biomes.md)** - Detailed terrestrial biome types and classification
- **[user-experience.md](./user-experience.md)** - Two-phase game flow, UI/UX, visualization modes
- **[data-model.md](./data-model.md)** - Output data structure and how 2D game samples it

### Related Game Design

**Game Overview**:
- [game-overview.md](../../game-overview.md) - Overall game concept and backstory

**2D Game View & Rendering** (how 3D world data is rendered in 2D gameplay):
- [Game View Overview](../game-view/README.md) - Complete 2D rendering system
- [biome-influence-system.md](../game-view/biome-influence-system.md) - Biome blending in tiles
- [biome-ground-covers.md](../game-view/biome-ground-covers.md) - How biomes appear visually in 2D
- [tile-transitions.md](../game-view/tile-transitions.md) - Visual transitions between biomes
- [procedural-variation.md](../game-view/procedural-variation.md) - Variation within biomes

### Technical Documentation

**Technical Implementation** (`/docs/technical/`):
- Technical design documents for world generation will be created during implementation
- See `/docs/technical/INDEX.md` for architecture and implementation details

## Key Design Questions

### Player Experience
**Q**: How much control should players have over generation?
**A**: Full control over physical parameters (size, water, plates), but biomes/terrain emerge naturally from these choices. Players don't directly control "more forests" or "more deserts" - those are consequences of their planet configuration.

**Q**: How long should generation take?
**A**: Target: 30 seconds to 2 minutes depending on resolution. Player watches progress with clear phase indicators. Generation runs asynchronously with real-time progress updates.

**Q**: Can players start playing before generation completes?
**A**: No. Full generation must complete before gameplay begins. The 3D visualization during generation keeps the player engaged.

### World Sampling
**Q**: How does the 2D game sample the 3D sphere?
**A**: The sphere is subdivided into spherical tiles (~5km across). The 2D game loads *chunks* (512×512 gameplay tiles, 512m square) by sampling the spherical data at specific coordinates. Chunks load on-demand as the player explores, creating an infinite-feeling world. See [data-model.md](./data-model.md) for complete sampling details.

**Q**: What happens at the poles?
**A**: Tile projection handles polar distortion. Players can land and play at the poles (arctic/antarctic biomes), but tiles may be smaller/denser. The spherical model remains accurate.

**Q**: Can players travel around the entire planet?
**A**: Yes! Walking east continuously will eventually return to the starting point. The world "wraps" because it's spherical. North/south reach poles and stop.

### Scientific Realism vs Gameplay
**Q**: How realistic should the simulation be?
**A**: **Plausibly realistic**. The system uses real geological/atmospheric principles, but simplified for gameplay and performance. Goal: worlds feel natural and believable, not scientifically accurate simulation.

**Q**: Do we simulate millions of years?
**A**: Conceptually yes - plate movement "simulates" geological time. But this is abstracted: we calculate steady-state results based on planet age, not step-by-step simulation.

## Visual Style

**3D Planet View**: Satellite/space view aesthetic. Clean, educational, visually clear. Not photorealistic - stylized to show features clearly.

**Visualization Modes**:
- **Terrain**: Elevation-based coloring (deep blue ocean → white mountains)
- **Temperature**: Heat map (blue poles → red equator)
- **Precipitation**: Moisture map (brown deserts → blue wet areas)
- **Biomes**: Color-coded biome types (green forests, yellow deserts, etc.)
- **Plates**: Tectonic plate boundaries and types
- **Snow/Ice**: Snow coverage and glaciers

See [user-experience.md](./user-experience.md) for complete UI/UX design.

## Integration with Game Systems

The world generation system interfaces with:

**Game Start Flow**:
1. Main menu → "New Game"
2. World generation UI (adjust parameters, generate, view)
3. Accept world → transition to gameplay
4. 2D game loads, samples world at landing coordinates

**Colony Building**: Buildings and colonists interact with terrain (slopes, water, biomes) sampled from the world model.

**Resource Distribution**: Resource placement will use biome and geological data from the world model (ore in mountains, plants in forests, etc.).

**Environmental Challenges**: Weather, temperature, and terrain difficulty come from the world model's climate data.

## Success Criteria

A successful world generation system will:

✅ **Generate believable worlds** - Players feel they've discovered a real alien planet
✅ **Provide variety** - Each world feels meaningfully different
✅ **Inform gameplay** - World structure creates interesting gameplay decisions (where to settle, resource scarcity, climate challenges)
✅ **Perform well** - Generation completes in reasonable time, 2D sampling is fast
✅ **Be engaging** - Watching world generation is interesting, not boring
✅ **Support iteration** - Players can easily regenerate to explore different worlds

## Next Steps

**For Game Designers**:
1. Review detailed design documents in this folder
2. Consider gameplay implications of different planet configurations
3. Define resource distribution rules based on biomes/geology

**For Developers**:
1. Read game design docs to understand requirements
2. Create technical design documents for implementation
3. Prototype sphere subdivision and 2D sampling
4. Implement generation phases incrementally

**For Artists**:
1. Design color schemes for visualization modes
2. Create UI mockups for parameter adjustment
3. Coordinate with procedural tile system for biome appearance

## Revision History

- 2025-10-26: Initial world generation design documentation created from previous project learnings
