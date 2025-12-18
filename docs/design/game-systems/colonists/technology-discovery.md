# Technology Discovery System

## Core Philosophy

Players should be able to understand the game through play, not wikis. The discovery system must teach itself - colonists visibly learn, experiment, and have insights that the player observes and learns from.

Technology is not gated by arbitrary "research points." It's gated by:
- **Material knowledge** - What resources the colonist has discovered
- **Manufacturing ability** - What stations exist to transform materials  
- **Colonist skill** - Whether they can execute the recipe (see skills.md)

## Relationship to Skills System

This document covers **discovery** (how colonists learn what's possible). The skills system (skills.md) covers **progression** (how colonists get better at what they know). Key interactions:

| Concept | Discovery System | Skills System |
|---------|------------------|---------------|
| Recipe visibility | Unlock when materials known | - |
| Recipe execution | Requires station | - |
| Output quality | - | Determined by skill level |
| Knowledge transfer | Found artifacts, observation | Manuals, teaching |

## Discovery Methods

### 1. Material Discovery (Observation)

When a colonist **sees** a resource in the world, they learn that resource exists.

**Example:**
- Bob walks near a rock → Bob now knows "stone" as a material
- Bob walks near a bush → Bob can harvest it → Bob now knows "stick" as a material

**Rules:**
- Discovery is **per-colonist** - Bob knowing stone doesn't mean Alice knows stone
- Proximity triggers discovery (exact range TBD)
- Some resources may require closer examination vs. just line-of-sight

### 2. Recipe Unlock ("Aha" Moment)

When a colonist knows **all materials** required for a recipe, that recipe unlocks. This is the "Aha" - the colonist realizes they can now make something new.

**Example:**
- Bob knows stone (saw a rock)
- Bob knows stick (harvested from bush)
- Bob knows plant fiber (harvested from grass)
- **Aha!** → "Primitive Axe" recipe unlocks for Bob

The Aha is not a separate system - it's the natural result of material discovery. The player sees a notification: *"Bob realized he can craft: Primitive Axe"*

### 3. Found Artifacts

Discovering a finished item proves that item **exists** without revealing how to make it.

**Example:**
- Bob finds a bronze axe in wreckage
- Bob now knows: bronze axes exist, they cut better than stone
- Bob does NOT know: what bronze is, how to make bronze, how to shape it into an axe

**Rules:**
- Finding an item ≠ knowing how to craft it
- Artifacts can be studied/reverse-engineered (separate process, TBD)
- Artifacts appear in player's tech tree as "known to exist" but not "craftable"

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

**A recipe unlocks for a colonist when they know ALL required materials.**

This is the core rule. No material knowledge → no recipe visibility.

**Example - Primitive Axe:**
- Requires: stone + stick + plant_fiber
- Bob knows stone only → no unlock
- Bob knows stone + stick → no unlock  
- Bob knows stone + stick + plant_fiber → "Primitive Axe" recipe unlocks for Bob

### Recipe Execution Requirements

Knowing a recipe ≠ being able to make it. Execution requires:

1. **All materials available** - In stockpile or world
2. **Required station exists** - Crafting spot, smelter, forge, etc.

Output quality is determined by the colonist's skill level at craft time. A colonist can *conceive* of a recipe before they can *execute* it.

**Example - Copper Ingot:**
```
Copper Ingot:
  materials: [copper_ore]
  station: smelter
  output: copper_ingot
```

- Bob sees copper ore → "Copper Ingot" recipe unlocks (he conceives of it)
- But no smelter exists → Bob cannot make it yet
- Colony builds smelter → Now Bob can smelt copper

### Stations as Recipes

Stations themselves are craftable items with their own recipes:

```
Smelter:
  materials: [stone, clay]
  station: crafting_spot
  output: smelter
```

This creates natural progression:
1. Bob knows stone + clay → smelter recipe unlocks
2. Colony builds smelter at crafting spot
3. Bob knows copper ore → copper ingot recipe unlocks
4. Bob can now smelt (has recipe + has station)

## Progression Cascades

Discovery cascades naturally through material chains:

1. **Start:** Bob knows stone + stick + plant fiber → unlocks primitive axe
2. **Craft:** Bob makes primitive axe
3. **Use:** Bob chops tree with axe → yields "wood"
4. **Discover:** Bob now knows "wood" as material
5. **Unlock:** New recipes requiring wood unlock
6. **Repeat:** Each new material opens new possibilities

## Data Model

### On-Disk Storage (Moddable XML)

Following the existing asset system pattern (see `/docs/technical/asset-system/`), all materials, recipes, and items are defined in XML for moddability.

#### Folder Structure

```
assets/
├── materials/
│   ├── Stone/
│   │   └── Stone.xml
│   ├── Stick/
│   │   └── Stick.xml
│   ├── CopperOre/
│   │   └── CopperOre.xml
│   └── Wood/
│       └── Wood.xml
│
├── items/
│   ├── tools/
│   │   ├── AxePrimitive/
│   │   │   ├── AxePrimitive.xml
│   │   │   └── axe_primitive.svg
│   │   ├── AxeStandard/
│   │   │   ├── AxeStandard.xml
│   │   │   └── axe_standard.svg
│   │   └── HammerPrimitive/
│   │       └── ...
│   └── weapons/
│       └── ...
│
├── stations/
│   ├── CraftingSpot/
│   │   └── CraftingSpot.xml
│   ├── Smelter/
│   │   └── Smelter.xml
│   └── Forge/
│       └── Forge.xml
│
└── recipes/
    ├── tools/
    │   ├── AxePrimitive.xml
    │   ├── AxeStandard.xml
    │   └── HammerPrimitive.xml
    └── materials/
        └── CopperIngot.xml
```

#### Material Definition

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!-- assets/materials/Stone/Stone.xml -->
<MaterialDef>
  <defName>Stone</defName>
  <label>Stone</label>
  <description>A common rock, useful for basic tools</description>
  
  <category>mineral</category>
  <stackLimit>50</stackLimit>
  
  <!-- How this material is discovered -->
  <discovery>
    <method>observation</method>
    <range>10</range> <!-- tiles -->
  </discovery>
  
  <!-- Visual properties for rendering -->
  <appearance>
    <baseColor>#808080</baseColor>
    <colorVariation>0.1</colorVariation>
  </appearance>
</MaterialDef>
```

#### Item Definition

Items are tools, weapons, furniture, etc. Each grade of an item is a separate file.

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!-- assets/items/tools/AxePrimitive/AxePrimitive.xml -->
<ItemDef>
  <defName>AxePrimitive</defName>
  <label>Primitive Axe</label>
  <description>A crude axe made from stone and sticks</description>
  
  <category>Tool</category>
  <function>Chopping</function>
  
  <efficiency>0.5</efficiency>
  <durability>100</durability>
  
  <svg>axe_primitive.svg</svg>
</ItemDef>
```

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!-- assets/items/tools/AxeStandard/AxeStandard.xml -->
<ItemDef>
  <defName>AxeStandard</defName>
  <label>Axe</label>
  <description>A proper metal axe with a wooden handle</description>
  
  <category>Tool</category>
  <function>Chopping</function>
  
  <efficiency>1.0</efficiency>
  <durability>250</durability>
  
  <svg>axe_standard.svg</svg>
</ItemDef>
```

Note: No abstract base class. Each item is self-contained. The material used (e.g., Granite vs Flint for a primitive axe) is stored on the item instance at craft time, affecting appearance and repair materials.

#### Recipe Definition

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!-- assets/recipes/tools/AxePrimitive.xml -->
<RecipeDef>
  <defName>Recipe_AxePrimitive</defName>
  <label>Primitive Axe</label>
  <description>A crude axe made from stone and sticks</description>
  
  <!-- Materials required (ALL must be known to unlock) -->
  <ingredients>
    <ingredient material="Stone" count="2"/>
    <ingredient material="Stick" count="1"/>
    <ingredient material="PlantFiber" count="1"/>
  </ingredients>
  
  <!-- Station required to craft -->
  <station>CraftingSpot</station>
  
  <!-- Output item -->
  <outputs>
    <output item="AxePrimitive" count="1"/>
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
  
  <ingredients>
    <ingredient material="MetalIngot" count="2"/>
    <ingredient material="Wood" count="1"/>
  </ingredients>
  
  <station>Forge</station>
  
  <outputs>
    <output item="AxeStandard" count="1"/>
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

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!-- assets/stations/Smelter/Smelter.xml -->
<StationDef>
  <defName>Smelter</defName>
  <label>Smelter</label>
  <description>A clay furnace for melting ore into metal</description>
  
  <!-- Recipe to build this station -->
  <buildRecipe>
    <ingredients>
      <ingredient material="Stone" count="10"/>
      <ingredient material="Clay" count="5"/>
    </ingredients>
    <station>CraftingSpot</station>
    <workAmount>2000</workAmount>
  </buildRecipe>
  
  <!-- What categories of recipes can be done here -->
  <allowedRecipeCategories>
    <category>Smelting</category>
  </allowedRecipeCategories>
  
  <!-- Fuel requirements -->
  <fuel>
    <required>true</required>
    <acceptedTypes>
      <material>Wood</material>
      <material>Coal</material>
    </acceptedTypes>
  </fuel>
</StationDef>
```

#### Innate Recipe Marker

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!-- assets/recipes/basics/CraftingSpotRecipe.xml -->
<RecipeDef>
  <defName>Recipe_CraftingSpot</defName>
  <label>Crafting Spot</label>
  
  <!-- Empty ingredients = no materials needed -->
  <ingredients />
  
  <!-- No station required = can be placed anywhere -->
  <station>none</station>
  
  <output><item>CraftingSpot</item></output>
  
  <!-- Innate = all colonists know this from start -->
  <innate>true</innate>
</RecipeDef>
```

### Runtime Data Structures

#### Colonist Knowledge (Savegame)

```
ColonistKnowledge:
  colonist_id: string
  known_materials: Set<MaterialDef>
  known_artifacts: Set<ItemDef>     # Seen but can't make
  
  # Computed at runtime:
  available_recipes: Set<RecipeDef>  # All materials known
  # Note: Skill levels stored separately in colonist data, not here
```

#### Global Player Knowledge (Savegame)

```
PlayerKnowledge:
  discovered_items: Set<ItemDef>   # Any colonist can make
  encountered_items: Set<ItemDef>  # Seen/found but can't make
  # Everything else is unknown/fog
```

### Mod Support

Following the existing asset system patterns:

**Adding new materials/recipes (modder):**
```
mods/MyMod/assets/materials/Unobtainium/
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
    <xpath>/StationDef[defName="Smelter"]/buildRecipe/ingredients/ingredient[@material="Stone"]/@count</xpath>
    <value>5</value>  <!-- Half the stone needed -->
  </Operation>
</Patch>
```

## Player-Facing Tech Tree

The tech tree is a **discovery map**, not a shopping list.

### Node States

1. **Known (solid)** - At least one colonist knows how to craft this
2. **Encountered (ghosted)** - Player has seen this item but no colonist can make it
3. **Unknown (hidden)** - Player hasn't encountered this yet

### Visibility Rules

- Player sees nodes, not prerequisites
- Finding a bronze axe shows "Bronze Axe" as encountered
- Prerequisites remain hidden until discovered through play
- Players must experiment/explore to find the path, not read it

### Per-Colonist vs Global View

- **Global view:** "These things exist in the world" (for player planning)
- **Colonist view:** "This colonist knows how to make these" (for work assignment)

Player might know bronze axes exist, but if no colonist knows how to make one, it remains ghosted.

## Example: First Tree

Complete walkthrough of early game progression:

### Setup
- Colonists spawn knowing one innate recipe: "crafting spot" (no materials required)
- Player places crafting spot via build UI → builds instantly

### Progression
1. Bob wanders, sees a rock → learns "stone"
2. Bob wanders, sees a bush → can harvest → learns "stick"
3. Bob wanders, sees grass → can harvest → learns "plant fiber"
4. **Recipe unlock:** Bob now knows stone + stick + plant fiber → "Primitive Axe" unlocks
5. Player creates work order: 1x Primitive Axe at crafting spot
6. Bob gathers stone, harvests stick from bush, harvests plant fiber from grass
7. Bob brings materials to crafting spot
8. Bob crafts primitive axe
9. Player assigns Bob to chop tree
10. Bob chops tree → yields "wood"
11. Bob learns "wood" → new recipes unlock (wooden structures, etc.)

## Open Questions

### Resolved

**Material subtypes:** Future consideration - stone → flint, granite, obsidian with different recipe unlocks. Data model supports this via separate MaterialDef entries.

**Process knowledge:** Abstracted into station requirements. Knowing "heat transforms things" is implicit in having a smelter available.

**Quality system:** Quality is a function of skill level + station quality + random chance. Higher skill = better odds of Good/Excellent outcomes.

### Still Open

**Reverse Engineering:**
How does studying an artifact work?
- Time-based study at a research bench?
- Skill-based (higher skill = faster insights)?
- Chance-based with prerequisites (need to know related materials)?
- Gradual revelation (first learn "it's metal," then "it's an alloy," then "copper + tin")?

**Recipe Hints:**
Should partially-known recipes show clues?
- Option A: Hidden entirely until all materials known
- Option B: Show "??? + Stick = ???" after seeing stick used with unknown material
- Option C: Show "Stone + ??? = Primitive Axe" once you've seen a primitive axe

**Discovery Radius:**
How close must colonist be to "see" a material?
- Line of sight only?
- Fixed radius (10 tiles)?
- Varies by material (hard to miss a boulder, easy to miss small ore)?

**Multi-Colonist Discovery:**
When Alice discovers stone, does Bob see Alice's excited reaction and learn too?
- Proximity-based knowledge spread?
- Only through explicit teaching/manuals?
- Shared "colony knowledge" for very basic materials?

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
