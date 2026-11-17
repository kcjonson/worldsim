# Colonist thoughts

A thought is something a colonist notices and how they feel about it. Thoughts are what give a colonist an inner life beyond the tasks they carry out: the same colony reads differently when one colonist is quietly pleased with a new axe and another can't stop thinking about the smell.

Every thought is recorded. A few are shown to the player as a bubble over the colonist's head. Most are not.

## Principles

- **Thoughts are the record, bubbles are the presentation.** A colonist has many thoughts a day. The player sees a handful.
- **Show first.** The player watches a colony, they don't read one. A thought is expressed by a bubble, a small animation, and what the colonist does. Words are a caption.
- **Words are quips.** When a thought has a line it is a few words, written by hand, and worth the glance. Lines are never assembled or generated into prose.
- **A bubble is a small delight.** It should feel meaningful and never crowd the screen. It appears for something major, or very occasionally for something small.
- **Thoughts report, they don't cause.** Another system does something (a need drops, a tree is discovered, a room starts to stink) and the thought is how the colonist registers it. The one thing a thought carries is a mood effect.
- **Every mood effect is a thought.** Nothing changes how a colonist feels without a thought arriving or leaving. Not every thought has a mood effect.
- **Numbers live in the log, never in a bubble.** A bubble is a picture or a quip. The log and the mood breakdown show a thought's mood effect and time left.
- **Every system can speak.** Needs, environment, knowledge, relationships, tasks, and mood all create thoughts through one interface, and so can mods.

## How the system works

### What a thought is

A thought is an event and a feeling about it.

| Part | Meaning |
|---|---|
| Event | What happened or what is true: "soaked", "saw a new kind of tree", "masterwork destroyed" |
| Subject | Who or what it is about: a colonist, an entity, a place, or nothing |
| Feeling | How this colonist takes it: an emotion word such as annoyed, delighted, resigned, vengeful |
| Mood effect | Optional. A signed amount and either a duration or a cause it lasts as long as |
| Importance | Major, normal, or trivial. Set per event type |
| Bubble | An animation, an icon, a line, or a combination |
| Time | When it arrived |

A thought with a mood effect is **held** while the effect lasts, then it ends. Its ending is a thought too when the event type defines one ("finally dry"). A thought with no mood effect is momentary: it is recorded and gone.

A recorded thought is part of the colonist's [memory](./memory.md). Memory holds what a colonist knows about the world; thoughts are what they went through in it. Thoughts are kept up to a high limit per colonist; see [What is kept](#what-is-kept).

A repeat of a held thought, meaning the same event type about the same subject, does not pile up: it refreshes the held thought and restarts its timer. The same event about a different subject is a separate thought.

A type can say it stacks. A stacking thought counts its repeats and shows the count; its mood effect is the amount times the count, the whole stack shares one timer that restarts on each repeat, and it ends as a whole.

### Event and feeling

The system that creates a thought reports only the event and its subject. The colonist supplies the feeling.

Each event type lists the feelings it can produce, each with a condition on the colonist's attributes, traits, likes and dislikes, and relationship to the subject. The most specific feeling whose condition holds is the one the colonist has. The feeling sets the size of the mood effect, and it can set it to nothing. The one exception is relief from a low-mood behavior, whose size the behavior sets ([Mood](./mood.md)).

One event, three colonists: a finished chair is smashed. The Volatile one is furious (-12). The one with low Work Ethic is resigned (no effect). The one who disliked its owner is quietly pleased (+3).

Feelings are a shared list of emotion words. Each has an icon and a colonist expression, so a feeling can be shown without text.

### What creates thoughts

| Source | When | Example | Mood effect |
|---|---|---|---|
| Needs | a need changes level, in either direction | "I'm starving", "that hit the spot" | yes |
| Environment | smell, noise, wetness, crowding, or temperature crosses a threshold, in either direction | "this toilet smells", "finally dry" | yes |
| Events | something happens to the colonist or in front of them: a meal, a night's sleep, an accident, a fight witnessed | "slept on the ground" | yes |
| Likes and dislikes | the colonist meets something they favor or can't stand: a food, a material, an animal, a kind of room | "ugh, turnips again" | yes |
| Relationships | an opinion shifts; time spent with a friend or with someone disliked | "Rin took the last of the berries" | usually |
| Mood | a low-mood behavior fires, relief lands, a refusal starts or ends | "I can't do this today" | yes |
| Knowledge | first sight of a new thing, a recipe learned, a first craft finished | "This is an interesting tree!" | sometimes |
| Tasks | notable work starts or finishes | "time to make an axe" | no |
| Recall | the colonist meets something tied to an old thought; see [Recalling old thoughts](#recalling-old-thoughts) | "that's my oak" | usually |
| Idle | a slow random roll while nothing else is on the colonist's mind | "my butt itches" | no |

A source does not decide how the colonist feels or whether the player sees the thought.

Thoughts are created on a change: a threshold crossed, an event, a first time. A condition that simply continues creates nothing new.

### Lines

A line is a quip of a few words. An event type has a small pool of them, each optionally limited to a feeling, an attribute range, or a trait, and able to name the subject ("{subject} took the last of the berries"). The most specific line that fits is used; among equals one is picked at random, avoiding the one this colonist used last for that event.

A line names its cause. "This toilet smells" tells the player something; "I feel bad" does not.

Many thoughts have no line. Their bubble is the feeling's icon or the event's animation.

### Which thoughts become bubbles

Every thought is scored when it is created.

- **Importance.** Major thoughts always pass. Trivial ones start low.
- **Novelty.** The first time this colonist has a thought of this event type scores highest. Each repeat scores lower, and the score recovers slowly over days without it.
- **Mood effect.** A larger effect scores higher.
- **Attention.** A colonist off screen never bubbles. A selected colonist scores higher.

A thought that scores above the threshold then has to fit the budget:

- a minimum gap between bubbles on the same colonist
- a minimum gap between bubbles of the same event type anywhere
- a cap on bubbles on screen at once
- a colony-wide rate, so a large colony is not noisier per minute than a small one

Major thoughts skip the budget. When two thoughts compete for one slot, the higher score wins and the loser is only recorded. A thought that does not bubble when it is created never bubbles later.

The result the tuning aims for: a first discovery always shows, the fortieth "I'm hungry" never does, and an ordinary colonist shows roughly one small bubble a game day.

### Bubbles

A bubble appears over the colonist's head for a few seconds and fades. It holds, in order of preference:

1. the event's animation, a short looping picture (a stink cloud, a spark of an idea, a rain drop)
2. the feeling's icon
3. a line

A bubble with an animation or icon and a line shows the picture, with the line beside it when the colonist is selected or hovered. A bubble whose whole point is the line (a quip) shows the line.

The colonist's expression matches the feeling for as long as the bubble shows.

Bubbles are private. Other colonists do not see or react to them.

### Recalling old thoughts

A colonist who meets something tied to an old thought can have a new thought about it. Recall always has a cue in the world, so the player can see why it happened. It is never random.

| Cue | Example |
|---|---|
| The subject again | passing the first oak they ever saw; seeing the colonist who soiled their bed |
| The place | walking by the riverbank where a friend drowned |
| The same event | eating the stew they first had at a party; the first cold rain after a soaking that made them sick |
| The date | a year since landing |

A recalled thought is a new thought. Its event is the recall, its subject is the old thought's subject, and it links to the old thought in the log.

- **Strength.** Its mood effect is a fraction of the original's, a third by default, and shorter.
- **Feeling.** It starts as the original feeling. An event type can say how its feeling changes with time: grief becomes fondness, mortification becomes amusement. A recall can therefore be a comfort where the original was a blow.
- **Floor.** Only thoughts that were major, were a first, or had a mood effect above a threshold can be recalled. Being rained on once is not something a colonist carries.
- **Cooldown.** Each old thought can be recalled at most once in a long interval, 5 game days by default.
- **No chains.** A recalled thought cannot itself be recalled, and cannot cue another recall.

Recall is what makes things matter to particular colonists.

- **Attachment.** A colonist who has fond recalls of a tree, a room, an object, or a person is attached to it. Losing it gives them a thought that anyone else would not have.
- **Likes and dislikes that develop.** Repeated recalls of the same subject with the same feeling can give the colonist a new like or dislike of that kind of thing.
- **Relationships with a past.** Recalls about another colonist feed opinion, so a grudge or a bond outlasts the timed mood effect that began it.

Recalled thoughts go through the bubble selector like any other, and a first recall of a given memory scores as novel.

### What is kept

Each colonist keeps up to a fixed number of thoughts, 2,000 by default. The limit is high enough that a colonist's whole first years fit. Pruning starts when a colonist reaches 90% of the limit and runs until they are back at 80%, so it happens in occasional batches and never one thought at a time.

Some thoughts are never pruned:

- held thoughts, whose mood effect is still running
- major thoughts
- the first thought of each event type, and the first about each subject (the first tree of a kind, the first meeting with a colonist)
- thoughts a colonist's attachment, like, or dislike grew from
- everything from the last 10 game days

The rest are pruned in two passes.

1. **Merge repeats.** Thoughts with the same event type, subject, and feeling collapse into one entry carrying a count and the first and last dates ("Hungry. Miserable. 40 times, spring to autumn"). The oldest runs merge first. Merging keeps the shape of what the colonist lived through at a fraction of the cost.
2. **Drop the least memorable.** If merging was not enough, entries are dropped lowest score first, where

   `score = importance x (1 + size of mood effect) x (1 + count) / age`

   Trivial thoughts with no mood effect from long ago go first. A merged entry that stood for many thoughts outlasts a single stray one. Ties drop the oldest.

Pruning uses no randomness, so the same history always prunes the same way.

Pruning changes only what the log can show. The bubble selector's novelty does not read the record: each colonist keeps a count and last time per event type, which is small and never pruned, so an old habit does not become new again when its thoughts are merged or dropped.

## Effect on gameplay

- **Reading a colonist at a glance.** Bubbles and expressions say how a colonist is taking things without opening a panel.
- **Noticing problems.** A bubble about a smell, a cold night, or a disliked bunkmate points the player at something to fix before it shows up as low mood.
- **Marking progress.** Discoveries and firsts get a moment on screen.
- **Things worth protecting.** Colonists grow attached to particular trees, rooms, objects, and people. What the player keeps, moves, or destroys costs or pays differently depending on who cares about it.
- **Character.** The same event lands differently on different colonists, and it shows. Likes, dislikes, and quips make colonists distinguishable.
- **Small pleasures.** A quip or a happy bubble is a reward for watching.

## Where it shows in the UI

- **World view.** Bubbles over colonists' heads, and matching expressions.
- **Info panel and colonist details, Log tab.** Each thought is one block holding the event, the feeling, and the mood adjustment ("Slept on the ground. Miserable. -10 mood, 1 day"). By default the log shows the thoughts that bubbled, every major thought, and every low-mood episode and refusal, with a filter to show all of them. Merged entries show their count and date range. It is a reference for "why", not something to read through.
- **Colonist details, Health tab.** The mood breakdown lists every held thought that has a mood effect, with its amount and time left. See [Mood](./mood.md).
- **Colony overview.** Held thoughts counted by event type across the colony ("slept on the ground: 5 colonists"), largest total mood effect first.
- **Settings.** Bubble frequency (fewer, normal, more), and a switch per source to stop its trivial and normal thoughts from bubbling. Major thoughts always show.

## Data

Event types, their feelings, lines, importance, mood effects, and animations are config (`assets/config/thoughts`), and mods add to them. The list of feelings is config. The scoring weights, the threshold, and the budget numbers are config.

```xml
<thoughts>
  <tuning maxLineLength="48" whileCheckMinutes="15"/>

  <feeling id="miserable" icon="feeling_miserable" expression="droop"/>

  <thought id="soaked" importance="normal" animation="rain_drop">
    <feeling is="miserable" mood="-6" while="wetness > 0.6"/>
    <feeling is="furious" when="trait(volatile)" mood="-9" while="wetness > 0.6"/>
    <feeling is="refreshed" when="feels_hot" mood="+4" hours="2"/>
    <line>I'm soaked through.</line>
    <line feeling="furious">One more minute of this...</line>
    <ends thought="dried_off"/>
  </thought>

  <thought id="dried_off" importance="trivial">
    <feeling is="relieved" mood="+2" hours="3"/>
    <line>Finally dry.</line>
  </thought>

  <thought id="itchy" importance="trivial">
    <feeling is="bored"/>
    <line>My butt itches.</line>
  </thought>
</thoughts>
```

- Every file's root is `<thoughts>`. The folder's own files load first, then its subfolders' in path order; a later file's feeling or thought type with the same id, or its `<tuning>`, replaces the earlier one, and the same id twice in one file is an error.
- A feeling in the shared list has an `icon` and an `expression`. A thought type has an `importance` (`trivial`, `normal`, or `major`), `stacks="true"` when it stacks, and an optional `animation`.
- A type lists at least one feeling. A feeling's mood effect is a signed `mood` with either `hours`, in game hours, or `while`, a condition; with no `mood` it has no effect. A `while` is checked every `whileCheckMinutes` game minutes and the thought ends when it is false; Unknown keeps it.
- A feeling's or line's specificity is the number of terms its conditions name, a line's `feeling` counting as one; the most specific that holds wins, and between feelings of equal specificity the one written first does. Unknown never holds, and when no feeling holds the colonist has no thought.
- A line is at most `maxLineLength` characters. `{subject}` in a line is the subject's name, and a line that names the subject fits only a subject that has one.
- `<ends thought>` names the type created when a held thought of this type ends, however it ends. Endings may not loop.
- Conditions are written in the [condition vocabulary](./mood.md#conditions); a `<` inside an attribute is written `&lt;`.

## Related documents

- [Mood](./mood.md): what mood effects add up to and what low mood does
- [Needs](./needs.md)
- [Attributes](./attributes.md)
- [Memory](./memory.md): what colonists know about the world; thoughts are kept alongside it
- [Smell](../world/smell.md), [Noise](../world/noise.md), [Wetness](../world/wetness.md), [Crowding](../world/crowding.md), [Fire](../world/fire.md), [Germs](../world/germs.md)

## Addendum: decisions

A supplement, not part of the spec. Kevin, 2026-11-16.

- Every mood effect added or removed is a thought; not every thought is a bubble.
- Bubbles are rare: major things, or very occasionally. They are little moments of delight.
- Some bubbles are small animations, others are text.
- The game is a movie, not a novel. It is not a story generator in the way Dwarf Fortress and RimWorld are, and it does not present walls of generated text.
- Numbers stay visible. Dwarf Fortress's text-only presentation is not adopted.
- Numbers never appear in bubbles. A log entry holds the event, the feeling, and the mood adjustment in one block.
- Emotion words for feelings are fine.
- Thoughts whose only payoff is reading them are fine as short, digestible quips of amusement.
- Thoughts are kept up to a high bound per colonist, with an algorithm that prunes as the limit approaches.
- Recalling old thoughts on a cue, with attachment, developing likes, and softened feelings, is wanted ("kinda fun").
- The game should carry some of The Sims' joy; balance of harsh against gentle is tuned at playtest.

## Addendum: prior work

A supplement, not part of the spec.

- Dwarf Fortress pairs a circumstance with one of about 120 emotions chosen by personality, and found likes and dislikes the most retold part of a character, ahead of wounds and family. Descriptions with no link to a mechanic (mannerisms) failed. Tarn Adams, "Characterization and Emergent Narrative in Dwarf Fortress", in *Narrative Mechanics* (2021), https://doi.org/10.14361/9783839453452-007.
- RimWorld and Stonehearth give each thought an amount, a duration, and a stack limit, shown as a list in a panel. Neither shows thoughts in the world.
- Valve's response rules pick the most specific matching line and use expiring facts for cooldowns. Elan Ruskin, "AI-driven Dynamic Dialog through Fuzzy Pattern Matching", GDC 2012.
- Planet Coaster counts guest thoughts by type across the park. Cities: Skylines' Chirper pushed flavor messages unrelated to anything actionable, and players muted it.
