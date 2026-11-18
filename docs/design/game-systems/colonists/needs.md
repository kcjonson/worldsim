# Colonist Needs System

**Status:** Design  
**Created:** 2024-12-04  
**MVP Status:** See [MVP Scope](../mvp-scope.md) — Hunger, Thirst, Energy, Bladder included in Phase 1

---

## Overview

Colonists have physical and emotional needs. Some needs drive behavior (actionable), others contribute to mood. This document defines ALL need mechanics in one place.

---

## Actionable Needs

These create tasks. When they drop below threshold, colonist seeks fulfillment.

### Hunger

| Property | Value |
|----------|-------|
| Seek Threshold | ~50% |
| Critical Threshold | ~10% |
| Failure Effect | Starvation damage |
| Fulfilled By | Eating food (Edible capability) |

Different foods restore different amounts. Food quality + colonist preferneces affects mood.

**eating effects need to use bathroom**


### Thirst

| Property | Value |
|----------|-------|
| Seek Threshold | ~50% |
| Critical Threshold | ~10% |
| Failure Effect | Dehydration damage |
| Fulfilled By | Drinking water (Drinkable capability) |

Clean vs dirty water affects health. **Drinking accelerates bladder need** (biological loop).

### Energy (Sleep)

| Property | Value |
|----------|-------|
| Seek Threshold | ~30% |
| Critical Threshold | ~10% |
| Failure Effect | Collapse where standing |
| Fulfilled By | Sleeping (Sleepable capability) |

Sleep quality affects recovery rate. Better sleeping spots = faster recovery + mood bonus.

### Bladder / Poop

| Property | Value |
|----------|-------|
| Seek Threshold | ~30% |
| Critical Threshold | ~10% |
| Failure Effect | Accident |
| Fulfilled By | Toilet capability OR outdoor spot |

**Accelerated by drinking / eating.** Each drink / food adds to bladder / poop need.

**Outdoor Relief Rules:**
- Must be outdoors
- NOT adjacent to water sources
- PREFER near existing waste (clustering)
- PREFER away from high-traffic areas
- Creates "Bio Pile" entity

**Accident Consequences:**
1. Major mood penalty (embarrassment)
2. Hygiene drops to zero
3. Social penalty if witnessed
4. Creates Bio Pile at location (requires cleanup)

### Hygiene

| Property | Value |
|----------|-------|
| Seek Threshold | ~40% |
| Critical Threshold | ~15% |
| Failure Effect | Continuous mood/social penalty |
| Fulfilled By | Washing (Washable capability) |

Drops to zero on bathroom accident. Low hygiene = others like you less.

A colonist washes at water the way it drinks: below the seek threshold it walks to the nearest shore it remembers and washes there, which restores Hygiene fully. Every shore point is both Drinkable and Washable, so baths and showers are more Washable sources for the same search.

### Recreation

| Property | Value |
|----------|-------|
| Seek Threshold | ~30% |
| Critical Threshold | ~10% |
| Failure Effect | Work efficiency drops, mood penalty |
| Fulfilled By | Recreation activities, socializing |

Decays while working, recovers while idle or recreating.

**Implementation Note:** Tracked in code now; AI does not seek recreation yet (no fulfillment sources).

**MVP Status:** Deferred to Phase 2

### Temperature / Thermal Comfort (Environmental)

| Property | Value |
|----------|-------|
| Comfort Band | Colonist-specific range (e.g., 15-25°C)
| Effects | Outside band increases Energy decay, reduces sleep recovery, and can apply mood/health penalties when extreme |
| Fulfilled By | Warmer/colder locations, shelter/indoors, fires, clothing (future) |

Not a need bar; it is an environmental modifier. Good shelter improves temperature stability and therefore boosts sleep quality rather than being its own need.

**Implementation Note:** Tracked in code as a Temperature need placeholder; AI does not act on it yet, and there is no fulfillment source until the temperature field exists.

**MVP Status:** Deferred to Phase 2 (framework introduced when temperature field lands; no separate Shelter need)

### Flee

| Property | Value |
|----------|-------|
| Trigger | Active threat |
| Fulfilled By | Reaching safety |

Highest priority. See Panic in [AI Behavior](./ai-behavior.md).

**MVP Status:** Deferred (no threats in MVP)

---

## Mood System

Mood is a composite score from all contributors. Range 0-100%.

### Mood Contributors

Mood has two parts ([Mood](./mood.md)):

| Contributor | Source |
|-------------|--------|
| Need satisfaction | 100, less what each unmet need costs, the costs added |
| Mood effects | Carried by [thoughts](./thoughts.md). Everything else that changes how a colonist feels (surroundings, comfort, company, work they like, things that happen to them) reaches mood as a thought |

### What a need costs mood

Each need takes its own points off mood, and a need that is met gives no credit against one that is not. Every need has a cost, the most mood it takes off at its worst, and the penalty curve says how much of that cost applies: none at or above the comfortable level, rising gently to the warning level, then steeply to the whole cost at an empty need. The needs score is 100 minus the sum of what the needs take off, never below 0. Freezing costs a colonist 40 points however well fed he is, and adding a need changes nothing about what the others cost.

These are the costs:

| Need | Cost |
|---|---|
| Temperature | 40 |
| Hunger | 25 |
| Thirst | 25 |
| Energy | 20 |
| Hygiene | 15 |
| Recreation | 15 |
| Bladder | 10 |
| Digestion | 10 |

Each need's cost is on its own entry in `assets/config/colonists/needs/`, beside its thresholds, so a need added by a mod carries its own cost and a mod file that replaces a need replaces its cost with it. `assets/config/colonists/mood.xml` holds the penalty curve, which every need shares.

### What low mood does

See [Colonist Mood](./mood.md): the roll curve, the behavior list, refusing work and food, and relief.

---

## Need-Capability Mapping

| Need | Capability Required | Example Entities |
|------|---------------------|------------------|
| Hunger | Edible | Berry Bush, Meals, Raw Meat |
| Thirst | Drinkable | Pond, Well, Water Bottle |
| Energy | Sleepable | Ground, Bedroll, Bed |
| Bladder | Toilet | Latrine, Toilet, Ground (fallback) |
| Hygiene | Washable | Pond, Bath, Shower |
| Recreation | Recreational | Horseshoe Pit, Chess Table |

See [Entity Capabilities](../world/entity-capabilities.md) for full capability definitions.

---

## Related Documents

- [AI Behavior](./ai-behavior.md) — How needs trigger behavior tiers
- [Colonist Attributes](./attributes.md) — Static personality traits
- [Entity Capabilities](../world/entity-capabilities.md) — How entities fulfill needs
- [MVP Scope](../mvp-scope.md) — What's included when

---

## Historical Addendum

This document consolidates content from two previous files. Original discussions preserved below.

### Previously in docs/design/mechanics/colonists.md

The original colonists.md file contained a "Basic Needs" section that listed:

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

This was duplicated in needs-system.md with more detail. The colonists.md file also had a "Dynamic Status" section with the same mood/breakdown thresholds now consolidated here.

**Note from original:** "Drinking accelerates bladder need, creating a realistic biological loop."

### Previously in docs/design/systems/needs-system.md

The original needs-system.md had its own "MVP Scope" section:

```
**Phase 1:** Hunger, Thirst, Energy, Bladder. Ground sleeping, outdoor bathroom with Bio Pile. Basic mood (need satisfaction only).

**Phase 2:** Hygiene, Recreation, Shelter. Mood thoughts.

**Phase 3:** Full mood system with all contributors. Breakdown behaviors.
```

This has been consolidated into the central [MVP Scope](../mvp-scope.md) document.

### Consolidation Date
- **2024-12-04:** Merged mechanics/colonists.md (needs sections) + systems/needs-system.md → game-systems/colonists/needs.md
