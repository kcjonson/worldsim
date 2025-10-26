# World Generation Concept

Created: 2025-10-26
Status: Design

## Core Concept

World-sim generates **complete 3D spherical planets** using scientifically-grounded procedural generation. Players customize planet parameters, watch the world form from space, and then play on the surface using a 2D tile-based interface that samples the spherical world model.

## Why Generate Spherical Worlds?

### The Problem with Traditional Terrain Generation

Most colony/base-building games generate flat, infinite 2D terrain using noise functions (Perlin, Simplex). This approach:
- Lacks global structure (no poles, equator, climate zones)
- Feels arbitrary (why is this desert here?)
- Provides shallow variety (change seed → slightly different noise)
- Misses emergent features (rain shadows, continental patterns, ocean currents)

### The Spherical Approach

By generating a **complete planet first**, we get:
- **Global coherence**: Climate zones follow latitude (cold poles, warm equator)
- **Emergent complexity**: Mountains create rain shadows, plates form realistic continents
- **Believable worlds**: Features have geological/atmospheric explanations
- **Meaningful variety**: Changing parameters creates fundamentally different worlds
- **Exploration potential**: Players discover a real place, not random noise

**Example**: A mountain range exists because two continental plates collided. That mountain creates a rain shadow desert on one side and a wet forest on the other. This emerges naturally from plate tectonics, not manual placement.

## Design Philosophy

### Scientific Plausibility, Not Simulation Accuracy

**Goal**: Worlds that **feel realistic** and **make intuitive sense**

**Approach**:
- Use real geological/atmospheric principles as foundations
- Simplify complex systems to essential characteristics
- Prioritize believability over scientific precision
- Abstract away unnecessary complexity

**Example**: We calculate steady-state plate positions based on planet age, rather than simulating millions of years step-by-step. The result looks plausible - that's what matters.

### Parameterized Generation

**Philosophy**: Give players **meaningful choices** that produce **understandable results**

**Good Parameters**:
- Planet size (small → less diverse, large → more variety)
- Water amount (dry → desert world, wet → ocean world)
- Number of tectonic plates (few → large continents, many → archipelagos)
- Planet age (young → jagged mountains, old → eroded terrain)

**What Players Don't Control**:
- Specific biome placement ("more forests")
- Exact mountain locations
- River networks
- Continent shapes

**Why**: Biomes, terrain, and features **emerge** from physical parameters. Players control the "laws of physics" for their world, not the specific outcomes. This creates discovery and surprise.

### Two-Phase Experience

The game has two distinct phases with different purposes:

#### Phase 1: World Generation (God View)
**Perspective**: Viewing planet from space
**Purpose**:
- Customize planetary parameters
- Observe global structure
- Evaluate habitability
- Decide if this world is worth colonizing

**Feeling**: Educational, exploratory, anticipatory

**Player Questions**:
- Is there enough land?
- Where are the temperate zones?
- Are there interesting geographic features?
- Is this the world I want to colonize?

#### Phase 2: Gameplay (Ground View)
**Perspective**: Top-down 2D colony management
**Purpose**:
- Build colony
- Manage colonists
- Survive and thrive on the generated world

**Feeling**: Tactical, immediate, detailed

**Player Questions**:
- Where should I build?
- What resources are nearby?
- How do I deal with this desert/mountain/river?
- How does weather affect my colony?

**Connection**: The spherical world model provides **context and coherence** to local 2D gameplay. Features have explanations (climate zones, geological history). The world feels like a real place.

## Game Experience Goals

### During World Generation

**Engagement**: Watching world generation should be **interesting**, not a loading screen

**How**:
- Real-time visualization showing each phase
- Clear progress indicators
- Changing views as features appear (plates → terrain → water → biomes)
- Educational tooltips explaining what's happening

**Feel**: Like watching a nature documentary about planet formation

### During Gameplay

**Discovery**: Players should feel like they're **exploring a real alien world**

**How**:
- Biomes make sense given location (cold at poles, wet near coasts)
- Terrain has geological explanations (mountains at plate boundaries)
- Climate affects gameplay (temperature, rainfall, seasons)
- Natural resources distributed based on geology

**Feel**: Like colonizing an actual planet, not playing on randomly generated noise

### Across Multiple Games

**Variety**: Each world should feel **meaningfully different**, not just visually distinct

**How**:
- Desert planets (low water, high temperature)
- Ocean worlds (high water, scattered islands)
- Mountainous worlds (many plates, young age)
- Earth-like worlds (balanced parameters)
- Exotic worlds (unusual star types, extreme orbits)

**Feel**: Strong motivation to try different parameters and discover unique worlds

## What Makes a "Good" Generated World?

### For World Generation Phase

A good world from the space view:
- ✅ **Visually clear**: Easy to distinguish oceans, continents, climate zones
- ✅ **Interesting**: Variety of features (mountains, plains, deserts, forests)
- ✅ **Balanced**: Mix of land/water, varied biomes (unless intentionally extreme)
- ✅ **Coherent**: Features make sense together (rain shadows, coastal forests)

### For Gameplay Phase

A good world for colony gameplay:
- ✅ **Habitable zones**: Areas suitable for initial colonization (not all desert/mountain)
- ✅ **Resource variety**: Different biomes provide different resources
- ✅ **Interesting challenges**: Mix of easy and difficult terrain
- ✅ **Exploration potential**: Enough variety to make exploration worthwhile
- ✅ **Strategic depth**: Geographic features create meaningful decisions

**Note**: Not all worlds need to be "Earth-like". A challenging desert world or dangerous ice world can be interesting gameplay.

## Player Stories

### Story 1: The Goldilocks World
> "I wanted an Earth-like planet for my first game. I set water to 60%, added 12 tectonic plates, and chose a Sun-like star. The generation showed me a planet with two large continents and a scattering of islands. Perfect! I landed on a temperate coast with forests nearby for wood and plains for farming. The river provided water. Classic colony start."

**Design Takeaway**: Default/suggested parameters should create approachable, balanced worlds for new players.

### Story 2: The Desert Challenge
> "After mastering the basics, I wanted something harder. I set water to 15% and cranked up the planet's star temperature. Generation created a harsh desert world with tiny polar ice caps and one small ocean. I had to land near the coast to have any water access. Every resource was scarce. Brutal, but rewarding!"

**Design Takeaway**: Parameter extremes create interesting challenge runs. The system should support wide parameter ranges.

### Story 3: The Archipelago Discovery
> "I set water to 75% and tectonic plates to 30. I was expecting an ocean world, but generation created hundreds of small volcanic islands - each plate boundary made island chains! I landed on a tropical island and built a seafaring culture. Completely different gameplay from my continent runs."

**Design Takeaway**: Emergent interactions between parameters create surprising, unique worlds. This is a feature, not a bug.

### Story 4: The Ancient World
> "I wanted a calm, peaceful world with gentle rolling hills. I set planet age to maximum. The old planet had heavily eroded mountains - just smooth highlands. Perfect for easy building. The rivers had cut deep, stable channels. Everything felt settled and ancient."

**Design Takeaway**: Planet age is a meaningful parameter that changes feel dramatically. Old = smooth, young = jagged.

## Design Principles Summary

1. **Global First, Local Second**: Generate complete planet, then sample for 2D gameplay
2. **Emergence Over Specification**: Features arise from parameters, not manual placement
3. **Plausible Not Accurate**: Scientifically grounded but simplified for gameplay
4. **Parameters Control Physics**: Players set rules, world emerges naturally
5. **Visual Feedback**: Show generation process, make it engaging
6. **Meaningful Variety**: Different parameters → fundamentally different worlds
7. **Context and Coherence**: Everything has a reason (geological, atmospheric)
8. **Discovery and Surprise**: Players explore worlds, not noise patterns

## Why This Matters for World-Sim

World-sim is about **building colonies on alien worlds**. The world generation system ensures:

- **Immersion**: The planet feels like a real place with history and structure
- **Replayability**: Endless variety of unique, interesting worlds
- **Strategic Depth**: Geographic/climatic features create meaningful decisions
- **Narrative Potential**: Each world tells a story (harsh desert, lush jungle, frozen wasteland)
- **Player Agency**: Meaningful choices in world creation

**Bottom Line**: The world generation system turns "random terrain" into "colonizing an alien planet" - the foundation of the entire game experience.

## Next Steps

- **[planet-parameters.md](./planet-parameters.md)**: What parameters do players control?
- **[generation-phases.md](./generation-phases.md)**: How does the planet form?
- **[user-experience.md](./user-experience.md)**: What does the player see and do?
- **[data-model.md](./data-model.md)**: How does the 2D game use the 3D world?

## Revision History

- 2025-10-26: Initial concept document created
