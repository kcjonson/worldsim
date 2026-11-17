# Config Loading System

**Related Specs:**
- [Task Generation Architecture](./task-generation-architecture.md) — Load order, validation
- [Work Types Config](../design/game-systems/colonists/work-types-config.md) — Work type definitions

---

## Overview

This document specifies **when** and **how** work configuration files are loaded during the game lifecycle. The design supports hot-reloading mods from the main menu without restarting the application.

## Numbers

Config numbers and true-or-false values go through the strict parsers (`parseConfigNumber` and `parseConfigInteger`, read by `ConfigReader`, in `libs/assets/assets/ConfigNumber.h`). A leading `+` is accepted; empty text, trailing characters ("30abc"), `nan`, `inf`, and values too large for the type are refused rather than read as a number. A true-or-false value (`ConfigReader`'s boolean readers) is exactly `true` or `false`; `1`, `yes`, and `ture` are refused rather than read as true or false. Text inside an element is trimmed first. Attribute text is not trimmed, so `gameMinutes=" 30"` is refused. A missing, malformed, or out-of-range value (a field stored in 16 bits or 8 bits is checked against its range, never truncated) is a config error naming the file, the element by its instance (its `name` or `defName` attribute, or its `<defName>` child, as in `<Task name="Haul"> tier`, `<Chain defName="Chain_Rescue"> step 2 order`, or `<RecipeDef defName="Recipe_AxePrimitive"> workAmount`), and the attribute, in the shape `<file>: <element> <attribute> must be <what is allowed>, got "<text>"` or `... is missing`. A loader that names a nested element itself (the thoughts' `thought 'ate' feeling 1 mood`) uses the same shape. The errors are shown on the loading scene ([where load errors surface](#where-load-errors-surface)). Base tuning lives in XML, so there is no C++ default for it.

A file is named by its path relative to the folder its load was given, with forward slashes on every platform: `loadWorkConfig`'s files relative to the config root (`work/priority-tuning.xml`), each colonist-rules folder's files relative to that folder after its stage prefix (`[needs] needs.xml`), and the recipes relative to `assets/recipes` after `[recipes]` (`[recipes] tools/AxePrimitive.xml`).

Any problem in a file (a bad number, an unknown element or attribute, a missing name, a name declared twice) means the load fails: the registry holds nothing from it and its errors reach the loading failure view.

### Attributes that may be left out

Everything not listed here must be given. Leaving one of these out has the stated meaning; it is not a fallback for a forgotten value.

| Loader | File | Attribute | Left out means |
|--------|------|-----------|----------------|
| WorkTypeRegistry | `work/work-types.xml` | `<Category canDisable>` | the player can disable the category |
| WorkTypeRegistry | `work/work-types.xml` | `<WorkType><minSkillLevel>` | no minimum skill level (0); when given, a number, 0 or more |
| WorkTypeRegistry | `work/work-types.xml` | `<filter>` elements `<looseItem>`, `<indoor>`, `<neededByRecipe>`, `<neededByBlueprint>`, `<hasPlacementTarget>` | no condition on that property; when given, `true` or `false` |
| WorkTypeRegistry | `work/work-types.xml` | `<label>`, `<description>`, `<triggerCapability>`, `<targetCapability>`, `<skillRequired>`, `<taskChain>`, `<filter>`, `<entityGroup>`, `<stationType>` | the label is the defName; the rest are absent (no skill needed, no chain, no filter condition) |
| TaskChainRegistry | `work/task-chains.xml` | `<Step optional>` | the step must be done (false) |
| TaskChainRegistry | `work/task-chains.xml` | `<Step requiresPreviousStep>` | the step waits for the one before it (true) |
| TaskChainRegistry | `work/task-chains.xml` | `<label>`, `<description>` | the label is the defName |
| RecipeRegistry | `assets/recipes/**/*.xml` | `<input count>`, `<output count>` | one of the thing; when given, a whole number, 1 or more |
| RecipeRegistry | `assets/recipes/**/*.xml` | `<innate>` | colonists learn the recipe (false) |
| RecipeRegistry | `assets/recipes/**/*.xml` | `<label>`, `<description>`, `<station>`, `<skill>`, `<inputs>`, `<outputs>` | the label is the defName; no station (as is `<station>none</station>`), no skill, no inputs or outputs |
| PriorityConfig | `work/priority-tuning.xml` | `<TaskTiers>`, `<WorkCategoryOrder>` | no tiers or order from this file (`loadWorkConfig` then reports every task type without a tier) |
| ThoughtConfig | `thoughts/*.xml` | a feeling's `mood` | the feeling has no mood effect |
| ThoughtConfig | `thoughts/*.xml` | `stacks`, an action source's `first` | false |

The `<Category canDisable>`, `<Step optional>`, `<Step requiresPreviousStep>`, and `<innate>` values above are `true` or `false` when given.

Required tuning includes a work category's `tier` (a number, 0 or more), a chain step's `order` (a whole number from 0 to 255, no two steps of a chain the same) and `target` (the role the step's action works on, as `source` or `station`), a recipe's `<workAmount>` (work ticks, 0 or more), and `<RecentSighting gameMinutes>` in `colonists/memory.xml`, beside the priority, mood, needs, attribute, trait, condition, and thought numbers. A recipe is one `<RecipeDef>` root per file whose `<input>` and `<output>` lines name their item with `thing`. Each `thing`, and a `<station>` other than `none`, must name a loaded asset definition: one that names nothing is a problem in the file, as in `[recipes] tools/AxePrimitive.xml: <RecipeDef defName="Recipe_AxePrimitive"> input thing "Wod" names no definition`. The asset manager's Reload loads the recipes the same way; it shows no recipes, so their problems surface when the game loads.

---

## Design Goals

1. **No restart required for mod changes** — Unlike RimWorld, players can enable/disable mods from the main menu and start a new game without relaunching
2. **Fail-fast validation** — Config errors shown on loading screen, not during gameplay
3. **Clear error messages** — Modders see exactly what's wrong and where
4. **Clean reload** — Registries can be cleared and reloaded for each game start

---

## When to Load

### Decision: Load in GameLoadingScene (NOT at app startup)

```
App Startup (Main.cpp)
    ↓
Main Menu ← Player can toggle mods here
    ↓
GameLoadingScene::initializeWorldSystems()
    ├── Load base configs
    ├── Load enabled mod configs
    ├── Validate all configs
    └── On error → Show error screen → Return to menu
    ↓
GameScene (gameplay begins)
```

### Why Not App Startup?

Loading at app startup (Main.cpp) would require a full restart to change mods. By loading in `GameLoadingScene`:

- Mods can be toggled from main menu
- Each "New Game" gets fresh config load
- Returning to menu and starting new game applies mod changes

---

## Load Order

Configs load in dependency order (same as [task-generation-architecture.md](./task-generation-architecture.md)):

```
1. ActionTypeRegistry    ← No dependencies
2. TaskChainRegistry     ← References ActionTypes
3. WorkTypeRegistry      ← References TaskChains
4. PriorityConfig        ← References WorkTypes
5. ConfigValidator       ← Validates cross-references
```

### Base Game Paths

```
assets/config/actions/action-types.xml
assets/config/work/task-chains.xml
assets/config/work/work-types.xml
assets/config/work/priority-tuning.xml
assets/config/construction/*.xml
assets/config/colonists/mood.xml
assets/config/colonists/traits/*.xml
assets/config/colonists/attributes/*.xml
assets/config/colonists/needs/*.xml
assets/config/conditions/*.xml
assets/config/thoughts/*.xml
```

`colonists/traits/`, `colonists/attributes/`, `colonists/needs/`, `conditions/`, and `thoughts/` load through `ecs::loadColonistRules(configRoot)` (libs/sim/ecs/ColonistRules.h),
stopping at the first stage with errors. The traits load first, then the attribute ranges, then the need thresholds (`loadNeedLevels`: `assets::loadNeedsConfig` reads the folder, a later file's `<Need>` replacing an earlier one of the same name and logged at info level, and `ecs::NeedLevels` checks that every need is given and each name is a need). A needs file with any error adds nothing, and its errors name the file (relative to the needs folder) and the `<Need name="...">`. There is no C++ default for a threshold: every colonist's `NeedsComponent` is constructed from the loaded `NeedLevels`, so the AI and the thought sources read the same numbers. Then the condition vocabulary is declared, answered by the sim's terms, and frozen, then the thought
types are read and their conditions parsed against it (`loadConditionVocabulary` and `loadThoughtTypes`,
which the tests use too). Each folder's own files load before its subfolders' (`assets::configFilesIn`),
so a mod adds or replaces by adding a file there. A clean load logs its counts at info level (traits; needs; terms;
feelings, thought types, and sources). `GameLoadingScene` adds its errors, prefixed `[traits]`, `[attributes]`, `[needs]`, `[conditions]`,
or `[thoughts]`, to the config errors the loading screen shows, and hands the rules to `GameScene` through
`GameWorldState`. A direct switch to the game scene skips the loading scene, so `GameScene` loads them
itself, once; when they are broken it hands the errors to the splash (`world_sim::failColonistRules`),
which shows them after the ground bake and loads nothing.

`mood.xml` loads into `assets::MoodConfig` (the mood rate, the need penalty curve, the per-need
weights by need name, and the band names) and is checked by `ConfigValidator::validateMood`.
The need names belong to the sim, so `GameLoadingScene` adds `ecs::moodConfigErrors` (a name that is
no need, a need with no weight) to `loadWorkConfig`'s errors before the game scene is entered.

Every `.xml` file under `colonists/traits/` loads into an `assets::TraitRegistry` (the trait ids, names and
descriptions, the 1 to 3 count, and the exclusive groups): the folder's own files first, then its subfolders' in
path order, a later file replacing a trait of the same id with a log line. `<Exclusive>` groups from every file
accumulate, so a trait in several groups is excluded from all of their members. A `<Count>` min of 0 is legal; a min
the exclusive groups make unreachable is an error, and a max above what they allow is legal (the roll gives as many
as fit). `TraitRegistry::reachable` answers both the load check and each draw of `ecs::rollTraits`, so a roll never
gives fewer than the min; it rejects an unreachable min by a bound (at most one of each group) before any search. A
trait id that is a word of the condition language (`assets::kConditionReservedWords`, which the condition parser
also reads) is an error. Like the conditions and thoughts, any problem (a bad number, an unknown element or attribute)
means the folder loads nothing.

Every `.xml` file under `colonists/attributes/` loads through `assets::loadAttributeConfig` (libs/assets/assets/AttributeConfig.h)
into an `ecs::AttributeRolls`, owned by `ecs::ColonistRules` beside the traits: one `<Attribute name min max/>` per
attribute, the folder's own files first, then its subfolders' in path order, a later file replacing an attribute of the
same name with a log line. The names come from `ecs::kAttributeFields` (libs/sim/ecs/components/Attributes.h), the one
table the config, the `attribute(...)` condition term and the dev verb share, and the load is told them: a name not in
it, an attribute no file gives, a bad or out-of-scale number (0 to 20, `min` at most `max`), or a name given twice in
one file is an error naming the file and the `<Attribute name="...">`, and any error loads nothing.
`ecs::rollAttributes` draws every attribute from the colonist's generator (one raw engine draw each, scaled by hand so
the result matches across platforms); there are no values in C++, so an `Attributes` is only ever built from a roll.

The registry is no singleton: `ecs::loadColonistRules` builds it as the first stage and `ecs::ColonistRules` owns it
beside the vocabulary and the thought types, so whatever needs it (the colonist spawn, the dossier's Bio tab) takes it
from the rules the world was built with. The conditions take their `trait` names from it. Its errors are prefixed
`[traits]` and reach the loading screen and the splash the same way as theirs.

Five condition symbol sets are owned, listed in `assets::kOwnedSymbolSets` (libs/assets/assets/ConditionConfig.h)
with their owners: `kind` (the `<Kind>` declarations), `trait` (the trait registry), `need` (`ecs::kNeedLabels`),
`attribute` (`ecs::kAttributeFields`), and `task` (the sim's task types, `assets::kTaskTypeNames`, and the `<TaskGroup>`
declarations). The condition loader declares each one closed and empty before filling `kind` and `task` from their
declarations, and a conditions file declaring any of them with `<Symbols>`, to add names or to open it, is an error
naming that file. The sim adds the rest when it builds the vocabulary (`conditions::buildSimVocabulary`), and
`ConditionVocabulary::addSymbols` refuses a set that is not owned, so the sim never adds to a set the conditions files
fill. A condition naming a need, attribute, trait, or task the owner lacks is a parse error, and the thought loader
reports it naming the file, the thought, and the feeling or line that holds the condition, with the set's valid names.
The other sets (`event`, `behavior`, `mood_cause`, `resource`, `scarcity_level`) are the conditions files' own: any file
may add names, and the open ones take any name.

### Mod Paths (Future)

```
mods/{mod-name}/config/actions/*.xml
mods/{mod-name}/config/work/*.xml
```

Mods load alphabetically after base game, allowing later mods to override earlier definitions.

---

## Implementation

### Loading the work config

`assets::loadWorkConfig(configRoot)` (libs/assets/assets/WorkConfig.h) clears the work-config
registries (ActionTypeRegistry, TaskChainRegistry, WorkTypeRegistry, PriorityConfig,
ConstructionRegistry, MoodConfig, and ConfigValidator's errors), loads them from `configRoot` in the
order above, checks that priority-tuning.xml gives every task type an arbitration tier, loads
construction and mood, and cross-validates. It returns every error as a string; an empty list means the config is loaded. It needs
no GL and no scene, so the headless sim calls it too.

The loading scene clears `ecs::GoalTaskRegistry` itself (a sim singleton libs/assets can't reach),
then calls it in the `Initializing` phase:

```cpp
// GameLoadingScene.cpp, initializeWorldSystems()
updateStatusText("Loading configuration...");
ecs::GoalTaskRegistry::Get().clear();
std::vector<std::string> configErrors = assets::loadWorkConfig("assets/config");
// ... the mood errors and the colonist rules' errors are added ...
if (!configErrors.empty()) {
    fail(std::move(configErrors));
    return;
}
```

### Where load errors surface

Every way a game load fails goes through the loading scene's one `fail(messages)`, which logs each message at
error level, keeps them for the scene's state view, and moves to the `Failed` phase. The causes are the asset
library's validation errors and the recipes' errors (prefixed `[recipes]`, read by `world_sim::stepGroundBake` from
`RecipeRegistry::problems()`, which the asset system's load fills), a failed ground bake (when a `--scene` jump skipped the splash), a missing or
unreadable quickstart planet, and the config errors above: `loadWorkConfig`'s, `ecs::moodConfigErrors`, and
`ecs::loadColonistRules`'s, each prefixed with its stage.

`Failed` hides the status line and the bar and shows a `world_sim::LoadFailureView`
(`apps/world-sim/scenes/shared/LoadFailureView.h`) in their place: an inset panel titled "Loading failed" with
the first five messages in full, wrapped to the panel's width, "and N more" under them when there are more,
the run's log file (`foundation::logFilePath()`, under `%TEMP%\world-sim\logs\` on Windows), which has every
message, and "Press Esc to return to the menu". Esc returns to the main menu, where the next game's load reads
a corrected file again.

The splash shows the same view for what blocks it: the asset library's validation and recipe errors and the ground bake's
at startup, and the colonist rules' errors a direct switch to the game scene hands it
(`world_sim::failColonistRules`). The view takes the boot log's place under the identity block, Enter
Expedition stays disabled, and its line under the messages says to fix the files and restart.

Both scenes put the messages in their `/api/state?what=scene` view, as `failed` and an `errors` array of the
strings shown, so a test reads them without the screen.

---

## Status Text

The loading scene's status line while it works:

| Phase | Status Text |
|-------|-------------|
| Ground bake | "Baking ground tiles..." |
| Planet | "Loading planet..." |
| Config loading | "Loading configuration..." |
| Terrain | "Generating terrain..." |
| Entities | "Placing entities..." |
| Complete | "Ready!" |

---

## Testing

### Unit Tests (existing)

`WorkConfig.test.cpp` covers registry loading and validation, pins the shipped work config's values, and checks each loader's missing, malformed, and unknown values; `AssetRegistryAsync.test.cpp` does the same for the recipes.

### Integration Test (manual)

1. Start game → Main Menu
2. Start New Game
3. Verify loading screen shows "Loading configuration..."
4. Verify game starts successfully
5. Return to Main Menu
6. Start New Game again
7. Verify configs reload cleanly (no stale data)

### Error Handling Test (manual)

[Scenario 19](../testing/scenarios/19-load-errors-on-screen.md): a broken value in a staged config file is named
on the loading scene and on the splash, Esc returns to the menu, and the restored file loads.

---

## Future: Mod Support

When mod support is added:

1. Main Menu gets "Mods" button
2. Mods screen shows available mods with enable/disable toggles
3. Enabled mods stored in save file or user preferences
4. `assets::loadWorkConfig` loads base + enabled mods in order
5. Mod load order: alphabetical (or explicit load order in mod metadata)

See [task-generation-architecture.md](./task-generation-architecture.md#mod-loading) for mod validation rules.

---

## Summary

| Decision | Choice | Rationale |
|----------|--------|-----------|
| When to load | GameLoadingScene | Enables mod changes without restart |
| Error handling | Show on loading screen, return to menu | Clear feedback for modders |
| Reload support | Clear registries before each load | Clean slate for each game start |
