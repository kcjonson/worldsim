# Priority Configuration System

**Status:** Design
**Created:** 2025-01-02
**MVP Status:** Phase 2+ (after basic needs/work loop)

---

## Overview

Task priority is calculated using a lexicographic `(tier: int, score: float)` key. Tier is compared first (lower number = higher priority) and is inviolable; a far tier-4 option always beats a near tier-5 one. Score orders options within the same tier only. All score weights and tier assignments are defined in `assets/config/work/priority-tuning.xml`, allowing tuning without code changes.

The numeric bands of the earlier design (`Critical 30000`, `Needs 10000`, etc.) were replaced by the tier integer and are gone, config and accessors both: nothing read them. See [Colonist Task Arbitration](../../../technical/colonist-task-arbitration.md) for the full implementation.

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
- Not implemented yet: no colonist carries a preference and nothing reads one
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
score = distanceFactor          // dominant; 300*max(0,1-d/60), nearest source wins
      + skillBonus              // colonist's skill level for this work type
      + goalPriority adjustment // urgent storage etc., within-tier lift only
      + taskAgeBonus            // stale tasks slowly rise
      + hysteresisBonus         // +50 on the current in-progress option only
```

Chain steps (`servesActiveWorkOrder == true`) are classified at tier 4, not scored higher within tier 6. Work type preference (1-9) affects within-tier ordering for tier 6 opportunistic work.

---

## Priority Formula

### AI Selection: lexicographic (tier, score)

When a colonist selects their next task, options are compared as `(tier, score)` pairs. Tier comes from the task type's definition in `priority-tuning.xml <TaskTiers>` (validated at startup; unassigned types fail with an error). Score orders within a tier only:

```
score = distanceFactor      // dominant: 300 * max(0, 1 - d/60), nearest source wins
      + skillBonus          // 0 to +100
      + taskAgeBonus        // 0 to +100 (old tasks rise)
      + hysteresisBonus     // +50 applied to the in-progress option only (same tier)
      + storagePriorityBias // stocking only: the destination box's priority rank (0..3) * 300
```

`chainBonus` (+2000) is gone; a chain step is classified at tier 4 (active work order), not scored higher within tier 6. `inProgressBonus` is gone; it is replaced by the `hysteresisBonus` (50) applied within the same tier only. `kWorkOrderProvisionFloor` and `kStorageStockingFloor` are deleted; tiers make them unnecessary.

---

## Config File Format

**File:** `assets/config/work/priority-tuning.xml`

```xml
<?xml version="1.0" encoding="UTF-8"?>
<PriorityTuning>
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

    <!-- How often colonists re-evaluate tasks (seconds), unchanged -->
    <reEvalInterval>0.5</reEvalInterval>
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

Skilled colonists naturally gravitate toward work they're good at. The bonus is `min(skillLevel * 10, 100)`, fixed in AIDecisionSystem (not tuned in the XML), and each skill's bonus is worked out once per decision.

**Examples:**
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

Hauling no longer computes a single additive priority. A haul's rank is its `(tier, score)` key:
a haul serving an active work order is tier 4; an opportunistic haul is tier 6. The situational
factors above (storage critical, blocking construction, perishable) are within-tier score lifts,
applied only against other tier-6 hauls, and can never push a haul across a tier boundary.

---

## Config Loading

### Registry Class

```cpp
class PriorityConfig {
public:
    static PriorityConfig& Get();

    bool loadFromFile(const std::string& xmlPath);

    // Task tier queries (AI arbitration). Tier is compared first; lower = higher priority.
    int getTaskTier(const std::string& taskTypeName) const;            // base tier, or kUnassignedTier
    static constexpr int kUnassignedTier = 9999;
    bool validateTaskTiers(const std::vector<std::string>& requiredTypeNames,
                           std::vector<std::string>& missingOut) const; // fail-loud at load

    // Within-tier score terms
    float   calculateDistanceFactor(float distance) const; // dominant: 300*max(0,1-d/60)
    int16_t calculateTaskAgeBonus(float taskAge) const;
    int16_t getInProgressBonus() const;                    // within-tier hysteresis margin (50)

    // Thresholds
    int16_t getTaskSwitchThreshold() const;
    float   getReEvalInterval() const;

private:
    std::unordered_map<std::string, int> taskTiers;  // per-task-type base tier (arbitration)
    DistanceFactorConfig  distanceFactorConfig;
    InProgressBonusConfig inProgressConfig;
    TaskAgeBonusConfig    taskAgeConfig;
    HaulingTuningConfig   haulingConfig;
    TimingConfig          timingConfig;
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

Tier is decided first and is not tuned by a score: move a task type between tiers in `<TaskTiers>`, or fix `classifyTier` when one option lands in the wrong tier. The knobs below only reorder options inside a tier.

### When to increase the distance factor

**Symptom:** Colonists walk across the map for a task when a nearby one of the same tier exists.

**Fix:** Increase `<DistanceFactor><maxFactor>` so distance outweighs skill, age, and hysteresis more, or decrease `<maxDistance>` so the factor falls to 0 sooner.

### Skill bonus (no knob)

The skill bonus is `min(skillLevel * 10, 100)`, fixed in AIDecisionSystem; `priority-tuning.xml` has no skill setting. If skill must count for more against distance, lower `maxFactor` instead.

### When to change the hysteresis margin

**Symptom:** Colonists flip between two same-tier tasks, or stick to a far task when a much nearer one appears.

**Fix:** Raise `<InProgress><bonus>` against thrashing, lower it against stickiness.

### When to change the storage priority weight

**Symptom:** A higher-priority box is stocked after a nearer lower-priority one, or the reverse is wanted.

**Fix:** `<StoragePriority><weight>` at or above `maxFactor` makes box priority beat distance within tier 6; below it, a much nearer lower box can win.

### Chain continuation (no score to tune)

There is no chain score to tune. A provisioning step (`servesActiveWorkOrder == true`) is
classified at tier 4, and tier beats score, so the colonist stays on the chain by construction.
If chains break, the bug is in tier classification (the option not being marked as serving the
work order), not in a bonus value.

### Hauling config (not read)

`<HaulingTuning>` is parsed but nothing in the decision reads it, so changing it changes no behaviour.

---

## Related Documents

- [Task Registry System](./task-registry.md) — Where priorities are used
- [Task Generation Architecture](../../technical/task-generation-architecture.md) — When priorities are calculated
- [Task Chains](./task-chains.md) — Multi-step tasks and chain bonus
- [Work Priorities](./work-priorities.md) — User-facing 1-9 priority system
