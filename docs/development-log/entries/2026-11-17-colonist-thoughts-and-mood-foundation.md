# 2026-11-17 - Colonist thoughts and mood: the foundation, and strict config loading

## Summary

Over 2026-11-16 and 2026-11-17 the colonist mood and thoughts work went from design docs to a running system. Mood is now a simulated value instead of a number the UI computed each frame, colonists hold thoughts that in-game events create from XML definitions, the thoughts show in the log and the mood breakdown, and the record of kept thoughts is bounded. Traits and attributes are rolled at spawn, and colonists remember each other, so `near(colonist)` finally means something.

Alongside it, the config loaders on the colonist side moved to strict reading: a file with any problem loads nothing, the message names the file, the element instance, and the attribute, and the loading failure view shows the messages on screen instead of only in the log. The ECS component pool also stopped answering stale entity ids.

## Details

### Specs

PR 461 added the design docs for colonist mood and thoughts ([mood.md](../../design/game-systems/colonists/mood.md), [thoughts.md](../../design/game-systems/colonists/thoughts.md)) and one-paragraph stubs under `world/` for fire, smell, germs, noise, crowding, and wetness, which mood and thoughts read from. Mood is meant to be the gameplay loop (the player manages it, and a colony that doesn't fails), and nothing in the sim read it before this.

### What was built, in order

- Thought record (PR 464, WOR-932, thought record): the `Thoughts` component and `ThoughtSystem`, with create, hold, and end, plus queries for the mood work (`forEachHeldThought`, `heldMoodTotal`) that don't allocate.
- Mood (PR 466, WOR-949, mood in the sim): `Mood` and `MoodSystem` at priority 12, easing toward a target at 5 points per game hour, with need weights, the penalty curve, and the bands in `assets/config/colonists/mood.xml`.
- Conditions (PR 465, WOR-948, condition evaluator): a parser and evaluator in `libs/sim/conditions/`, with the vocabulary (terms, named sets, which tasks count as work, night hours) in XML.
- Colonists remember colonists (PR 467, WOR-968, remember the movers colonists see): the vision pass records each mover seen, and `near(colonist, r)` reads it, with a recency window in `memory.xml`.
- Thought types as XML (PR 469, WOR-933, thought types; it also covers WOR-934, feeling selection, and WOR-938, lines): feelings, lines, importance, and stacking per type.
- Strict config numbers (PR 475, WOR-978, mood and priority numbers): one `ConfigReader`, with the C++ defaults deleted.
- Traits (PR 477, WOR-969, colonist traits as data) and attribute rolls (PR 480, WOR-980, roll attributes at spawn).
- Thought sources (PR 479, WOR-935, thought sources 1): need levels, actions, discoveries, and idle, with the need thresholds moved to data.
- Thoughts on screen (PR 481, WOR-941, thoughts in the log and the mood breakdown).
- Pruning (PR 483, WOR-945, bound and pruning for kept thoughts).
- Load errors on screen (PR 484, WOR-992, loading failure view), then closed symbol sets (PR 487, WOR-996, unknown names in a condition fail the load).
- Action events (PR 488, WOR-999, an action reports Finished only when its effect took).
- Strict loaders for work types, recipes, and task chains (PR 489, WOR-991), then recipe names, the asset manager, and action types (PR 490, WOR-1046).
- Stale entity ids (PR 492, WOR-977, a stale entity id finds no component).
- Dev tools (PR 494, WOR-947, give a thought and set an attribute from the Dev Tools tab).

### Decisions worth keeping

A thought is an event plus a feeling, and every mood effect arrives and leaves as a thought, so there is one place to look for why a colonist's mood moved. Need thoughts carry no mood effect, because the needs score already prices a low need into mood; giving them one would count it twice.

C++ defines the trigger mechanisms (`needFalls`, `needRises`, `actionStarts`, `actionFinishes`, `firstSight`, `recipeLearned`, `idle`) and XML decides which thought each one creates, the thresholds, and the idle rate. A thought fires on a change only: a jump across several levels fires once, a colonist spawned at a low level is silent, and work already begun is no start. The landing look is memory given, so it creates no first-sight thoughts.

Need thresholds (seek, urgent, critical) moved out of C++ into `assets/config/colonists/needs/`, so the AI and the thoughts read the same numbers and the AI's fixed critical constant is gone.

There is one load path, `ecs::loadColonistRules`, whose stages stop at the first failure, and the registries it builds (traits, attribute rolls) are owned by `ColonistRules` with no singletons. Spawn, the condition vocabulary, and the Bio tab all read the registry the world was built with.

A config file with any problem loads nothing, and the message names the file, the element instance, and the attribute. The C++ defaults were deleted so a missing value cannot hide; the exception is where omission keeps a documented meaning (`needsHands` omitted means false).

The symbol sets whose names have one owner (`kind`, `trait`, `need`, `attribute`, `task`) are closed, listed in `assets::kOwnedSymbolSets`, and a conditions file that declares or opens one is a load error. A typo such as `need(Hungr)` then fails at load with the valid names listed, instead of reading Unknown forever. `event`, `behavior`, and `mood_cause` stay open, and a term or kind declared ahead of its system (`wetness`, say) still parses and reads Unknown, as mood.md's Unknown rule says.

A recipe's input, output, or station name must be a loaded definition. Before, any name the asset registry had seen passed, including a misspelled harvest yield.

Pruning runs from `ThoughtSystem::create` when the record reaches the start size and stops at the stop size. Never removed: held thoughts, major thoughts, the first of each event type, the first about each subject, and anything whose latest arrival or end is inside the recent window. Pass 1 merges runs of the same event type, subject, and feeling entry into the earliest thought, one at a time, so it stops exactly at the stop size; pass 2 drops by weight, one occurrence's amount, and count over age in game days, lowest first. There is no randomness and no unordered container, so the same record always prunes the same way, and ids stay ascending so nothing that names a thought by id can point at a removed entry.

A thought's arrival is stamped with the clock's day and minute (`TimeSystem::clockTime()`, which the HUD clock reads too), so a dev time-set does not re-date old thoughts. The dossier sizes from its bounded tabs only, with Tasks and Log filling that area and scrolling, so it opens at the same size for every colonist.

Trait and attribute rolls share a hand-rolled draw on raw `mt19937` output (`RandomDraw.h`), so a seed gives the same colonist on every platform. Idle thought rolls use a per-colonist seeded stream.

`ActionSystem::completeAction` used to report Finished before applying the effect, so an Eat whose food had vanished still gave `ate_good_meal`. It now applies the effect first and reports Finished only when the effect took. The audit found three actions (Craft, PickupPackaged, PlacePackaged) crediting an effect that never happened, and all three are fixed. A Deconstruct also counts as resumed work only once `StructureBlueprint::teardownBegun` is set.

`ComponentPool` used to look components up by entity index and ignore the generation, so an id held after its entity died reached whatever entity reused the index. The pool already stored the full id beside each component; lookups now compare it, so a stale id finds nothing, and `addComponent` on a dead id asserts in Debug and attaches nothing in release. The `isAlive` checks that existed only to guard a lookup were removed. The cost was reasoned from the code (one compare on `get`, one more load on `has`) and not measured.

Load errors reach the player through a shared `LoadFailureView` (the first five messages in full, "and N more", the log path), used by both the loading scene and the splash. The asset manager lists recipe problems in a RECIPES block and counts them in its header.

## Related documentation

- [mood.md](../../design/game-systems/colonists/mood.md)
- [thoughts.md](../../design/game-systems/colonists/thoughts.md)
- [attributes.md](../../design/game-systems/colonists/attributes.md)
- [needs.md](../../design/game-systems/colonists/needs.md)
- [config-loading.md](../../technical/config-loading.md)
- [interruptable-crafts.md](../../technical/interruptable-crafts.md)
- [Scenario 18, colonist thoughts](../../testing/scenarios/18-colonist-thoughts.md)
- [Scenario 19, load errors on screen](../../testing/scenarios/19-load-errors-on-screen.md)

## Next steps

- The accident event and its thought source (WOR-1048): the sim has no accident event, so `accident_in_public` never fires.
- The bubble selector (WOR-939) waits on two design decisions: what a thought's source is for the per-source switches (WOR-981), and starting numbers for the bubble budget (WOR-965).
- Low-mood behaviors as data (WOR-951) wait on settling how several table entries are expressed and how a behavior's effect is scripted.
- Need decay rates are still in C++ (WOR-834, need levels as data).
- The construction loader still has silent defaults (WOR-1047).
- The colony overview should show a count of held thoughts (WOR-1040).
- Recall (WOR-943) must protect or redirect pruned targets.
- No save system exists, so nothing here round-trips yet.
