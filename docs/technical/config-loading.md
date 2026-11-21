# Config Loading System

**Related Specs:**
- [Task Generation Architecture](./task-generation-architecture.md) — Load order, validation
- [Work Types Config](../design/game-systems/colonists/work-types-config.md) — Work type definitions

---

## Overview

This document specifies **when** and **how** work configuration files are loaded during the game lifecycle. The design supports hot-reloading mods from the main menu without restarting the application.

## Numbers

Config numbers and true-or-false values go through the strict parsers (`parseConfigNumber` and `parseConfigInteger`, read by `ConfigReader`, in `libs/assets/assets/ConfigNumber.h`). A leading `+` is accepted; empty text, trailing characters ("30abc"), `nan`, `inf`, and values too large for the type are refused rather than read as a number. A true-or-false value (`ConfigReader`'s boolean readers) is exactly `true` or `false`; `1`, `yes`, and `ture` are refused rather than read as true or false. Text inside an element is trimmed first. Attribute text is not trimmed, so `gameMinutes=" 30"` is refused. A missing, malformed, or out-of-range value (a field stored in 16 bits or 8 bits is checked against its range, never truncated) is a config error naming the file, the element by its instance (its `name` or `defName` attribute, or its `<defName>` child, as in `<Need name="Hunger"> seek` or `<RecipeDef defName="Recipe_AxePrimitive"> workAmount`; an entry of the behavior library by its `id`, as in `<need id="hunger"> seek`), and the attribute, in the shape `<file>: <element> <attribute> must be <what is allowed>, got "<text>"` or `... is missing`. A loader that names a nested element itself (the thoughts' `thought 'ate' feeling 1 mood`) uses the same shape. The errors are shown on the loading scene ([where load errors surface](#where-load-errors-surface)). Base tuning lives in XML, so there is no C++ default for it.

A file is named by its path relative to the folder its load was given, with forward slashes on every platform: `loadWorkConfig`'s files relative to the config root (`legacy/work-types.xml`), the arbitration file relative to its folder after `[arbitration]` (`[arbitration] priority-tuning.xml`), the human's config files relative to its folder (`mood.xml`, and after the colonist rules' stage prefix `[needs] needs.xml`), the condition and thought files relative to their folder after their prefix, the behavior library's files relative to their folder after its prefix (`[behaviors] human/eating.xml`, `[actions] shared.xml`), a creature's files relative to the creatures folder after `[creatures]` (`[creatures] Human/Human.xml`), and the recipes relative to `assets/recipes` after `[recipes]` (`[recipes] tools/AxePrimitive.xml`).

Any problem in a file (a bad number, an unknown element or attribute, a missing name, a name declared twice) means the load fails: the registry holds nothing from it and its errors reach the loading failure view.

### Attributes that may be left out

Everything not listed here must be given. Leaving one of these out has the stated meaning; it is not a fallback for a forgotten value.

| Loader | File | Attribute | Left out means |
|--------|------|-----------|----------------|
| WorkTypeRegistry | `legacy/work-types.xml` | `<Category canDisable>` | the player can disable the category |
| WorkTypeRegistry | `legacy/work-types.xml` | `<WorkType><minSkillLevel>` | no minimum skill level (0); when given, a number, 0 or more |
| WorkTypeRegistry | `legacy/work-types.xml` | `<filter>` elements `<looseItem>`, `<indoor>`, `<neededByRecipe>`, `<neededByBlueprint>`, `<hasPlacementTarget>` | no condition on that property; when given, `true` or `false` |
| WorkTypeRegistry | `legacy/work-types.xml` | `<label>`, `<description>`, `<triggerCapability>`, `<targetCapability>`, `<skillRequired>`, `<filter>`, `<entityGroup>`, `<stationType>` | the label is the defName; the rest are absent (no skill needed, no filter condition) |
| RecipeRegistry | `assets/recipes/**/*.xml` | `<input count>`, `<output count>` | one of the thing; when given, a whole number, 1 or more |
| RecipeRegistry | `assets/recipes/**/*.xml` | `<innate>` | colonists learn the recipe (false) |
| RecipeRegistry | `assets/recipes/**/*.xml` | `<label>`, `<description>`, `<station>`, `<skill>`, `<inputs>`, `<outputs>` | the label is the defName; no station (as is `<station>none</station>`), no skill, no inputs or outputs |
| PriorityConfig | `arbitration/priority-tuning.xml` | `<WorkCategoryOrder>` | no category order from this file |
| ThoughtConfig | `thoughts/*.xml` | a feeling's `mood` | the feeling has no mood effect |
| ThoughtConfig | `thoughts/*.xml` | `stacks`, an action source's `first` | false |

The `<Category canDisable>` and `<innate>` values above are `true` or `false` when given. What the behavior library's entries may leave out is in [Queries and chains](./queries-and-chains.md); the loader is [below](#the-behavior-library-and-the-creatures).

Required tuning includes a work category's `tier` (a number, 0 or more), the arbitration file's `<ladder>` of `<rung name>` entries, a recipe's `<workAmount>` (work ticks, 0 or more), and `<RecentSighting gameMinutes>` in the human's `memory.xml`, beside the priority, mood, needs, attribute, trait, condition, and thought numbers. A recipe is one `<RecipeDef>` root per file whose `<input>` and `<output>` lines name their item with `thing`. Each `thing`, and a `<station>` other than `none`, must name a loaded asset definition. A name the definition index holds with no definition behind it (a harvest yield no definition defines, or a synthetic terrain name such as the shore's `Terrain_Shore`) is not one; neither is crafted, hauled, or used as a station. One that names no definition is a problem in the file, as in `[recipes] tools/AxePrimitive.xml: <RecipeDef defName="Recipe_AxePrimitive"> input thing "Wod" names no definition`. The asset manager's Reload loads the recipes the same way and shows their problems: counted in its header's errors and listed in full at the top of its asset tree, cleared and read again on every Reload.

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

Configs load in dependency order:

```
1. WorkTypeRegistry      ← No dependencies (legacy/work-types.xml)
2. PriorityConfig        ← References WorkTypes (arbitration/: the ladder and the bonuses)
3. ConstructionRegistry, MoodConfig, MemoryConfig
4. ConfigValidator       ← Validates cross-references
5. The behavior library and the creatures   ← References the ladder's rungs and the asset definitions;
                                              the action factories' entries are resolved from it
6. The colonist rules    ← traits, attributes, needs, conditions, thoughts; the thought sources'
                           actions are resolved against the behavior library
```

### Base Game Paths

```
assets/config/legacy/work-types.xml
assets/config/arbitration/priority-tuning.xml
assets/config/behaviors/<folder>/*.xml
assets/config/actions/*.xml
assets/config/work/*.xml
assets/config/orders/*.xml
assets/config/facts/*.xml
assets/config/construction/*.xml
assets/config/conditions/*.xml
assets/config/thoughts/*.xml
assets/world/creatures/<Name>/<Name>.xml (the <creature> block)
assets/world/creatures/Human/mood.xml
assets/world/creatures/Human/memory.xml
assets/world/creatures/Human/traits.xml
assets/world/creatures/Human/attributes.xml
assets/world/creatures/Human/needs.xml
```

`legacy/` holds `work-types.xml`, which a later ticket of the behavior data migration replaces with entries, so
no folder the entry loaders own holds a root they do not own. The arbitration folder holds one file, whose root is
`<arbitration>`: its `<ladder>` of `<rung name>` entries, given once, at most 255 rungs and one of them
`survival`, and `<WorkCategoryOrder>` and `<Bonuses>`; any other element is an error, and a
second file in the folder is an error naming both
(`[arbitration] arbitration: 2 files; one file declares the ladder and the bonuses (...)`). The ladder must
also have every rung the decision ranks options on (`critical_need`, `committed_work`, `ordinary_need`,
`opportunistic_work`, `idle`), in that order, with other rungs anywhere among them; `loadWorkConfig` reports
each one missing
(`[arbitration] priority-tuning.xml: <ladder> has no rung named "idle", which the decision ranks options on`),
else the first two out of order (`<ladder> has "idle" above "opportunistic_work"; ...`).

A creature's config sits in its asset folder beside its definition and is found by root element
(`assets::creatureConfigFiles`, libs/assets/assets/CreatureConfigFiles.h). In a creature folder:

- a file not ending in lower-case `.xml` is ignored (the SVGs, notes);
- the definition, named for the folder exactly (`Human.xml`, compared case-sensitively on every
  platform), and the motion files it names in `<motion>` elements belong to the asset registry;
- every other `.xml` file is a `<Mood>`, `<Memory>`, `<Traits>`, `<Attributes>`, `<Needs>`, `<creature>`, or
  `<behavior>` file, read in file name order;
- any subfolder, and any other `.xml` file, is an error.

The finder reports structural faults, one error each, prefixed `[creatures]` by the finder itself:

- a file that is not readable XML, or whose root is none of the seven, names the file
  (`[creatures] mood.xml: not readable XML: ...`); a `<Motion>` file the definition does not name, or
  an `<AssetDefinitions>` file not named for its folder, says so;
- a subfolder names the subfolder;
- a second `<Mood>` or `<Memory>` file names the folder by its last two parts
  (`[creatures] creatures/Human: more than one file has a <Mood> root: ...`);
- a missing or unreadable definition is the one error, since the motion files it names cannot be told
  from the rest without it;
- a filesystem fault names the entry, or the folder by its last two parts.

A kind with no file is not a fault of the folder: a deer has no mood. The human needs all five, so the
human's load adds one error per missing kind (`assets::missingKinds`,
`[creatures] creatures/Human: no file has a <Needs> root`).

Where each appears:

- Structural faults in any creature folder with a readable definition: the asset check. The asset
  validator runs the finder on every folder directly under the asset root's `creatures/` folder and
  reports its errors as errors under the definition's name, so the splash shows `Human: [creatures]
  herd.xml: ...` on a normal launch (the loading scene's ground bake step shows it after a `--scene`
  jump), and the asset manager lists it under the asset. The game stops on validation errors before its
  config load, so that one message is the only one. The validator gives the finder's files no "does not
  match its folder" warning. A creature folder with no definition is walked like any folder (an empty
  one gives nothing, an `.xml` file in it the usual warning). One whose definition the walk itself flags
  (unreadable, no `<AssetDefinitions>`) gets only the walk's message.
- A missing or misnamed creature definition (`[creatures] Cow.xml: missing; it is the creature's
  definition`) or a missing human folder (`[creatures] creatures/Human: missing; ...`): the config load,
  on the loading scene, since the asset check passes a folder with no definition.
- Missing kinds and bad values (a threshold that is not a number, an entry naming a chain no file
  defines): the config load, on the loading scene, through `world_sim::loadGameConfig`
  (apps/world-sim/scenes/loading/GameConfig.h).

`loadGameConfig` finds every creature folder's files. When any folder has structural errors, or the human
lacks a kind, those are the load's errors and no loader runs. Otherwise it calls `assets::loadWorkConfig`
(the human's one `<Mood>` and one `<Memory>` file), then, when that loaded clean, `assets::loadEntries`
(below), then `ecs::loadColonistRules`, each of which takes only error-free folders with every kind it reads
(asserted). A fault in `assets/config` and a fault in a creature folder at once show only the folder's
errors until those are fixed; the `assets/config` fault shows on the next load. A fault in the work config
holds back the entry loaders, whose rungs come from the arbitration file, until it is fixed.

### The behavior library and the creatures

`assets::loadEntries` (libs/assets/assets/EntryLoader.h) reads the entries [Queries and
chains](./queries-and-chains.md) defines into an `assets::EntryLibrary` (libs/assets/assets/Entries.h), which
`GameLoadingScene` hands to `GameScene` through `GameWorldState` beside the colonist rules. The tickets of
[Behavior data migration](./behavior-data-migration.md) are what read it when a creature decides.

The loader is given the sim's fixed list of needs as need ids (`EntrySources::trackedNeeds`, from
`ecs::trackedNeedIds`), so an action's `<restore>` may name `hunger` with no behavior bringing it
([Behavior data migration](./behavior-data-migration.md), Seams). Once the library loads clean,
`ecs::resolveActionEntries` (libs/sim/ecs/ActionEntries.h) resolves every action entry, once, to its handle and
plain values in a table by handle, so nothing reads the library when an action runs. That pass refuses what the
engine cannot do, each an error naming the file, the entry, and the element: a need the sim does not track, a
`scaleBy` other than `sleep_quality`, a `<damage>`, a `<consume>` of other than edible units or of more than one
unit beside a `from="nutrition"` restore, a row that can take nothing written after a `<restore>` or `<spawn>`, such a
row in a fixed-duration action paying along, a second bound in an open-ended action, an open-ended `<restore>` of
amount 0, and a `<spawn>` on the ground of a carryable, harvestable, craftable, or storage definition (`[actions]
extra.xml: <action id="bash"> effect 1 <damage> lowers a condition, which nothing in the sim has`). The
loader itself refuses `pays="end"` and a `curve` on an open-ended action. Over those rules, each entry the
hand-written action factories and the hands rule read (`human/eat`, `common/drink`, `pickup`, `craft`, and the rest)
is checked against the fixed shape its factory reads, listed in that header; an entry missing, or with anything its
factory does not read, is an error the same way (`[behaviors] human/washing.xml: <action id="wash"> effect 2 <consume> is not an effect the sim's Wash
action performs`; a missing one, `action "human/wash" is missing; the sim's Wash action reads it from
behaviors/human/washing.xml`).
`GameScene` holds the resolved `ecs::ActionEntries` and hands it to `ActionSystem` and `AIDecisionSystem`.

| Folder | Root | Prefix |
|---|---|---|
| `behaviors/<folder>/` | `<behavior>` | `[behaviors]` |
| `actions/` | `<actions>` | `[actions]` |
| `work/` | `<colonyWork>` | `[work]` |
| `orders/` | `<orders>` | `[orders]` |
| `facts/` | `<facts>` | `[facts]` |
| a creature folder | the base definition's `<creature>` block, `<creature>` and `<behavior>` files | `[creatures]` |

Each folder must exist; a shipped folder that holds no entry keeps a `.gitkeep`. `behaviors/` loads its own
files first, then its subfolders' in path order (`configFilesIn`); `actions/`, `work/`, `orders/`, and
`facts/` hold no subfolder, since their entries take top-level ids, and one is an error naming it. A file
with another root is an error, so no file in an owned folder is skipped. The library loads first, then the
creatures, in folder name order. A behavior's id is its path under `behaviors/` without the extension, a
creature's own behavior's id is the creature's name and the file's (`Cow/being_milked`), and the entries a
behavior file holds take its folder. Every declared id, file name, and folder name in an id is letters,
digits, and underscores, case as written. Ids are unique per entry type, and a short id names an entry of
the same folder, else a top-level one. Every `<fact>` the sim's facts read a value from must be given once
across `facts/`.

Errors name the file, the entry, the element, and the attribute, in the shape the other loaders use, as
`[behaviors] human/eating.xml: <chain id="fetch_food"> step 2 <do> action "takee" names no action`. A file
with any problem of its own loads nothing; the references between entries are resolved only once every file
reads clean, so a broken file never shows as a cascade of names it would have defined; one fault gives one
error; and an error of any kind leaves the library empty. Warnings (a folder entry hiding a top-level id, a
chain nothing starts) do not fail the load, and `loadGameConfig` logs them at warning level only when the
whole config load succeeds, the name check below included. A clean
load logs its counts at info level.

The names an entry borrows from config that loads after it, a `<scale trait>`, a `<from kind>`, and a
`<think thought>`, are checked by `assets::checkEntryNames` once `loadColonistRules` has loaded the traits,
the condition vocabulary, and the thought types, and each one that names nothing is an error in the same
shape. A thought source's `action` names an action entry by its id (`human/eat`, `craft`); once both have
loaded, `ThoughtTypes::resolveActions` resolves each, and one that names no action, or names an action group,
is an error (`[thoughts] events.xml: thought 'ate_good_meal' source 1: no action is called 'human/eats'`).
Conditions (`when`, `per`) and skill names (`skill`, `<work rate>`) are kept as written and checked by
nothing at load: the conditions wait on the vocabulary owning the loaded need ids, and no loaded config
declares the skills.

The human's traits, attributes, and needs, then `conditions/` and `thoughts/`, load through
`ecs::loadColonistRules(configRoot, human)` (libs/sim/ecs/ColonistRules.h), stopping at the first stage
with errors. The traits load first, then the attribute ranges, then the needs (`loadNeedLevels`:
`assets::loadNeedsConfig` reads the `<Needs>` files, each `<Need>` giving its thresholds and its mood
`cost`, a later file's `<Need>` replacing an earlier one of the same name, cost included, and logged at
info level, and `ecs::NeedLevels` checks that every need is given and each name is a need). A missing or
malformed threshold or cost, or a cost below 0, is an error; a needs file with any error adds nothing, and
its errors name the file (relative to the human's folder) and the `<Need name="...">`. There is no C++
default for a threshold or a cost: every colonist's `NeedsComponent` is constructed from the loaded
`NeedLevels`, each `Need` carrying its thresholds and its `moodCost`, so the AI, the thought sources, and
`MoodSystem` read the same numbers. Then the condition vocabulary is declared, answered by the sim's terms,
and frozen, then the thought types are read and their conditions parsed against it
(`loadConditionVocabulary` and `loadThoughtTypes`, which the tests use too). Each of `conditions/` and
`thoughts/` loads its own files before its subfolders' (`assets::configFilesIn`), so a mod adds or
replaces by adding a file there; the human's files load in file name order, so a later file replaces in
the same way. A clean load logs its counts at info level (traits; needs; terms; feelings, thought types,
and sources). Its errors carry their stage's prefix (`[traits]`, `[attributes]`,
`[needs]`, `[conditions]`, or `[thoughts]`), added by `loadColonistRules`; `loadGameConfig` returns them
with the work config's, and `GameLoadingScene` shows them and hands the rules to `GameScene` through
`GameWorldState`. A switch to the game scene with no loaded world waiting (the dev server's `scene`
action, `reload`, `--scene=game`) builds the loading scene instead, so the game scene never runs without
this config.

`mood.xml` loads into `assets::MoodConfig` (the mood rate, the need penalty curve, and the band names)
and is checked by `ConfigValidator::validateMood`. It names no need: each need's mood cost is on its own
`<Need>` in `needs.xml`, above. The loader refuses an element under `<Mood>` other than
`Easing`, `NeedPenalty`, and `Bands` (the error names the file and the element).

Every `<Traits>` file in the human's folder loads into an `assets::TraitRegistry` (the trait ids, names and
descriptions, the 1 to 3 count, and the exclusive groups) in file name order, a later file replacing a trait
of the same id with a log line. `<Exclusive>` groups from every file accumulate, so a trait in several groups
is excluded from all of their members. A `<Count>` min of 0 is legal; a min the exclusive groups make
unreachable is an error, and a max above what they allow is legal (the roll gives as many as fit).
`TraitRegistry::reachable` answers both the load check and each draw of `ecs::rollTraits`, so a roll never
gives fewer than the min; it rejects an unreachable min by a bound (at most one of each group) before any
search. A trait id that is a word of the condition language (`assets::kConditionReservedWords`, which the
condition parser also reads) is an error. Like the conditions and thoughts, any problem (a bad number, an
unknown element or attribute) means no trait loads.

Every `<Attributes>` file in the human's folder loads through `assets::loadAttributeConfig`
(libs/assets/assets/AttributeConfig.h) into an `ecs::AttributeRolls`, owned by `ecs::ColonistRules` beside the
traits: one `<Attribute name min max/>` per attribute, in file name order, a later file replacing an attribute
of the same name with a log line. The names come from `ecs::kAttributeFields`
(libs/sim/ecs/components/Attributes.h), the one table the config, the `attribute(...)` condition term and the
dev verb share, and the load is told them: a name not in it, an attribute no file gives, a bad or out-of-scale
number (0 to 20, `min` at most `max`), or a name given twice in one file is an error naming the file and the
`<Attribute name="...">`, and any error loads nothing. `ecs::rollAttributes` draws every attribute from the
colonist's generator (one raw engine draw each, scaled by hand so the result matches across platforms); there
are no values in C++, so an `Attributes` is only ever built from a roll.

The registry is no singleton: `ecs::loadColonistRules` builds it as the first stage and `ecs::ColonistRules`
owns it beside the vocabulary and the thought types, so whatever needs it (the colonist spawn, the dossier's
Bio tab) takes it from the rules the world was built with. The conditions take their `trait` names from it.
Its errors are prefixed `[traits]` and reach the loading scene the same way as theirs.

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

Mods load alphabetically after the base game. A mod adds entries and never replaces one: an entry with a
shipped entry's full id is the duplicate-id error ([Queries and chains](./queries-and-chains.md#loading-a-creature)).

---

## Implementation

### Loading the work config

`assets::loadWorkConfig(configRoot, human)` (libs/assets/assets/WorkConfig.h) clears the work-config
registries (WorkTypeRegistry, PriorityConfig, ConstructionRegistry, MoodConfig,
MemoryConfig, and ConfigValidator's errors), loads them from `configRoot` in the order above, checks that
the arbitration file's ladder has `survival` and the five rungs the decision ranks on (`critical_need`,
`committed_work`, `ordinary_need`, `opportunistic_work`, `idle`), the five in that order, loads construction, then mood and memory
from the human's one `<Mood>` and one `<Memory>` file (its caller has already refused none, or more than
one), and cross-validates. It returns every error as a string; an empty list means the config is loaded. It
needs no GL and no scene.

The loading scene clears `ecs::GoalTaskRegistry` itself (a sim singleton libs/assets can't reach),
then calls it through `world_sim::loadGameConfig` in the `Initializing` phase:

```cpp
// GameLoadingScene.cpp, initializeWorldSystems()
updateStatusText("Loading configuration...");
ecs::GoalTaskRegistry::Get().clear();
world_sim::GameConfigLoad config = world_sim::loadGameConfig("assets/config", "assets/world/creatures");
worldState->colonistRules = std::move(config.rules);
worldState->entries = std::move(config.entries);
worldState->actionEntries = std::move(config.actions);
if (!config.errors.empty()) {
    fail(std::move(config.errors));
    return;
}
```

### Where load errors surface

Every way a game load fails goes through the loading scene's one `fail(messages)`, which logs each message
at error level, keeps them for the scene's state view, and moves to the `Failed` phase. The causes are:

- the asset library's validation errors, the creature folders' structural faults among them
  (`Human: [creatures] ...`), and the recipes' errors (prefixed `[recipes]`), read by
  `world_sim::stepGroundBake` from the validation report and `RecipeRegistry::problems()`;
- a failed ground bake (when a `--scene` jump or a dev switch skipped the splash);
- a missing or unreadable quickstart planet;
- the config errors from `world_sim::loadGameConfig`: a creature folder's structural faults and the human's
  missing kinds (`[creatures] ...`), then `loadWorkConfig`'s, `assets::loadEntries`'s (prefixed with their
  folder), and `ecs::loadColonistRules`'s (prefixed with their stage).

The splash runs the same `stepGroundBake` and shows its errors the same way, so the asset and recipe
errors stop a normal launch on the splash before the loading scene.

`Failed` hides the status line and the bar and shows a `world_sim::LoadFailureView`
(`apps/world-sim/scenes/shared/LoadFailureView.h`) in their place: an inset panel titled "Loading failed" with
the first five messages in full, wrapped to the panel's width, "and N more" under them when there are more,
the run's log file (`foundation::logFilePath()`, under `%TEMP%\world-sim\logs\` on Windows), which has every
message, and "Press Esc to return to the menu". Esc returns to the main menu, where the next game's load reads
a corrected file again.

The splash shows the same view for what blocks it: the asset library's validation and recipe errors and the ground
bake's at startup. The view takes the boot log's place under the identity block, Enter
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

`WorkConfig.test.cpp` covers registry loading and validation, pins the shipped work config's values, and checks each loader's missing, malformed, and unknown values; `EntryLoader.test.cpp` does the same for the behavior library and the creatures, one suite per folder, and loads the shipped config; `AssetRegistryAsync.test.cpp` does the same for the recipes.

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
on the loading scene, a structural fault in a creature folder on the splash, Esc returns to the menu, and the
restored file loads.

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
