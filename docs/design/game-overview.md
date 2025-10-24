# Game Overview

Two dimensional open world survival game. A group (or single) character starts in an unknown area and must explore and survive.

## Progression

- Tech tree not limited by arbitrary "research" - tech is gated by manufacturing ability and inhabitant skill. Complex things require a lot of tools and materials, which are difficult to gather. This prevents players from jumping into something like power armor very quickly.
- Progression is based on gathering and production of many (like hundreds) of unique materials and manufacturing processes in a way more similar to Settlers or Factorio. This will require exploring, stockpiling.

## World Mechanics

### Overview

- Infinite (or nearly) procedurally generated world
- Sight lines / "Fog of war" (areas not visible to the character are hidden)
- Local mini map showing the extent of the explored area
- Overview map pre-generated but only a very "high level" idea of what is on a tile; the scope of this depends on the backstory

### Creation

A world level map is generated at the start of a game, this defines high level geographic features of the world. The world would be a spherical polyhedron of world tiles, and each tile would be assigned properties. The time of year modifies the temperature and wetness of tiles.

**Resources:**
- https://github.com/jarettgross/Procedural-Planet-Generator
- https://experilous.com/1/blog/post/procedural-planet-generation
- http://www-cs-students.stanford.edu/~amitp/game-programming/polygon-map-generation/

## Backstory

*Totally not settled on backstory, they all seem so familiar*

### Crash Landed
*Similar to: Rimworld, Factorio*

You and your crew were on a spaceship that crash landed into an unknown planet. Luckily for you, the planet is earth like and can support human life. You must survive until you are rescued. Your ship launched from space research outpost 28b 18 years ago on an independent prospecting mission; it will take that long for another to arrive. The landscape is sparse and you're only able to salvage a few things from the wreckage of your ship. The world seems uninhabited at first, but it turns out that this particular planet has caused other ships to crash as well, some human, some not.

**Implications:**
- Flora and fauna should be alienish, familiar looking but not earth names
- Landscape and materials could be anything
- Opportunity to discover technologies and rich random encounters
- Aliens and other intelligent species fit into the story
- Planet scale map, starts with world view since you would have seen it flying in

### Science Experiment
*Similar to: ARK*

You wake up not knowing your name or who the people around you are. You vaguely remember how to talk and some simple skills. Why are you here? What life will you make? Over time you discover that you're part of an evil alien experiment, and your goal is to break free and get home.

**Implications:**
- Random events can get really weird
- Flora and fauna are earth based, but with some weird quirks (Pigoons)
- Huge opportunity for freaky random events
- Could go full horror and take a Resident Evil like backstory
- Could bring magic-like powers into the game
- Continent, Island, Valley, or Planet Scale map, but no overview

### Post Apocalypse
*Similar to: Rust, Miscreated, Fallout*

The world has gone to shit, and it's just you and your party left. You must rebuild society, or not. You've been underground for generations so not much of the earth is left as you know it. The data stores were lost a hundred years ago and life in the bunker has regressed to a primitive society.

**Implications:**
- Could be in the distant future or based off modern time. Modern time makes world generation more difficult and would block world scale generation
- Big opportunity for social interactions and factions in game, almost required
- Flora and fauna must be earth like, but could have mutated variants or additions
- Landscape and materials must be earth like
- Would need to generate cities and infrastructure; could do something really interesting here by using google earth data as the seed, make it very earth like by actually generating towns based on real town layouts
- Continent or valley scale. Would need to define some bounding area if the earth is used as a backsetting

### Dimensionally Ported
*Similar to: Stargate*

You are a research expedition under the polar ice cap. You activate a mysterious device and are violently sucked to an unknown world. The device becomes inactive and your best scientist thinks that it's lost its power source or overloaded. You must establish a base and find a way home.

**Implications:**
- Could be magic, or not. Was it technology that ported you, or something else?
- Random events are essentially unbounded, anything could happen
- Technology would follow modern earth patterns, but could have some unique additions on the other side
- Flora and fauna could be a mix of earth and other. Did the builders of the device seed earth? Was there rich ancient travel? Was it aliens?
- Materials should be earth like, but with a couple added
- Planet scale map, but you wouldn't be able to really see it, since there would be no satellites or space program

### Exiled Heretic
*Similar to: Brandon Sanderson's Stormlight Archive*

You and your band has been exiled and dropped on a distant shore of an uninhabited continent for crimes against the Vorin church. The main population of the world is unaware that this part of the world even exists as the church is keeping the population in a regressive state where they're the only ones with power. You must survive and build a new society.

**Implications:**
- Rich backstory and storytelling required
- Magic, spellcasting, sorcery and unique abilities easily fit in
- Limited high technology, but familiar weapons and arms from earth's past
- Flora and fauna earth like, but could get exotic
- Opportunity for other factions and inhabitants to exist
- Continent scale map, no world view

### Shipwrecked on the Other Side of the World

Against common wisdom at the time you personally finance an expedition into the unknown on your planet. The whole world believes that there is nothing on the other side of the planet, but there is no such thing as a two dimensional world - even a piece of paper has another side, right? Your ship makes it to the other side but crashes into an unknown land after most of the crew have died from starvation.

**Implications:**
- Could get silly. Imagine a flat world and this takes place on the other side. The introduction could include a great cinematic about sailing over the edge of the world
- Small space for magic to exist since this is obviously not earth
- Technologically regressive, mostly around middle ages tech, but some firearms
- Flora and fauna are earth like since birds make it around the edge. Whales do not - they're torn to pieces, poor whales
- Planet scale map, at least the other half of the planet

## Similar Games

### Rimworld
- Not infinite, base building area is very small
- Hex tile world pre-generated
- No fog of war; can see everywhere in the current world hex
- 2D top down isometric
- Single height level
- Tech tree
- Full raids and base defense

### Necesse
TODO

### Dwarf Fortress
- Extremely deep
- Looks like hell

### Factorio
- 2D top down world
- Resource production based

### Rise to Ruins
TODO

### Settlers (All million of them)
- Supply chain management, heavily focuses on how to move goods around
- Resource management
- No individual inhabitant control

### Prison Architect
- 2D Builder
- Inhabitant Management

### Castle Story
- 3D Voxel
- Base defense

### Minecraft
- Infinite procedurally generated world
- 3D Voxel
- Minor base defense

### ARK
- Non procedurally generated
- Tech tree

### Rust

### Miscreated

### Hurtworld

### Icarus