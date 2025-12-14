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

**Implementation Note:** Tracked in code now; AI does not seek washing yet (no fulfillment sources).

**MVP Status:** Deferred to Phase 2

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

**Implementation Status:** Mood is computed in code from needs using weighted penalties (see table below) and shown near the colonist portrait/sidebar. Needs above ~70% contribute no penalty; below ~30% the penalty ramps quickly.

### Mood Contributors

| Contributor | Source | Type |
|-------------|--------|------|
| Need Satisfaction | All needs above comfortable | Continuous |
| Beauty | Aesthetic quality of surroundings | Environmental |
| Comfort | Quality of furniture being used | Environmental |
| Temperature | Comfort relative to tolerance | Environmental |
| Social | Running tally of interaction quality | Social |
| Fulfillment | Using preferred skills | Work |
| Purpose | Completing meaningful tasks | Work |
| Thoughts | Temporary event modifiers | Events |

### Need-to-Mood Weights (current tuning)

| Need | Weight | Notes |
|------|--------|-------|
| Hygiene | 1.0 | Biggest comfort/social driver |
| Temperature | 1.0 | Thermal comfort is critical to morale |
| Recreation | 0.9 | Fun/leisure strongly affects mood |
| Energy | 0.7 | Tiredness hurts mood meaningfully |
| Hunger | 0.6 | Survival need, medium mood impact |
| Thirst | 0.6 | Survival need, medium mood impact |
| Bladder | 0.3 | Minor mood hit unless accidents |
| Digestion | 0.3 | Minor mood hit unless accidents |

Penalty curve per need: 0 when value ≥ ~70%; mild up to ~0.3 penalty down to 30%; steep to 1.0 below 30%. Mood = 100 × (1 − weightedPenalty/totalWeight), clamped 0-100.

### Thoughts (Temporary Modifiers)

Examples:
- "Ate a good meal" (+5, 6 hours)
- "Slept on ground" (-10, 1 day)
- "Had accident in public" (-20, 1 day)
- "Talked with friend" (+5, 6 hours)

### Breakdown Thresholds

| Mood Level | Risk |
|------------|------|
| Below 35% | Minor break (wandering, sadness) |
| Below 20% | Major break (binging, withdrawal) |
| Below 5% | Extreme break (destructive behavior) |

After a breakdown ends, colonist receives temporary mood boost (catharsis).

**MVP Status:** Basic mood (need satisfaction only) in Phase 1. Full mood system deferred to Phase 3.

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
