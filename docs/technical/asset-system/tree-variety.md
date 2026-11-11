# Tree variety and world fit

Status: spec, partly built. Created 2026-11-10; revised 2026-11-10 for continuous age.

Trees in a forest read as one tree at a few sizes. This spec makes each tree a distinct individual (shape, age, species), grows each species the way the real one branches, lets trees age and grow during play, roughly doubles tree size while thinning the forest, lets the player see through a crown that hides something, makes a tree's size matter to the wood economy, and redraws trees so they sit in the textured ground instead of on top of it.

The aim is trees and forests more realistic than RimWorld's, held to what stays readable and fast. Form comes from botany (branching habit, crown shape, how a species ages, relative sizes). Absolute size and density are compressed to the game.

A plant's age is one number. A species lives a set number of years, a plant is some number of years old, and everything about how it looks and what it yields follows from the fraction of its life it has lived. There are no named age classes. Aging is a plant mechanism, not a tree one: any flora def that declares an age block gets it, and trees are the first users and the only ones this document tunes. Plants age in real game time with real lifespans, so most of a forest does not change within a colony, and young trees grow a bit in a very long one; the mechanism's larger visible effect belongs to short-lived plants, later (R12).

## Where it stands today

Surveyed 2026-11-10, before the first tasks of this spec landed. "Built so far" below lists what has changed since.

- A procedural tree def is generated once. `AssetRegistry::getTemplate` calls `generateAsset(defName, 42, ...)` and caches the mesh by defName. `variantCount` (15 on oak, 20 on maple) is read only by the groundcover and ground-tile paths.
- The tall-tree draw (`InstancedEntityRenderer`) uploads one GPU mesh per defName and coalesces consecutive same-def entities into one `drawInstanced` run.
- Per-tree difference is placement noise only: scale 0.8 to 1.2 and a grey brightness of 0.82 to 0.98 (`PlacementExecutor.cpp`).
- Oak and maple both run `@shared/deciduous.lua` with one palette. They differ in size params and in `branchCount`, which shifts the random stream, so the forest has two shapes.
- `deciduous.lua` predates the converged flora style: one wobbly blob, one to three concentric layers, a highlight, orange-red limbs drawn over the canopy, an offset grey drop shadow.
- Its variation is independent uniform jitter on every parameter, which sums toward an average tree.
- Placed entities are not saved. Each chunk is regenerated from the world seed, with one RNG shared by every def in spawn order, so an extra RNG draw per tree would reshuffle every later placement in the chunk.
- Collision is one rect per def, captured at load from variant 0, and scaled by `entity.scale` in `NavInputBuilder` and `StaticRectCollisionSystem`.
- Yield and work come straight from the def's `<harvestable>` (`HarvestActions.cpp`, `NeedActions.cpp`); the roll is a fresh random. The sim, colonist memory, and selection identify a tree by defName and position.
- Scale: a colonist is 1.0 m tall, an oak crown nominally 1.6 m across, a maple 1.2 m. A real open-grown oak is 15 to 24 m across, so today's trees are about 12x compressed. Measured with R11's flora view on 2026-11-10: a 100 m square centred on the quickstart landing holds 36.9 trees per 1,000 m2 (oak 19.4, maple 17.5) at 13.3% canopy cover, with 800 wood per 1,000 m2. The default camera view there (128 m x 72 m) reads 37.4 trees and 13.6%. The square takes in the river, so the forest away from it is somewhat denser. Cover includes the oak's offset drop shadow, which `getSilhouette` keeps until R3.
- Nothing fades. `rendering/world-depth-sorting.md` cut canopy fade outright ("a pawn may be fully hidden behind a tree"), which was the right call for 1.6 m crowns.
- Game time (`TimeSystem`): at 1x, one game minute passes per real second, so a game day is 24 real minutes. A season is 15 days and a year is four seasons, 60 days. At 1x a season is 6 real hours and a year 24; at 3x a year is 8 real hours, at 10x 2.4. The clock counts days from colony founding and has no year counter.

### Built so far

These landed under the first version of this spec, which had three named age classes per species. They exist in the code today and are being replaced by the continuous-age design below; each line says what replaces it.

- `<ageClasses>` with `<ageClass name weight variants yieldScale workScale>` is parsed and validated (`TreeDefSchema`, `AssetDefinition::ageClasses`). Oak and maple declare sapling, mature, and veteran at weights 0.25, 0.55, 0.20 with 4, 8, and 4 variants. Replaced by R2's lifespan and age distribution.
- `TreeTraits { ageClass, variant }` is computed from (def, world position, world seed) by `computeTreeTraits`, with the age field, per-tree hash, and thicket lean R1 keeps. The variant is taken modulo the class's variant count. Replaced by R1's growth step and individual.
- Templates are keyed (def, age class, variant) through `AssetRegistry::templateId`, and `PlacedEntity` carries `traits` and `templateId`. `generationSeed` hashes the def and the age class name with the variant, so a class's variants and another class's variants are unrelated trees. R3 keys on growth step instead and seeds from the individual alone.
- `prewarmTreeVariants` builds every (age class, variant) of a def with `<ageClasses>` in parallel at load. Tall or short routing is decided from the class named "mature", variant 0 (`matureTraits`, `isTallDef`).
- The generator receives `ageClass` (the class name) as a Lua global. Replaced by `age` (R5).
- The flora view counts trees per age class under each def and takes `age=<name>&near=x,y`. Replaced by R11's histogram and age range.
- On a branch not yet merged when this was revised: the collision rect captured per age class during the prewarm, read through `AssetDefinition::collisionFor(ageClass)`, with `AssetRegistry::checkCollisionVariantIndependence` run by `asset-cli validate --render-smoke`. R4 keeps all of it, indexed by growth step.
- Not built: R4 on main, R5 to R10, and yield by age (R8).

### Existing code this builds on

From a survey of the render, asset, placement, and tool code on 2026-11-10. Each line is something to reuse or something the plan has to change.

Variants and generation:
- The Lua global `variantIndex` already existed and was always 0 (`generateAsset` hard-set it; it now passes the variant). Callers put the variant in the seed instead: groundcover uses seeds 0 to N-1, ground tiles use `variantSeed` (a hash of the def's seed group and the variant, `GroundTileBaker.cpp`), collision capture uses 0. Lua seeds with `seed + variantIndex`, so consecutive integers in both would collide.
- Templates are built lazily on first `getTemplate`, from a chunk worker or the render thread, under one registry-wide mutex held through generation and tessellation. Nothing was generated at load except the collision rect.
- Ground tiles already have what tree templates need: parallel generation per variant and a disk cache keyed on def, params, script bytes, and the Lua API version (`GroundTileCache.cpp`). A planned variant cache for procedural assets was never built.
- `GroundcoverRenderer` already picks a variant from world position at draw time, with one GPU handle per variant. Its hash has no world seed and no def salt, and its per-chunk bucketing cannot interleave in the depth-sorted stream, so trees share the handle-per-variant idea and nothing else.
- A template is one mesh of at most 65,535 vertices. Over that, `getTemplate` returns null and the tree does not draw.
- Each GPU mesh handle reserved room for 50,000 instances (1.6 MB). `InstancedEntityRenderer` now makes a handle on first draw at 4,096 instances and doubles it when a run needs more.

Generators:
- `conifer.lua` and `palm.lua` already follow the converged recipe (aligned rim copies, backing fill, lit caps, textured bark) with the trunk foot at the origin. `deciduous.lua` is the only tree script that does not. Both read a `growthStage` param that no def sets.
- No branching or skeleton code exists in any generator. Useful pieces sit in the ground scripts: a tapered strip along a polyline (`stripShape`, `ground_bare.lua`), isolated random sub-streams per stage (`F.stream`, `ground_grass.lua`), a grass tuft (`upFan`), and HSV tone mixing.
- Generators cannot share code. `require` is disabled and every generation builds a fresh Lua state, so each script carries its own copy of the same helpers.
- Lua has no strokes, gradients, or named parts. A procedural mesh carries no `MeshPart`s; those come only from SVG shape ids.
- `deciduous.lua` draws its trunk foot 0.4 to 1.1 m south of the origin while its collision rect sits at the origin, so today the trunk a colonist walks around is not where the trunk is drawn.

Placement:
- A grove field already shapes forests: domain-warped value noise at 105 m, a pure function of world position and seed (`PlacementExecutor.cpp`). Its low tail thins spawn chance to 12% (glades); its high tail tightens spacing to 55% (thickets).
- `spaced` spacing checks only the same def in the same chunk. Oak, maple, and birch would be placed with no regard for each other.
- `<avoids>` and `<affinity>` strengths are computed and then only compared against zero, so they do nothing. Only `<requires>` has an effect.
- One candidate is tried per 4 m x 4 m block per def, so a def tops out at 62.5 per 1,000 m2.
- Scale (0.8 to 1.2) and brightness are one distribution shared by every def.

Rendering:
- The depth key is the mesh's lowest drawn point (`computeAnchorY`), and tall-or-short is decided per placed instance from mesh height times scale, in two places that must agree (`WorldDepthSort.h`, `BakedEntityMesh.cpp`).
- Visible trees are gathered by trunk position inside the view padded by a fixed 2 m. The selection broad phase is a fixed 8 m.
- `getSilhouette` rasterises every triangle whatever its alpha, so the oak's outline already includes its drop shadow.
- The instanced path multiplies vertex colour by a per-instance RGBA tint and blends premultiplied. Vertex hooks move positions only.
- salal has an offscreen `RenderTarget` usable inside a frame (the ground tile baker uses one) and `drawImage` with a tint, which together give layer opacity. It has `drawPolygon` (cached fill with an antialiased border), and `emitSorted` already injects the selection outline at a sorted depth. The backend's instanced draw takes an index range that `drawInstanced` does not expose. There is no stencil use, no multiply blend, and no base-vertex draw.
- `GhostRenderer` (placement preview) lowers every vertex's alpha, so it has the layer-stacking problem and is no use for trees. It draws above all entities.
- The tall path has no fade or LOD; trees draw at every zoom down to 0.25. `path:setLod` exists and only the ground scripts use it.
- `emitSorted` counts its entities, draw calls, and triangles; the last two are `/api/state?what=uprights`, which `scripts/perf.mjs capture` records. At the landing's default view it is 228 draw calls for about 380 tall occluders.

Sim and tools:
- Harvest and the selection panel know a tree only as defName and position; neither ever holds a `PlacedEntity`. Trees spawned by `/api/dev/spawn` are ECS entities, not placed entities.
- `main-game-ui-design.md` mocks the tree panel as "Growth: Mature" and "Yields: ~40 wood". R8 replaces the growth line with the tree's age in years.
- Wood stacks to 40, so one large tree fells into several piles.
- `asset-cli` is headless: `render <def> --seed`, and `validate --render-smoke`, which is the only validation that runs a generator. `AssetValidator` is static XML checks.
- The asset manager shows procedural variants at seeds 1 to 6, each fitted to its own thumbnail, so it shows neither the game's variants nor relative size.

## Decisions

| # | Decision |
|---|----------|
| D1 | Per-tree traits (age class, mesh variant) are a pure function of the def and the tree's world position. No placement RNG draws, no save data. Superseded by D15, 2026-11-10: the owner made age continuous and growing, so traits also depend on the game clock. |
| D2 | Age class is spatially coherent: a low-frequency field picks it, so old stands and young regrowth form patches. Superseded by D15, 2026-11-10: the owner dropped named age classes; the field now picks a base age. |
| D3 | The generator draws each age class at its true size. Placement scale narrows to 0.93 to 1.07. Superseded by D16, 2026-11-10: there are no age classes to draw; the generator draws each growth step at its true size. |
| D4 | Variation between individuals of a species is discrete, correlated traits (crown form, condition), never per-parameter jitter. |
| D5 | Species differ by growth habit first (R6), then palette, lobe style, and cluster density. This epic covers oak, maple, and a new birch. Pine and palm keep their generators and are rescaled only; growing them from a skeleton is a later epic. |
| D6 | Wood yield and felling work scale with age class. Superseded by D18, 2026-11-10: the owner dropped named classes; yield and work follow the size the generator drew. |
| D7 | Ground contact is a contact shadow at the trunk foot, a root flare, and grass tufts over the base. No canopy or drop shadow. Engine blob shadows stay with the Living Environment epic. |
| D8 | Age is fixed for the life of the tree. No growth over time in this epic. Superseded by D17, 2026-11-10: the owner wants trees to update periodically as they age. |
| D9 | A species uses one render path for all its age classes, chosen by its mature height. Superseded by D16, 2026-11-10: the same rule, chosen at the reference age, since there is no mature class. |
| D10 | Scale and density are "open woodland": an oak crown at the reference age is 3.6 m (about 5x compressed from real), forest canopy cover targets 35%, and tree count roughly halves. No biome goes above 40% cover without a new decision, except thickets, which may reach 55% (R9). Decided 2026-11-10. |
| D11 | The generator grows a branch skeleton from a few per-species growth numbers and hangs foliage on the branch tips. Age is how long it grew. Concepts from Honda and Borchert-Honda; our own code, no library. |
| D12 | A tree that hides an actor turns see-through: it is flattened offscreen and drawn at about 30% opacity. This reverses the canopy-fade cut in `world-depth-sorting.md`, because D10's crowns hide a colonist for seconds, and several at once. Render-only; the sim never reads it. |
| D13 | Generators share helpers through a prelude the engine loads before each script (R5). In scope for this epic. Decided 2026-11-10. |
| D14 | Age is continuous. A species declares a lifespan in years; a tree has an age in years; the generator and everything else see the age fraction, age over lifespan, clamped to 0 to 1. No named age classes anywhere: not in XML, code, or UI. A tree past its lifespan stays at fraction 1, an old tree. Decided 2026-11-10 by the owner. |
| D15 | A tree's age in years is a base age from (def, world position, world seed) plus the game years elapsed since the colony was founded. The base age is spatially coherent (old stands and young regrowth form patches) and comes from noise and a hash, never the chunk RNG. Nothing per tree is saved: a chunk generated later computes the same tree at its current age. Decided 2026-11-10. |
| D16 | Growth steps are mesh sharing and nothing more. The engine draws a tree at the nearest of a fixed number of growth steps per species so thousands of trees share GPU meshes. Steps are unnamed and never appear in data files or UI. The generator draws each step at its true size; placement scale narrows to 0.93 to 1.07. A species uses one render path, chosen at its reference age. Decided 2026-11-10. |
| D17 | Trees age with game time. On a slow tick (once per game season) every loaded tree whose age has crossed into the next growth step takes its new step. Size follows a saturating curve of age, so young trees change within a playthrough and old ones barely. Decided 2026-11-10 by the owner. |
| D18 | A tree's collision rect, wood yield, and felling work come from the generator per growth step, from the tree it drew: never from a named scale and never from the individual. Decided 2026-11-10. |
| D19 | Plants age in real game time with real lifespans. Nothing is added to speed that up: no growth-rate factor, shorter year, or younger age mix. Most of a forest does not change within a colony, and in a very long colony young trees grow a bit, which is fine and expected. That follows from real time and is not a rule against change. Decided 2026-11-10 by the owner. |
| D20 | Aging is a plant mechanism. Lifespan, age as a fraction of it, the `age` generator input, growth steps, and the per-step box and yield apply to any flora def that declares an age block. Trees are the first users and the only ones this document tunes. Decided 2026-11-10 by the owner. |
| D21 | The mechanism's code is named for plants (R1, R2). Tree-specific parts, the skeleton generator, species, scale and density, and see-through crowns, keep tree names. |
| D22 | The slow plant tick is deliberately coarse and has little to do until other inputs exist. Seasons and fruit are two later inputs that land in the same tick; both are out of scope here (R12). |
| D23 | Wood does not depend on regrowth. The forest is effectively unbounded, so wood is not scarce; a colony that has cut what is near it goes further for more. Felled trees are not replanted or regrown, by design. Decided 2026-11-10 by the owner. |

## Requirements

### R1 Per-tree age and traits

- One function in `libs/assets` gives a plant's age in years from (def, world position, world seed, game years elapsed). A second gives its traits, `assets::PlantTraits { uint8_t growthStep; uint16_t variant; }`, from the same inputs. They link no renderer, so `sim` can call them. The mechanism is plant-general (D20): today's `TreeTraits`, `computeTreeTraits`, and the `TreeTraits` files become `PlantTraits`, `computePlantTraits`, and `PlantTraits.h/.cpp`, and the age function is `plantAgeYears`. `kAgeFeatureMeters` keeps its name.
- Harvest, selection, and dev-spawned plants have only a def and a position, so they call the functions with the current game clock. `PlacedEntity` and the ECS `Appearance` keep a copy of the traits for the renderer. Position is quantised to 0.1 m first, the grid colonist memory already uses, so every caller agrees.
- Game years elapsed is the clock's days since founding, plus the time of day, over 60. When saving exists, the game clock is saved; no tree is.
- Base age keeps today's mechanism. The thicket lean applies to a def placed in groves; it is a tree detail. The age field is the grove field's noise at about 30 m with its own salt, blended with a per-tree hash term and made uniform. Where the grove field is in its thicket band the value leans young, so a thicket is a dense stand of small trees and not old trees packed trunk to trunk. That uniform value then maps to an age fraction through the def's age distribution (R2): piecewise linear through (0, 0), its three percentile points, and (1, 1). Base age in years is that fraction times the lifespan.
- Age in years is base age plus game years elapsed. The age fraction is that over the lifespan, clamped to 0 to 1. Outside thickets, at founding, the fractions follow the def's distribution.
- The growth step is the step whose age range holds the age fraction (R3).
- The variant is the individual: a hash of position, world seed, and the def, modulo the def's individual count. It does not depend on age, so a tree keeps its variant for life.
- Placement positions, rotation, scale, and tint of every existing entity are unchanged for a given seed, apart from the scale range in D16.
- A def without an age block does not age: it has one growth step and `variantCount` variants, and every other asset behaves as today. Pine, palm, and every bush, grass, and reed are such defs until they are given one (R12).

### R2 Def schema

The block belongs to any flora def (D20). Oak, maple, and birch are the first to declare it. Today's `TreeDefSchema.h/.cpp` (`TreeDefIssue`, `readAgeClasses`, `readPlacementScale`) become `PlantDefSchema.h/.cpp` and `PlantDefIssue`, with `readAgeClasses` replaced by `readAge`; `readPlacementScale` keeps its name.

```xml
<age lifespanYears="200" steps="16" individuals="8">
  <distribution p10="0.08" p50="0.35" p90="0.75"/>
</age>
```

- `lifespanYears` is the species' lifespan. Starting values: oak 200, maple 150, birch 80.
- `<distribution>` is the age fraction at the 10th, 50th, and 90th percentile of base age. Starting values 0.08, 0.35, 0.75 for all three species. The validator requires 0 < p10 < p50 < p90 < 1.
- `steps` is the number of growth steps (starting value 16) and `individuals` the number of individuals, the mesh variants (starting value 8).
- The reference age is the def's p50. Yield (R8), the tall-or-short decision, and R9's crown widths are stated at it.
- `<age>` replaces `<variantCount>` on a def that has it; the validator rejects both together. `<ageClasses>` and its `<ageClass>` attributes are deleted from the schema, the parser, and every def.
- The generator receives `age` (the age fraction of the step being drawn) and `variantIndex` (the individual) as Lua globals. The `ageClass` global goes. The generation seed is the ground tiles' `variantSeed` hash of the def and the individual, shared, and Lua seeds from that alone; the `seed + variantIndex` sum goes. The seed does not include the step, so every step of an individual is drawn from the same random streams.
- A def may set its placement scale range, `<placement><scale min="0.93" max="1.07"/>` (trees: 0.93 to 1.07, D16). It replaces the shared range for that def and draws the same number of random values, so nothing else moves.
- `growthStage` in `conifer.lua` and `palm.lua` stays as it is. When those generators get a lifespan in the later epic, `age` replaces it.

### R3 Growth steps and meshes on every tree path

Thousands of plants share GPU meshes, so a plant is drawn at the nearest of a fixed set of growth steps of its species. This is an implementation detail: steps have no names and are not in data or UI. The key is (def, growth step, individual) and nothing in it is tree-specific. Later inputs (R12) would add further parts to it beside the step and the individual; none is built here.

- Step ages are spaced so each step is a similar visible change: from age 0 to 0.75 at equal ratios of crown width along the def's size curve (R9), which puts them closer together where the tree grows fastest, when young. The remaining steps cover old age at equal intervals of age, since old-age change is shape, not width. With 16 steps, starting split: 14 up to 0.75, two for old age at 0.875 and 1.0. For oak (0.4 m at age 0 to 4.9 m at 0.75) that is about 21% wider per step.
- Each step owns the age range from halfway to the step below to halfway to the step above (halfway in log width below 0.75, in age above it).
- `getTemplate(defName, traits)`; the cache key is (def, growth step, individual). A def that does not age keys on (def, 0, variant) as today.
- Template count: 3 species x 16 steps x 8 individuals = 384. R5's triangle budget applies to each of them, and so does the prewarm.
- Callers that take the traits: `TemplateMeshCache`, `InstancedEntityRenderer` (mesh handles and run key), `bakeChunkEntities`, the depth attributes at placement, `getSilhouette` (hit test and selection outline), and `DynamicEntityRenderSystem`. `GhostRenderer` previews placement defs only and passes default traits.
- Templates of aging defs are generated before the first chunk needs them. `prewarmTreeVariants` already does this in parallel for the classed defs, with generation and tessellation outside the registry mutex. It is renamed `prewarmPlantVariants` and moves to (step, individual): 128 templates per species. Built for 32 classed variants it took about 11 ms, and the whole library load went from about 145 ms to 160 ms (asset-cli, RelWithDebInfo). Report the delta at 384 templates with the new generator. If it is more than the player would notice, reuse the ground tile disk cache's keying to store tessellated templates.
- A template over 65,535 vertices is a load error naming the def, step, and individual, never a missing tree.
- Instance capacity per mesh handle starts at 4,096 and doubles on demand (built). Handles are made on first draw, so only templates on screen hold one; 384 handles at 4,096 would be about 50 MB, and a view shows a fraction of them.
- Tall or short routing (`kShortFloraMaxHeight`) is decided per def from its step at the reference age, individual 0 (D16), at both sites that decide it today. A seedling of a tall species stays on the tall path.
- The depth key for a def with a collision rect is the rect's lower edge, the trunk foot, which `world-depth-sorting.md` already allows. Otherwise R7's shadow, flare, and tufts would move every tree forward in the sort. This waits for R5's generator, since today's oak and maple draw their trunk foot up to 1.1 m south of the rect.
- The silhouette skips triangles under 0.5 alpha, so shadows are not part of the outline or the hit area.
- The gather margin and the selection broad phase use the registry's largest template extent, not 2 m and 8 m. Without this, R9's trees pop in at the screen edges. As built, both use the largest extent of any template but groundcover (`maxReachMeters`), since short selectables such as bushes must stay clickable, and short sprites drawn, where no tree is loaded. The gather states its floor once, `entityCullMarginMeters` in `RenderContext.h`: the reach, never under the 2 m it had before.
- The asset-manager detail view shows an age strip: one row per individual, each the same individual at about ten ages left to right, using the same (step, individual) keys the game uses and one scale across the strip, so a young tree looks small beside an old one. Its own task.
- Considered and not taken: a unique mesh per tree, regenerated at its exact age, which would make growth truly continuous. It gives up instancing, so every tree is its own draw and its own mesh in memory (tens of thousands of meshes in a loaded area), and it is not proposed. If the owner asks for it, the cost is that, measured.

### R4 Collision per growth step

- The rect comes from the trunk foot the generator drew at that growth step, through `asset:setCollisionRect`, never from a named scale and never from the individual.
- It is captured per growth step at load, during the prewarm, from individual 0 of each step. A def that does not age keeps the one eager capture, stored as step 0. One accessor on the def returns the rect for a step; XML and SVG-metadata colliders apply to every step. The storage, accessor, and variant-independence check built for age classes on the unmerged branch carry over with "age class" read as "growth step".
- It must not depend on the individual. `asset-cli validate --render-smoke` generates two individuals at each step and fails the def, naming the step, if their rects differ (the XML validator never runs a generator). The twin-crowned form forks above the foot, so the foot is one trunk for every individual.
- `NavInputBuilder` and `StaticRectCollisionSystem` read the rect for the entity's growth step (`PlacedEntity::traits`). When a tree steps, its chunk's nav input changes if the rect did (R12).

### R5 Generator

`deciduous.lua` is rewritten on the recipe in `.claude/skills/flora-asset/references/style-and-view.md`; the old blob construction is deleted.

It works skeleton first, so the crown's shape is a result of how the tree grew. It is driven by one number, `age`, the age fraction, and every size and form below is a smooth function of it.

**Shared helpers first.** The engine gains a prelude: every `.lua` under `assets/shared/scripts/lib/` runs in each generator's state before the generator, and the cache keys that hash script bytes include it. The helpers five scripts copy today (clamp, random distributions, hex and tone mixing) move there once, with the pieces this generator needs: the tapered strip for limbs, random sub-streams per stage, the grass tuft, and `conifer.lua`'s textured bark. Each growth round and each other stage draws from its own sub-stream, so adding a draw in one cannot reshuffle the rest, and a tree drawn at one age and a little older is the same individual slightly grown.

The rim is expanded dark copies of the cluster shapes, as `conifer.lua` does it, since Lua has no strokes. The trunk foot is at the origin, as in `conifer.lua` and `palm.lua`; today's oak and maple shift on screen by the old offset, and their collision finally sits under the drawn trunk.

**Skeleton.** Nodes carry a ground offset and a height; the 3/4 view comes from adding height to the up-screen offset, as the style guide already does. Growth runs in rounds from a single stem. In each round every live tip extends or forks by the species' numbers (R6). The number of rounds is a smooth function of age and may be fractional: the last round runs part-way, extending tips by its fractional part, so a slightly older tree has slightly longer tips and not a different skeleton.

- Apical control: the share of vigour the leading shoot keeps at a fork. Above 0.5 the leader wins and the tree keeps one stem (birch, pine). Below 0.5 the laterals take over and the stem dissolves into boughs (oak, maple). This one number is most of the difference between tree shapes.
- Bud arrangement: alternate puts one lateral per node, each turned about 137 degrees around the stem from the last, so limbs spiral and never mirror. Opposite puts two per node, each pair at right angles to the pair before, so forks come in matched pairs.
- Branch angle off the parent, and the child-to-parent length ratio.
- Limb width follows the pipe rule (a parent's cross-section is the sum of its children's), then is drawn about twice as thick as real proportions so limbs read at game zoom.
- Shedding: the chance a shaded lower or inner branch is dropped. High shedding gives a gappy crown with trunk showing, low gives a solid one.
- Droop: how far outer tips sag below the branch that carries them.

A tree has at most 40 tips. The recursion is a few hundred operations per template.

**Foliage.** Leaf clusters hang on branch tips, sized by the tip's vigour and the species' cluster size. Their union is the silhouette. Each cluster has a dark base, a mid tone, and a cap offset up-left; one dark rim sits behind the union and one darkest-tone backing shape fills holes. Tip height sets draw order and tone: low clusters first and darker, the highest get the caps. Limbs show only where shedding left a gap. A new cluster appears small at its tip and grows with the tip's vigour, so the cluster count rising by one is not a pop.

**Age is growth time**, so a young tree and an old tree are different shapes, not scales of one. Sizes are in R9. Starting ranges along the age fraction, each blending smoothly into the next:

- 0 to 0.10: 1 to 3 rounds. Apical control starts forced high (every young tree has one leader) and eases to the species' value by 0.25. A thin stem, 1 to 5 clusters, no caps.
- 0.10 to 0.35: 3 to 5 rounds. Clusters 5 to 10; caps and full layering come in from about 0.2. Trunk width reaches its R9 value at 0.35.
- 0.35 to 0.75: 5 to 6.5 rounds at the species' numbers. Clusters 10 to 14, limbs heavier, trunk width still rising.
- 0.75 to 1.0: rounds level off near 7. Apical control lowers and shedding and droop rise, then the species' own way of growing old (R6) comes in from about 0.8. Up to 15 clusters, heaviest limbs.

**Crown forms are growth events.** Each individual draws one form and the age it begins at, from its own sub-stream: none (the species' textbook shape), a main bough lost (lopsided), the leader forked at the first round (twin-crowned, two stems from above the foot), heavy shedding (ragged, a limb showing), or wind-pruned (tips shortened on one side). Lost-bough and ragged draw late onset ages, so they are common among old trees and rare among young ones, and an individual keeps its form once it has it.

- Rare condition, about 1 individual in 8, also with an onset age: a thinning crown or a yellowing patch.
- Randomness is seeded per individual; palette jitter is small (+/-3% per channel) so species identity holds.
- The trunk foot sits at the origin and the trunk's collision rect depends on age only (R4).
- The generator reports the step's wood and work factors (R8).
- Budget: at the reference age a template tessellates to at most 1.5x the triangles of today's oak template, which `asset-cli inspect` reads as 102 triangles and 126 vertices (88 to 146 triangles across seeds 0 to 14); no step goes over 2x. Starting values.

Left out on purpose: light competition between neighbours and crowns leaning away from each other (real and visible, but it makes one tree's shape depend on others, which breaks D15), and anything at leaf level.

### R6 Species

How each grows, and what that looks like from above:

| | Oak | Maple | Birch |
|---|---|---|---|
| Real habit | stem dissolves into a few massive boughs | forks into co-equal stems, foliage packed at the shoot tips | one leader to the top, fine hanging twigs |
| Lifespan (starting) | 200 years | 150 years | 80 years |
| Apical control | 0.40 | 0.45 | 0.62 |
| Buds | alternate | opposite | alternate |
| Branch angle | 55 to 75 degrees | 40 to 50 | 30 to 40, tips drooping |
| Main limbs | 3 to 5, heavy | 4 to 6, paired | 6 to 9, thin |
| Shedding | medium | low | high |
| Light | mid-tolerant | deep shade tolerant, so leaves all through | needs full light, so leaves at the rim only |
| Crown from above | broad, uneven, lobed dome | dense symmetric oval, paired lobes, no gaps | small, thin, gappy; white trunk shows through |
| Crown width vs oak | 1.0 | 0.75 | 0.45 |
| Growing old | crown pulls in, bare dead limbs stand past it ("stag-headed"), trunk very thick | broadens, stays dense | thins and leans; does not broaden |

The growth numbers are starting values. Published parameter sets exist for oak, maple, and pine but not birch, and none were made for a top-down game, so all of them are tuned by eye in the asset manager against the "from above" row. The real-habit rows are the acceptance test: someone who knows trees should name each species from its silhouette.

| Species | Lobes | Foliage tones (edge, base, mid, highlight) | Trunk |
|---|---|---|---|
| Oak | rounded, notched | `#2f4a26 #3e5e2c #4f7536 #6f9a4a` | bark brown, thickest |
| Maple | pointed | warm green, between oak and aspen in the style guide | bark brown |
| Birch | fine small tufts | `#38491f #4f6a2a #6c8a36 #8fab4f` | white with dark flecks, darkening at the foot from about age 0.7 |

- All three are params to the one generator: the growth numbers above, palette, lobe style, cluster size, and the size curve in R9.
- Birch is a new def, `Flora_TreeBirch`, placed in TemperateDeciduousForest, harvestable for Wood with an Axe at about 35% of oak's yield at the reference age.
- Density and spawn chances are set by R9.

### R7 World fit

- Bark uses the style guide's browns (`#45301d #5a3f26 #73512f`). The orange-red trunk goes.
- Limbs are not drawn over the canopy. One shows only through a ragged-form gap.
- Contact shadow: a small lobed patch at the trunk foot in `#1d2616` at 0.25 to 0.35 alpha, at most 1.5 trunk widths across. The offset drop shadow is deleted.
- Root flare at the trunk base, and two to four grass tufts drawn over the trunk foot in the greens of `ground_grass.lua`.
- Leaf-scale flecks: small lobed chips in the mid and highlight tones along cluster edges and on caps, inside the R5 triangle budget.
- The style guide's "No ground shadow" rule is rewritten as "contact shadow only", and its `deciduous.lua` references updated.

### R8 Yield and felling by size

- Yield and work follow the size of the tree the generator drew, not a multiplier per class. For each growth step the generator reports a wood factor and a work factor, through a new call alongside `asset:setCollisionRect`, captured per step the same way the rect is (R4) and checked the same way for independence from the individual. Starting choice: wood is trunk cross-section times stem height; work is trunk cross-section, since felling is one cut through the trunk.
- The def's `<harvestable>` amount and `durability` are the yield and work at the reference age. A tree's yield is that amount times its step's wood factor over the reference step's (rounded, minimum 1); its work is `durability` times the work factor ratio.
- Both harvest sites (`HarvestActions.cpp`, `NeedActions.cpp`) go through one shared helper, which computes the tree's traits from def, position, and the game clock at harvest start.
- Colonist memory keeps storing defName and position; age is computed when needed.
- Task choice is unchanged: colonists still pick the nearest reachable tree. Preferring bigger trees is out of scope.
- The selection panel for a tree shows its age in years and "Yields: ~N wood" for that tree. This replaces the "Growth: Mature" line in the `main-game-ui-design.md` mock.
- With the reference yield raised about 1.8x (R9) to about 45 wood, an oak at age 0.75 yields about 85 wood, three piles at Wood's stack size of 40, and one at age 1 about 165. Large old trees are meant to be a prize; check hauling copes in the scenario. If old trees outweigh the economy, the wood factor takes a gentler exponent on cross-section. Starting values.

### R9 Scale and density

Crown width follows a saturating curve of age: fast when young, slow when old. Each species' curve is a monotone interpolation through these anchors, drawn by the generator at this size (D16). The middle three are the sizes the first version of this spec gave its three classes, placed at the 10th, 50th, and 90th percentile ages. All are starting values.

| Age fraction | 0 | 0.10 | 0.35 (reference) | 0.75 | 1.0 | Trunk width at 0.35 |
|---|---|---|---|---|---|---|
| Oak | 0.4 | 1.6 | 3.6 | 4.9 | 5.0 | 0.35 |
| Maple | 0.3 | 1.2 | 2.7 | 3.6 | 3.8 | 0.28 |
| Birch | 0.2 | 0.7 | 1.6 | 1.8 | 1.8 | 0.16 |
| Pine | | | 2.2 | | | 0.25 |
| Palm | | | 3.0 | | | 0.22 |

- Trunk width keeps rising past the reference age: about 1.3x at 0.75 and 1.6x at 1 (oak 1.8x, "very thick"). Stem height rises about 10% from the reference age to 0.75 and 15% to 1. Starting values; R8's yield follows both.
- At the default zoom (24 px per metre) an oak crown at the reference age is 86 px, at 0.75 118 px, a colonist 24 px tall.
- Birch is narrow but tall: its trunk is drawn longer than maple's, so on screen it stands about as high.
- Pine and palm get the new sizes by changing their def params and spacing. Their generators are not touched, and they do not age.
- TemperateDeciduousForest targets 35% canopy cover, within 5 points, at about 55 trees per 1,000 m2, measured in the grove field's normal band with R11's view. Starting spacing: oak 4.5 m, maple 3.5 m, birch 2.2 m, then spawn chances tuned to the target.
- The grove field stays. Glades thin as they do today. Thickets may reach 55% cover, above D10's ceiling, on the grounds that they are rare, read as young growth (R1), and crowns go see-through (R10). Lower the thicket tightening if 55% is exceeded.
- Spacing between species is new: a `spaced` def also rejects a spot within 1.5 m of any member of its group (`trees`), so trunks of different species never sit on top of each other. This is a rejection only, with no extra random draws. Making `<avoids>` and `<affinity>` strengths work as documented would change every biome's placement and is filed separately.
- Spacing across chunk seams is an existing open bug and stays out of scope.
- Other forest biomes are tuned to the same target, with the 40% ceiling of D10. Grassland keeps its count of lone trees.
- Base yield and durability per tree rise so wood per 1,000 m2 of forest, and wood per unit of chopping work, stay within 10% of today's at founding. With about half the trees that is roughly 1.8x per tree. Today's is 800 wood per 1,000 m2 at the landing (R11's view, the 100 m square above). That baseline, like every measured number in this spec, was taken before trees had age or growth.
- Collision stays the trunk foot, so colonists walk under crowns.
- Cover and wood per area rise slowly as a colony's forest ages (R12). The targets above are at founding.

Left for later: open-grown form (a lone tree's crown is about twice as wide as the same tree in a wood), and splitting trunk from crown in the depth sort so a colonist standing in front of a trunk does not draw over a crown that should be above them.

### R10 See-through crowns

- A see-through tree is the real tree at reduced opacity, flattened first: it is drawn once, opaque, into an offscreen `RenderTarget`, and that image is composited at its sorted depth with `drawImage` at about 0.3 alpha. Flattening is the point. Lowering alpha on the mesh itself would stack its five or so overlapping layers back to nearly opaque, which is what `GhostRenderer` does today.
- Nothing is authored or built per asset: no second mesh, no named parts, no generator work. It works for pine, palm, and any other tall def as they are, and the same pass can later serve roofs and walls.
- Which tree is hiding what uses the existing silhouette (`AssetRegistry::getSilhouette`, cached per template under R3): a tree is see-through when it draws in front of a trigger (its depth key is greater) and the trigger's body point falls inside the tree's silhouette, the same point-in-ring test selection uses, so "behind this tree" and "clicked this tree" agree.
- Triggers: colonists and other moving actors, and the selected entity. The placement preview already draws above every tree, so it is never hidden; it is a trigger too, so the player can see the ground and what lies on it under a crown while placing.
- The offscreen pass runs before the entity pass, for the trees flagged this frame, each into its own cell of one shared target sized for a handful of trees at the current zoom. First check: that the target's edges match the backbuffer's antialiasing, so a tree does not visibly change as it starts to fade.
- Considered and not taken: a ghost mesh built from the silhouette with the trunk copied over it (needs named parts plumbed through four layers of the procedural pipeline), and the silhouette alone filled by `drawPolygon` at the sorted depth (uses only existing pieces, but shows a flat shape and not the tree). The second is the alternative if the offscreen pass fails the perf gate.
- The test runs where the depth-sorted items are gathered, in `world-view`: per trigger, the tall trees within the largest crown reach. Dropped items are not triggers (a stockpile in the woods would ghost its trees for good); revisit after playing with it.
- Transition: about 150 ms each way, the composite's alpha easing between 1 and 0.3. At 1 it is the same picture as the instanced draw, so there is no pop. Tuned by eye.
- See-through trees are a handful at a time, bounded by the number of visible actors. Each costs one offscreen draw and one image quad, and breaks the instanced run at its depth. If that fails the gate with many actors, keep each template's image across frames at the current zoom so a steady view does no offscreen work.
- Clicks: colonists already win over world entities in `SelectionSystem`, and repeated clicks cycle what is under the cursor. A ghosted tree stays selectable.
- Not part of this: a hold-key "peek" that ghosts everything near the cursor. Add it only if the triggers above turn out not to be enough.

### R11 Measuring the forest

No tool reported trees per area, canopy cover, or wood per area, and R9 and the yield rules are all stated in those terms.

- A view, `/api/dev/flora?rect=x0,y0,x1,y1` (defaulting to the camera's view): per def, the count and count per 1,000 m2, and an age histogram, the count in each tenth of the age fraction (0 to 0.1, 0.1 to 0.2, and so on); canopy cover as the rasterised union of the trees' silhouettes over the rect; expected wood per 1,000 m2 from each tree's own yield (R8). It is a dev route rather than a state view because salal's state providers take no query parameters. Built with counts per age class under each def; the histogram replaces them.
- With `age=<min>-<max>&near=x,y` it lists the nearest trees whose age fraction is in that range, with each one's age in years and fraction, so a test scenario can find a young and an old tree without spawning one. Today it takes `age=<name>`.
- The baseline on main at the quickstart landing is in "Where it stands today". It was taken before age or growth existed.
- `asset-cli inspect` gains triangle and vertex counts per def, growth step, and individual, for R5's budget.
- `emitSorted` counts its own draw calls and triangles, so the perf captures can separate trees from everything else.

### R12 Aging during play

Plants age with game time (D19, D20). A plant's age advances at the clock's rate, so change is slow, and the size curve (R9) puts most of it in the young. Most of a forest does not change within a colony, and in a very long one young trees grow a bit. That follows from real time, and the arithmetic below is the intended behaviour.

What a colony sees, from `TimeSystem`'s defaults (a year is 60 game days, 24 real hours at 1x, 8 at 3x): a colony played for about 40 real hours, mostly at 3x, lives about 5 game years. Twenty hours at 1x is under one year; 60 hours at 10x is 25. In 5 years:

- An oak lives 2.5% of its life, a maple 3.3%, a birch 6.3%.
- A median oak (age 0.35, crown 3.6 m) widens about 2%: almost never a step.
- An oak at age 0.10 (20 years, 1.6 m) widens about 12%, about half a step. An oak seedling at 0.02 (4 years, about 0.6 m) widens by half and crosses about two steps.
- A birch at age 0.10 (8 years, 0.7 m) widens about a third and crosses one or two steps. A median birch does not visibly change.

So the bulk of a forest looks the same from founding to the end of a colony, and only the youngest trees step at all. That is expected, and nothing here speeds it up. A plant with a short lifespan gets the same arithmetic at a different scale: a bush or an annual is a large fraction of its life older within one colony, so growth shows there, and that is where growth is meant to be visible. Giving those defs age blocks and age-driven generators is future work and no task in this document. The berry bush already has a regrowth mechanic (`regrowthTime` and a cooldown after a non-destructive harvest, plus optional `totalResourceMin` and `totalResourceMax` resource pools on other bushes); how age would meet it is for that work to settle, and nothing here changes it.

- Tick: once per game season (starting value), every loaded plant with an age block computes its growth step from its age. Every plant whose step changed takes the new one. With a 15-day season that is every 2 real hours at 3x, every 36 minutes at 10x. The tick is slow and coarse on purpose; with trees as the only users it mostly finds nothing to do. Crossings are spread out across ticks on their own, because every plant has its own base age.
- Room for later inputs: the same slow update is where seasons (a plant looks different in each season) and fruit (a plant's fruiting state) would land. Each is its own future work and is not designed here. The design leaves room by shape only: they would be further parts of the template key (R3) beside growth step and individual, and further reasons for the tick to refresh a plant, beside crossing a step. Neither is built, and nothing in this document depends on them.
- A chunk generated between ticks places its plants at their current age, so one loaded later is never behind.
- What a step refreshes: the placed entity's traits and `templateId`; its entry in the sorted tall path (the spatial index copy and its depth attributes, `anchorY`), or its chunk's baked mesh if the def routes short; the nav input for its chunk if the collision rect changed, which triggers that chunk's nav rebuild the way felling one does; the selection silhouette and panel if it is selected; and the see-through cell if it is ghosted. Colonists already holding a work target on it re-resolve the target point, since the trunk foot moved.
- Cost: measure the tick at a loaded area of a few thousand trees, both the tick itself and the frame where many trees step at once (the first tick after a long skip). Nav rebuilds are the likely cost; if they dominate, batch a chunk's steps into one rebuild per tick.
- `/api/dev/time?skip=` already skips game time, in minutes or hours (a year is 1440h). It gains a year unit, and a skip runs the aging tick once at its end, so the age strip can be checked in game and a long colony simulated.

## Out of scope

- A plant dying at the end of its lifespan, and regrowth after death. A plant at age fraction 1 stays old.
- Regrowth or replanting after felling. Not planned: the forest is effectively unbounded, so wood is not scarce (D23).
- Seasons in how plants look. They would land in the slow plant tick and the template key (R12); not designed here.
- Fruit, a plant's fruiting state, likewise a later input to that tick and key (R12).
- Age blocks and age-driven generators for short-lived plants (bushes, grasses, annuals), where growth shows within a colony (R12), and how age meets a bush's regrowth and resource pool.
- Rewriting the pine and palm generators on the skeleton model (pine: one whorl of limbs per year, conical then flat-topped with age; palm: a single bud and a rosette), and giving them an age block.
- Crowns shaped by their neighbours, and open-grown versus forest-grown form.
- Making `<avoids>` and `<affinity>` strengths take effect, and spacing across chunk seams. Both are placement bugs that predate this and change every biome.
- Named parts on procedural meshes, and with them wind sway that knows trunk from crown. The Living Environment plan bends by mesh height, which needs neither.
- Wind sway and engine blob shadows (Living Environment epic).
- Felled trees returning after a chunk unloads and reloads. Placement keeps no removal record today; filed separately. Aging does not change this: a felled tree comes back at its current age.
- A unique mesh per tree (R3).

## Performance plan

Trees draw in the depth-sorted stream, and a run breaks whenever the mesh changes. Two species interleaved on main; with R3 built, 16 templates per species did; after this, up to 128 per species can.

- The timed steps below were this epic's plan, written before 2026-11-10. Timed runs now follow the [testing README](../../testing/README.md#performance): scoped, after a machine-load check, a worse result unconfirmed unless the machine was idle, and no baselines with any activity on it. The noise-floor baseline below is the nightly batch's.
- Baseline main twice with `scripts/perf.mjs capture` for the noise floor, plus a forest view at the quickstart landing at zoom 1 and at 0.25, the furthest zoom (trees draw at every zoom). Record frame time, draw calls, and triangles, with trees counted apart (R11).
- A/B R3 alone (templates wired, old generator) against that baseline. This isolates the draw-call cost from the new art's triangle cost.
- Gate: no regression beyond noise in any cell. If R3 fails the gate, pack a def's templates into one vertex and index buffer and draw each by index range, so the run key goes back to the def. salal's backend already takes an index range for instanced draws; `drawInstanced` needs only to expose it. A def's templates must then fit 65,535 vertices together, which 128 templates per def will not, so it would need a 32-bit index or a base-vertex draw. Taken only on a measured failure.
- Measured with R3 built for age classes (RelWithDebInfo, 3072x1728, landing forest at world 5,4, GPU held, read from `/api/metrics` and `/api/state?what=uprights`): GPU time tracked upright draws, about 5 microseconds each on main and on the branch alike. Forcing every tree to its def's mature variant 0 put zoom 1.5 back at 1,003 draws and 5.3 to 5.9 ms (main: 911 and 4.9 to 5.4), so the cost was the runs breaking between variants; the per-def tall routing added the other 10%. Restoring 50,000-instance buffers changed nothing on the GPU. The fix kept the variants and changed the run rule instead (`world-depth-sorting.md`, "Submit in order"): an entity joins its template's latest open run unless something in a later run may overlap it, so only overlapping pairs keep the painter's order. No salal change was needed.

| Zoom | GPU ms main | before fix | after fix | Upright draws main | before | after |
|---|---|---|---|---|---|---|
| 8 | 1.3 to 1.9 | | 1.3 to 1.8 | 40 | | 44 to 49 |
| 3 | 2.1 to 2.6 | | 1.8 to 2.5 | 229 | | 162 to 165 |
| 1.5 | 4.9 to 5.4 | 8.6 to 9.5 | 2.7 to 3.2 | 911 | 1,709 | 471 to 473 |
| 0.75 | 18.0 to 18.7 | 32.8 to 34.0 | 8.3 | 3,310 | 6,398 | 1,457 |
| 0.25 | 117 | | 45 | 23,700 | | 9,220 |

- Those numbers are for 16 templates per species. With 128, more runs may break where trees overlap; repeat the zoom 1.5 and 0.75 cells once the step-keyed templates are wired.
- Prewarm: report load time at 384 templates (R3).
- Aging tick: measure as R12 says, plus a frame-time trace across a tick at 10x.
- If far zoom fails on triangles, gate the leaf flecks and caps with `path:setLod` and build the tree template per zoom band, which the registry already supports for ground tiles.
- R9 cuts tree count about in half and raises covered pixels from 13.3% (measured, R11) to 35% of a forest view, each crown several layers deep. A/B the rescale on its own (old generator, new sizes and spacing): draw calls should fall, fill cost may rise. If fill fails the gate, the first lever is fewer full-size layers per cluster.
- R10: measure a forest view with 20 colonists standing under crowns against the same view with none.
- One full capture at the end with the new generator and all three species.
- `InstancedEntityRenderer::emitSorted` keys on the integer `templateId` already; keep it that way when the key moves to growth step.

## Testing

- Unit, bounded: age and traits are deterministic for (def, position, seed, game years); base age hits the def's three percentile points within tolerance over 10,000 positions outside thickets; age advances one year per 60 game days and clamps at fraction 1; the individual does not change as age advances; placement of non-tree entities is byte-identical before and after for a fixed seed (a hash golden, checked only with MSVC's standard library, whose distributions the hashes were captured from); per-step collision rect and harvest factors are independent of the individual; harvest amount and work follow the step's factors.
- Asset-manager: every growth step and individual of oak, maple, and birch renders with zero validation errors. Kevin approves the age strips before rollout.
- `asset-cli render` produces the acceptance pictures headless: per species, an age strip of one individual at about ten ages left to right, with a few individuals as rows, in colour and in greyscale silhouette, all at one scale.
- Acceptance for growth: along a strip each picture reads as the same individual a little grown, with no jump in form between neighbours, and young trees change more between pictures than old ones.
- Acceptance for variety: any two individuals of one species at the same age are tellable apart by silhouette alone at game zoom, in greyscale.
- Acceptance for species: oak, maple, and birch are tellable apart in greyscale silhouette at the reference age, and each matches its "from above" row in R6, including the way it grows old.
- Acceptance for scale: on the quickstart landing, canopy cover and trees per 1,000 m2 hit R9 at founding, and a colonist can walk the forest without leaving the player's sight.
- Unit, bounded: the ghost trigger test (in front and overlapping ghosts; behind or clear does not).
- Scenario: a new step in `01-craft-axe-and-box.md`, walk a colonist behind an old oak; the crown goes see-through while they are behind it and returns after, and clicking them through it selects them.
- Scenarios: extend `01-craft-axe-and-box.md` (fell a young and an old tree found with R11's age range, wood counts differ as specified, selection panel shows age in years and yield), `06-asset-manager-browse.md` (age strips), and add a step that skips the clock forward several years and checks a young tree near the landing has grown, its collision and selection outline with it. Run 01 to 04 and 06 as the regression pass.
- In-game forest screenshot from the same camera as the 2026-11-10 report, before and after.

## Docs to update when built

- `docs/technical/asset-system/asset-definitions.md` and `lua-scripting-api.md`: `<age>` and `<distribution>` in place of `<ageClasses>`, the `age` global in place of `ageClass`, the wood and work factor call.
- `docs/technical/vector-graphics/collision-shapes.md`: collision per growth step.
- `docs/design/main-game-ui-design.md`: the tree panel mock shows "Age: N years" in place of "Growth: Mature".
- `.claude/skills/flora-asset/references/`: style-and-view (shadow rule, generator description, the skeleton-first recipe driven by `age`), xml-schema, lua-api.
- `docs/technical/rendering/world-depth-sorting.md`: its non-goal "canopy fade, cut not deferred" and "no canopy-fade phase" are superseded by R10; point it here.
- `docs/technical/entity-placement-system.md`: the cover target, group spacing, and the grove field, which it never mentions; it also still lists `spaced` as planned.
- `docs/technical/asset-system/lua-scripting-api.md`: it carries an unbuilt Weber and Penn branch recursion that `require`s a `branch_utils` that does not exist. Replace it with the prelude and R5's model. `README.md` in the same folder describes the unbuilt variant cache.
- `docs/design/procedural-variation.md`: "80% to 120% height variation" and "trees rotated randomly" both disagree with this spec and the code.
- The flora skill has more to fix than the shadow rule: `SKILL.md` calls `deciduous.lua` canonical and defines life stages as one construction scaled by `growthStage` (R5 grows them from `age`); the cheat sheet's maple is a burgundy Japanese maple and its "trunk is only a small stub" rule conflicts with a tall birch; `xml-schema.md` ships a `Flora_TreeBirch` example that contradicts R6 and uses `duration` where the parser reads `durability`; `lua-api.md` says `variantIndex` runs 0 to N-1 and omits `noise2`, `setLod`, and `setHeight`.

## Sources

Concepts only; no code or parameter tables are copied.

- Honda, 1971, "Description of the form of trees by the parameters of the tree-like body": length ratios and branch angles are enough to produce recognisable species.
- Borchert and Honda, 1984, and Palubicki et al., 2009, "Self-organizing tree models for image synthesis": the vigour split at a fork that separates single-stem from spreading trees.
- Stava et al., 2014, "Inverse procedural modelling of trees": per-species parameter tables for oak, maple, and pine.
- Halle, Oldeman, and Tomlinson's architectural models (oak, maple, birch, and pine are all Rauh's model; palms are Corner's).
- Prior art in games: RimWorld picks from a folder of textures, flips, and scales by growth; Factorio ships about ten variations per tree family and tints leaves separately from the trunk.
