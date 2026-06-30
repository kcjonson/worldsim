# Priority Configuration System

**Status:** Design
**Created:** 2025-01-02
**MVP Status:** Phase 2+ (after basic needs/work loop)

---

## Overview

Task priority is calculated using a lexicographic `(tier: int, score: float)` key. Tier is compared first (lower number = higher priority) and is inviolable — a far tier-4 option always beats a near tier-5 one. Score orders options within the same tier only. All score weights and tier assignments are defined in `assets/config/work/priority-tuning.xml`, allowing tuning without code changes.

The numeric bands documented below (`Critical 30000`, `Needs 10000`, etc.) **no longer drive AI arbitration** — they have been replaced by the tier integer. The bands and `getBandBase()` survive only for the separate work-type **display-priority** path (the global task list UI), which is unchanged. See [Colonist Task Arbitration](../../../technical/colonist-task-arbitration.md) for the full implementation.

**Design Goal:** Make priority tuning a data-driven process. When testers report "colonists walk too far for low-priority tasks," designers can adjust distance penalties without programmer involvement.

---

## Two Priority Concepts

**Important:** There are two distinct priority concepts. Don't confuse them.

### 1. Colonist Work Type Preference (1-9)

Each colonist has a personal preference for each work category:

| Colonist | Farming | Hauling | Crafting |
|----------|---------|---------|----------|
| Bob | 2 (prefers) | 7 (avoids) | 5 (neutral) |
| Alice | 6 | 3 | 2 |

- **Lower number = colonist prefers this work**
- Set per-colonist in the work priorities UI
- Maps to Work bands via `UserPriorityMapping` (see below)
- Affects which *type* of work a colonist gravitates toward

### 2. Goal Priority (Buildings/Entities)

Buildings and entities can be marked with urgency:

- Storage container marked "Urgent" → all haul tasks to it get +bonus
- Crafting station marked "High" → crafting orders there score higher
- Construction blueprint marked "Critical" → builds it first

- **Set on the goal (building/entity), not on individual tasks**
- Affects all tasks that target that goal
- Players define outcomes ("fill this with stone"), not individual tasks

### How They Combine (AI selection)

AI arbitration is a `(tier, score)` key, not a single float. Within the same tier:

```
score = distanceFactor          // dominant; 300*max(0,1-d/60) — nearest source wins
      + skillBonus              // colonist's skill level for this work type
      + goalPriority adjustment // urgent storage etc. — within-tier lift only
      + taskAgeBonus            // stale tasks slowly rise
      + hysteresisBonus         // +50 on the current in-progress option only
```

Chain steps (`servesActiveWorkOrder == true`) are classified at tier 4, not scored higher within tier 6. Work type preference (1-9) affects within-tier ordering for tier 6 opportunistic work.

---

## Priority Formula

### AI Selection: lexicographic (tier, score)

When a colonist selects their next task, options are compared as `(tier, score)` pairs. Tier comes from the task type's definition in `priority-tuning.xml <TaskTiers>` (validated at startup; unassigned types fail with an error). Score orders within a tier only:

```
score = distanceFactor      // dominant: 300 * max(0, 1 - d/60) — nearest source wins
      + skillBonus          // 0 to +100
      + taskAgeBonus        // 0 to +100 (old tasks rise)
      + hysteresisBonus     // +50 applied to the in-progress option only (same tier)
```

`chainBonus` (+2000) is gone — a chain step is classified at tier 4 (active work order), not scored higher within tier 6. `inProgressBonus` is gone — it is replaced by the `hysteresisBonus` (50) applied within the same tier only. `kWorkOrderProvisionFloor` and `kStorageStockingFloor` are deleted; tiers make them unnecessary.

### Display priority (UI task list only)

For the global task list (no colonist context), a separate display-priority path is unchanged:

```
displayPriority = basePriority - (distance * distancePenaltyMultiplier)
```

`basePriority` here comes from `getBandBase()` / the `<Bands>` section of `priority-tuning.xml`, which survives solely for this display path.

---

## Priority Bands (display path only)

These bands are **display-priority only** — they drive the global task list UI, not AI arbitration. AI arbitration uses the tier integer from `<TaskTiers>` instead. Colonist work type preferences (1-9) still map into the Work bands for display purposes.

| Band Name | Base Value | Description |
|-----------|------------|-------------|
| Critical | 30000 | Life-threatening (< 10% need) |
| PlayerDraft | 20000 | Direct player command to colonist |
| Needs | 10000 | Actionable needs (below threshold) |
| WorkHigh | 5000 | Colonist preference 1-3 (prefers) |
| WorkMedium | 3000 | Colonist preference 4-6 (neutral) |
| WorkLow | 1000 | Colonist preference 7-9 (avoids) |
| Idle | 0 | Wander (fallback) |

### Colonist Work Type Preference Mapping

Each colonist's 1-9 work type preference maps into the Work bands.
This is NOT per-task priority — it's the colonist's preference for work categories.

```
User Priority 1 → WorkHigh + 800 = 5800
User Priority 2 → WorkHigh + 700 = 5700
User Priority 3 → WorkHigh + 600 = 5600
User Priority 4 → WorkMedium + 500 = 3500
User Priority 5 → WorkMedium + 400 = 3400
User Priority 6 → WorkMedium + 300 = 3300
User Priority 7 → WorkLow + 200 = 1200
User Priority 8 → WorkLow + 100 = 1100
User Priority 9 → WorkLow + 0 = 1000
```

Formula: `bandBase + (10 - userPriority) * step`

---

## Config File Format

**File:** `assets/config/work/priority-tuning.xml`

```xml
<?xml version="1.0" encoding="UTF-8"?>
<PriorityTuning>
  <!-- Priority bands define the int16 ranges for each tier -->
  <Bands>
    <Band name="Critical" base="30000">
      <description>Life-threatening situations (critical needs)</description>
    </Band>
    <Band name="PlayerDraft" base="20000">
      <description>Direct player control overrides</description>
    </Band>
    <Band name="Needs" base="10000">
      <description>Actionable needs below seek threshold</description>
    </Band>
    <Band name="WorkHigh" base="5000">
      <description>User priority 1-3 work tasks</description>
    </Band>
    <Band name="WorkMedium" base="3000">
      <description>User priority 4-6 work tasks</description>
    </Band>
    <Band name="WorkLow" base="1000">
      <description>User priority 7-9 work tasks</description>
    </Band>
    <Band name="Idle" base="0">
      <description>Wander and idle behaviors</description>
    </Band>
  </Bands>

  <!-- User priority (1-9) to band mapping -->
  <UserPriorityMapping>
    <bandAssignment priority="1-3" band="WorkHigh"/>
    <bandAssignment priority="4-6" band="WorkMedium"/>
    <bandAssignment priority="7-9" band="WorkLow"/>
    <stepSize>100</stepSize>
  </UserPriorityMapping>

  <!-- Work category tier ordering (for simple mode) -->
  <WorkCategoryOrder>
    <Category name="Emergency" tier="1"/>
    <Category name="Medical" tier="2"/>
    <Category name="Farming" tier="3"/>
    <Category name="Crafting" tier="4"/>
    <Category name="Construction" tier="5"/>
    <Category name="Hauling" tier="6"/>
    <Category name="Cleaning" tier="7"/>
  </WorkCategoryOrder>

  <!-- Bonus calculations -->
  <Bonuses>
    <!-- Distance factor: dominant within-tier term; nearest source wins -->
    <Distance>
      <formula>300 * max(0, 1 - distance / 60)</formula>
      <description>
        Yields 0-300 across the working range (~60m). Dominates skill and age
        so that the nearest reachable source reliably wins within a tier.
        Replaces the old -50..+50 distanceBonus, which was too narrow relative
        to the skill/age terms and allowed distant tasks to win via bonuses.
      </description>
    </Distance>

    <!-- Skill bonus: skilled colonists prefer work they're good at -->
    <Skill>
      <multiplier>10</multiplier>
      <maxBonus>100</maxBonus>
      <formula>min(skillLevel * multiplier, maxBonus)</formula>
    </Skill>

    <!-- Chain continuation: chain steps are classified at tier 4 (active work order),
         not scored higher within tier 6. The +2000 score term is deleted;
         servesActiveWorkOrder == true drives the tier assignment instead. -->
    <ChainContinuation>
      <bonus>0</bonus>
      <description>
        SUPERSEDED: chain bonus is now a tier-4 classification, not a score term.
        A provisioning haul/harvest with servesActiveWorkOrder=true is assigned
        tier 4 rather than tier 6; no score inflation needed or applied.
      </description>
    </ChainContinuation>

    <!-- In-progress: within-tier hysteresis margin (replaces the old cross-tier bonus) -->
    <InProgress>
      <bonus>50</bonus>
      <description>
        Applied to the currently in-progress option when comparing within the same tier.
        Prevents thrashing on small score differences. A same-tier challenger must
        exceed the in-progress option's score by this margin to trigger a switch.
        This is the taskSwitchThreshold from Thresholds (kept in sync).
      </description>
    </InProgress>

    <!-- Task age: old unclaimed tasks slowly rise in priority -->
    <TaskAge>
      <bonusPerMinute>1</bonusPerMinute>
      <maxBonus>100</maxBonus>
      <description>
        Prevents task starvation. After 100 minutes, even low-priority
        tasks get +100 bonus.
      </description>
    </TaskAge>
  </Bonuses>

  <!-- Thresholds and timings -->
  <Thresholds>
    <!-- Within-tier hysteresis margin: same-tier challenger must exceed in-progress
         option's score by this amount to trigger a switch. Matches InProgress.bonus. -->
    <taskSwitchThreshold>50</taskSwitchThreshold>

    <!-- How often colonists re-evaluate tasks (seconds) — unchanged -->
    <reEvalInterval>0.5</reEvalInterval>

    <!-- Reservation timeout: release if no progress (seconds) — unchanged -->
    <reservationTimeout>10.0</reservationTimeout>
  </Thresholds>

  <!-- Hauling-specific tuning to avoid the "hauling problem" -->
  <HaulingTuning>
    <!-- Boost haul priority when storage is critically low -->
    <storageCriticalThreshold>0.2</storageCriticalThreshold>
    <storageCriticalBonus>500</storageCriticalBonus>

    <!-- Boost if loose item is blocking construction -->
    <blockingConstructionBonus>1000</blockingConstructionBonus>

    <!-- Boost if item is perishable and will spoil soon -->
    <perishableSpoilThreshold>60.0</perishableSpoilThreshold>
    <perishableBonus>800</perishableBonus>

    <!-- Batch nearby items to reduce task spam -->
    <batchRadius>8.0</batchRadius>
    <maxBatchSize>5</maxBatchSize>
  </HaulingTuning>
</PriorityTuning>
```

---

## Bonus Details

### Distance factor

The dominant within-tier term; nearest reachable source wins.

```cpp
float distanceFactor(float distance) {
    return 300.0f * std::max(0.0f, 1.0f - distance / 60.0f);
}
```

**Examples:**
- 0m away: 300 (maximum)
- 15m away: 225
- 30m away: 150
- 45m away: 75
- 60m+ away: 0

The old `±50 distanceBonus` range was too narrow relative to the skill/age terms; a high-skill colonist could prefer a tree 70m away over an adjacent one. The new range (0-300) dominates skill (0-100) and age (0-100), so the nearest source reliably wins within a tier.

### Skill Bonus

Skilled colonists naturally gravitate toward work they're good at.

```cpp
int16_t calculateSkillBonus(float skillLevel, const SkillConfig& config) {
    return std::min(
        static_cast<int16_t>(skillLevel * config.multiplier),
        config.maxBonus
    );
}
```

**Examples (multiplier=10, max=100):**
- Skill level 0: +0
- Skill level 5: +50
- Skill level 8: +80
- Skill level 10+: +100 (capped)

### Chain continuation

Chain steps are kept together by tier classification, not score inflation. Any haul or harvest task with `servesActiveWorkOrder == true` is assigned tier 4 (active work order). Since tier 4 beats tier 5 (actionable needs) and tier 6 (opportunistic work), chain steps stay bound to their parent order without a score term. The old `+2000 chainBonus` is deleted.

**Effect:** A colonist finishing a provisioning haul cannot be pulled away by a non-critical need or an opportunistic task, because the chain step is tier 4 and those alternatives are tier 5 or tier 6.

### Task Age Bonus

Prevents low-priority tasks from being starved forever.

```cpp
int16_t calculateTaskAgeBonus(float taskAge, const TaskAgeConfig& config) {
    float minutes = taskAge / 60.0f;
    return std::min(
        static_cast<int16_t>(minutes * config.bonusPerMinute),
        config.maxBonus
    );
}
```

**Example (1 per minute, max 100):**
- Created just now: +0
- 30 minutes old: +30
- 100+ minutes old: +100 (capped)

---

## Hauling Problem Mitigation

The "hauling problem" (from RimWorld) occurs when colonists haul everything before doing other work, often walking across the entire map for low-value items.

### Mitigation Strategies

1. **Distance Bonus:** Nearby items score higher than distant ones
2. **Storage Critical Boost:** When storage is < 20% full for an item category, boost haul priority by +500
3. **Blocking Construction:** Items blocking build sites get +1000 boost
4. **Perishable Items:** Food that will spoil in < 60s gets +800 boost
5. **Batch Nearby Items:** Group items within 8m into single task (max 5 per batch)

```cpp
int16_t calculateHaulPriority(const HaulTask& task, const HaulingConfig& config) {
    int16_t priority = WorkLow;  // Base: hauling is low priority

    // Storage critical?
    if (getStorageFullness(task.itemCategory) < config.storageCriticalThreshold) {
        priority += config.storageCriticalBonus;
    }

    // Blocking construction?
    if (isBlockingConstruction(task.sourcePosition)) {
        priority += config.blockingConstructionBonus;
    }

    // Perishable?
    if (task.spoilTime > 0 && task.spoilTime - gameTime < config.perishableSpoilThreshold) {
        priority += config.perishableBonus;
    }

    return priority;
}
```

---

## Config Loading

### Registry Class

```cpp
class PriorityConfig {
public:
    static PriorityConfig& Get();

    bool loadFromFile(const std::string& xmlPath);

    // Band queries
    int16_t getBandBase(const std::string& bandName) const;
    int16_t userPriorityToBase(uint8_t userPriority) const;

    // Bonus calculations
    int16_t calculateDistanceBonus(float distance) const;
    int16_t calculateSkillBonus(float skillLevel) const;
    int16_t calculateChainBonus(uint64_t currentChainId, uint8_t currentStep,
                                uint64_t candidateChainId, uint8_t candidateStep) const;
    int16_t calculateTaskAgeBonus(float taskAge) const;
    int16_t calculateHaulBonus(const HaulContext& context) const;

    // Thresholds
    float getTaskSwitchThreshold() const;
    float getReEvalInterval() const;
    float getReservationTimeout() const;

private:
    BandConfig m_bands;
    BonusConfig m_bonuses;
    ThresholdConfig m_thresholds;
    HaulingConfig m_hauling;
};
```

### Loading at Startup

```cpp
// In AppLauncher.cpp or similar
void initializeGameSystems() {
    // ... existing asset loading ...

    // Load priority tuning
    std::string configPath = Foundation::findResourceString("assets/config/work/priority-tuning.xml");
    if (!PriorityConfig::Get().loadFromFile(configPath)) {
        LOG_ERROR(Engine, "Failed to load priority config from %s", configPath.c_str());
    }
}
```

---

## Tuning Guidelines

### When to Increase Distance Penalty

**Symptom:** Colonists walk across map for distant tasks when nearby tasks exist.

**Fix:** Increase `maxPenalty` or decrease `maxPenaltyDistance`.

### When to Increase Skill Bonus

**Symptom:** Skilled colonists don't gravitate toward their specialty.

**Fix:** Increase `multiplier` (but watch for skill bonus dominating distance).

### When to Increase Chain Bonus

**Symptom:** Colonists drop items mid-haul for other tasks.

**Fix:** Increase `chainContinuationBonus` (2000 is already large).

### When to Adjust Hauling Config

**Symptom:** Colonists haul constantly instead of doing other work.

**Fix:** Decrease `storageCriticalBonus`, or tighten `storageCriticalThreshold`.

**Symptom:** Food spoils because nobody hauls it.

**Fix:** Increase `perishableBonus` or increase `perishableSpoilThreshold`.

---

## Related Documents

- [Task Registry System](./task-registry.md) — Where priorities are used
- [Task Generation Architecture](../../technical/task-generation-architecture.md) — When priorities are calculated
- [Task Chains](./task-chains.md) — Multi-step tasks and chain bonus
- [Work Priorities](./work-priorities.md) — User-facing 1-9 priority system
