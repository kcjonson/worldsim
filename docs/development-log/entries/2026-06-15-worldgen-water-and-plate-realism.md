# 2026-06-15 - Water availability + plate-boundary realism

## Summary

Two worldgen pieces shipped together. **Water availability** makes the coarse drainage network real (depression routing so flow reaches the sea or ponds into basins, the previously-dead `kFlagRiver`/`kFlagLake` flags now set) and surfaces it to the player without rendering rivers on the globe: a landing-site details pane reports whether a site has fresh water, and a globe Hydrology color mode shows the drainage. **Plate-boundary realism** removes the dead-straight mountain walls that appeared across many generated worlds by replacing the great-circle rift cut with an organically curved one and giving plates an Earth-like size hierarchy.

PRs: [#144](https://github.com/kcjonson/worldsim/pull/144) (water), [#145](https://github.com/kcjonson/worldsim/pull/145) (plate realism), [#146](https://github.com/kcjonson/worldsim/pull/146) (lake spill-depth fix).

## Details

### Water availability

The architecture decision that drove the design: **worldgen stays coarse.** At the ~21M-tile target a tile is still several km, far too coarse to hold real river/lake geometry, and precomputing it is too much data. Worldgen stores only water *amount* (`precipitation`, `flowAccum`) and drainage *direction* (`downhill`), plus coarse per-tile flags and basin spill levels. The 2D layer will compute the actual river/lake geometry at chunk load as a deterministic function of the coarse data plus world position (a later epic). So the 3D world never knows where a specific river *is*, only that a tile carries one.

- **Drainage fix** (`PrecipitationStage`): priority-flood depression routing (Barnes 2014). Flow used to die in inland pits; now every pit fills to its spill lip, so flow routes across flat lake surfaces and resumes below. Lakes form where the filled level exceeds terrain (`kFlagLake`, spill height stored in `waterDepth`); endorheic basins that reach no ocean keep `downhill == 0xFF` and are left as genuine sinks. The river flag is a resolution-invariant top fraction of land by `flowAccum` (a quantile cut), stable at ~4-5% across n=256..1024 rather than a magnitude threshold that drifts with resolution.
- **Globe Hydrology mode** (`PlanetColorizer`, `PlanetTileColor`): land tinted by `log(flowAccum)` for river networks, lakes cyan, oceans by depth. Cycles with the other color modes.
- **Landing-site water signal + details pane** (`LandingSite`, new `LandingSiteDetailsPanel`/Model): classifies each candidate tile as river / lake / coastal saltwater / rain-fed from its own coarse state plus its neighbors, and the landing pane reports water, terrain, biome, climate, and a difficulty rating. The default landing site is biased toward freshwater.
- **Measurement** (`WorldStats`, `worldgen-cli`): water stats (river/lake/sink/endorheic fractions, % land with water nearby) and a drainage debug image for before/after.
- **Lake spill-depth fix** (#146): the spill-depth store truncated toward zero, so a lake-edge tile with a sub-1m spill got `kFlagLake` set while storing `waterDepth = 0`. Now the spill rounds and the lake flag is only set once it rounds to >= 1m, so a flagged lake always carries a meaningful depth for the future chunk layer to fill.

No PlanetIO format bump (reuses existing `flags` / `waterDepth`). Deterministic: worldHash bit-identical across thread counts; priority-flood heap and flow-accumulation pass run in a fixed total order (filled elevation, then ascending TileId).

### Plate-boundary realism

The artifact was a dead-straight band of snow-capped mountains running a long arc across a continent, on many seeds. A multi-agent diagnosis (four parallel code deep-reads plus a tectonics research arm, synthesized) found the root cause and ruled out a red herring:

- **Root cause (bug):** `PlateSim::tryRift` split a plate along a **great circle** (a flat plane sign-test), which is a straight line on the sphere, and the de-straightening noise was quantifiably ~50-100x too weak (sub-cell wander over a 60-190 cell cut). When such a seam later went convergent, the orogeny stamp followed it and `TerrainStage` rendered a continuous uniform wall.
- **Why it was systematic:** the plate-size model was single-tier (every plate drawn from one uniform growth range), so plates routinely grew past the 22% oversized cap and were force-split by that exact straight cut.
- **Ruled out:** the M-T4.5 linear-orogeny-belt concentration does *not* snap belts to great circles; it faithfully traces whatever boundary it is given.

Fix, in three parts plus a generator option:

- **Curved rift cut**: replaced the analytic plane split with a two-front noisy Dijkstra cut, seeded from the two extreme cells along the cut axis and grown with the same `boundaryNoise` scheme that already grows the initial plate boundaries. The seam meanders like a real margin. The great-circle primitive is gone for both the oversized and suture-biased paths, so the artifact cannot recur. Determinism preserved (same hashed rift stream, ascending-TileId tie-breaks, `det_math` only).
- **Power-law plate-size hierarchy**: seeding growth weights are square-warped into a few-large-many-small distribution (Earth's "broken sheet"), so plates seldom run oversized in the first place. The 22% cap is itself Earth-correct (the Pacific is ~20%); what was missing was the size *distribution*.
- **Broken mountain belts** (`TerrainStage`): the belt lift fraction is modulated along strike within a floored range, so ranges get real passes and saddles instead of a flat-topped uniform crest.
- **Ultra 2048** generator resolution option: the save format's existing ceiling (~42M tiles), no format change.

Verification: organically curved boundaries across many seeds including low plate counts (which trigger oversized rifts most); golden sim hash re-pinned; the biome-fraction acceptance test, which had been overfit to one seed's continental layout, now averages across six seeds to guard the model's central tendency; full `world-tests` green; worldHash bit-identical across thread counts.

## Related Documentation

- `.claude/plans/water-hydrology.md` — the water/erosion/rendering plan and the chunk-time model
- `/docs/design/features/world-generation/data-model.md` — coarse worldgen data fields
- `status.md` — Water Availability and Plate Boundary Realism under Recently Completed

## Next Steps

- **Valley erosion (3b)**: coarse stream-power incision on the drainage tree, carving km-scale valleys into the continental platforms so rivers land in valleys before they are rendered.
- **2D chunk-time river/lake rendering**: draw rivers/lakes in the local tile map as a deterministic distance-field along the coarse `downhill` polyline plus basin fill, computed at chunk load (no fine geometry stored), per the chunk-time model in the water plan. Erosion comes first so rivers render into the carved valleys the first time.
