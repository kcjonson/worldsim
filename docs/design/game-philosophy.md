# Game Philosophy & Core Loop

Created: 2025-12-15
Status: Design

## Overview

This document defines what the game is about, what makes it fun, and how players engage with it across different timescales. It serves as the north star for design decisions.

## Core Fantasy

**"Unravel the mystery of the forbidden sector while building a thriving colony capable of escape - or choosing to stay."**

Players are survivors of a crash landing on an unknown planet in a sector of space where ships vanish without explanation. Technology has failed. Most of the crew is dead. The few survivors must build a life from nothing while piecing together what happened, why their ship went dark, and whether escape is even possible.

The game is not about micromanaging colonists. Colonists are autonomous - they handle their own survival. The player's role is creating the conditions for a good life: building shelter, establishing food production, expanding territory, and pushing technological capability forward. The mystery of the forbidden sector provides direction; player creativity provides expression.

## What Makes This Game Good

Six emotional hooks drive engagement:

### Curiosity
The planet is unknown. The sector is forbidden for a reason. What's over the next hill? What crashed here before you? Why does technology fail? The game rewards exploration with answers, but each answer raises new questions.

### Nurturing
Your colonists are precious - survivors of hundreds. Keeping them alive, healthy, and happy matters. You watch them struggle, grow, form relationships, and build lives. Their wellbeing is your responsibility, even if their moment-to-moment actions are their own.

### Mastery
Hundreds of materials. Complex production chains. Manufacturing capability gates progression, not arbitrary research points. The satisfaction of building an efficient supply chain, of finally producing that advanced component after hours of infrastructure work.

### Discovery
The planet is filled with handcrafted anomalies, ruins, crashed ships, other survivors, alien life - some friendly, some not. Exploration isn't just fog-clearing; it's a primary driver of progression. Resources vary by region, but the world offers multiple paths forward - exploration, scavenging crashed ships, trade with other survivors. Each playthrough presents different opportunities based on where you land and what you find. Other colonies might teach you things you can't figure out alone.

### Choice
Four endings, none pre-selected. You build toward capabilities, then decide what to do with them. Escape to the galaxy. Fly deeper into the forbidden sector. Establish an official settlement. Or simply keep building. The ending emerges from what you've accomplished and what you choose in the moment.

### Creativity
Build a beautiful settlement, not just a functional one. Freeform construction with non-tile-based positioning allows real architecture - not chunky grid squares. The "high tech cowboy" aesthetic rewards visual expression. Room design affects colonist mood. A lovingly-designed dining hall makes people happier than a functional box.

## The Core Loop

### Minute-to-Minute
Colonists autonomously handle survival. They eat when hungry, sleep when tired, work when able, wander when idle. 

The player ensures they *can* - providing food sources, shelter, facilities. Managing work priorities. Adjusting what gets built and where. The player creates conditions; colonists respond to them.

A colonist will find food - but only if food exists. They'll sleep in a bed - but only if you've built one. They'll stay happy - but only if their environment supports it.

### Hour-to-Hour
Expand territory. Explore the unknown. Discover resources, anomalies, and encounters. Establish production chains to turn raw materials into useful goods.

Improve living conditions as the colony grows. Better housing, better food, recreational facilities. Respond to threats and opportunities the world presents - hostile creatures, harsh weather, other survivors seeking trade or conflict.

The planet is a large, scary, mysterious place. Anything can happen.

### Session-to-Session
Push technological capability forward. Unlock countermeasures to the tech-disabling phenomenon. Build toward major milestones: long-range communication, spaceflight capability.

Navigate encounters with whatever else is out here. Other crashed humans. Alien life. Things that explain why this sector was forbidden in the first place.

Each session should end with progress toward understanding and capability, and new questions to pursue next time.

### Endgame
Reaching a milestone triggers a choice, not a victory screen.

Build long-range communication → Contact the galactic government → "Do we request rescue, or declare ourselves a permanent settlement?"

Build a ship → "Do we fly home, or fly deeper into the forbidden sector?"

These are dramatic narrative moments. The player has *earned* the choice through gameplay, not selected it from a menu.

### Across Runs (New Game+)
Choosing to "fly deeper" starts a new game on a new planet. Knowledge and experience carry forward - research is faster, colonists are more skilled, the player understands more about what they're facing. 

Material possessions are lost. The ship crashes again. But you're better prepared this time.

This creates a narratively-justified New Game+ loop for players who want replayability with progression.

## Key Differentiators

### Colonist Memory System
Colonists only know what they've personally seen or learned from others. They cannot pathfind to resources they don't know exist. Knowledge spreads through social conversation. Exploration is meaningful because *someone has to go there* to know what's there.

### True Autonomy
Colonists make their own decisions based on needs and priorities. The player influences behavior through environmental design and priority settings, not direct orders. Leave the game running and watch the colony function - or intervene when you want to push in a particular direction.

### Freeform Building
Non-tile-based positioning allows realistic architecture. Walls can be thin. Rooms can be any shape. The multi-tier construction system (foundation → walls → features) gives players architectural control while keeping the process structured. Construction is a visible process - colonists gather materials and build over time.

### Pure Vector Graphics
Zero raster input. Every visual element is vector-based, rendering crisp at any zoom level. This is technically ambitious but creates a distinctive look that rewards zooming in to admire what you've built.

### Mystery-Driven Progression
Technology isn't gated by arbitrary research points. It's gated by manufacturing capability, discovered knowledge, and materials. You can't build advanced electronics because you lack the materials, tools, and understanding - not because you haven't clicked "research" enough times.

The central mystery - why does technology fail in this sector? - drives long-term progression. Countermeasures must be discovered and developed, not simply researched.

## Endings

### Escape to the Galaxy
Build a ship capable of surviving the tech-disabling phenomenon. Fly home. You beat the game. Your colonists return to civilization with knowledge of what lies in the forbidden sector.

### Fly Deeper (New Game+)
Build a ship and fly further into the forbidden sector. The ship crashes on a new planet. This is the game's New Game+ mode - start over with knowledge and experience carried forward, facing new challenges with hard-won understanding. For players who want "one more run" with meta-progression.

### Establish Official Settlement
Build long-range communication. Contact the galactic government. Choose to stay. Your colony becomes an official outpost - the first permanent human presence in the forbidden sector. Narrative closure for players who built something they want to keep.

### Sandbox
Do none of these things. Build a big, happy, beautiful colony for as long as you want. No pressure, no required ending. For players who just want to create.

## The World Pushes Back

The forbidden sector earned its reputation. External pressure exists but is tunable:

**Hostile fauna** - The planet has dangers beyond resource scarcity
**Environmental hazards** - Seasons, storms, harsh conditions
**Other factions** - Crashed humans (friendly or hostile), alien life, things stranger still
**The mystery itself** - Whatever causes technology to fail may not be passive

Players who want a peaceful building experience can tune threats down. Players who want survival tension can face a harsh, unforgiving world. The storyteller-style system adapts to preference.

## Related Documents

**Backstory & Setting:**
- Forbidden sector backstory (to be written - see chat history)

**Colonist Systems:**
- [Colonist AI](./systems/colonist-ai.md) - Decision hierarchy, autonomy
- [Needs System](./systems/needs-system.md) - Physical and emotional needs
- [Colonist Memory](./systems/colonist-memory.md) - Knowledge acquisition and sharing

**Building & Construction:**
- Construction system specification (to be written)
- [Room Types](./mechanics/rooms.md) - Room mechanics and bonuses

**World & Exploration:**
- [World Generation](./features/world-generation/README.md) - Spherical planet generation
- [Game View](./features/game-view/README.md) - 2D rendering of the world

**Visual Design:**
- [Visual Style](./visual-style.md) - Art direction
- [UI Art Style](./ui-art-style.md) - "High tech cowboy" aesthetic

## Revision History

- 2025-12-15: Initial document created from design discussion
