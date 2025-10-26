# User Experience: World Generation

Created: 2025-10-26
Status: Design

## Overview

World generation is experienced in **two distinct phases**: first, a standalone 3D world generation UI where players create their planet, then transition to the 2D tile-based gameplay. This document describes the player experience, UI/UX, and flow between phases.

## Two-Phase Game Experience

### Phase 1: World Generation (3D Space View)

**Duration**: 2-10 minutes (parameter adjustment + generation + evaluation)

**Player Perspective**: Viewing planet from space, like a satellite or orbiting spacecraft

**Purpose**:
- Create a unique procedural planet
- Evaluate habitability and features
- Decide if this world is worth colonizing
- Generate anticipation for gameplay

**Player Activities**:
- Adjust planet parameters (star, planet, orbit)
- Initiate generation process
- Watch generation progress
- View completed world from different angles
- Switch visualization modes (terrain, biomes, climate)
- Evaluate world characteristics
- Regenerate or accept world

---

### Phase 2: 2D Gameplay (Colony Management)

**Duration**: Many hours (standard gameplay)

**Player Perspective**: Top-down 2D view of colony and surroundings

**Purpose**:
- Build and manage colony
- Survive in generated environment
- Explore the planet surface
- Achieve game objectives

**Connection to Phase 1**:
- World from Phase 1 provides terrain, biomes, climate
- Global structure informs local features
- Spherical world model is sampled to generate 2D view

---

## Phase 1: World Generation UI/UX

### User Flow

```
Main Menu
    ↓
[New Game]
    ↓
World Generation Screen
    ↓
Adjust Parameters → Preview → Generate → Watch Progress → View Result
    ↓                                                            ↓
[Regenerate]                                                [Accept World]
    ←──────────────────────────────────────────────────────────┘
                                                                 ↓
                                                        Choose Landing Site
                                                                 ↓
                                                        Start 2D Gameplay
```

### Screen Layout

**World Generation Screen** is divided into three main areas:

```
┌─────────────────────────────────────────────────────────┐
│  Title: "World Generation"                     [X] Exit │
├────────────────┬────────────────────────────────────────┤
│                │                                        │
│   Parameter    │                                        │
│   Controls     │         3D Planet View                 │
│                │      (Interactive Globe)               │
│   [Presets]    │                                        │
│   Star ▼       │                                        │
│   Planet ▼     │                                        │
│   Orbit ▼      │                                        │
│   Generator ▼  │                                        │
│                │                                        │
│   [Randomize]  │                                        │
│                ├────────────────────────────────────────┤
│   [Generate]   │   Visualization: [Terrain][Biomes]... │
│                │   Info Panel: Temperature, Habitability │
├────────────────┴────────────────────────────────────────┤
│  Progress: [████████░░░░░░░░] 60% - Assigning biomes... │
└─────────────────────────────────────────────────────────┘
```

### Component Details

#### 1. Parameter Panel (Left Side)

**Preset Dropdown**:
- Earth-Like (Default)
- Desert World
- Ocean World
- Frozen World
- Volcanic World
- Ancient Garden
- [Custom]

**Collapsible Parameter Sections**:

**Star Properties** ▼
- Mass (slider: 0.1 - 50 solar masses)
- Radius (slider: 0.1 - 1000 solar radii)
- Temperature (slider: 2000K - 50000K)
- Age (slider: 1M - 10B years)

**Planet Properties** ▼
- Radius (slider: 0.1 - 10 Earth radii)
- Mass (slider: 0.1 - 10 Earth masses)
- Rotation Rate (slider: 0.1 - 100 days)
- Tectonic Plates (slider: 2 - 30)
- Water Amount (slider: 0% - 100%)
- Atmosphere Strength (slider: 0.1 - 10 atm)
- Planet Age (slider: 10M - 10B years)

**Orbital Parameters** ▼
- Semi-Major Axis (slider: 0.1 - 100 AU)
- Eccentricity (slider: 0.0 - 0.95)
- (Periapsis/Apoapsis/Period shown as calculated values)

**Generator Settings** ▼
- Resolution (dropdown: Low/Medium/High/Ultra)
- Seed (text input, or "Random")

**Buttons**:
- [Randomize] - Generate random but plausible parameters
- [Generate] - Start world generation
- [Cancel] - Stop ongoing generation

**UX Notes**:
- Tooltips on hover explain each parameter
- Warnings for extreme values ("Warning: High eccentricity causes extreme seasons")
- Sliders have detents at Earth-like values
- Advanced parameters collapsed by default (star properties, orbital details)

---

#### 2. 3D Planet View (Center/Right)

**Interactive 3D Globe**:
- Rendered as textured sphere
- Rotates slowly by default (auto-rotate)
- Mouse drag to rotate manually
- Scroll to zoom in/out
- Shows current generation progress

**Visualization Modes** (tab bar or buttons):

**Terrain Mode**:
- Color by elevation (blue ocean → green lowlands → brown highlands → white mountains)
- 3D displacement (mountains actually stick up)
- Clear geographic features

**Temperature Mode**:
- Heat map (blue poles → red equator)
- Shows climate zones visually
- Affected by latitude, elevation, ocean proximity

**Precipitation Mode**:
- Moisture map (brown dry → green moderate → blue very wet)
- Shows deserts, rainforests, rain shadows
- River networks visible

**Biome Mode**:
- Color-coded biome types
- Green forests, yellow grasslands, tan deserts, white tundra
- Most "gameplay-relevant" view

**Plate Mode**:
- Each tectonic plate colored differently
- Plate boundaries highlighted (red = collision, blue = spreading)
- Educational/interesting but not gameplay-critical

**Snow/Ice Mode**:
- Shows snow coverage, glaciers, polar ice caps
- White = permanent snow, light blue = seasonal
- Glaciers visible at high mountains

**Combined Mode** (default after generation):
- Biomes as base colors
- Snow overlay at poles/mountains
- Rivers and oceans visible
- Most "complete" view

**UX Notes**:
- Smooth transitions between visualization modes
- Labels on major features (oceans, continents, mountain ranges?)
- Optional grid showing latitude/longitude
- Ability to screenshot for sharing

---

#### 3. Info Panel (Bottom Right)

**World Statistics**:
- Total Land Area: 45%
- Total Water Area: 55%
- Number of Continents: 4
- Largest Continent: 12,500,000 km²
- Average Temperature: 15°C
- Habitability Rating: ★★★★☆ (Good)

**Climate Summary**:
- Tropical: 20%
- Temperate: 35%
- Boreal: 15%
- Tundra/Polar: 10%
- Desert: 20%

**Notable Features**:
- Major mountain ranges: 8
- Large rivers: 23
- Polar ice caps: Yes
- Volcanic activity: Low

**Habitability Assessment**:
- Water availability: Good ✓
- Temperature range: Moderate ✓
- Arable land: 25% (Good) ✓
- Extreme weather: Moderate ⚠
- Overall: **Suitable for colonization**

**UX Notes**:
- Updates in real-time during generation
- Click features for details (e.g., click "8 mountain ranges" to highlight them)
- Color-coded ratings (green good, yellow moderate, red poor)

---

#### 4. Progress Bar (Bottom)

**During Generation**:
```
Progress: [████████████████░░░░] 75% - Phase 7/8: Assigning biomes...
Estimated Time Remaining: 14 seconds
```

**Phases Displayed**:
1. Generating tectonic plates...
2. Simulating plate movement...
3. Creating terrain from plate interactions...
4. Modeling atmospheric circulation...
5. Calculating precipitation and rivers...
6. Forming oceans and seas...
7. Assigning biomes...
8. Calculating snow and glaciers...
9. Finalizing world data...
10. Complete!

**UX Notes**:
- Smooth progress bar updates
- Phase names are educational (player learns how planets form)
- Time estimate based on current progress
- Cancel button available during generation
- Visual feedback in planet view (features appear as phases complete)

---

### Generation States

#### State 1: Parameter Setup (Initial)

**Planet View**: Default Earth-like planet (placeholder)
**Info Panel**: Shows stats for default parameters
**Actions**: Adjust parameters, select preset, randomize, generate

**UX Goal**: Make parameter selection approachable, not overwhelming

---

#### State 2: Generating (Progress)

**Planet View**: Planet updates in real-time as phases complete
- After Phase 1: Colored plate regions appear
- After Phase 3: Terrain becomes 3D (mountains visible)
- After Phase 6: Oceans fill basins (blue appears)
- After Phase 7: Biomes color the planet
- After Phase 8: Snow caps poles and mountains

**Info Panel**: Stats update as data becomes available

**Progress Bar**: Active, showing current phase and percentage

**Actions**: Watch progress, cancel if desired

**UX Goal**: Make generation engaging and educational, not a boring loading screen

---

#### State 3: Complete (Evaluation)

**Planet View**: Fully generated world, interactive
**Info Panel**: Complete statistics and habitability rating
**Actions**:
- Rotate and zoom to explore
- Switch visualization modes
- Save world
- Regenerate (with same or different parameters)
- Accept world (proceed to landing site selection)

**UX Goal**: Give player time to evaluate and decide if this is the world they want

---

### Additional Features

#### Save/Load Worlds

**Save World**:
- Button: [Save World]
- Prompt for world name
- Saves all parameters + generated data
- Shows thumbnail of planet

**Load World**:
- From main menu or world gen screen
- Shows list of saved worlds with thumbnails
- Click to load and continue evaluation or regenerate

**Share World**:
- Display seed + parameters
- Copy to clipboard
- Other players can recreate exact same world

**UX Notes**:
- Autosave most recent generation
- Limit saved worlds (storage management)
- Preview thumbnails crucial for selection

---

#### World Comparison

**Feature** (optional): Compare two generated worlds side-by-side
- Useful for evaluating parameter effects
- Shows both planets, synchronized rotation
- Stats shown for both

**Use Case**: "I want to see how increasing water from 50% to 80% changes the world"

---

## Landing Site Selection

**After Accepting World**:

**Transition Screen**: "Select your landing site"

**View**: Same 3D planet, now in "landing mode"

**Interaction**:
- Click anywhere on land to select landing coordinates
- Shows local preview (zoom in to show biome at that location)
- Info: "Temperate Deciduous Forest, 15°C average, moderate rainfall"
- Warnings if inhospitable: "Warning: Arctic tundra - extreme cold survival challenge"

**Recommendations**:
- Highlight "good starting areas" (temperate, near water, flat terrain)
- Show difficulty rating for each region

**Confirm Landing**:
- Button: [Confirm Landing Site]
- "You will begin your colony at these coordinates. Are you sure?"
- [Yes] → Transition to 2D gameplay

**UX Notes**:
- Default landing site suggestion (temperate coastal area)
- Player can ignore and land anywhere (even Antarctica-equivalent)
- Builds anticipation for starting colony

---

## Phase 2: 2D Gameplay Transition

**Fade Out**: 3D planet view
**Loading**: "Preparing landing site..."
**Fade In**: 2D tile-based view at landing coordinates

**Initial 2D View**:
- Shows immediate surroundings (chunk loaded from world model)
- Reveals local terrain, biome, features (rivers, elevation)
- Player starts building colony

**Connection to World Model**:
- As player explores (moves camera), new chunks load
- Each chunk samples the spherical world model at those coordinates
- Walking east eventually wraps around planet
- Walking north/south reaches poles

**Player Understanding**:
- They're now "on the surface" of the planet they created
- Global features visible in 3D view inform local gameplay
- Example: "I saw a river delta on the globe, now I'm at that delta in 2D"

---

## Visual Style

### 3D Planet View

**Aesthetic**: Clean, scientific, satellite view
- Not photorealistic (simplified, stylized)
- Clear legibility (features easy to distinguish)
- Educational feel (like planetarium or science museum exhibit)

**Lighting**:
- Subtle directional light (simulates sun)
- Planet rotates through day/night
- City lights at night? (if civilization modeled)

**Effects**:
- Subtle atmosphere glow at edges
- Clouds? (if modeled)
- Smooth visualization mode transitions

---

### UI Style

**Theme**: "High tech cowboy" (per project style guide)
- Western-themed UI elements (leather, wood textures)
- High-tech sci-fi overlays (holographic planet projection)
- Blend of rustic and futuristic

**Colors**:
- Warm earth tones for UI chrome
- Scientific blue/teal for data displays
- Clear, high-contrast text

**Typography**:
- Readable, sci-fi inspired
- Clear parameter labels
- Educational tone (tooltips, descriptions)

---

## Accessibility Considerations

**Colorblind Modes**:
- Visualization modes use colorblind-safe palettes
- Option to add patterns/textures in addition to colors

**Text Size**:
- Adjustable UI scaling
- Clear, readable fonts at default size

**Controls**:
- Keyboard shortcuts for all actions
- Alternative to mouse drag (arrow keys to rotate planet)

**Progress Indication**:
- Both visual (bar) and text (percentage, time remaining)

---

## Performance Considerations

**Target**:
- 60 FPS 3D planet rotation
- Smooth visualization mode transitions
- Generation completes in < 2 minutes for normal resolution

**Optimization**:
- Level-of-detail for planet mesh (lower poly at distance)
- Texture streaming for visualization modes
- Background thread for generation (UI remains responsive)
- Progressive visualization updates (not per-frame, per phase)

---

## Error Handling

**Generation Failure**:
- "World generation encountered an error"
- Suggest parameter changes or different seed
- Offer to regenerate automatically

**Invalid Parameters**:
- Real-time validation (sliders prevent invalid values)
- Warnings before generation ("Are you sure? This will create a nearly uninhabitable ice world")

**Extreme Worlds**:
- Generation succeeds but world is extreme (all ice, all desert)
- Warning after completion: "This world may be very challenging to play"
- Option to regenerate or proceed anyway

---

## Educational Elements

**Tooltips**: Explain parameters and phases in simple terms

**Phase Descriptions** (during generation):
- "Tectonic plates divide the planet's crust and drive mountain formation"
- "Atmospheric circulation creates wind patterns that distribute heat and moisture"
- "Biomes emerge from the interaction of temperature, precipitation, and elevation"

**Feature Highlights**:
- Click highlighted feature to learn about it
- "This mountain range formed from the collision of two continental plates"

**UX Goal**: Players learn basic planetary science without feeling lectured

---

## Future Enhancements

**Time-Lapse Mode**:
- Watch plate movement over geological time (animation)
- See mountains rise, continents drift
- Educational and engaging

**Seasonal Variation**:
- Toggle through seasons to see snow coverage change
- Useful for understanding climate

**Weather Simulation** (live):
- Show cloud patterns, storms
- Animated, dynamic
- Expensive (probably late-stage feature)

**Multi-World Comparison**:
- Generate multiple worlds in parallel
- Choose the best
- Requires significant performance

---

## Success Criteria

A successful world generation UX will:

✅ **Be engaging** - Players enjoy watching generation, not bored
✅ **Be educational** - Players learn how planets work
✅ **Be approachable** - Not overwhelming, clear what to do
✅ **Be informative** - Clear understanding of world characteristics
✅ **Build anticipation** - Excitement to start playing on generated world
✅ **Be flexible** - Support experimentation (easy regeneration, presets)
✅ **Be beautiful** - Visually appealing planet visualization

---

## Next Steps

- **[data-model.md](./data-model.md)**: How the 3D world data is structured and sampled for 2D gameplay

## Related Documentation

**Visual Style**:
- [ui-art-style.md](../../ui-art-style.md) - "High tech cowboy" UI aesthetic

**Technical Implementation** (future):
- 3D planet rendering system (OpenGL, shaders, mesh generation)
- World data serialization (save/load)
- UI framework for world generation screen

---

## Revision History

- 2025-10-26: Initial user experience documentation created
