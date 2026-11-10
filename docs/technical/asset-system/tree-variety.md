# Tree variety and world fit

Status: spec, not built. Created 2026-11-10.

Trees in a forest read as one tree at a few sizes. This spec makes each tree a distinct individual (shape, age, species), grows each species the way the real one branches, roughly doubles tree size while thinning the forest, lets the player see through a crown that hides something, makes age matter to the wood economy, and redraws trees so they sit in the textured ground instead of on top of it.

The aim is trees and forests more realistic than RimWorld's, held to what stays readable and fast. Form comes from botany (branching habit, crown shape, how a species ages, relative sizes). Absolute size and density are compressed to the game.

## Where it stands today

- A procedural tree def is generated once. `AssetRegistry::getTemplate` calls `generateAsset(defName, 42, ...)` and caches the mesh by defName. `variantCount` (15 on oak, 20 on maple) is read only by the groundcover and ground-tile paths.
- The tall-tree draw (`InstancedEntityRenderer`) uploads one GPU mesh per defName and coalesces consecutive same-def entities into one `drawInstanced` run.
- Per-tree difference is placement noise only: scale 0.8 to 1.2 and a grey brightness of 0.82 to 0.98 (`PlacementExecutor.cpp`).
- Oak and maple both run `@shared/deciduous.lua` with one palette. They differ in size params and in `branchCount`, which shifts the random stream, so the forest has two shapes.
- `deciduous.lua` predates the converged flora style: one wobbly blob, one to three concentric layers, a highlight, orange-red limbs drawn over the canopy, an offset grey drop shadow.
- Its variation is independent uniform jitter on every parameter, which sums toward an average tree.
- Placed entities are not saved. Each chunk is regenerated from the world seed, with one RNG shared by every def in spawn order, so an extra RNG draw per tree would reshuffle every later placement in the chunk.
- Collision is one rect per def, captured at load from variant 0, and scaled by `entity.scale` in `NavInputBuilder` and `StaticRectCollisionSystem`.
- Yield and work come straight from the def's `<harvestable>` (`HarvestActions.cpp`, `NeedActions.cpp`); the roll is a fresh random. The sim, colonist memory, and selection identify a tree by defName and position.
- Scale: a colonist is 1.0 m tall, an oak crown nominally 1.6 m across, a maple 1.2 m. A real open-grown oak is 15 to 24 m across, so today's trees are about 12x compressed. The quickstart forest is about 100 trees per 1,000 m2 at about 15% canopy cover; both figures are read off one screenshot, and no tool measures them (R11).
- Nothing fades. `rendering/world-depth-sorting.md` cut canopy fade outright ("a pawn may be fully hidden behind a tree"), which was the right call for 1.6 m crowns.

### Existing code this builds on

From a survey of the render, asset, placement, and tool code on 2026-11-10. Each line is something to reuse or something the plan has to change.

Variants and generation:
- The Lua global `variantIndex` already exists and is always 0 (`generateAsset` hard-sets it). Callers put the variant in the seed instead: groundcover uses seeds 0 to N-1, ground tiles use `variantSeed` (a hash of the def's seed group and the variant, `GroundTileBaker.cpp`), the tree template uses 42, collision capture uses 0. Lua seeds with `seed + variantIndex`, so consecutive integers in both would collide.
- Templates are built lazily on first `getTemplate`, from a chunk worker or the render thread, under one registry-wide mutex held through generation and tessellation. Nothing is generated at load except the collision rect.
- Ground tiles already have what tree variants need: parallel generation per variant and a disk cache keyed on def, params, script bytes, and the Lua API version (`GroundTileCache.cpp`). The planned "Variant Cache System" epic in Specboard (WOR-261) is this, never built.
- `GroundcoverRenderer` already picks a variant from world position at draw time, with one GPU handle per variant. Its hash has no world seed and no def salt, and its per-chunk bucketing cannot interleave in the depth-sorted stream, so trees share the handle-per-variant idea and nothing else.
- A template is one mesh of at most 65,535 vertices. Over that, `getTemplate` returns null and the tree does not draw.
- Each GPU mesh handle reserves room for 50,000 instances (1.6 MB). Fine for 4 tree defs, 128 MB for 80 variant handles.

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
- `emitSorted` counts entities but not its draw calls or triangles.

Sim and tools:
- Harvest and the selection panel know a tree only as defName and position; neither ever holds a `PlacedEntity`. Trees spawned by `/api/dev/spawn` are ECS entities, not placed entities.
- `main-game-ui-design.md` already mocks the tree panel as "Growth: Mature" and "Yields: ~40 wood".
- Wood stacks to 40, so one large tree fells into several piles.
- `asset-cli` is headless: `render <def> --seed`, and `validate --render-smoke`, which is the only validation that runs a generator. `AssetValidator` is static XML checks.
- The asset manager shows procedural variants at seeds 1 to 6, each fitted to its own thumbnail, so it shows neither the game's variants nor relative size.

## Decisions

| # | Decision |
|---|----------|
| D1 | Per-tree traits (age class, mesh variant) are a pure function of the def and the tree's world position. No placement RNG draws, no save data. |
| D2 | Age class is spatially coherent: a low-frequency field picks it, so old stands and young regrowth form patches. |
| D3 | The generator draws each age class at its true size. Placement scale narrows to 0.93 to 1.07. |
| D4 | Variation inside an age class is discrete, correlated traits (crown form, condition), never per-parameter jitter. |
| D5 | Species differ by growth habit first (R6), then palette, lobe style, and cluster density. This epic covers oak, maple, and a new birch. Pine and palm keep their generators and are rescaled only; growing them from a skeleton is a later epic. |
| D6 | Wood yield and felling work scale with age class. |
| D7 | Ground contact is a contact shadow at the trunk foot, a root flare, and grass tufts over the base. No canopy or drop shadow. Engine blob shadows stay with the Living Environment epic. |
| D8 | Age is fixed for the life of the tree. No growth over time in this epic. |
| D9 | A species uses one render path for all its age classes, chosen by its mature height. |
| D10 | Scale and density are "open woodland": a mature oak crown is 3.6 m (about 5x compressed from real), forest canopy cover targets 35%, and tree count roughly halves. No biome goes above 40% cover without a new decision, except thickets, which may reach 55% (R9). Decided 2026-11-10. |
| D13 | Generators share helpers through a prelude the engine loads before each script (R5). In scope for this epic. Decided 2026-11-10. |
| D11 | The generator grows a branch skeleton from a few per-species growth numbers and hangs foliage on the branch tips. Age is how long it grew. Concepts from Honda and Borchert-Honda; our own code, no library. |
| D12 | A tree that hides an actor turns see-through: it is flattened offscreen and drawn at about 30% opacity. This reverses the canopy-fade cut in `world-depth-sorting.md`, because D10's crowns hide a colonist for seconds, and several at once. Render-only; the sim never reads it. |

## Requirements

### R1 Per-tree traits

- `assets::TreeTraits { uint8_t ageClass; uint16_t variant; }`, computed by one function in `libs/assets` from (def, world position, world seed). It links no renderer, so `sim` can call it.
- Harvest, selection, and dev-spawned trees have only a def and a position, so they call the function. `PlacedEntity` and the ECS `Appearance` keep a copy for the renderer. Position is quantised to 0.1 m first, the grid colonist memory already uses, so every caller agrees.
- The grove field's noise moves out of `PlacementExecutor.cpp`'s anonymous namespace into a shared header, and the age field is the same noise at about 30 m with its own salt, plus a per-tree hash term, mapped through the def's age weights. Where the grove field is in its thicket band the age leans young, so a thicket is a dense stand of small trees and not veterans packed trunk to trunk.
- Variant is a hash of position, world seed, and the def, modulo that age class's variant count.
- Placement positions, rotation, scale, and tint of every existing entity are unchanged for a given seed, apart from the scale range in D3.
- Defs without `<ageClasses>` have one age class and `variantCount` variants; every non-tree asset behaves as today.

### R2 Def schema

```xml
<ageClasses>
  <ageClass name="sapling" weight="0.25" variants="4" yieldScale="0.25" workScale="0.35"/>
  <ageClass name="mature"  weight="0.55" variants="8" yieldScale="1.0"  workScale="1.0"/>
  <ageClass name="veteran" weight="0.20" variants="4" yieldScale="1.8"  workScale="1.6"/>
</ageClasses>
```

- Weights sum to 1 (validator error otherwise). With the numbers above the mean yield is 0.97 of today's, so the wood economy holds.
- `<ageClasses>` replaces `<variantCount>` on a def that has it; the validator rejects both together.
- The generator receives `ageClass` (name, new) and `variantIndex` (existing, now set) as Lua globals. The generation seed for (def, age class, variant) comes from ground tiles' `variantSeed` hash, shared, and Lua seeds from that alone; the `seed + variantIndex` sum goes.
- A def may set its placement scale range (trees: 0.93 to 1.07, D3). It replaces the shared range for that def and draws the same number of random values, so nothing else moves.
- `growthStage` in `conifer.lua` and `palm.lua` stays as it is. When those generators get age classes in the later epic, `ageClass` replaces it.

### R3 Variant meshes on every tree path

- `getTemplate(defName, traits)`; the cache key is (def, age class, variant). The fixed seed 42 is deleted; a def with one variant generates with seed 0.
- Callers that take the traits: `TemplateMeshCache`, `InstancedEntityRenderer` (mesh handles and run key), `bakeChunkEntities`, the depth attributes at placement, `getSilhouette` (hit test and selection outline), and `DynamicEntityRenderSystem`. `GhostRenderer` previews placement defs only and passes default traits.
- Tree variants are generated before the first chunk needs them. That is new: add a prewarm for defs with `<ageClasses>`, run in parallel per variant the way the ground tile baker does, with generation and tessellation outside the registry mutex. Report the load-time delta. If it is more than the player would notice, reuse the ground tile disk cache's keying to store tessellated variants; that would also deliver the planned Variant Cache System.
- A variant over 65,535 vertices is a load error naming the def, age class, and variant, never a missing tree.
- Instance capacity per mesh handle is sized to what a variant can plausibly show (start at 4,096 and grow), not 50,000.
- Tall or short routing (`kShortFloraMaxHeight`) is decided per def from its mature variant 0 (D9), at both sites that decide it today.
- The depth key for a def with a collision rect is the rect's lower edge, the trunk foot, which `world-depth-sorting.md` already allows. Otherwise R7's shadow, flare, and tufts would move every tree forward in the sort.
- The silhouette skips triangles under 0.5 alpha, so shadows are not part of the outline or the hit area.
- The gather margin and the selection broad phase use the registry's largest tall-entity extent, not 2 m and 8 m. Without this, R9's trees pop in at the screen edges.
- Asset-manager detail view shows one row per age class, using the same (age class, variant) keys the game uses and one scale across the rows, so a sapling looks small beside a veteran.

### R4 Collision per age class

- The generator's `asset:setCollisionRect` is captured per age class at load, where the one eager capture runs today. It must not depend on the variant; `asset-cli validate --render-smoke` generates two variants of each age class and fails the def if their rects differ (the XML validator never runs a generator).
- `NavInputBuilder` and `StaticRectCollisionSystem` read the rect for the entity's age class.

### R5 Generator

`deciduous.lua` is rewritten on the recipe in `.claude/skills/flora-asset/references/style-and-view.md`; the old blob construction is deleted.

It works skeleton first, so the crown's shape is a result of how the tree grew.

**Shared helpers first.** The engine gains a prelude: every `.lua` under `assets/shared/scripts/lib/` runs in each generator's state before the generator, and the cache keys that hash script bytes include it. The helpers five scripts copy today (clamp, random distributions, hex and tone mixing) move there once, with the pieces this generator needs: the tapered strip for limbs, random sub-streams per stage, the grass tuft, and `conifer.lua`'s textured bark. Each growth stage draws from its own sub-stream, so adding a draw in one stage cannot reshuffle the rest.

The rim is expanded dark copies of the cluster shapes, as `conifer.lua` does it, since Lua has no strokes. The trunk foot is at the origin, as in `conifer.lua` and `palm.lua`; today's oak and maple shift on screen by the old offset, and their collision finally sits under the drawn trunk.

**Skeleton.** Nodes carry a ground offset and a height; the 3/4 view comes from adding height to the up-screen offset, as the style guide already does. Growth runs in steps from a single stem. At each step every live tip extends or forks by the species' numbers (R6):

- Apical control: the share of vigour the leading shoot keeps at a fork. Above 0.5 the leader wins and the tree keeps one stem (birch, pine). Below 0.5 the laterals take over and the stem dissolves into boughs (oak, maple). This one number is most of the difference between tree shapes.
- Bud arrangement: alternate puts one lateral per node, each turned about 137 degrees around the stem from the last, so limbs spiral and never mirror. Opposite puts two per node, each pair at right angles to the pair before, so forks come in matched pairs.
- Branch angle off the parent, and the child-to-parent length ratio.
- Limb width follows the pipe rule (a parent's cross-section is the sum of its children's), then is drawn about twice as thick as real proportions so limbs read at game zoom.
- Shedding: the chance a shaded lower or inner branch is dropped. High shedding gives a gappy crown with trunk showing, low gives a solid one.
- Droop: how far outer tips sag below the branch that carries them.

A tree has at most 40 tips. The recursion is a few hundred operations per variant.

**Foliage.** Leaf clusters hang on branch tips, sized by the tip's vigour and the species' cluster size. Their union is the silhouette. Each cluster has a dark base, a mid tone, and a cap offset up-left; one dark rim sits behind the union and one darkest-tone backing shape fills holes. Tip height sets draw order and tone: low clusters first and darker, the highest get the caps. Limbs show only where shedding left a gap.

**Age is growth time**, so age classes are different trees, not scales of one. Spreads are in R9.

- sapling: 2 to 3 steps, and apical control forced high (every young tree has one leader). A thin stem, 3 to 5 clusters, no caps.
- mature: 4 to 5 steps at the species' numbers. 8 to 11 clusters, full layering.
- veteran: 6 to 7 steps with apical control lowered and shedding and droop raised, then the species' own way of growing old (R6). 12 to 15 clusters, heavier limbs.

**Crown forms are growth events.** Each variant draws one: none (the species' textbook shape), a main bough lost early (lopsided), the leader forked at the first step (twin-crowned, two stems), heavy shedding (ragged, a limb showing), or wind-pruned (tips shortened on one side). Lost-bough and ragged are weighted up for veterans.

- Rare condition, about 1 variant in 8: a thinning crown or a yellowing patch.
- Randomness is seeded per variant; palette jitter is small (+/-3% per channel) so species identity holds.
- The trunk foot sits at the origin and the trunk's collision rect depends on age class only (R4).
- Budget: a mature variant tessellates to at most 1.5x the triangles of today's oak template. Measure today's first and record it here.

Left out on purpose: light competition between neighbours and crowns leaning away from each other (real and visible, but it makes one tree's shape depend on others, which breaks D1), growth over time, and anything at leaf level.

### R6 Species

How each grows, and what that looks like from above:

| | Oak | Maple | Birch |
|---|---|---|---|
| Real habit | stem dissolves into a few massive boughs | forks into co-equal stems, foliage packed at the shoot tips | one leader to the top, fine hanging twigs |
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
| Birch | fine small tufts | `#38491f #4f6a2a #6c8a36 #8fab4f` | white with dark flecks, darkening at the foot on veterans |

- All three are params to the one generator: the growth numbers above, palette, lobe style, cluster size.
- Birch is a new def, `Flora_TreeBirch`, placed in TemperateDeciduousForest, harvestable for Wood with an Axe at about 35% of oak's yield.
- Density and spawn chances are set by R9.

### R7 World fit

- Bark uses the style guide's browns (`#45301d #5a3f26 #73512f`). The orange-red trunk goes.
- Limbs are not drawn over the canopy. One shows only through a ragged-form gap.
- Contact shadow: a small lobed patch at the trunk foot in `#1d2616` at 0.25 to 0.35 alpha, at most 1.5 trunk widths across. The offset drop shadow is deleted.
- Root flare at the trunk base, and two to four grass tufts drawn over the trunk foot in the greens of `ground_grass.lua`.
- Leaf-scale flecks: small lobed chips in the mid and highlight tones along cluster edges and on caps, inside the R5 triangle budget.
- The style guide's "No ground shadow" rule is rewritten as "contact shadow only", and its `deciduous.lua` references updated.

### R8 Yield and felling by age

- At harvest start the action reads the placed entity's age class and multiplies the def's rolled amount by `yieldScale` (rounded, minimum 1) and `durability` by `workScale`. Both harvest sites (`HarvestActions.cpp`, `NeedActions.cpp`) go through one shared helper.
- Colonist memory keeps storing defName and position; age is read from the placed entity when needed.
- Task choice is unchanged: colonists still pick the nearest reachable tree. Preferring bigger trees is out of scope.
- Selection panel for a tree follows the mock in `main-game-ui-design.md`: "Growth: Sapling" (or Mature, Veteran) and "Yields: ~N wood" for that tree.
- A veteran oak yields up to about 95 wood, which drops as three piles at Wood's stack size of 40. That is intended; check hauling copes in the scenario.

### R9 Scale and density

Crown width in metres, drawn by the generator at this size (D3):

| | Sapling | Mature | Veteran | Mature trunk width |
|---|---|---|---|---|
| Oak | 1.6 | 3.6 | 4.9 | 0.35 |
| Maple | 1.2 | 2.7 | 3.6 | 0.28 |
| Birch | 0.7 | 1.6 | 1.8 | 0.16 |
| Pine | | 2.2 | | 0.25 |
| Palm | | 3.0 | | 0.22 |

- At the default zoom (24 px per metre) a mature oak crown is 86 px, a veteran 118 px, a colonist 24 px tall.
- Birch is narrow but tall: its trunk is drawn longer than maple's, so on screen it stands about as high.
- Pine and palm get the new sizes by changing their def params and spacing. Their generators are not touched.
- TemperateDeciduousForest targets 35% canopy cover, within 5 points, at about 55 trees per 1,000 m2, measured in the grove field's normal band with R11's view. Starting spacing: oak 4.5 m, maple 3.5 m, birch 2.2 m, then spawn chances tuned to the target.
- The grove field stays. Glades thin as they do today. Thickets may reach 55% cover, above D10's ceiling, on the grounds that they are rare, read as young growth (R1), and crowns go see-through (R10). Lower the thicket tightening if 55% is exceeded.
- Spacing between species is new: a `spaced` def also rejects a spot within 1.5 m of any member of its group (`trees`), so trunks of different species never sit on top of each other. This is a rejection only, with no extra random draws. Making `<avoids>` and `<affinity>` strengths work as documented would change every biome's placement and is filed separately.
- Spacing across chunk seams is an existing open bug and stays out of scope.
- Other forest biomes are tuned to the same target, with the 40% ceiling of D10. Grassland keeps its count of lone trees.
- Base yield and durability per tree rise so wood per 1,000 m2 of forest, and wood per unit of chopping work, stay within 10% of today's. With about half the trees that is roughly 1.8x per tree. Measure today's first.
- Collision stays the trunk foot, so colonists walk under crowns.

Left for later: open-grown form (a lone tree's crown is about twice as wide as the same tree in a wood), and splitting trunk from crown in the depth sort so a colonist standing in front of a trunk does not draw over a crown that should be above them.

### R10 See-through crowns

- A see-through tree is the real tree at reduced opacity, flattened first: it is drawn once, opaque, into an offscreen `RenderTarget`, and that image is composited at its sorted depth with `drawImage` at about 0.3 alpha. Flattening is the point. Lowering alpha on the mesh itself would stack its five or so overlapping layers back to nearly opaque, which is what `GhostRenderer` does today.
- Nothing is authored or built per asset: no second mesh, no named parts, no generator work. It works for pine, palm, and any other tall def as they are, and the same pass can later serve roofs and walls.
- Which tree is hiding what uses the existing silhouette (`AssetRegistry::getSilhouette`, cached per variant under R3): a tree is see-through when it draws in front of a trigger (its depth key is greater) and the trigger's body point falls inside the tree's silhouette, the same point-in-ring test selection uses, so "behind this tree" and "clicked this tree" agree.
- Triggers: colonists and other moving actors, and the selected entity. The placement preview already draws above every tree, so it is never hidden; it is a trigger too, so the player can see the ground and what lies on it under a crown while placing.
- The offscreen pass runs before the entity pass, for the trees flagged this frame, each into its own cell of one shared target sized for a handful of trees at the current zoom. First check: that the target's edges match the backbuffer's antialiasing, so a tree does not visibly change as it starts to fade.
- Considered and not taken: a ghost mesh built from the silhouette with the trunk copied over it (needs named parts plumbed through four layers of the procedural pipeline), and the silhouette alone filled by `drawPolygon` at the sorted depth (uses only existing pieces, but shows a flat shape and not the tree). The second is the alternative if the offscreen pass fails the perf gate.
- The test runs where the depth-sorted items are gathered, in `world-view`: per trigger, the tall trees within the largest crown reach. Dropped items are not triggers (a stockpile in the woods would ghost its trees for good); revisit after playing with it.
- Transition: about 150 ms each way, the composite's alpha easing between 1 and 0.3. At 1 it is the same picture as the instanced draw, so there is no pop. Tuned by eye.
- See-through trees are a handful at a time, bounded by the number of visible actors. Each costs one offscreen draw and one image quad, and breaks the instanced run at its depth. If that fails the gate with many actors, keep each variant's image across frames at the current zoom so a steady view does no offscreen work.
- Clicks: colonists already win over world entities in `SelectionSystem`, and repeated clicks cycle what is under the cursor. A ghosted tree stays selectable.
- Not part of this: a hold-key "peek" that ghosts everything near the cursor. Add it only if the triggers above turn out not to be enough.

### R11 Measuring the forest

No tool reports trees per area, canopy cover, or wood per area, and R9 and the yield rules are all stated in those terms.

- A new state view, `/api/state?what=flora&rect=x0,y0,x1,y1` (defaulting to the camera's view): per def and per age class, the count and count per 1,000 m2; canopy cover as the rasterised union of the trees' silhouettes over the rect; expected wood per 1,000 m2 from the yields.
- With `age=<name>&near=x,y` it lists the nearest trees of that age class, so a test scenario can find a sapling and a veteran without spawning one.
- Take the baseline on main at the quickstart landing before any other task changes placement, and record the numbers in this spec in place of the screenshot estimates.
- `asset-cli inspect` gains triangle and vertex counts per def, age class, and variant, for R5's budget.
- `emitSorted` counts its own draw calls and triangles, so the perf captures can separate trees from everything else.

## Out of scope

- Growth over time, seasons, regrowth after felling.
- Rewriting the pine and palm generators on the skeleton model (pine: one whorl of limbs per year, conical then flat-topped with age; palm: a single bud and a rosette).
- Crowns shaped by their neighbours, and open-grown versus forest-grown form.
- Making `<avoids>` and `<affinity>` strengths take effect, and spacing across chunk seams. Both are placement bugs that predate this and change every biome.
- Named parts on procedural meshes, and with them wind sway that knows trunk from crown. The Living Environment plan bends by mesh height, which needs neither.
- Wind sway and engine blob shadows (Living Environment epic).
- Felled trees returning after a chunk unloads and reloads. Placement keeps no removal record today; filed separately.

## Performance plan

Trees draw in the depth-sorted stream, and a run breaks whenever the mesh changes. Today two species interleave; after this, up to 16 meshes per species do, so draw calls for visible trees rise toward one per tree.

- Baseline main twice with `scripts/perf-capture.ps1` for the noise floor, plus a forest view at the quickstart landing at zoom 1 and at 0.25, the furthest zoom (trees draw at every zoom). Record frame time, draw calls, and triangles, with trees counted apart (R11).
- A/B R3 alone (variants wired, old generator) against that baseline. This isolates the draw-call cost from the new art's triangle cost.
- Gate: no regression beyond noise in any cell. If R3 fails the gate, pack a def's variants into one vertex and index buffer and draw each by index range, so the run key goes back to the def. salal's backend already takes an index range for instanced draws; `drawInstanced` needs only to expose it. A def's variants must then fit 65,535 vertices together. Taken only on a measured failure.
- If far zoom fails on triangles, gate the leaf flecks and caps with `path:setLod` and build the tree template per zoom band, which the registry already supports for ground tiles.
- R9 cuts tree count about in half and raises covered pixels from about 15% to 35% of a forest view, each crown several layers deep. A/B the rescale on its own (old generator, new sizes and spacing): draw calls should fall, fill cost may rise. If fill fails the gate, the first lever is fewer full-size layers per cluster.
- R10: measure a forest view with 20 colonists standing under crowns against the same view with none.
- One full capture at the end with the new generator and all three species.
- Look for wins while in `InstancedEntityRenderer::emitSorted`: it does a string compare and a string-keyed map lookup per entity per frame; an integer mesh id on the placed entity removes both.

## Testing

- Unit, bounded: traits are deterministic for (def, position, seed) and hit the age weights within tolerance over 10,000 positions; placement of non-tree entities is byte-identical before and after for a fixed seed; per-age collision rect is variant-independent; harvest amount and work scale by age class.
- Asset-manager: every age class and variant of oak, maple, and birch renders with zero validation errors. Kevin approves the grids before rollout.
- `asset-cli render` produces the greyscale silhouette grids for the two acceptance checks below, headless.
- Acceptance for variety: any two variants of one species and age class are tellable apart by silhouette alone at game zoom, in greyscale.
- Acceptance for species: oak, maple, and birch are tellable apart in greyscale silhouette, and each matches its "from above" row in R6.
- Acceptance for scale: on the quickstart landing, canopy cover and trees per 1,000 m2 hit R9, and a colonist can walk the forest without leaving the player's sight.
- Unit, bounded: the ghost trigger test (in front and overlapping ghosts; behind or clear does not).
- Scenario: a new step in `01-craft-axe-and-box.md`, walk a colonist behind a veteran oak; the crown goes see-through while they are behind it and returns after, and clicking them through it selects them.
- Scenarios: extend `01-craft-axe-and-box.md` (fell a sapling and a veteran, wood counts differ as specified, selection panel shows age and yield) and `06-asset-manager-browse.md` (age-class rows). Run 01 to 04 and 06 as the regression pass.
- In-game forest screenshot from the same camera as the 2026-11-10 report, before and after.

## Docs to update when built

- `docs/technical/asset-system/asset-definitions.md` and `lua-scripting-api.md`: `<ageClasses>`, the `ageClass` global.
- `.claude/skills/flora-asset/references/`: style-and-view (shadow rule, generator description, the skeleton-first recipe), xml-schema, lua-api.
- `docs/technical/rendering/world-depth-sorting.md`: its non-goal "canopy fade, cut not deferred" and "no canopy-fade phase" are superseded by R10; point it here.
- `docs/technical/entity-placement-system.md`: the cover target, group spacing, and the grove field, which it never mentions; it also still lists `spaced` as planned.
- `docs/technical/asset-system/lua-scripting-api.md`: it carries an unbuilt Weber and Penn branch recursion that `require`s a `branch_utils` that does not exist. Replace it with the prelude and R5's model. `README.md` in the same folder describes the unbuilt variant cache.
- `docs/design/procedural-variation.md`: "80% to 120% height variation" and "trees rotated randomly" both disagree with this spec and the code.
- The flora skill has more to fix than the shadow rule: `SKILL.md` calls `deciduous.lua` canonical and defines life stages as one construction scaled by `growthStage` (R5 grows them); the cheat sheet's maple is a burgundy Japanese maple and its "trunk is only a small stub" rule conflicts with a tall birch; `xml-schema.md` ships a `Flora_TreeBirch` example that contradicts R6 and uses `duration` where the parser reads `durability`; `lua-api.md` says `variantIndex` runs 0 to N-1 and omits `noise2`, `setLod`, and `setHeight`.

## Sources

Concepts only; no code or parameter tables are copied.

- Honda, 1971, "Description of the form of trees by the parameters of the tree-like body": length ratios and branch angles are enough to produce recognisable species.
- Borchert and Honda, 1984, and Palubicki et al., 2009, "Self-organizing tree models for image synthesis": the vigour split at a fork that separates single-stem from spreading trees.
- Stava et al., 2014, "Inverse procedural modelling of trees": per-species parameter tables for oak, maple, and pine.
- Halle, Oldeman, and Tomlinson's architectural models (oak, maple, birch, and pine are all Rauh's model; palms are Corner's).
- Prior art in games: RimWorld picks from a folder of textures, flips, and scales by growth; Factorio ships about ten variations per tree family and tints leaves separately from the trunk.
