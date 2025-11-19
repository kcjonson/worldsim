# Game Start Experience

Created: 2025-11-18
Status: Design

## Overview

This document describes the complete player experience from launching the game to the moment gameplay begins. The game start experience is designed to build anticipation, establish the narrative context, and guide the player through critical decisions that shape their unique playthrough.

**Design Goals**:
- **Build excitement** - Each step increases anticipation for the adventure ahead
- **Establish context** - Player understands the scenario and their role
- **Meaningful choices** - Decisions during setup affect gameplay
- **Streamlined flow** - No unnecessary steps, clear progression
- **Visual polish** - Every screen feels professional and thematic

## Flow Overview

The game start experience follows this sequence:

1. **Launch Game** - Application startup and initialization
2. **Loading Screen** - Initial asset loading with thematic visuals
3. **Main Menu** - Central hub for all game actions
4. **New Game → Scenario Selection** - Choose the survival scenario
5. **Party Selection** - Select and customize colonists
6. **Planet Generation** - Configure, generate, and select landing site
7. **Landing Sequence** - Crash animation and narrative transition
8. **Gameplay Begins** - Player takes control of their colony

---

## 1. Launch Game

**What Happens**:
- Player double-clicks game executable or launches from Steam/platform
- Operating system starts the game process
- Game initializes core systems (graphics, audio, logging)
- Window opens and takes focus

**Technical Requirements**:
- Fast startup on modern hardware
- Fullscreen or borderless windowed by default
- Immediate visual feedback (window appears quickly)

**Player Experience**:
- Instant gratification - game launches fast
- Clean, professional window appearance
- No console windows or debug output visible (release builds)

---

## 2. Loading Screen

**What Happens**:
- Game loads essential assets (fonts, UI textures, main menu resources)
- Displays thematic loading screen with progress indicator
- May display tips, lore, or atmospheric artwork

**Visual Design**:
- **Thematic artwork** - Space/colony/survival theme
- **Progress bar** - Shows loading progress (0-100%)
- **Flavor text** - Rotating tips, lore snippets, or quotes
- **Style** - "High tech cowboy" aesthetic (see `/docs/design/ui-art-style.md`)

**Player Experience**:
- Immediate visual interest - beautiful artwork
- Clear progress - not staring at a frozen screen
- Thematic immersion begins

---

## 3. Main Menu

**What Happens**:
- Main menu appears with primary options
- Background shows animated scene (rotating planet, stars, colony footage, etc.)
- Audio: Ambient music, subtle sound effects

**Menu Options**:
- **New Game** - Start a new colony (primary call-to-action)
- **Continue** - Resume last saved game (if available)
- **Load Game** - Select from saved games
- **Settings** - Graphics, audio, controls, gameplay options
- **Credits** - Development team, licenses, acknowledgments
- **Exit** - Quit to desktop

**Visual Design**:
- Clean, readable typography
- Animated background (not static)
- Highlighted selection (clear affordances)
- Version number in corner (subtle, bottom-right)

**Player Experience**:
- Clear primary action ("New Game" is obvious)
- Professional, polished feel
- Music sets the tone for the game

---

## 4. Scenario Selection

**What Happens**:
- Player clicks "New Game" → Scenario selection screen appears
- Displays 3-5 preset scenarios with different difficulty/narrative contexts
- Each scenario has a name, description, and modified starting conditions

**Scenario Examples** (initial implementation):
1. **Standard Colony** - Balanced start, recommended for first-time players
2. **Harsh World** - Difficult climate, limited resources, experienced players
3. **Rich Resources** - Abundant materials, easier economy, casual play
4. **Lone Survivor** - Single colonist start, extreme challenge
5. **Large Expedition** - Many colonists, complex management, sandbox mode

**Visual Design**:
- Card-based layout (each scenario is a card)
- Scenario artwork/icon
- Difficulty indicator (1-5 stars or Easy/Normal/Hard)
- Description text (2-3 sentences)
- "Select" button on each card

**Interaction**:
- Click scenario card to highlight it
- Click "Confirm" or "Next" to proceed to party selection
- "Back" button returns to main menu

**Player Experience**:
- Meaningful choice - scenarios affect gameplay significantly
- Clear descriptions - player knows what they're choosing
- Low commitment - can back out and try different scenarios

---

## 5. Party Selection

**What Happens**:
- Player customizes their starting colonists (3-8 colonists depending on scenario)
- Each colonist has attributes, skills, traits (see `/docs/design/mechanics/colonists.md`)
- Player can randomize, manually adjust, or use preset parties

**Interface Elements**:
- **Colonist List** (left side) - Shows all colonists in the party
- **Selected Colonist Details** (right side) - Attributes, skills, traits, backstory
- **Randomize Button** - Generate random party
- **Add/Remove Colonist** - Adjust party size (within scenario limits)
- **Confirm Button** - Proceed to planet generation

**Customization Options**:
- **Name** - Rename colonists
- **Appearance** - Avatar/portrait selection (simple, not deep character creator)
- **Skills** - Adjust skill points within budget (optional, scenario-dependent)
- **Traits** - Select personality traits (affects behavior, morale)

**Visual Design**:
- Portrait/avatar for each colonist
- Skill bars or icons
- Trait badges (visual icons for traits)
- Color-coded stats (green = good, red = poor)

**Player Experience**:
- Investment in characters - player names and customizes their crew
- Strategic decisions - skill composition affects gameplay
- Quick option available - "Randomize" for players who want to start fast
- Narrative connection - each colonist has a backstory snippet

---

## 6. Planet Generation

**What Happens**:
- Player configures planet parameters (see `/docs/design/features/world-generation/`)
- System generates a complete 3D spherical planet
- Player views planet from space with visualization modes
- Player selects landing coordinates
- System samples world data at landing site

### 6.1 Configure Planet Data

**Interface**:
- **Parameter Sliders** - Planet size, water %, atmosphere density, tectonic activity, etc.
- **Presets** - Quick options (Earth-like, Desert World, Ocean World, Ice World)
- **Seed Input** - Manual seed for reproducible worlds
- **Randomize Button** - Random parameters

**Player Experience**:
- Control over world type - choose desert vs lush vs ice planet
- Preset shortcuts - casual players use presets, enthusiasts tweak parameters
- Seed sharing - players can share interesting world seeds

### 6.2 Generate Planet

**What Happens**:
- Player clicks "Generate" button
- System runs 8-phase generation process (see `/docs/design/features/world-generation/generation-phases.md`)
- Progress bar shows current phase and percentage
- 3D planet view shows generation in real-time (growing continents, ocean filling, biomes appearing)

**Visual Feedback**:
- **Progress Bar** - Phase name and percentage (e.g., "Tectonic Plates: 45%")
- **Planet View** - Rotating 3D sphere showing partial results
- **Phase Descriptions** - Text explaining what's happening ("Simulating plate tectonics...")

**Player Experience**:
- Engaged watching - planet visibly changes as generation runs
- Educational - learns how planets are built
- Anticipation - excitement builds as their world takes shape

### 6.3 Select Landing Site

**What Happens**:
- Generation completes → Player can now rotate/explore planet
- Visualization modes available (Terrain, Temperature, Precipitation, Biomes)
- Player clicks on planet surface to place landing marker
- System shows preview of selected location (biome, climate, resources)
- Player confirms landing site or selects different location

**Interface**:
- **3D Planet View** - Rotatable, zoomable
- **Visualization Toggle** - Switch between terrain/temp/precip/biome views
- **Landing Marker** - Visual indicator on planet surface
- **Location Preview** - Panel showing biome type, temperature range, resources
- **Confirm Button** - "Land Here" button

**Player Experience**:
- Exploration - player examines their generated world
- Strategic choice - landing site affects starting conditions
- Visual clarity - can see biomes, continents, oceans clearly
- Anticipation - picking the perfect spot to begin their colony

---

## 7. Landing Sequence

**What Happens**:
- Player confirms landing site → Transition to cinematic sequence
- Brief cutscene/animation showing crash landing
- Narrative text or voiceover establishes the scenario context
- Fade to gameplay view at landing coordinates

**Narrative Elements**:
- **Crash Scene** - Ship descending through atmosphere (stylized, not photorealistic)
- **Impact** - Ship crashes/lands roughly at selected coordinates
- **Aftermath** - Colonists emerge, survey damage, begin survival mode

**Visual Style**:
- Consistent with "high tech cowboy" aesthetic
- 2D animated sequence or simple 3D cutscene
- Dramatic but brief

**Player Experience**:
- Narrative payoff - all choices culminate in this moment
- Cinematic feel - game has production value
- Seamless transition - cutscene flows into gameplay view
- Context established - player understands "why am I here?"

---

## 8. Gameplay Begins

**What Happens**:
- View transitions to top-down 2D tile-based game view
- Camera centered on landing site (crashed ship, colonists nearby)
- UI appears (HUD, action menus, colonist portraits)
- Player has full control - can issue commands, build, explore

**Initial State**:
- **Crashed ship** - Visible on map, provides some starting resources
- **Colonists** - Standing near ship, awaiting orders
- **Revealed map** - Small radius around landing site is visible
- **Resources** - Starting materials from ship wreckage (scenario-dependent)

**Tutorial/Guidance** (optional, scenario-dependent):
- Pop-up tips for first-time players
- Objectives panel (e.g., "Gather wood", "Build shelter")
- Highlight key UI elements

**Player Experience**:
- Immediate agency - player is in control
- Clear starting point - ship and colonists are obvious
- Goals emerging - survival needs become apparent (hunger, shelter, etc.)
- Open-ended - player decides what to do first

---

## Navigation & Backtracking

At any step before planet generation completes, player can go back:

- **Scenario Selection** → Back to Main Menu
- **Party Selection** → Back to Scenario Selection
- **Planet Configuration** → Back to Party Selection
- **Planet Generation (in progress)** → Can cancel, returns to Planet Configuration
- **Landing Site Selection** → Can regenerate planet (returns to configuration)

Once landing sequence begins, no backtracking - player commits to this game.

---

## Technical Considerations

**Save Points**:
- Autosave after party selection (can resume if game crashes during planet gen)
- Autosave after planet generation (can resume if crash during landing)
- Manual save option at any step (player can save their setup and return later)

**Performance**:
- Planet generation runs on background thread (UI remains responsive)
- Progress updates frequently (smooth progress bar)
- Planet visualization updates regularly (balanced between visual feedback and performance)

**Error Handling**:
- Generation failure → Error message, return to planet configuration
- Save/load failures → Clear error messages, don't lose player progress
- Graphics errors → Fallback to simplified visuals

---

## Future Enhancements

Features not in initial implementation but considered for future:

- **Quickstart Mode** - Skip all setup, use preset scenario/party/world, jump directly to gameplay (for experienced players)
- **Story Mode** - Pre-designed scenarios with narrative arcs, fixed setups
- **Multiplayer Lobby** - Join friend's world, ready-up system
- **Planet Library** - Save favorite planet seeds for reuse
- **Advanced Character Creator** - Deeper colonist customization (appearance, detailed backstory)
- **Intro Cinematic** - Opening movie explaining game lore (skippable)

---

## Success Criteria

A successful game start experience will:

✅ **Be streamlined** - No unnecessary clicks, clear path to gameplay
✅ **Build anticipation** - Player excitement grows with each step
✅ **Establish narrative** - Player understands the scenario and context
✅ **Enable meaningful choices** - Setup decisions affect gameplay
✅ **Feel polished** - Every screen is visually appealing and bug-free
✅ **Support different playstyles** - Quick presets for casual, deep customization for enthusiasts
✅ **Transition smoothly** - No jarring cuts, logical flow between steps

---

## Related Documentation

**Game Design**:
- [Game Overview](../../game-overview.md) - Core concept and narrative
- [World Generation](./world-generation/README.md) - Planet generation system details
- [Colonist Attributes](../mechanics/colonists.md) - Character stats and traits

**Technical Documentation**:
- `/docs/technical/scene-management.md` - Scene transitions (to be written)
- `/docs/technical/application-lifecycle.md` - Startup sequence (to be written)

---

## Revision History

- 2025-11-18: Initial game start experience design document created
