# Smell

Some things stink and some smell good, and colonists notice. A smell is an emitter on an entity (bio pile, rotting food, an unwashed colonist, smoke, cooking, flowers) with a kind, a strength, and a radius; a colonist's exposure is the sum of emitters in range with distance falloff, queried on demand, with no diffusion field. Walls and closed doors block most of it, so a latrine with walls is better than a hole and an enclosed room holds a smell in. Strength decays as the source ages or is cleaned. Exposure feeds mood as thoughts ("something reeks", "smells like dinner") and steers choices: where to sleep, where to eat, where not to linger.

## Consumers

- [Mood](../colonists/mood.md): bad smells are a penalty, good ones a small bonus; sleep-spot and bio-spot preferences read it.
- [Germs](./germs.md): most strong bad smells mark a contamination source.
