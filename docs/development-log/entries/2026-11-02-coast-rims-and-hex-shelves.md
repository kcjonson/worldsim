# 2026-11-02 - Coast rims, hexagonal shelves, and active margins

**Board:** WOR-581. Follow-ups: WOR-571 (emergent continental shelves), WOR-582 (PlateSim continental area undershoot), WOR-597 (plate boundaries on the coarse lattice), WOR-598 (inflated tile km), WOR-599 (deep-ocean hex mosaic).

## Summary

From orbit, every planet's coasts read land, light-blue band, a one-tile dark-green ring, then deep blue. Inland seas were hexagons with green rims. Some subduction coasts had sheared dark teeth in the shallows and parallel streaks across the lowland, and every trench lay against the shore. All of it came from TerrainStage. The ring and the hexagons are gone, the teeth and streaks are gone, and trenches now sit about 100-150 km offshore behind a submerged forearc with the volcanic arc inland. The painted constant-width shelf itself stays until [Continental Margins](../../technical/continental-margins.md) replaces it.

## Root causes

**Abyssal relief at the break.** Oceanic tiles took 300 m abyssal hills plus 250 m swell at full amplitude right up to the crust edge, on top of a slope profile pinned at the -140 m break. The quantile sea level lands in the shelf band on many worlds, so any break tile with noise above about +10 m stood dry outside a flooded shelf. 31% of passive-coast edge tiles on one ordinary seed.

**Rifted margins.** The rift kernel digs a valley into the continental edge while the oceanic tile beside it stays at break depth. On low-sea worlds that made a dry break row beside a flooded rift valley along every spreading margin.

**Lagoons behind the break.** The continental shelf is a 20 m ramp, and its hills fade in from zero at the break. A few km inland, faded negative hills outweighed the ramp, so the shelf surface dipped below its own seaward edge and flooded into lagoons behind a dry strip.

**Hexagons and hop ties.** The crust-edge distance and the nearest-boundary search were BFS hop counts, whose iso-contours on the hex lattice are hexagons. Hop ties between a boundary's two rows broke in FIFO order, so trench and arc regions sheared along lattice directions and sent one-tile rays far inland and out to sea.

**Boundary type flicker.** Each boundary tile's type came from its own 6-neighbour normal on a warped boundary, thresholded at |conv| > 0.35 |v_rel|. Along one boundary it flipped OO / transform every 5 to 15 tiles, and each type's kernel applied at full strength in its own cells with hard edges between them.

**No forearc.** The trench is centred on the plate boundary, and the overriding plate's continental crust runs right up to it. The overriding side only took a -400 m dip about 17 km wide, so the coast sat on the trench's inner wall and the arc 133 km inland. On Earth the trench lies 100-250 km offshore of a submerged forearc. Trench kernels also sank continental tiles nearest a subducting row.

## Details

- `libs/worldgen/worldgen/stages/SeedDistance.{h,cpp}` is new. It returns the straight-line distance to the nearest seed tile in mean tile spacings, plus that seed's index, by vector propagation (serial FIFO, strict improvement, deterministic at any thread count). A labelled second pass returns the nearest seed with a different label near region borders, within a reach cap. On a dense seed row a few tiles come out up to 0.3 spacings long; the header documents it.
- Boundary attributes are read from the straight-line nearest boundary tile. The hop BFS, the Jacobi smoothing that hid its terraces, the mode filters, and the belt-end taper are deleted.
- Classification averages the boundary normal along each plate pair's boundary over 300 km (`kBoundaryTypeSmoothKm`, true km), shrinking to 150 or 75 km where the average loses coherence across a narrow plate tongue. A spreading boundary is segmented into ridge and transform with a 50 km normal, floored at 2.5 tile spacings. Each kernel family scales by its share among boundary tiles within 150 km, so type changes and segment ends fade.
- Side is the tile's own plate, and the slab is decided once per plate pair. Each boundary carries a subducting share: the oceanic fraction of the slab side, smoothed along the pair. Trench, forearc, and arc scale by it, so a continental slab side behaves as collision onset, with the axis shoaling and the forearc rising with it (as at Timor or Taiwan).
- On the slab side the floor returns to depth-age along the trench kernel's own falloff. The overriding side is pulled toward a margin profile: an inner wall that starts from that boundary's actual axis floor (slab rows are synthesized first), then a forearc terrace at -2500 m (CO) or -3000 m (OO), full out to 90-100 km and released by 180 km. The axis is the deepest point, and the two sides meet there. The arc-trench gap is 250 km (CO) and 190 km (OO), after Gill 1981 and Syracuse and Abers 2006. Volcanic cones are suppressed on the forearc. Kernel distances run from the boundary, half a spacing past the row centres, and every kernel is a Gaussian cut to exactly zero at 3 sigma.
- OO island arcs are built from a sparse island noise field over abyssal-depth water, so they read as chains at any sea level. Cones are suppressed across the arc; their ridged noise had been lining them up into land ribbons just behind the forearc.
- Where two boundaries' regions meet, both boundaries' kernels blend across the bisector over 150 km. The forearc and arc reach far enough that without this they were cut into straight cliffs. The second boundary must include the tile's own plate, so the blend stays continuous near triple junctions. Step 2's kernel shares are counted per plate pair.
- Within one pair, a tile takes the kernel inputs (shares, convergence, subducting share, axis floor) as a weighted mean over the boundary's 20 km anchors within a band of distance. The band is 2 spacings at the boundary and widens to 150 km. The inputs don't step where the nearest boundary tile jumps across a bend or a notch in the row; before this, the forearc pull turned those jumps into straight coasts and cuts 3-4 km high.
- Rift and axial valley blend across the crust edge over the slope width, the rifted-margin weight scales by the divergent share, and volcanism ramps in from its threshold.
- On the oceanic side, abyssal hills and swell fade up the continental slope, and the slope profile blends to the depth-age depth by the rift and trench kernels' own falloff. `shelfRelief` admits no negative relief on the outer shelf: wave-base planing and sediment infill keep a shelf from holding basins below its surface. The crust edge sits half a spacing between the two edge rows. Before, 4.2% of tiles sat at exactly -140 m, and that pinned the quantile sea level at the break.
- Gates:
  - `MountainsAtConvergentCC`: the filter was vacuous and now uses each tile's nearest boundary row within 8 hops; it measures 941 m above the land mean against the original 800 m bound.
  - The shallow band is 0-250 m below sea at > 1%, and the share of it that is shelf or the edge row beside it is asserted (98.1%).
  - New rim assertion: rims over passive edge tiles beside a flooded shelf, 0 of 7526.
  - `TrenchArcStructure` is rewritten, since the old arc check matched no tiles. At n=256 it asserts a median slab-to-overriding row step under 1.5 km, the overriding row deeper in under 20% of pairs, a median trench-to-coast distance between 60 and 200 km, under 15% land in the forearc, and more land in the arc band than in the forearc.
  - HotDesert at n=64 goes from 0.06 to 0.045 (measured mean 5.6%): less land at low latitudes once island arcs are chains and the forearc drowns margins, with sea level lower on low-crust worlds (WOR-582).
  - New `TerrainStageHeavy.DeterministicAcrossThreadCounts` (1 vs 8 threads).
  - `GoldenWorldHash` is re-pinned.

## Results

At n=1024 across 7 seeds, passive-margin rims are 0 on every seed, down from up to 14,618. Total rims go from 17,520 to 279 on seed 1 and from 19,252 to 142 on seed 3735928559. What remains sits at active margins.

On main the CO trench axis tile was land 86-100% of the time, and the slab and overriding boundary rows differed by 7.6-8.4 km on average. On the branch they differ by 300-561 m (CO) and about 150 m (OO), and the overriding row is the deeper one in 13-17% of CO pairs, since the inner wall starts from a 20 km anchor's mean floor. Where subduction is full, the median distance from the trench axis to the coast is 105-132 km on five seeds. It's 68 km and 27 km on the two deep-sea seeds, where a -2500 m forearc terrace is only a few hundred metres below sea. OO first-land medians are 137-181 km, and island-arc land runs along strike average 28-65 km, against 104-838 km of ribbon on main.

Isolated cliffs are steps over 500 m that are more than twice the steps on either side. At n=1024 across the seven seeds they go from 47-64k to 2.2-3.0k within 60 km of a boundary, from 22-29k to 7-168 at 60-200 km, and from 11-201 to 0 at 200-400 km.

At n=2048 (seed 424242, RelWithDebInfo), TerrainStage takes 14.35 s against main's 14.58 / 13.86 s. Its peak working set is +1023 MB over the stage start, against main's +1488 MB.

Sea level on worlds with little continental crust falls a further 300-800 m, e.g. seed 7 from -1549 to -2247 m. That's because the quantile holds land area fixed while the forearc drowns margin land. These worlds already sat far below the shelf break on main; the cause is the plate sim's crust shortfall (WOR-582).

Known residuals:
- Long plate boundaries come out dead straight, so their trenches do too (WOR-597).
- On low-crust worlds the dry painted shelf shows as an even band around every coast (WOR-582, then WOR-571).
- The deep ocean and mountain interiors show a hex mosaic, since crustAge and orogenyAge are constant per coarse cell (WOR-599).
- On a slow oblique margin the trench breaks into blobs, because the boundary flips between convergent and transform at the 0.35 threshold and the trench follows that share (WOR-600).

## Related Documentation

- [Continental Margins](../../technical/continental-margins.md), the physical model that replaces the painted shelf
- [Scenario 10: World creator coasts](../../testing/scenarios/10-world-creator-coasts.md)

## Next Steps

- WOR-582: continental crust fraction, which sets how far the sea drops below the shelves.
- WOR-597: plate boundaries that follow coarse cell outlines, behind straight coasts, zig-zag trench edges, and the slab-side trenches above.
- WOR-571: emergent shelves, which removes the constant-width shelf band this change only rounds.
