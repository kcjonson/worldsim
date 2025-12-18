# Technology Discovery System

## Core Philosophy

Players should be able to understand the game through play, not wikis. The discovery system must teach itself - colonists visibly learn, experiment, and have insights that the player observes and learns from.

Technology is not gated by arbitrary "research points." It's gated by:
- **Discovery** - What things the colonist has seen in the world
- **Manufacturing ability** - What stations exist to transform inputs
- **Colonist skill** - Whether they can execute the recipe (see skills.md)

## Relationship to Skills System

This document covers **discovery** (how colonists learn what's possible). The skills system (skills.md) covers **progression** (how colonists get better at what they know). Key interactions:

| Concept | Discovery System | Skills System |
|---------|------------------|---------------|
| Recipe visibility | Unlock when all inputs known | - |
| Recipe execution | Requires station | - |
| Output quality | - | Determined by skill level |
| Knowledge transfer | Observation, found items | Manuals, teaching |

## Discovery Methods

### 1. Observation (Seeing Things)

When a colonist **sees** anything in the world, they learn that thing exists. This applies uniformly to all defs - raw resources, crafted items, structures, everything.

**Example:**
- Bob walks near a rock → Bob now knows "Rock" exists
- Bob walks near a bush → Bob now knows "Bush" exists
- Bob finds a bronze axe in wreckage → Bob now knows "BronzeAxe" exists

**Rules:**
- Discovery is **per-colonist** - Bob knowing Rock doesn't mean Alice knows Rock
- Proximity triggers discovery (uses existing VisionSystem sight radius)
- All defs are treated the same - no distinction between "materials" and "items"

### 2. Recipe Unlock ("Aha" Moment)

When a colonist knows **all inputs** required for a recipe, that recipe unlocks. This is the "Aha" - the colonist realizes they can now make something new.

**Example:**
- Bob knows Rock (saw one)
- Bob knows Bush (saw one, can harvest sticks)
- Bob knows Grass (saw it, can harvest plant fiber)
- **Aha!** → "Primitive Axe" recipe unlocks for Bob

The Aha is not a separate system - it's the natural result of discovery. The player sees a notification: *"Bob realized he can craft: Primitive Axe"*

**Note:** Knowing a crafted item (like BronzeAxe) doesn't automatically teach you how to make it. You must know all the *inputs* to that recipe. But if BronzeAxe is itself an input to another recipe, knowing it helps unlock that recipe.

## Recipe System

### Output Quality

Output quality depends on the colonist's skill level plus random chance. Higher skill means better odds of good outcomes.

**Quality outcomes:**
- **Failure** - Materials wasted, colonist learns something
- **Poor** - Functional but degrades fast
- **Normal** - Standard quality
- **Good** - Above average durability/effectiveness
- **Excellent** - Rare, significant bonus

A colonist with Woodworking 2 might mostly produce Poor/Normal items with occasional Failures. A colonist with Woodworking 15 might mostly produce Good/Excellent items.

Note: Quality (how well it's made) is separate from grade (what type of item). A "Primitive Axe" is a different grade than "Standard Axe" - different recipes, different materials. But both can be crafted at any quality level depending on the crafter's skill.

### Innate Recipes

Some recipes are known by all colonists from game start:
- **Crafting spot** - No materials, builds instantly
- **Improvised clothing** - Very low quality, any materials
- **Campfire** - Basic fire for warmth/cooking

These bootstrap the progression loop without requiring initial discovery.

### Recipe Definition

A recipe consists of:

```
Recipe:
  id: axe_primitive
  materials: [stone, stick, plant_fiber]
  station: crafting_spot
  output: axe_primitive
  skill: woodworking
```

Note: Quality is computed from the colonist's skill level when they craft. Material variant (e.g., granite vs flint) is stored on the item instance.

### Recipe Unlock Rules

**A recipe unlocks for a colonist when they know ALL required inputs.**

This is the core rule. Unknown inputs → recipe not visible to that colonist.

**Example - Primitive Axe:**
- Requires: Rock + Bush (yields Stick) + Grass (yields PlantFiber)
- Bob knows Rock only → no unlock
- Bob knows Rock + Bush → no unlock
- Bob knows Rock + Bush + Grass → "Primitive Axe" recipe unlocks for Bob

### Recipe Execution Requirements

Knowing a recipe ≠ being able to make it. Execution requires:

1. **All inputs available** - In stockpile or world
2. **Required station exists** - Crafting spot, smelter, forge, etc.

Output quality is determined by the colonist's skill level at craft time. A colonist can *conceive* of a recipe before they can *execute* it.

**Example - Copper Ingot:**
```
Copper Ingot:
  inputs: [CopperOre]
  station: smelter
  output: CopperIngot
```

- Bob sees copper ore → "Copper Ingot" recipe unlocks (he conceives of it)
- But no smelter exists → Bob cannot make it yet
- Colony builds smelter → Now Bob can smelt copper

### Stations as Recipes

Stations themselves are craftable items with their own recipes:

```
Smelter:
  inputs: [Rock, Clay]
  station: CraftingSpot
  output: Smelter
```

This creates natural progression:
1. Bob knows Rock + Clay → Smelter recipe unlocks
2. Colony builds Smelter at crafting spot
3. Bob knows CopperOre → CopperIngot recipe unlocks
4. Bob can now smelt (has recipe + has station)

## Progression Cascades

Discovery cascades naturally through crafting chains:

1. **Start:** Bob knows Rock + Bush + Grass → unlocks Primitive Axe recipe
2. **Craft:** Bob makes Primitive Axe
3. **Use:** Bob chops tree with axe → yields "Wood"
4. **Discover:** Bob now knows "Wood" (a new thing in the world)
5. **Unlock:** Recipes requiring Wood as input unlock
6. **Repeat:** Each new thing opens new possibilities

## Data Model

### On-Disk Storage (Moddable XML)

Following the existing asset system pattern (see `/docs/technical/asset-system/`), all things and recipes are defined in XML for moddability.

#### Folder Structure

```
assets/
├── things/
│   ├── resources/
│   │   ├── Stone/
│   │   │   └── Stone.xml
│   │   ├── Stick/
│   │   │   └── Stick.xml
│   │   ├── CopperOre/
│   │   │   └── CopperOre.xml
│   │   └── Wood/
│   │       └── Wood.xml
│   │
│   ├── tools/
│   │   ├── AxePrimitive/
│   │   │   ├── AxePrimitive.xml
│   │   │   └── axe_primitive.svg
│   │   ├── AxeStandard/
│   │   │   ├── AxeStandard.xml
│   │   │   └── axe_standard.svg
│   │   └── HammerPrimitive/
│   │       └── ...
│   │
│   └── stations/
│       ├── CraftingSpot/
│       │   └── CraftingSpot.xml
│       ├── Smelter/
│       │   └── Smelter.xml
│       └── Forge/
│           └── Forge.xml
│
└── recipes/
    ├── tools/
    │   ├── AxePrimitive.xml
    │   ├── AxeStandard.xml
    │   └── HammerPrimitive.xml
    └── processing/
        └── CopperIngot.xml
```

**Note:** All things (resources, tools, stations, etc.) live under `things/` with subcategories for organization. There's no special `materials/` vs `items/` distinction - everything is a "thing" that can be an input or output of recipes.

#### Thing Definition

All things use the same `ThingDef` format. Categories organize them but don't affect the discovery/recipe system.

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!-- assets/things/resources/Stone/Stone.xml -->
<ThingDef>
  <defName>Stone</defName>
  <label>Stone</label>
  <description>A common rock, useful for basic tools</description>

  <category>Resource</category>
  <stackLimit>50</stackLimit>

  <!-- Visual properties for rendering -->
  <appearance>
    <baseColor>#808080</baseColor>
    <colorVariation>0.1</colorVariation>
  </appearance>
</ThingDef>
```

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!-- assets/things/tools/AxePrimitive/AxePrimitive.xml -->
<ThingDef>
  <defName>AxePrimitive</defName>
  <label>Primitive Axe</label>
  <description>A crude axe made from stone and sticks</description>

  <category>Tool</category>
  <function>Chopping</function>

  <efficiency>0.5</efficiency>
  <durability>100</durability>

  <svg>axe_primitive.svg</svg>
</ThingDef>
```

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!-- assets/things/tools/AxeStandard/AxeStandard.xml -->
<ThingDef>
  <defName>AxeStandard</defName>
  <label>Axe</label>
  <description>A proper metal axe with a wooden handle</description>

  <category>Tool</category>
  <function>Chopping</function>

  <efficiency>1.0</efficiency>
  <durability>250</durability>

  <svg>axe_standard.svg</svg>
</ThingDef>
```

Note: Each thing is self-contained. The input variant used (e.g., Granite vs Flint stone for a primitive axe) is stored on the thing instance at craft time, affecting appearance and repair inputs.

#### Recipe Definition

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!-- assets/recipes/tools/AxePrimitive.xml -->
<RecipeDef>
  <defName>Recipe_AxePrimitive</defName>
  <label>Primitive Axe</label>
  <description>A crude axe made from stone and sticks</description>

  <!-- Inputs required (ALL must be known to unlock recipe) -->
  <inputs>
    <input thing="Stone" count="2"/>
    <input thing="Stick" count="1"/>
    <input thing="PlantFiber" count="1"/>
  </inputs>

  <!-- Station required to craft -->
  <station>CraftingSpot</station>

  <!-- Output thing -->
  <outputs>
    <output thing="AxePrimitive" count="1"/>
  </outputs>

  <!-- Skill used (quality determined by colonist's level) -->
  <skill>Woodworking</skill>

  <!-- Work amount in ticks -->
  <workAmount>500</workAmount>
</RecipeDef>
```

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!-- assets/recipes/tools/AxeStandard.xml -->
<RecipeDef>
  <defName>Recipe_AxeStandard</defName>
  <label>Axe</label>

  <inputs>
    <input thing="MetalIngot" count="2"/>
    <input thing="Wood" count="1"/>
  </inputs>

  <station>Forge</station>

  <outputs>
    <output thing="AxeStandard" count="1"/>
  </outputs>

  <skill>Smithing</skill>
  <workAmount>800</workAmount>
</RecipeDef>
```

#### Skill Definition

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!-- assets/skills/Woodworking.xml -->
<SkillDef>
  <defName>Woodworking</defName>
  <label>Woodworking</label>
  <description>Working with wood to create tools, furniture, and structures</description>
  <maxLevel>20</maxLevel>
</SkillDef>
```

Quality outcomes are determined by skill level + random chance (see Output Quality section above).

#### Station Definition

Stations are just things with extra properties for crafting:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!-- assets/things/stations/Smelter/Smelter.xml -->
<ThingDef>
  <defName>Smelter</defName>
  <label>Smelter</label>
  <description>A clay furnace for melting ore into metal</description>

  <category>Station</category>

  <!-- What categories of recipes can be done here -->
  <allowedRecipeCategories>
    <category>Smelting</category>
  </allowedRecipeCategories>

  <!-- Fuel requirements -->
  <fuel>
    <required>true</required>
    <acceptedTypes>
      <thing>Wood</thing>
      <thing>Coal</thing>
    </acceptedTypes>
  </fuel>
</ThingDef>
```

And a recipe to build it:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!-- assets/recipes/stations/Smelter.xml -->
<RecipeDef>
  <defName>Recipe_Smelter</defName>
  <label>Build Smelter</label>

  <inputs>
    <input thing="Stone" count="10"/>
    <input thing="Clay" count="5"/>
  </inputs>

  <station>CraftingSpot</station>

  <outputs>
    <output thing="Smelter" count="1"/>
  </outputs>

  <workAmount>2000</workAmount>
</RecipeDef>
```

#### Innate Recipe Marker

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!-- assets/recipes/basics/CraftingSpot.xml -->
<RecipeDef>
  <defName>Recipe_CraftingSpot</defName>
  <label>Crafting Spot</label>

  <!-- Empty inputs = nothing needed -->
  <inputs />

  <!-- No station required = can be placed anywhere -->
  <station>none</station>

  <outputs>
    <output thing="CraftingSpot" count="1"/>
  </outputs>

  <!-- Innate = all colonists know this from start -->
  <innate>true</innate>
</RecipeDef>
```

### Runtime Data Structures

#### Colonist Knowledge (Savegame)

```
ColonistKnowledge:
  colonist_id: string
  knownDefs: Set<DefNameId>      # All things this colonist has ever seen

  # Computed at runtime:
  available_recipes: Set<RecipeDef>  # All inputs known for recipe
  # Note: Skill levels stored separately in colonist data, not here
```

**Key insight:** There is no distinction between "materials" and "items" or "artifacts". Everything is a def. A microchip is both an output of one recipe and an input to another. The same `knownDefs` set handles all cases.

#### Global Player Knowledge (Savegame)

```
PlayerKnowledge:
  knownDefs: Set<DefNameId>      # Union of all colonists' knownDefs
  craftableDefs: Set<DefNameId>  # At least one colonist can craft this
  # Everything else is unknown/fog
```

### Mod Support

Following the existing asset system patterns:

**Adding new things/recipes (modder):**
```
mods/MyMod/assets/things/resources/Unobtainium/
└── Unobtainium.xml
```

**Overriding existing recipes (modder):**
```xml
<!-- mods/MyMod/assets/recipes/tools/AxePrimitive.xml -->
<RecipeDef override="true">
  <defName>Recipe_AxePrimitive</defName>
  <workAmount>300</workAmount>  <!-- Faster! -->
</RecipeDef>
```

**Patching existing definitions (modder):**
```xml
<!-- mods/MyMod/patches/easier_smelter.xml -->
<Patch>
  <Operation type="replace">
    <xpath>/RecipeDef[defName="Recipe_Smelter"]/inputs/input[@thing="Stone"]/@count</xpath>
    <value>5</value>  <!-- Half the stone needed -->
  </Operation>
</Patch>
```

## Player-Facing Tech Tree

The tech tree is a **discovery map**, not a shopping list.

### Node States

1. **Craftable (solid)** - At least one colonist can craft this (knows all inputs)
2. **Known (ghosted)** - Colony has seen this but no colonist can craft it yet
3. **Unknown (hidden)** - No colonist has encountered this yet

### Visibility Rules

- Player sees nodes, not prerequisites
- Seeing a bronze axe shows "Bronze Axe" as known (ghosted)
- Prerequisites remain hidden until discovered through play
- Players must experiment/explore to find the path, not read it

### Per-Colonist vs Global View

- **Global view:** "These things exist in the world" (for player planning)
- **Colonist view:** "This colonist can craft these" (for work assignment)

Player might know bronze axes exist, but if no colonist has seen all the inputs to craft one, it remains ghosted.

## Example: First Tree

Complete walkthrough of early game progression:

### Setup
- Colonists spawn knowing one innate recipe: "CraftingSpot" (no inputs required)
- Player places crafting spot via build UI → builds instantly

### Progression
1. Bob wanders, sees a rock → learns "Rock" (adds to knownDefs)
2. Bob wanders, sees a bush → learns "Bush" (can harvest for Stick)
3. Bob wanders, sees grass → learns "Grass" (can harvest for PlantFiber)
4. **Recipe unlock:** Bob now knows Rock + Bush + Grass → "Primitive Axe" recipe unlocks
5. Player creates work order: 1x Primitive Axe at crafting spot
6. Bob gathers rock, harvests stick from bush, harvests plant fiber from grass
7. Bob brings inputs to crafting spot
8. Bob crafts primitive axe
9. Player assigns Bob to chop tree
10. Bob chops tree → yields "Wood"
11. Bob learns "Wood" → recipes requiring Wood as input unlock

## Open Questions

### Resolved

**Material vs Item distinction:** Resolved - there is no distinction. Everything is a def. A microchip can be both an output of one recipe and an input to another. The `knownDefs` set handles all cases uniformly.

**Process knowledge:** Abstracted into station requirements. Knowing "heat transforms things" is implicit in having a smelter available.

**Quality system:** Quality is a function of skill level + station quality + random chance. Higher skill = better odds of Good/Excellent outcomes.

### Still Open

**Studying Found Items:**
When a colonist finds a complex item they can't craft, can they study it to learn the recipe?
- Time-based study at a research bench?
- Skill-based (higher skill = faster insights)?
- Gradual revelation (first learn component types, then specific inputs)?
- Or simply: finding an item doesn't help learn to make it - you must discover all inputs naturally?

**Recipe Hints:**
Should partially-known recipes show clues?
- Option A: Hidden entirely until all inputs known
- Option B: Show "??? + Bush = ???" after seeing bush used with unknown input
- Option C: Show "Rock + ??? = Primitive Axe" once you've seen the output item

**Discovery Radius:**
How close must colonist be to "see" something?
- Line of sight only? (Use existing VisionSystem)
- Fixed radius?
- Varies by thing (hard to miss a boulder, easy to miss small ore)?

**Multi-Colonist Discovery:**
When Alice discovers Rock, does Bob see Alice's reaction and learn too?
- Proximity-based knowledge spread?
- Only through explicit teaching/manuals?
- Shared "colony knowledge" for very basic things?

**Unlock Notification:**
How does the player see that Bob just unlocked a recipe?
- Toast notification?
- Icon above Bob's head?
- Entry in a colony log?
- All of the above?

## Related Documents

- [skills.md](./skills.md) - Colonist learning, skill progression, knowledge transfer
- [crafting.md](./crafting.md) - Resource primitives and crafting stations
- [rooms.md](./rooms.md) - Workshop types and bonuses
- [asset-definitions.md](../technical/asset-system/asset-definitions.md) - XML schema for moddable content
- [folder-based-assets.md](../technical/asset-system/folder-based-assets.md) - Asset folder structure
