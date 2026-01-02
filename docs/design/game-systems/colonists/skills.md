# Skills and Talents

**Status:** Design
**Created:** 2024-12-04
**Updated:** 2025-01-02 (Added technical implementation for task priority)
**MVP Status:** Basic skill component in Phase 2, full learning system Phase 3+

---

## High Level Concepts

- **Learn by doing**: Most things can be learned by performing the task, many will have prerequisites
- **No global research tree**: There will not be a "research" tree like in Rimworld that unlocks abilities for everyone. Instead, colonists follow their own research paths like in Oxygen Not Included. However, these paths are automatically progressed by learning
- **Skill relationships**: Learning in one tree can influence another tree. For instance, a super advanced woodworker should have some basic understanding of metalworking. There will be a mapping between related skills
- **Knowledge transfer**: Colonists can write manuals/books to teach others their skills

## Learning Methods

Colonists can gain skill points through:

1. **Doing** - Performing tasks
2. **Self study (Researching)** - Independent research
3. **Self study (Book assisted)** - Reading manuals/books
4. **Being taught** - Learning from other colonists
5. **Being taught (Book assisted)** - Structured learning with materials

## Creating Knowledge Resources

- If a colonist has a particular skill, they can write a manual/book (pick term)
- Other colonists may read this resource to learn the skill faster
- **Note:** Don't limit to books - maybe call "manual"? Could be in a computer?

## Example: Learning to Make Clothing

1. **Starting Point**: None of your colonists knows anything about making clothes, and they need new clothing. However everyone has an innate ability to create really crap "improvised" clothing, so the colony can start this crafting process.

2. **First Order**: The player puts in a request to have 10x "improvised shirts" and 10x "improvised pants" crafted at a location that allows crafting of this item. At this tier, any table will do and any colonist can do it, and the priority system puts the task into the queue. A colonist named Bob picks up the task and they begin to craft the items. They create knowledge and level up in clothing creation as they do so, tick by tick. This rate will be determined by a number of things such as their intelligence. This accrual is time based, not based on the completion of the task. Even if they fail, they will have learned something.

3. **Building Skill**: Each item Bob crafts of the improvised clothing increases their skill. Every item has a somewhat random chance of having a specific quality within its tier, including complete failure. If they accidentally create a good improvised shirt, it adds more knowledge at completion than just creating a poor improvised shirt.

4. **Specialization Emerges**: That colonist, Bob, now knows more about making improvised clothing than anyone else at the colony and the priority system now has a bias for this colonist doing the task again. At this point anyone can do it, so the player must decide if they want to limit the crafting order to one colonist.

5. **Assignment**: The player decides that this colonist, Bob, should do the tailoring for the colony, and modifies the crafting order/request at the table so that only that colonist can do it. (**Note:** There still is nothing that says that this colonist is a "tailor", they just happen to have some experience now and are assigned the tasks.) This limit can be set on just the order, on the workstation that the order is at, or even the ownership of the room (see [rooms.md](./rooms.md)) can be set. The player decides. Perhaps they created a specific room for tailoring things, and they want to now limit jobs in that room to just be for Bob.

6. **Tier Unlock**: Once enough skill/knowledge/xp accrues, they have an "inspiration" moment and unlock the next tier of that specific item group. In this case, after enough improvised shirts, Bob would unlock the ability to create "rough" clothing (or whatever tier is next). **THIS DOES NOT UNLOCK FOR THE WHOLE COLONY**, just that colonist, Bob. There needs to be a UI affordance for when this next tier gets unlocked, because each tier is a deliberate crafting target.

7. **Continued Progression**: The player then chooses to change the crafting order to create 10x "rough" shirts and pants, and the cycle continues. However, for this next order, they no longer set an owner for this task, since Bob is the only one in the colony that can do it.

8. **Knowledge is Fragile**: Bob continues to craft and gain experience, as before. Then Bob dies of dysentery. The colony has now lost the ability to create "rough" tier shirts since Bob never wrote down his thoughts or taught anyone on how to do it. There are still some "rough" tier items around, but nobody knows how to make them anymore. The cycle starts again. Next time around the player might want to have their tailor write a manual on how to make that item so that others can learn how to do it faster.

## Notes

- **No hauling skill**: There will not be a "hauling" skill or a generic "manual labor" skill. The act of doing something will be a calculation that takes into account skill, traits, environment and possibly more. Generic tasks like hauling will just be based on strength without a skill
- **Skill combinations**: Other tasks will gain a bonus from multiple skills
- **Construction and Repair**: These are more complicated. Someone who is a skilled woodworker and has low repair should not be able to repair advanced machines. This will require some tuning

## Skill List

### Trades
- **Woodworking** - Working with wood materials
- **Smithing** - Forging metal items
- **Metalworking / Machining** - Advanced metal fabrication
- **Masonry** - Stone and brick work
- **Electronics** - Electronic devices and circuits
- **Tailoring** - Clothing and fabric work
- **Cooking** (includes butchering) - Food preparation
- **Medicine** (doctoring) - Medical treatment
- **Farming** (starts at gathering?) - Growing crops
- **Ranching** - Animal husbandry
- **Art** - Creating art and decorations

### Activities
- **Hunting** - Tracking and hunting animals
- **Mining** - Extracting minerals

### Generic (joint with other skills)
- **Cleaning** - Maintaining cleanliness
- **Construction** - Building structures
- **Repair** - Fixing broken items
- **Harvesting** (wood and crops?) - Gathering resources
- **Shooting** - Ranged combat
- **Fighting** - Melee combat

## Tasks and Priorities

### Emergency
- Rescue
- Firefight
- Patient (heal)

### Critical
- Non-critical patient care

### Standard Priority
- Tend (doctor)
- Basic tasks (close doors, open things)
- Repair
- Harvesting (everyone pitches in on basic harvest tasks, it takes a village!)
  - Harvest crops
  - Forage (wild)
  - Chop trees (wild)
- Construct
- Skilled labor (at workbenches and items) - includes farming and ranching
- Clean
- Haul (organize)

---

## Technical Implementation: Skills Component

### Overview

For the task priority system, colonists need a Skills component that tracks their proficiency in each skill area. This affects:
1. **Task Priority:** Skilled colonists prefer work they're good at (+skill bonus)
2. **Work Access:** Some work types require minimum skill levels
3. **Efficiency:** Higher skill = faster completion, better quality (future)

### Skills Component (ECS)

```cpp
struct Skills {
    // Skill levels: 0.0 (untrained) to 20.0 (master)
    std::unordered_map<std::string, float> levels;

    // Get skill level (0.0 if not in map)
    float getLevel(const std::string& skillDefName) const {
        auto it = levels.find(skillDefName);
        return it != levels.end() ? it->second : 0.0f;
    }

    // Check if colonist meets skill requirement
    bool meetsRequirement(const std::string& skillDefName, float minLevel) const {
        return getLevel(skillDefName) >= minLevel;
    }
};
```

### Skill Level Ranges

| Level | Description | Unlock |
|-------|-------------|--------|
| 0.0 | Untrained | Basic work (improvised items) |
| 1.0-4.0 | Novice | Tier 1 recipes |
| 5.0-9.0 | Competent | Tier 2 recipes |
| 10.0-14.0 | Skilled | Tier 3 recipes |
| 15.0-19.0 | Expert | Tier 4 recipes |
| 20.0 | Master | All recipes, teaching bonus |

### Skill Bonus for Task Priority

From [Priority Config](./priority-config.md):

```cpp
int16_t calculateSkillBonus(float skillLevel, const SkillConfig& config) {
    // config.multiplier = 10 (default)
    // config.maxBonus = 100 (default)
    return std::min(
        static_cast<int16_t>(skillLevel * config.multiplier),
        config.maxBonus
    );
}
```

**Examples:**
- Level 0 (untrained): +0 priority
- Level 5 (competent): +50 priority
- Level 10 (skilled): +100 priority (capped)
- Level 20 (master): +100 priority (capped)

**Effect:** A skilled farmer has +100 priority bonus for farming tasks. They'll naturally prefer farming over other work they're less skilled at.

### Skill Requirements for Work Types

Defined in `assets/config/work/work-types.xml`:

```xml
<WorkType defName="Work_Doctoring">
  <label>Doctoring</label>
  <skillRequired>Medicine</skillRequired>
  <minSkillLevel>1.0</minSkillLevel>
</WorkType>

<WorkType defName="Work_Surgery">
  <label>Surgery</label>
  <skillRequired>Medicine</skillRequired>
  <minSkillLevel>10.0</minSkillLevel>
</WorkType>
```

If colonist doesn't meet `minSkillLevel`, the work type is **locked** (cannot enable in work priorities UI).

### Default Skills by Backstory

Colonists start with skills based on backstory:

```xml
<!-- assets/config/colonists/backstories.xml -->
<Backstory defName="Backstory_Farmer">
  <label>Farmer</label>
  <description>Grew up on a farm...</description>
  <startingSkills>
    <Skill name="Farming" level="5.0"/>
    <Skill name="Ranching" level="2.0"/>
    <Skill name="Cooking" level="1.0"/>
  </startingSkills>
</Backstory>
```

### MVP Scope

**Phase 2 (Basic):**
- Skills component with level tracking
- Skill bonus for task priority
- Skill requirements for work types
- Starting skills from backstory

**Phase 3+ (Full System):**
- XP gain from performing tasks
- Tier unlocks (inspiration moments)
- Knowledge transfer (manuals, teaching)
- Skill decay over time
- Passion system (learning rate bonus)

---

## Related Documents

- [Priority Config](./priority-config.md) — Skill bonus formula
- [Work Types Config](./work-types-config.md) — Skill requirements per work type
- [Task Registry](./task-registry.md) — How skills affect task filtering
- [Technology Discovery](./technology-discovery.md) — How skills unlock recipes
