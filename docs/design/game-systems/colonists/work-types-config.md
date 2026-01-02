# Work Types Configuration

**Status:** Design
**Created:** 2025-01-02
**MVP Status:** Phase 2+ (basic work types), Phase 3+ (full categories)

---

## Overview

Work types define the categories of labor colonists can perform. Rather than hardcoding work types, they're defined in XML config files. This enables:
- **Tuning:** Designers adjust work categories without code
- **Modding:** Add new work types by adding XML files
- **Capability-driven:** Work types map to entity capabilities

---

## Key Concepts

### Work Types vs Tasks

| Concept | Definition | Example |
|---------|------------|---------|
| **Work Type** | A category of work | "Harvest Wild" |
| **Work Category** | A group of related work types | "Farming" |
| **Task** | A specific instance of work | "Harvest Berry Bush at (10, 15)" |

Work types are defined in config. Tasks are generated at runtime from entities with matching capabilities.

### Capability Mapping

Work types specify which entity capabilities trigger task generation:

```
Work Type: "Harvest Wild"
Trigger Capability: Harvestable
→ Any entity with Harvestable capability generates a harvest task
```

---

## Config File Format

**File:** `assets/config/work/work-types.xml`

```xml
<?xml version="1.0" encoding="UTF-8"?>
<WorkTypes>
  <!--
    Work categories group related work types.
    Tier determines priority in simple mode (lower tier = higher priority).
    canDisable: false means all colonists always do this work.
  -->

  <!-- TIER 1: Emergency (cannot disable) -->
  <Category defName="Emergency" tier="1" canDisable="false">
    <label>Emergency</label>
    <description>Urgent actions that cannot be disabled</description>

    <WorkType defName="Work_Rescue">
      <label>Rescue</label>
      <description>Carry incapacitated colonists to safety</description>
      <triggerCapability>Incapacitated</triggerCapability>
      <taskChain>Chain_Rescue</taskChain>
    </WorkType>

    <WorkType defName="Work_Firefight">
      <label>Firefight</label>
      <description>Extinguish fires</description>
      <triggerCapability>OnFire</triggerCapability>
    </WorkType>
  </Category>

  <!-- TIER 2: Medical -->
  <Category defName="Medical" tier="2">
    <label>Medical</label>

    <WorkType defName="Work_PatientCare">
      <label>Patient Care</label>
      <description>Bring food to bedridden colonists</description>
      <triggerCapability>Bedridden</triggerCapability>
    </WorkType>

    <WorkType defName="Work_Doctoring">
      <label>Doctoring</label>
      <description>Treat injuries and illnesses</description>
      <triggerCapability>NeedsTreatment</triggerCapability>
      <skillRequired>Medicine</skillRequired>
      <minSkillLevel>1.0</minSkillLevel>
    </WorkType>

    <WorkType defName="Work_Surgery">
      <label>Surgery</label>
      <description>Perform surgical operations</description>
      <triggerCapability>NeedsSurgery</triggerCapability>
      <skillRequired>Medicine</skillRequired>
      <minSkillLevel>10.0</minSkillLevel>
    </WorkType>
  </Category>

  <!-- TIER 3: Farming -->
  <Category defName="Farming" tier="3">
    <label>Farming</label>

    <WorkType defName="Work_Plant">
      <label>Plant</label>
      <description>Sow seeds in prepared soil</description>
      <triggerCapability>Plantable</triggerCapability>
      <skillRequired>Farming</skillRequired>
    </WorkType>

    <WorkType defName="Work_HarvestCrops">
      <label>Harvest Crops</label>
      <description>Gather mature crops</description>
      <triggerCapability>Harvestable</triggerCapability>
      <filter>
        <entityGroup>crops</entityGroup>
      </filter>
      <skillRequired>Farming</skillRequired>
    </WorkType>

    <WorkType defName="Work_HarvestWild">
      <label>Harvest Wild</label>
      <description>Forage wild plants</description>
      <triggerCapability>Harvestable</triggerCapability>
      <filter>
        <entityGroup>wild_plants</entityGroup>
      </filter>
      <!-- No skill required - anyone can forage -->
    </WorkType>

    <WorkType defName="Work_AnimalCare">
      <label>Animal Care</label>
      <description>Feed and tend to animals</description>
      <triggerCapability>NeedsAnimalCare</triggerCapability>
      <skillRequired>Ranching</skillRequired>
    </WorkType>
  </Category>

  <!-- TIER 4: Crafting (dynamic per station type) -->
  <Category defName="Crafting" tier="4">
    <label>Crafting</label>

    <!--
      Dynamic work types: One work type per crafting station type.
      {StationDefName} and {StationLabel} are replaced at load time.
    -->
    <WorkType defName="Work_Craft_{StationDefName}" dynamic="true">
      <label>Craft at {StationLabel}</label>
      <description>Craft items at a {StationLabel}</description>
      <triggerCapability>Craftable</triggerCapability>
      <filter>
        <stationType>{StationDefName}</stationType>
      </filter>
      <taskChain>Chain_CraftWithGather</taskChain>
      <skillRequired>{StationSkill}</skillRequired>
    </WorkType>
  </Category>

  <!-- TIER 5: Construction -->
  <Category defName="Construction" tier="5">
    <label>Construction</label>

    <WorkType defName="Work_Build">
      <label>Build</label>
      <description>Construct new structures</description>
      <triggerCapability>Constructable</triggerCapability>
      <skillRequired>Construction</skillRequired>
    </WorkType>

    <WorkType defName="Work_Repair">
      <label>Repair</label>
      <description>Fix damaged structures</description>
      <triggerCapability>Repairable</triggerCapability>
      <skillRequired>Repair</skillRequired>
    </WorkType>

    <WorkType defName="Work_Deconstruct">
      <label>Deconstruct</label>
      <description>Tear down structures</description>
      <triggerCapability>Deconstructable</triggerCapability>
      <skillRequired>Construction</skillRequired>
    </WorkType>
  </Category>

  <!-- TIER 6: Hauling -->
  <Category defName="Hauling" tier="6">
    <label>Hauling</label>

    <WorkType defName="Work_HaulToStorage">
      <label>Haul to Storage</label>
      <description>Move loose items to storage containers</description>
      <triggerCapability>Carryable</triggerCapability>
      <targetCapability>Storage</targetCapability>
      <filter>
        <looseItem>true</looseItem>
      </filter>
      <taskChain>Chain_PickupDeposit</taskChain>
      <!-- No skill - speed based on Strength attribute -->
    </WorkType>

    <WorkType defName="Work_HaulToWorkbench">
      <label>Haul to Workbench</label>
      <description>Deliver materials to crafting stations</description>
      <triggerCapability>Carryable</triggerCapability>
      <targetCapability>Craftable</targetCapability>
      <filter>
        <neededByRecipe>true</neededByRecipe>
      </filter>
      <taskChain>Chain_PickupDeposit</taskChain>
    </WorkType>

    <WorkType defName="Work_HaulToConstruction">
      <label>Haul to Construction</label>
      <description>Deliver materials to build sites</description>
      <triggerCapability>Carryable</triggerCapability>
      <targetCapability>Constructable</targetCapability>
      <filter>
        <neededByBlueprint>true</neededByBlueprint>
      </filter>
      <taskChain>Chain_PickupDeposit</taskChain>
    </WorkType>

    <WorkType defName="Work_PlaceItem">
      <label>Place Item</label>
      <description>Carry items to their designated placement</description>
      <triggerCapability>Carryable</triggerCapability>
      <filter>
        <hasPlacementTarget>true</hasPlacementTarget>
      </filter>
      <taskChain>Chain_PlaceItem</taskChain>
    </WorkType>
  </Category>

  <!-- TIER 7: Cleaning -->
  <Category defName="Cleaning" tier="7">
    <label>Cleaning</label>

    <WorkType defName="Work_CleanIndoor">
      <label>Clean Indoor</label>
      <description>Clean indoor floors and remove debris</description>
      <triggerCapability>Cleanable</triggerCapability>
      <filter>
        <indoor>true</indoor>
      </filter>
    </WorkType>

    <WorkType defName="Work_CleanOutdoor">
      <label>Clean Outdoor</label>
      <description>Clear outdoor areas</description>
      <triggerCapability>Cleanable</triggerCapability>
      <filter>
        <indoor>false</indoor>
      </filter>
    </WorkType>
  </Category>

  <!-- OPTIONAL: Research/Learning -->
  <Category defName="Research" tier="8" optional="true">
    <label>Research</label>

    <WorkType defName="Work_SelfStudy">
      <label>Self-Study</label>
      <description>Independent research and learning</description>
      <triggerCapability>Researchable</triggerCapability>
    </WorkType>

    <WorkType defName="Work_WriteManual">
      <label>Write Manual</label>
      <description>Document skills for others to learn</description>
      <triggerCapability>WritingStation</triggerCapability>
      <skillRequired>Teaching</skillRequired>
    </WorkType>

    <WorkType defName="Work_Teach">
      <label>Teach</label>
      <description>Train other colonists</description>
      <triggerCapability>TeachingStation</triggerCapability>
      <skillRequired>Teaching</skillRequired>
    </WorkType>
  </Category>
</WorkTypes>
```

---

## Work Type Properties

### Required Properties

| Property | Description |
|----------|-------------|
| `defName` | Unique identifier |
| `label` | Human-readable name |
| `triggerCapability` | Which capability generates tasks |

### Optional Properties

| Property | Default | Description |
|----------|---------|-------------|
| `description` | "" | Tooltip text |
| `skillRequired` | none | Skill needed to do this work |
| `minSkillLevel` | 0.0 | Minimum skill level required |
| `targetCapability` | none | Second capability for two-target tasks |
| `taskChain` | none | Chain definition for multi-step tasks |
| `filter` | none | Additional conditions for task generation |

---

## Filters

Filters narrow down which entities generate tasks:

```xml
<filter>
  <entityGroup>crops</entityGroup>        <!-- Must be in this group -->
  <looseItem>true</looseItem>             <!-- Must be loose (not in storage) -->
  <indoor>true</indoor>                   <!-- Must be indoors -->
  <neededByRecipe>true</neededByRecipe>   <!-- Must be needed by active recipe -->
  <stationType>CraftingSpot</stationType> <!-- Must be this station type -->
</filter>
```

### Entity Groups

Assets can declare group membership:

```xml
<!-- assets/world/flora/BerryBush/BerryBush.xml -->
<AssetDef>
  <defName>Flora_BerryBush</defName>
  <placement>
    <groups>
      <group>wild_plants</group>
      <group>food_sources</group>
    </groups>
  </placement>
</AssetDef>
```

Work types can filter by group:
```xml
<WorkType defName="Work_HarvestWild">
  <filter>
    <entityGroup>wild_plants</entityGroup>
  </filter>
</WorkType>
```

---

## Dynamic Work Types

For crafting, we want one work type per station type, but don't want to manually list every station:

```xml
<WorkType defName="Work_Craft_{StationDefName}" dynamic="true">
  <label>Craft at {StationLabel}</label>
  <skillRequired>{StationSkill}</skillRequired>
</WorkType>
```

At load time, the system:
1. Scans all assets with `Craftable` capability
2. For each unique station type, creates a work type instance
3. Replaces placeholders with station-specific values

**Example generated work types:**
- `Work_Craft_CraftingSpot` — "Craft at Crafting Spot" (skill: none)
- `Work_Craft_Forge` — "Craft at Forge" (skill: Smithing)
- `Work_Craft_TailoringBench` — "Craft at Tailoring Bench" (skill: Tailoring)

---

## Modding: Adding New Work Types

### Adding a New Work Type to Existing Category

Create a new XML file that extends the category:

**File:** `mods/my-mod/config/work/extra-farming.xml`
```xml
<?xml version="1.0" encoding="UTF-8"?>
<WorkTypes>
  <!-- Extends existing Farming category -->
  <Category defName="Farming">
    <WorkType defName="Work_Beekeeping">
      <label>Beekeeping</label>
      <description>Tend to bee hives</description>
      <triggerCapability>BeeHive</triggerCapability>
      <skillRequired>Farming</skillRequired>
      <minSkillLevel>3.0</minSkillLevel>
    </WorkType>
  </Category>
</WorkTypes>
```

### Adding a New Category

Create a new category with a tier number:

**File:** `mods/my-mod/config/work/magic.xml`
```xml
<?xml version="1.0" encoding="UTF-8"?>
<WorkTypes>
  <!-- New category between Construction (5) and Hauling (6) -->
  <Category defName="Magic" tier="5.5">
    <label>Magic</label>

    <WorkType defName="Work_EnchantItem">
      <label>Enchant Item</label>
      <description>Apply magical enchantments</description>
      <triggerCapability>Enchantable</triggerCapability>
      <skillRequired>Enchanting</skillRequired>
    </WorkType>
  </Category>
</WorkTypes>
```

### Loading Order

Config files are loaded in order:
1. Base game: `assets/config/work/work-types.xml`
2. Mods: `mods/*/config/work/*.xml` (alphabetical by mod, then file)

Later definitions can:
- Add new work types to existing categories
- Add new categories
- Override existing work types (same defName)

---

## WorkTypeRegistry (Code)

### API

```cpp
class WorkTypeRegistry {
public:
    static WorkTypeRegistry& Get();

    // Loading
    bool loadFromFile(const std::string& xmlPath);
    size_t loadFromFolder(const std::string& folderPath);

    // Queries
    const WorkType* getWorkType(const std::string& defName) const;
    const WorkCategory* getCategory(const std::string& defName) const;
    std::vector<const WorkType*> getWorkTypesInCategory(const std::string& categoryDefName) const;
    std::vector<const WorkCategory*> getAllCategories() const;  // Sorted by tier

    // For task generation
    std::vector<const WorkType*> getWorkTypesForCapability(CapabilityType cap) const;
};
```

### Usage in Task Generation

```cpp
void TaskRegistry::generateTasksFromEntity(const DiscoveredEntity& entity) {
    // Get capabilities from asset definition
    auto caps = AssetRegistry::Get().getCapabilities(entity.defNameId);

    // Find work types that match these capabilities
    for (auto cap : caps) {
        auto workTypes = WorkTypeRegistry::Get().getWorkTypesForCapability(cap);
        for (auto* workType : workTypes) {
            // Check filters
            if (!workType->filter.passes(entity)) continue;

            // Create task
            GlobalTask task;
            task.type = workType->defName;
            task.targetEntity = entity.entityId;
            task.position = entity.position;
            task.chainDefName = workType->taskChain;
            addTask(task);
        }
    }
}
```

---

## Summary

| What | Where |
|------|-------|
| Work categories & types | `assets/config/work/work-types.xml` |
| Task chains | `assets/config/work/task-chains.xml` |
| Priority tuning | `assets/config/work/priority-tuning.xml` |
| Entity groups | Individual asset XML files |
| Mod work types | `mods/*/config/work/*.xml` |

**All work behavior is config-driven.** Adding a new work type = add XML. No code changes required.

---

## Related Documents

- [Task Registry](./task-registry.md) — How tasks are generated from work types
- [Work Priorities](./work-priorities.md) — Per-colonist work preferences UI
- [Skills](./skills.md) — Skill requirements for work types
- [Entity Capabilities](../world/entity-capabilities.md) — Capabilities that trigger tasks
- [Task Chains](./task-chains.md) — Multi-step task definitions
