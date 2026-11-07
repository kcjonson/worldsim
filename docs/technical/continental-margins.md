# Continental margins: emergent shelves and inland seas

Created: 2026-11-02
Status: Spec (not started)

Continental shelves, slopes, and shallow inland seas built from physics instead of
a painted ramp: rifting thins the crust, isostasy and thermal subsidence sink it,
sediment from the drainage fills the room to a surface planed at ice-age sea
level, and the lithosphere flexes under all of it. Shelf width then varies the way
Earth's does, from a few km off an Andean coast to hundreds of km off a big river
on an old passive margin. The globe view is the judge; realism outranks golden
hashes and the old tuning.

This replaces the "continental shelf profile" in
`docs/design/features/world-generation/generation-phases.md` (Phases 3 and 4).

---

## What's wrong today

`TerrainStage` paints the shelf. `shelfElevationForSignedEdge` ramps elevation by
distance from the `kFlagContinentalCrust` edge: -140 m at the edge, -120 m across a
flat 200 km band, then a 50 km smoothstep up to the platform. Everything below
follows from that.

- **The coast is the crust edge, offset inland by a constant.** Sea level lands in
  the inner blend, so every coastline copies the crust edge's shape at a fixed
  distance, everywhere on the planet.
- **Distance was a hex hop count**, so the offset has hexagonal iso-contours:
  straight 60 degree coast runs, and hexagon-shaped seas around small oceanic
  patches inside continents.
- **A rim stood above sea level at the shelf break.** Ocean-floor noise
  (+-550 m) rode on top of the -140 m break while the land side faded its noise
  to zero, leaving a moat of flooded shelf behind a strip of land.
- **Active margins get the same shelf as passive ones.** The continental branch
  applies the ramp whatever the boundary type.
- **Thickness below 44 km is inert.** The isostasy knee puts all crust up to 44 km
  on one flat +450 m platform, so the crust can't sink by thinning even if
  something thinned it. Nothing does: `PlateSim::tryRift` splits a plate
  instantly, and the continent ends in a hard ~35 km to 7 km step.
- **The km constants are off by 1.65x.** `tileWidthKm` is circumference /
  sqrt(N), but the true hex center spacing is about R * sqrt(8 pi / (sqrt(3) N)).
  The "200 km" shelf is ~121 km and the "60 km" slope ~36 km. The same formula is
  in `AtmosphereStage`, `PrecipitationStage`, `WorldStats`, and `worldgen-cli`.

The rim and the hex contours get an interim fix on
`kj/world-gen-coastline-edges-59d38b`: the ocean noise fades across the slope and
the crust-edge distance becomes Euclidean (`SeedDistance`). This epic deletes the
ramp but keeps `SeedDistance`.

---

## The physics, as a causal chain

The key finding from the research: **Airy isostasy on thinning crust can't make a
wide flat shelf by itself.** With McKenzie densities each km of crustal thinning
lowers the surface 159 m (227 m water-loaded). From a +450 m platform the coast
falls where the crust has thinned ~2.8 km and the -130 m break needs only ~0.6 km
more. Across a real proximal thinning gradient that band is ~9 km wide; across a
necking zone, ~1.4 km. Real shelves average 57 km and reach several hundred. The
flat top comes from sediment filling the room up to a surface planed at
glacial lowstands. Tectonics sets the room, sediment supply sets how far the fill
builds out, and glacio-eustasy sets the depth of the break.

1. **Rifting thins the crust in domains.** Along a margin normal: a proximal ramp
   (beta 1 to 1.5), a necking zone (30 km to under 10 km over 50-60 km), a
   hyperextended zone (under 10 km), then the ocean-continent transition. Margin
   width runs from 200-300 km on segments that opened head-on to under 100 km on
   transform segments.
2. **Isostasy sinks the thinned crust, and the hot lithosphere holds it up for a
   while.** Present elevation is crust-only Airy plus a decaying thermal uplift
   (McKenzie 1978). Young margins ride high; by 150 Myr ~9% of the uplift is left.
3. **Sediment fills the room to a profile.** A topset sloping ~0.1 degrees out to
   the break, a foreset at 2-4 degrees below it. Supply comes from the drainage
   basins behind each coast segment and grows with margin age. A starved margin
   gets a narrow shelf, a big river a wide one.
4. **The break sits at the lowstand.** Shelves are coastal plains drowned when the
   ice ages ended: break at ~130-140 m on Earth (LGM sea level -134 m), deeper
   (300-550 m) where ice sat on the shelf. Highs above that surface were planed.
5. **Active margins don't rift.** No thinning, most sediment bypasses to the
   trench, steep forearc slopes. Earth mean active-margin shelf: 31 km.
6. **The lithosphere flexes.** Loads (sediment, ice, thinned crust, mountain
   belts) deflect it over a flexural wavelength of tens to ~175 km. This bends the
   sediment wedge down, lifts the onshore flank, blurs the crust edge so the coast
   stops tracing it, and digs foredeeps beside mountain belts.
7. **Interiors flood only where the surface drops below sea level:** failed rifts
   with thermal sag (North Sea), ice loading that hasn't rebounded (Hudson Bay,
   Baltic), and mantle-driven subsidence over sinking slabs (~300 m under the
   Western Interior Seaway).

---

## Model

### Plate sim: rift history (`tectonics/PlateSim`)

Rifting becomes a phase, not an instant split.

- When `tryRift` fires, the rift zone (the cut's band of cells) enters an
  extension phase. Cells along it stretch: beta grows with the divergence the new
  Euler poles impose, and coarse thickness becomes thickness / beta.
- **Breakup** happens when the crust at the rift axis falls below the breakup
  thickness (~10 km). `gapFill` then makes oceanic crust between the pieces, as
  today. Cells that stay continental keep their final beta.
- **Failed rifts:** an extension phase can stop before breakup (the pieces' poles
  re-converge, or a probability per step). Those cells keep beta ~1.2-2 and become
  aulacogens.
- Record per coarse cell, and carry through `TectonicHistory`:
  `riftOnsetMyr`, `breakupMyr` (or never), `betaMax`, and the extension direction
  (for obliquity against the margin).
- Margin type is labeled at the end of the sim: **active** where the continent's
  edge sits on a convergent boundary with ocean subducting beneath it,
  **transform** on a transform boundary, **passive** where the continent/ocean
  edge lies inside one plate. Margin age for passive margins is `breakupMyr`; the
  age of the adjacent ocean floor cross-checks it.

The coarse grid (~60 km spacing) only supplies parameters. The thinning profile
itself is built at full resolution.

### Full-res crust thickness (`TerrainStage`)

- Distance to the ocean-continent transition (the `kFlagContinentalCrust` edge)
  comes from `SeedDistance` in true km, with the nearest seed's coarse margin
  parameters carried along so each margin point knows its beta, age, type, and
  obliquity.
- Thickness inside the margin band = interior thickness / beta(d), where beta(d)
  is the domain profile above. Domain widths scale with obliquity (head-on 200-300
  km total, transform under 100 km) and vary along the margin with the sim's
  parameters plus low-frequency noise. Magma-poor margins get a hyperextended
  zone; volcanic margins (high `volcanism` at breakup) end in thick igneous crust
  instead.
- Outside the band, thickness is the smooth-sampled coarse field, as now.

### Isostasy: Airy everywhere, plus thermal age

- Delete the knee. Continental elevation is Airy on the full-res thickness
  against a reference column calibrated so normal ~35-40 km crust sits near
  Earth's continental freeboard.
- The sim's initial thickness distribution (38 +- 6 km today) is the honest knob
  for continental relief; re-anchor it to Earth's crustal thickness distribution
  rather than flattening the response.
- Add the McKenzie thermal term to rifted crust:
  `z += E0 * (beta / pi) * sin(pi / beta) * exp(-t / tau)`, t = time since rifting.
- Oceanic crust keeps the GDH1 depth-age law. The abyssal noise applies on oceanic
  crust only; the slope is now hyperextended continental crust, so the interim
  slope fade becomes unnecessary.

### Flexure

The Airy response becomes a flexural one:
`D * del4(w) + (rho_m - rho_infill) * g * w = q`, with `D = E * Te^3 / (12 (1 - nu^2))`.
Airy is the D = 0 limit, and the two agree past ~400 km wavelengths.

- Loads: thinned or thickened crust, sediment, glacial ice, water.
- Te varies: ~5 km on young margins, 10-30 km on old ones, thicker under cratons.
  That gives alpha ~19-74 km, so a load's deflection reaches its first zero at
  ~45-175 km, which is 2 to 50 tiles. It matters.
- Solve on a grid with spacing at most alpha / 3, i.e. min(n, 1024), and
  interpolate up. The solver (multigrid over the nested icosahedral levels, or
  conjugate gradient with a fixed iteration count) is picked in the spike. It
  must be deterministic and thread-count independent.
- Side effect, intended: foredeeps beside mountain belts (Ganges plain, Persian
  Gulf). Check `MountainsAtConvergentCC` and the belt metrics against it.

### Glacial-maximum pass

One extra climate pass at glacial forcing serves three things: sediment supply,
the lowstand depth, and glacial loading.

- Run Atmosphere, Precipitation, Snow, and Glacier under glacial forcing. Lower
  the radiative forcing and let the existing ice-albedo feedback (the cryosphere
  two-pass) grow the ice. A uniform temperature offset would miss polar
  amplification.
- **Lowstand:** `delta = (V_ice_glacial - V_ice_present) * rho_i / rho_w / A_ocean`.
  Earth gives ~120-134 m. A planet with no glacial cycles gets a shallow break set
  by storm wave base (~20-60 m, like tropical shelves).
- **Glacial load:** equilibrium depression is ~0.3 of ice thickness
  (rho_i / rho_m), through the flexure solver. Today's residual is a fraction of
  that, still rebounding. Shelves that were under ice get a 300-550 m break.

### Margin stage (new, after the glacial pass)

1. **Supply.** For each coastal outlet, the BQART flux
   `Qs = w * B * Q^0.31 * A^0.5 * R * T` from drainage area (`flowAccum`), runoff,
   relief, and temperature from the glacial pass, times margin age. Active margins
   retain little of it, since most bypasses to the trench.
2. **Longshore spread.** Spread each outlet's supply along the coast with an
   exponential kernel (length ~100-300 km), so deltas build lobes, not spikes.
3. **Fill to profile.** Walk outward from the coast in order of distance, filling
   each ocean tile up to the target surface: topset at the lowstand sloping
   ~0.1 degrees, foreset at 2-4 degrees past the break. Fill volume per meter of
   water depth replaced is ~2.5x, since the load sinks
   ((rho_m - rho_w) / (rho_m - rho_s)). The break sits where the supply runs out.
   Progradation is capped at 40-60 km/Myr.
4. **Plane.** Clip shelf highs above the lowstand surface (minus wave base) down
   to it.
5. **Flex the sediment load,** then refill once. Two passes at most; report the
   residual.
6. **Final sea level.** Area quantile at `waterAmount`, as today, then one
   re-check of the fill against it.

### Inland seas

They come out of the model where the surface ends up below sea level; nothing
paints them.

- **Failed rifts:** beta 1.2-2 plus thermal sag plus sediment.
- **Glacial depression:** the residual from the glacial pass (Hudson Bay, Baltic).
- **Dynamic topography:** a long-wavelength low (~300 +- 100 m) over the slab a
  subduction zone has sent down beneath the overriding plate, from the sim's
  subduction record.
- **Intracratonic sag basins** (Michigan, Congo): mechanism debated; out of scope
  unless failed-rift sag already produces them.

---

## Pipeline

| Order | Stage | Change |
|---|---|---|
| 1 | TectonicHistory | Rift phase, breakup, failed rifts; rift-history fields; margin labels |
| 2 | Crust | Unchanged (`kFlagContinentalCrust` edge is the ocean-continent transition) |
| 3 | Terrain | Full-res thinning, Airy without the knee, thermal term, flexure. Ramp deleted. Provisional sea level |
| 4 | Erosion | Unchanged; provides drainage and outlets |
| 5 | Glacial pass (new) | Atmosphere, Precipitation, Snow, Glacier at glacial forcing; lowstand, ice load |
| 6 | Margin (new) | Supply, longshore spread, fill, planing, sediment flexure, glacial depression, final sea level |
| 7-12 | Present climate tail | Atmosphere ... Glacier as today, including the ice feedback pass |

Erosion runs against the provisional coast. Deltas then move the coast seaward,
which is what happens on Earth too.

### Data

- `TectonicHistory` (coarse): `riftOnsetMyr`, `breakupMyr`, `betaMax`, extension
  direction, margin type.
- Transient full-res: thickness, beta, sediment thickness, glacial depression.
  Nothing new is persisted. Persisting sediment thickness for 2D soils or
  resources is a separate decision.
- `GeneratedWorld`: `lowstandMeters`, glacial ice volume (for the world summary).
- Planet file format unchanged; elevation content changes.

---

## Constants

Numbers to start from. Sources marked * were reached only through secondary
sources (Wikipedia, GRID-Arendal) or are the research agent's own arithmetic, and
need a check against the primary paper before they anchor a gate.

| Quantity | Value | Source |
|---|---|---|
| Shelf width, all margins | mean 57 km, max 778 km | Harris et al. 2014* (https://doi.org/10.1016/j.margeo.2014.01.011) |
| Shelf width by margin type | passive 88 km, active 31 km | Harris et al. 2014* (https://www.grida.no/publications/318) |
| Extremes | Siberian ~1500 km; Chile/Peru under 10 km; Amazon ~300 km | * |
| Shelf break depth | 130-140 m typical; 20 m tropical to 550 m polar | Shepard 1973* |
| Shelf gradient | ~0.1 degrees | * |
| Slope | width 36 km active, 46 km passive; gradient 0.5-4.4 degrees regional | Pratson & Haxby 1996; GRID-Arendal* |
| Rifted-margin crust | proximal 30-35 km (beta under 1.5); necking 30 to under 10 km over 50-60 km; hyperextended under 10 km; oceanic 6-7 km | Mohn et al. 2012 (https://doi.org/10.1029/2011TC002961) |
| Margin width vs obliquity | 200-300 km head-on, under 100 km transform | Loparev et al. 2021 (https://doi.org/10.1029/2020TC006159) |
| McKenzie constants | a = 125 km, rho_m 3330, rho_c 2800, rho_w 1000, alpha 3.28e-5/K, T1 1333 C, tau 62.8 Myr, E0 3.2 km | McKenzie 1978 (https://doi.org/10.1016/0012-821X(78)90071-7) |
| Thermal subsidence | E0 (beta/pi) sin(pi/beta) (1 - e^(-t/tau)): 2.0 km at beta 2, 3.2 km as beta grows | McKenzie 1978 |
| Sediment load factor | ~2.5x water depth replaced | * (arithmetic) |
| Progradation | under 40-60 km/Myr | Carvajal et al. 2009 (https://doi.org/10.1016/j.earscirev.2009.06.008) |
| Sediment wedge | 9-13 km (Niger, New Jersey) | * |
| River supply | BQART: Qs = w B Q^0.31 A^0.5 R T | Syvitski & Milliman 2007 (https://doi.org/10.1086/509246) |
| Elastic thickness at margins | under 5 km young to 10-20 km old (studies range 0-60) | Karner & Watts 1982 (https://doi.org/10.1029/JB087iB04p02923) |
| Flexural parameter | alpha ~19/32/60/74 km at Te 5/10/23/30 km | Hutton/Syvitski/Watts; * |
| LGM sea level | -134 m | Lambeck et al. 2014 (https://doi.org/10.1073/pnas.1411762111) |
| Dynamic topography (Western Interior Seaway) | ~300 +- 100 m | * |

Still to source: Earth's shelf area as a fraction of ocean area, the land
hypsometric percentiles that replace the bimodality bounds, and Hudson Bay's
residual depression as a fraction of its LGM depression.

---

## Prior art

Nobody we found builds shelves from margin physics. The closest is Tectonics.js,
which gets the isostasy right but has no thinning, so its margins are sharp.

| Project | At margins | Emergent? |
|---|---|---|
| Cortial et al. 2019, Procedural Tectonic Planets (https://doi.org/10.1111/cgf.13614) | Elevation pushed by subduction, collision, erosion; no isostasy, no shelf found (paper not fully verified) | No |
| Tectonics.js (https://github.com/davidson16807/tectonics.js) | Local Airy, crust density by age, sea level solved by bisection | Partly; no thinning |
| PlaTec (Viitanen 2012) | Crust amount is elevation; threshold continent/ocean | No |
| WorldEngine (https://github.com/Mindwerks/worldengine) | Depth scaled by distance within 5 cells of land | Painted |
| Undiscovered Worlds (https://undiscoveredworlds.blogspot.com/2021/06/installing-shelving.html) | Fractal-radius circles per coast cell, flood fill; terraced seams | Painted, variable |
| Red Blob 1843 planet gen (https://www.redblobgames.com/x/1843-planet-generation/) | Boundary elevations interpolated by distance | No |
| Azgaar FMG | Graph-distance ocean rings | Painted |

Fill-to-profile amounts to Undiscovered Worlds' variable-radius idea with the
radius coming from sediment supply and accommodation instead of a fractal.

---

## Validation

Gates are re-anchored to Earth; the old bounds go where they no longer measure
anything real. Goldens get re-pinned.

- **Shelf width**, measured along margin normals from coast to break, over
  fixed seeds at n=512: overall mean 40-90 km; passive mean at least 2x active
  mean; some margins wider than 300 km and some narrower than 10 km.
- **Break depth:** median within lowstand +- 30 m on margins that weren't under
  ice.
- **No rim:** oceanic crust above sea level only at arcs and hotspots (carried
  from the interim fix).
- **Isotropy:** coast direction, smoothed over ~10-tile windows, has no peaks at
  the hex axes.
- **Hypsometry:** the bimodality check stays, with bounds from Earth's curve; the
  shallow peak sits near Earth's shelf peak (~40 m below sea level).
- **Inland seas:** at least one emerges across the fixed seed set; no gate on
  count.
- **Downstream:** biome fraction acceptance and climate gates pass, or move with a
  stated reason.
- **Determinism:** identical across thread counts.
- **Perf:** total generation time at n=1024 and n=2048, two runs each, before and
  after, same machine. The budget is set from the spike's numbers before
  implementation starts.
- **Globe review:** before/after globe screenshots at fixed seeds and views,
  judged by eye in Terrain and Combined modes.

---

## Out of scope and honest simplifications

- Coast shape below one planet tile at the landing scale: WOR-467 and the ground
  blending work (WOR-556).
- Shelf-edge canyons, glacial troughs, clinoform detail (two segments stand in),
  Holocene transgression features.
- Wave-driven coastline shaping (Ashton 2001, Sapoval 2004); a candidate follow-up.
- The bathymetry "comb" from the crust-age depth field (status.md, Fluvial
  Erosion) is separate; the shelf half of it goes away with the ramp.

## Open questions

- Flexure solver and grid, and the generation-time budget: from the spike.
- How glacial forcing enters `AtmosphereStage` (forcing term vs. insolation
  scaling).
- Whether to persist sediment thickness for 2D soils and resources.
