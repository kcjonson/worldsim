# World Generation Foundation Merged (PRs #118-#124)

## Summary

Seven PRs merged to main, covering milestones M1, M2, M3a, M3b, M3f-1, M3g, and M3h of the World Generation & Creator epic. Each PR was agent-reviewed plus Copilot-reviewed, all findings fixed before merge, CI green on all three jobs.

- **#118 (M1)** Foundation primitives: TaskPool with thread-count-independent ParallelFor, SplitMix64/PCG32, integer-hash noise (value/gradient/fractal/ridged), polynomial DeterministicMath, FNV-1a WorldHash. Golden bit-pattern tests guard cross-platform drift.
- **#119 (M2)** Worldgen core, frozen contracts: icosahedral rhombus SphereGrid (10n^2 tiles, implicit TileId, O(1) inverse mapping ~59ns), 21-biome taxonomy, PlanetParams + 6 presets, SoA WorldData (~26 B/tile), stage interface + PlanetGenerator (background jthread, atomic progress, cancel, per-stage snapshots), equirectangular BMP debug exporter. Contracts in `docs/technical/world-generation-implementation.md`.
- **#120 (M3a)** Tectonic plates: multi-source Dijkstra Voronoi with per-plate growth rates and noise-perturbed boundaries, craton pass decoupling continental crust from plate ids (passive + active margins), Euler poles with momentum balance.
- **#124 (M3b)** Terrain from tectonics: smoothed boundary classification (crust-aware via neighbor-tile majority, not plate flags), side-aware uplift kernels (CC ranges, CO trench + volcanic arc with the trench-arc gap, OO island arcs, mid-ocean ridges, rifts), jittered+Jacobi-smoothed distance fields (no terracing), BFS-propagated belt-end tapering, hotspot chains, sea level by histogram quantile.
- **#121 (M3f-1)** planet-view: isolated 3D globe sidecar library; 10 rhombus patch meshes from the SphereGrid mapping (seam-free), per-rhombus data textures honoring validFields (progressive phase rendering), orbit camera with camera-anchored sun, ray-sphere picking, own depth FBO with full GL state save/restore, composited beneath all 2D UI. Chunked-LOD deferred to M3f-2.
- **#122 (M3g)** UI Slider (linear/log/step, keyboard, reentrancy-guarded callbacks) + WorldCreator scene shell driving the real PlanetGenerator (Configuring -> Generating -> Reviewing).
- **#123 (M3h)** Biome migration 8 -> 21: engine Biome aliases worldgen::Biome, BiomeWeights sparse top-4 with largest-remainder normalization, dispatcher maps 21 biomes onto existing generators, asset XML biomeName renames. Game verified visually unchanged with 10,802 entities placed.

## TaskPool ABA race (root cause of the "flaky" tests)

Every intermittent world-tests crash/hang traced to one bug: a pool worker that snapshotted a job but was preempted before claiming a slab could outlive `parallelFor`; the next job reset the shared slab counter, and the stale worker claimed a slab of the new job and invoked the destroyed `fn` of the old one. Reproduced deterministically (12/12) under concurrent MSVC compile load; 0/12 after the fix. Fix: `parallelFor` waits for zero registered participants before returning, and workers join each job at most once via a job sequence number. A separate lost-wakeup race (final completion increment outside the mutex) was fixed in the same file.

## Other notable fixes from review

- Negative-integer lattice flooring bug in HashNoise (discontinuities at exact negative integer coordinates); union type-punning UB replaced with std::bit_cast; Lemire bound computation moved to unsigned arithmetic.
- PlanetGenerator: stale snapshot leaked across start() restarts; Complete state was visible before the final snapshot publish; progress could regress under out-of-order slab completion (now monotonic CAS).
- Stub stages claimed validity for fields they never wrote; flags validity-bit ownership moved to the last writer (SnowStage).
- MSVC: forced /O2 on benchmark targets collided with Debug /RTC1 (D8016) and broke the test-windows CI job for any branch carrying bench files; per-config defaults now used. world-sim's shader copy ordered after planet-view's staging to avoid a parallel-build file-lock race.
- planet-view: inverted triangle winding (back-face culling deleted the near hemisphere), left-drag orbit never engaged, GL active-texture/VAO/scissor/front-face/clear-color state now fully saved/restored.

## Follow-ups tracked

- Beach influence injection in the 2D sampler (M4 scope).
- Antimeridian chunk-coordinate wrap + polar chunk scaling (post-M6).
- planet-view layering: add planet-view to the monorepo dependency-layer docs.
- M3b note: collision mountain belts are compact at triple junctions on some seeds; if longer cordilleras are wanted, bias craton placement toward plate edges in a tuning pass.

## Related Documentation

- Plan: `.claude/plans/world-generation.md`
- Contracts: `docs/technical/world-generation-implementation.md`
- Spec: `docs/design/features/world-generation/`

## Next Steps

M3c (atmosphere/temperature), M3d (precipitation/rivers), M3e (oceans/biomes/snow/summary) replace the remaining stub stages; M3f-2 (chunked-LOD + zoom continuum); M4 (PlanetSampler + GeneratedWorldSampler replacing MockWorldSampler); M5 (WorldCreator + planet view integration); M6 (landing site selection + New Game handoff); M7 (perf gate, cross-platform determinism CI).
