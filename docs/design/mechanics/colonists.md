# Colonist Attributes

**Status:** Design (Updated 2024-12-04)

## Overview

Colonists are the core agents of the game. Each has personality traits, physical attributes, and skills that affect their behavior and capabilities.

## Basic Needs

Colonists must fulfill these physical needs to survive and thrive. See [Needs System](../systems/needs-system.md) for detailed mechanics.

**Survival Needs:**
- Eat (Hunger)
- Drink (Thirst) 
- Sleep (Energy)
- Use bathroom (Bladder)
- Wash (Hygiene)

**Wellbeing Needs:**
- Recreation/Fun
- Shelter (from weather)
- Flee (from danger)

**Note:** Drinking accelerates bladder need, creating a realistic biological loop.

## Personality (Core Attributes)

These are fixed traits that define who the colonist is.

### Physical Attributes
- **Strength** - Carrying capacity, melee power, physical labor speed
- **Agility** - Movement speed, reaction time, dexterity for fine work
- **Intelligence** - Learning speed, problem solving, research ability

### Mental/Social Attributes
- **Civility** - Social grace, politeness, conflict avoidance
- **Sanity** - Mental stability, breakdown resistance
- **Kindness** - Empathy, willingness to help others
- **Work Ethic** - Task dedication, resistance to distraction
- **Social Need** - How much interaction they require (high = extrovert)

### Skills
- **Starter Skill Level** - Initial proficiency (see [Skills](./skills.md))
- **Skill Affinity** - Natural talent for specific skills, affects learning rate and fulfillment mood

## Dynamic Status

These change moment-to-moment based on activities and environment.

### Physical Status
| Status | Range | Effect |
|--------|-------|--------|
| Energy | 0-100% | Below 30% seeks sleep, 0% = collapse |
| Hunger | 0-100% | Below 50% seeks food, 0% = starvation |
| Thirst | 0-100% | Below 50% seeks water, 0% = dehydration |
| Bladder | 0-100% | Below 30% seeks toilet, 0% = accident |
| Hygiene | 0-100% | Below 40% seeks washing, affects social |

### Mental/Emotional Status

These contribute to overall Mood score:

- **Mood** - Composite score from all contributors (0-100%)
- **Social** - Running tally of interaction quality (not a "need to socialize")
- **Recreation** - Satisfaction from leisure activities
- **Fulfillment** - Satisfaction from using preferred skills
- **Purpose** - Satisfaction from helping the colony

### Environmental Status
- **Temperature Comfort** - Too hot/cold affects mood and eventually health
- **Beauty** - Aesthetic quality of surroundings
- **Comfort** - Quality of furniture being used

### Physical Condition
- **Injuries** - Wounds, diseases, conditions
- **Immediate Effects** - Wet, cold, hot, in darkness, etc.

## Mood and Breakdown

**Mood Calculation:** Sum of all mental/emotional and environmental contributors plus active thoughts.

**Breakdown Thresholds:**
| Mood Level | Risk |
|------------|------|
| Below 35% | Minor break (wandering, sadness) |
| Below 20% | Major break (binging, withdrawal) |
| Below 5% | Extreme break (destructive behavior) |

After a breakdown ends, colonist receives temporary mood boost (catharsis).

## Backstory

Each colonist has a procedurally generated backstory affecting:
- Starting skill levels
- Skill affinities (what they enjoy)
- Default work priorities
- Personality trait tendencies

## Design Notes

- Status attributes visible to player via colonist info panel
- Core personality affects how quickly status changes
- Backstory creates initial differentiation between colonists
- Mood system similar to RimWorld but with our unique needs (drinking→bladder loop)
- Memory system means colonists only know what they've seen (see [Colonist Memory](../systems/colonist-memory.md))

## Related Documents

- [Needs System](../systems/needs-system.md) - Detailed need mechanics
- [Colonist AI](../systems/colonist-ai.md) - Decision-making behavior
- [Skills](./skills.md) - Skill progression and learning
- [Work Priorities](../systems/work-priorities.md) - Work assignment system
