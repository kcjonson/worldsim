# Colonist Mood

Mood is how a colonist feels, 0 to 100. Mood and happiness are the same thing; the name is mood.

Managing mood is the player's job as the colony's overseer. Colonists can feed and look after themselves; what they cannot do is keep each other content. A colony whose mood is neglected fails: people stop working, turn on each other and on the place, and in the end stop eating.

---

## Principles

- There is one list of low-mood behaviors, shared by every colonist. No colonist has a fixed one. Each tends toward some of them as their personality develops.
- A behavior fits the situation. Starting a fight is likely with people around and close to impossible alone; nobody crosses the map to pick one.
- There is no hard line at a mood value. Onset is a fuzzy, non-linear zone, so the player sees mood fall and has time to act.
- Doing nothing is one of the behaviors.
- Every behavior can end, by its own conditions, and gives its own relief.
- Nothing is out of bounds for tone. Petty and funny behaviors belong beside the dangerous ones.
- The player can always see why. Every point of mood traces to a named cause, and every behavior is announced and logged.
- The list, the weights, and the curves are data, and a mod can add a behavior whole.

---

## How the system works

### Where mood comes from

A colonist's mood has two parts, added together and clamped to 0-100.

- **Needs.** A base score from how well the eight needs are met, weighted per need. Defined in [Needs](./needs.md).
- **Mood effects.** Signed amounts carried by the [thoughts](./thoughts.md) the colonist is holding, from things that happened to them or that they are living with.

A mood effect always belongs to a thought. It is either timed ("slept on the ground", -10 for a day; "vented to Idris", +8 for 12 hours) or lasts while its cause does ("something reeks", "crowded", "soaked"). A repeat of a held thought about the same subject does not pile up unless the type says it stacks; a stacking thought shows its count ("bottled up x3"). Every other system that affects how a colonist feels does so by giving a thought. What a thought is, what creates one, and which ones the player sees as bubbles are defined in [Thoughts](./thoughts.md).

The sum of the two parts is the colonist's **target mood**. Mood itself moves toward the target at up to 5 points per game hour, so one bad event is felt as a slide the player can watch, never a jump.

**Mood cause** is the single largest negative contributor, a need or a thought. It is shown to the player and steers which behavior a colonist reaches for.

### Mood bands

Bands name a range for the player. They gate nothing; the curves below do the work.

| Band | Mood | Reads as |
|---|---|---|
| Content | 55 and up | nothing to manage |
| Low | 30 to 54 | below 45 episodes begin, mostly held in |
| Very low | 12 to 29 | regular episodes, refusing work |
| Collapsing | under 12 | dangerous behaviors, refusing food |

### The episode cycle

1. **Roll.** Each game hour a colonist under mood 45 may roll, with a chance that rises steeply as mood falls.
2. **Pick.** Every behavior the situation allows takes part, weighted. One is picked. Often it is Hold it together and nothing visible happens.
3. **Act.** The behavior becomes what the colonist is doing. It outranks work and ordinary needs. A critical need interrupts it.
4. **End.** The behavior stops on the first of its own end conditions.
5. **Relief.** If the behavior gives relief, the colonist gets a positive thought.
6. **Develop.** The behavior's affinity rises for that colonist, and a cooldown of a few hours begins before the next roll.

### When a colonist rolls

`chance per hour = ((45 - mood) / 40)^2 x 30%`, never more than 30%

| Mood | Chance per game hour |
|---|---|
| 45 and up | 0 |
| 35 | ~2% |
| 20 | ~12% |
| 5 and under | 30% |

Personality slides the curve along the mood axis: low Sanity or the Volatile trait shifts it up by as much as ten points of mood, high Sanity down by as much. Two colonists at mood 30 do not crack on the same day.

### What they roll

Every behavior whose `requires` is met takes part, with:

`weight = base x ramp(mood) x situation multipliers x cause multiplier x affinity`

- **ramp(mood)** is 0 above the behavior's `rampStart`, 1 at or below its `rampFull`, linear between. A colonist at 18 can still just dawdle; the worse behaviors are merely available.
- **requires** is a hard gate. Unmet, the behavior is not in the roll.
- **situation multipliers** come from what is in sight or a short walk away right now.
- **cause multiplier** is x2 when the behavior matches the mood cause: hungry leans to bingeing, exhausted to oversleeping, a recent argument to a fight with that colonist.
- **affinity** is the developing personality.

Colonists act on what they know. Conditions read the colonist's and the colony's memory, never the world directly.

### Affinity

Each colonist holds an affinity per behavior. It starts at 1.0, nudged at creation by attributes and traits: low Civility raises the confrontational behaviors, high Social Need raises venting, low Work Ethic raises dawdling. When a behavior fires its affinity rises by 0.15, capped at 3.0, and the others drift slowly back toward 1.0. Over a few bad weeks a colonist becomes the one who picks fights, or the one who hides in bed.

An affinity at 2.0 or above is a **tendency**, and is shown to the player by name ("Picks fights", "Hoards", "Bottles things up").

A behavior that gives relief is one the colonist comes back to, so a player who keeps healthy outlets within reach (a friend nearby, somewhere to walk) shapes who the colonist turns into.

### Hold it together

A behavior like any other. It is available at every mood level with a base of 30, so it wins most early rolls and nothing visible happens.

- More likely: high Sanity x2, observed x1.5, a liked colonist nearby x1.5.
- Less likely: x0.8 for each consecutive hold.
- No relief. Adds a stacking "bottled up" thought, -2 each, that clears when the colonist next gets relief.

It has affinity too. A colonist who keeps holding gets better at it, and the stack means that when they do break they are lower on the curve with the worse behaviors in play.

### Refusing work and refusing food

These are states, not rolled behaviors, and behaviors happen on top of them.

- **Refuses work:** a rising chance per hour below mood 20. Work is not offered to the colonist. Needs are: they still eat, drink, sleep, and wander.
- **Refuses food:** a rising chance per hour below mood 8. Hunger is not acted on at any level. Left alone, the colonist starves.

Each clears only once mood has climbed 5 points past where the state began, so a colonist doesn't flicker in and out.

### Ending and relief

Every behavior lists its own end conditions; whichever comes first stops it. A critical need interrupts any behavior. Refusing food is the exception: hunger does not end it.

Relief is a timed positive thought. Its size comes from the behavior that gave it, not from the colonist's feeling about it:

| Relief | Mood | Lasts |
|---|---|---|
| none | 0 | |
| small | +3 | 6 h |
| medium | +8 | 12 h |
| large | +15 | 1 day |

### How it spreads

A behavior aimed at someone, or seen or heard by someone, gives them a thought: insulted, argued with, saw a fight, found their bed soiled, watched the stockpile thrown about. Those thoughts lower the witnesses' mood, and they make their own rolls. One colonist breaking in a crowded room is how three do.

---

## Effect on gameplay

### What low mood costs

| Cost | From |
|---|---|
| Work slows, then stops | dawdle, oversleep, wander off, abandon craft, refuses work |
| Supplies drain | binge eat, eat the last of something, hoard, scatter stockpile |
| Relationships sour | snipe, argue, fist fight, the spite behaviors |
| Property is damaged | smash item, vandalize structure, arson |
| Sanitation fails | go anywhere, relieve self on a station, dirty hands in the food, soil a bed |
| People are hurt | fist fight, arson, walk into the unknown |
| People are lost | refuses food, leave the colony, walk into the unknown |

### How a colony fails

Failure is a spiral, not an event. A shortage or a bad stretch lowers several colonists at once. The first episodes cost work and supplies, which deepens the shortage. Social behaviors spread the damage to colonists who were coping. Colonists who refuse work leave more for the rest, who become overworked. A colony that reaches the point where most of its people refuse work cannot feed itself, and the ones who refuse food die.

The spiral is slow at the top and fast at the bottom. Between Content and Very low the player has days; under 12 they have hours.

### What the player does about it

The player never controls mood directly. They change the conditions that make it.

- **Meet needs well, not barely.** A need that sits just above its threshold still costs mood.
- **Remove the cause.** The mood cause names it: a smell, a crowded room, a bed on the ground, a colonist they can't stand.
- **Don't overwork people.** Rest and recreation are part of the schedule.
- **Build for it.** Separate rooms, a latrine with walls, bedrooms away from the workshop, enough beds.
- **Mind who is near whom.** Friends nearby turn a bad day into venting; enemies nearby turn it into a fight.
- **Keep good outlets in reach.** A colonist who vents or walks it off gets relief and grows a tendency toward that. One with only bad options grows a bad tendency.
- **Deny bad outlets.** Behaviors need something in reach. Food stored away from where people brood is binged less; a fire with nothing flammable near it is only a fire.
- **Read the warnings.** A Low band, a "bottled up" stack, and a named tendency each say what is coming and from whom.

### What it means for building a colony

Mood is why a colony needs more than food and shelter. It is the reason to build rooms, furniture, sanitation, and places to rest, and the reason the makeup of the group matters. A colony that is efficient and miserable is a colony that is about to fail.

---

## Where it shows in the UI

Screen layout is in [In-game](../../ui/screens/in-game.md). This is what mood puts on each surface.

### World view

A colonist in an episode is seen doing it: walking off, arguing face to face, sitting idle beside their work. A colonist in a refusal state, or with an episode under way, carries a small marker above their head, coloured by severity.

### Colonist roster

- The avatar is tinted by mood and the card shows the mood percentage.
- A state badge on the card for refuses work, refuses food, and an episode under way.
- A card pulses once when its colonist drops a band.

### Info panel

- **Header.** The mood meter, with the band name and the mood cause beside it ("Low · crowded"). During an episode or a refusal state, the current-task line shows it ("Arguing with Rin", "Refusing work").
- **Needs tab.** A mood block above the needs: the meter, then the top three contributors with their amounts.
- **Log tab.** Episodes and state changes are log entries, with the picked behavior and what set it off.

### Colonist details dialog

- **Header.** Mood in the stat grid, with the band.
- **Health tab.** The full mood breakdown: the meter with target mood marked on it, then every contributor as a row with its name, signed amount, and time left. Needs and thoughts share the list, largest first. "Bottled up" shows its stack count. Refusal states are listed with how far mood must climb to clear them.
- **Bio tab.** Tendencies, by name, with a line on how each was earned ("Picks fights · 4 times this season").
- **Social tab.** Each relationship row shows the opinion score and the last few events that moved it ("Argued · 2 days ago").
- **Log tab.** As in the info panel, with the full history.

### Toasts and alerts

| Event | Severity |
|---|---|
| A colonist drops to Very low or Collapsing | warn |
| A behavior hits another colonist or colony property | warn |
| Refuses work | warn |
| Refuses food | crit |
| Dangerous behavior under way (fight, arson, vandalism) | crit |
| A colonist has left, or walked into the unknown | crit |

A toast names the colonist and the behavior, and selecting it selects the colonist and moves the camera to them. Hold it together and the quiet withdrawal behaviors raise no toast; they show on the colonist only.

The top bar alert count includes every colonist in a refusal state.

### Colony overview

Average mood, the number of colonists in each band, and the most common mood cause across the colony, so a shared problem (food, crowding, a smell) reads as one problem and not twelve.

---

## Behaviors

"Ramp" is `rampStart / rampFull`.

### Withdrawal

| Behavior | Ramp | Base | Requires | More likely when | Ends | Relief |
|---|---|---|---|---|---|---|
| Hold it together | any | 30 | none | high Sanity x2, observed x1.5, liked colonist near x1.5; x0.8 per consecutive hold | at once | none, adds "bottled up" |
| Dawdle | 40 / 30 | 10 | has a work task | overworked x2, no recreation x2 | mood recovers, or 6 h | none |
| Wander off alone | 40 / 30 | 8 | none | crowded x2, recent argument x2, low Social Need x2 | 3 h, or an urgent need | small |
| Oversleep | 40 / 30 | 8 | a sleep spot | low energy x2, night x1.5 | 4 h past rested | small |
| Walk into the unknown | 12 / 3 | 5 | unexplored ground in sight range | alone x2, low Social Need x2 | 1 day, a critical need, or lost | none |
| Leave the colony | 12 / 3 | 2 | mood under 12 for 2+ days | no liked colonists x3, no bed x2 | never | n/a |

### Social

| Behavior | Ramp | Base | Requires | More likely when | Ends | Relief |
|---|---|---|---|---|---|---|
| Vent to someone | 40 / 30 | 6 | colonist within 5 m | likes target x3, high Social Need x2 | conversation finishes | medium; small drain on the listener |
| Snipe / insult | 40 / 30 | 6 | colonist within 5 m | dislikes target x3, crowded x1.5, low Civility x2 | exchange finishes | none |
| Argue | 25 / 15 | 8 | colonist within 5 m | dislikes target x3, target also low x2 | exchange finishes, or target walks away | small |
| Spread gloom | 25 / 15 | 4 | 2+ colonists nearby | low colony mood x2 | a short while | none |
| Fist fight | 25 / 15 | 4 | colonist within 3 m | argued with target recently x4, dislikes x3 | one side hurt or flees, or a third breaks it up | medium for the winner |

### Consumption

| Behavior | Ramp | Base | Requires | More likely when | Ends | Relief |
|---|---|---|---|---|---|---|
| Binge eat | 40 / 30 | 5 | food within 15 m | hunger is the cause x2, Gourmand x2 | full, or food runs out | small |
| Eat the last of something | 40 / 30 | 3 | a food type with 1 or 2 left | scarcity x2 | eaten | small; a thought for whoever wanted it |
| Hoard | 25 / 15 | 5 | storage within 15 m | scarcity x3 | the stash is full | small |

### Property

| Behavior | Ramp | Base | Requires | More likely when | Ends | Relief |
|---|---|---|---|---|---|---|
| Abandon craft | 25 / 15 | 7 | mid-craft at a station | long craft x2, repeated interruption x2 | walks off | none |
| Smash item | 25 / 15 | 5 | loose item or tool within 5 m | just failed a task x2 | one object | medium |
| Vandalize structure | 12 / 3 | 6 | wall or station within 5 m | built it themselves x0.5, confined indoors x2 | one structure, or a short timer | medium |
| Scatter stockpile | 12 / 3 | 5 | storage within 10 m | scarcity x0.5, hoard affinity x2 | one stockpile | medium |
| Arson | 12 / 3 | 2 | lit fire within 5 m | flammable structure near x2 | fire is lit | large, then guilt if anyone is hurt |

### Spite

| Behavior | Ramp | Base | Requires | More likely when | Ends | Relief |
|---|---|---|---|---|---|---|
| Skip hygiene / go anywhere | 40 / 30 | 4 | none | no latrine x2 | done | none |
| Relieve self on a station or doorway | 40 / 30 | 3 | station or door within 5 m | just had a craft interrupted x2, unobserved x2 | done | small |
| Hide someone's tool | 40 / 30 | 3 | another colonist's tool within 5 m | dislikes owner x3, unobserved x2 | done | small |
| Soil someone's bed | 25 / 15 | 4 | someone else's bed within 15 m, owner not present | dislikes owner x4, unobserved x2 | done | medium; strong hit to the owner on discovery |
| Dirty hands in the food | 25 / 15 | 4 | prepared food or food storage within 5 m, low hygiene | unobserved x2 | done | small |

---

## Conditions

Every `requires` and multiplier is built from these.

| Group | Conditions |
|---|---|
| Nearby | `near(kind, radius)` for colonist, food, storage, station, structure, bed, lit fire, loose item |
| Nearby | `count_near(colonist, radius)`, `in_room`, `room_occupants`, `unexplored_in_sight` |
| Observation | `observed`: another colonist has line of sight to this one |
| Ownership | `owner(target)`, `owner_present(target)` |
| Relationships | `opinion(target)`, with `likes` and `dislikes` as thresholds; `recent_event(target, kind, within)` |
| Self | `mood`, `mood_cause`, `need(kind)`, `attribute(kind)`, `trait(kind)`, `current_task(kind)`, `mid_craft` |
| Self | `hours_worked(window)`, `holds_in_a_row`, `affinity(behavior)`, `time_below(mood, duration)` |
| Colony | `scarcity(kind)`: plenty, tight, short; `colony_mood` |
| Time | `hour`, `is_night` |

## Data

The behavior list, the thoughts, the roll curve, relief sizes, the cooldown, the mood rate, and the affinity rates are config. A behavior is one entry:

```xml
<behavior id="soil_bed" rampStart="25" rampFull="15" base="4">
  <requires>near(bed, 15) and not owner_present(bed)</requires>
  <weight when="dislikes(owner(bed))" multiply="4"/>
  <weight when="not observed" multiply="2"/>
  <ends after="task"/>
  <relief amount="medium"/>
</behavior>
```

Conditions are data and need no scripting. A behavior's effect, what the colonist physically does, is a script, so a mod adds a behavior by adding an entry and its effect.

## Signals from other systems

| System | What mood reads |
|---|---|
| Relationships | an opinion score per pair of colonists, moved by shared events |
| Workload | hours worked against hours rested over the last day or two |
| Scarcity | days of food and water left, as the colony knows it |
| [Crowding](../world/crowding.md) | people nearby against the colonist's tolerance |
| [Fire](../world/fire.md) | a lit fire in reach, what is flammable |
| [Germs](../world/germs.md) | contamination on food and beds, sickness |
| [Smell](../world/smell.md), [Noise](../world/noise.md), [Wetness](../world/wetness.md) | thoughts that lower or raise mood |
| Time | hour of day, darkness |
| [Crafting](../world/crafting.md) | a craft in progress, and its interruption |
| Sleep | sleep spots, bed ownership |

---

## Related documents

- [Needs System](./needs.md): the needs and their weights in mood
- [Thoughts](./thoughts.md): what carries a mood effect, and the bubbles
- [Attributes](./attributes.md): Sanity, Civility, Social Need, Work Ethic, and traits
- [AI Behavior](./ai-behavior.md)
- [Memory System](./memory.md)
- [In-game UI](../../ui/screens/in-game.md)

---

## Addendum: decisions

A supplement, not part of the spec. Where it and the spec disagree, the spec is right.

Kevin, 2026-11-16:

1. Mood is critical to the core loop, the player's job as overseer or manager of the colony.
2. The colony fails if mood isn't managed properly.
3. Colonists with critically low mood refuse to work, and eventually refuse to eat, causing death.
4. Low-mood behaviors are not fixed per colonist as in RimWorld or Oxygen Not Included. There is a list of destructive behaviors and colonists tend toward them dynamically as their personality develops.
5. A behavior is weighted by the current situation. A fight, argument, or insult is far more likely with other colonists around and very unlikely with nobody around. "It's odd in RimWorld that someone will storm across the map just to start a fight."
6. Config driven and moddable, possibly with Lua if it can be done in a performant way.
7. Not guaranteed to happen right at a threshold: a fuzzy, non-linear zone.
8. Holding it together is a behavior in itself, "a 'do nothing' outlet".
9. There must be a mechanism to stop behaviors even when serious: a timeout, and catharsis, specific to the behavior and not generic.
10. Nothing is out of bounds. Add funny ones like "shit in someone's bed" or "put their dirty hands in the food".

## Addendum: superseded content

From [Needs System](./needs.md), 2024-12-04. Fixed thresholds and generic catharsis are replaced by the roll curve and per-behavior relief.

**Thoughts (temporary modifiers), examples:** "Ate a good meal" (+5, 6 hours); "Slept on ground" (-10, 1 day); "Had accident in public" (-20, 1 day); "Talked with friend" (+5, 6 hours).

**Breakdown thresholds:**

| Mood Level | Risk |
|------------|------|
| Below 35% | Minor break (wandering, sadness) |
| Below 20% | Major break (binging, withdrawal) |
| Below 5% | Extreme break (destructive behavior) |

After a breakdown ends, colonist receives temporary mood boost (catharsis).
