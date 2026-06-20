# Cryosphere: sea ice, snow, and glaciers

Created: 2026-06-20
Status: Implemented

How the planet generator produces and renders the cryosphere: sea ice on frozen
oceans, permanent snow on cold land, and glaciers / ice sheets, plus the
ice → climate feedback that lets ice sheets grow where Earth has them. The model
is physically grounded (positive-degree-day mass balance, perfect-plastic ice
mechanics, Stefan-Boltzmann albedo, a real land/ocean thermal contrast); the few
remaining shortcuts are listed under Honest simplifications.

This supersedes the conceptual "Phase 8: Snow & Glacier Formation" in
`docs/design/features/world-generation/generation-phases.md`.

---

## Three surfaces

The cryosphere is three distinct surfaces, deliberately kept separate (they look
and behave differently):

| Surface | Where | Field(s) | Render |
|---|---|---|---|
| **Permanent snow** | cold LAND (annual mean below the snow line) | `snowCover` (0..255) + `kFlagPermanentSnow` | near-opaque white snowfield |
| **Sea ice** | frozen OCEAN | `iceThickness` (~1-3 m) + `kFlagSeaIce` | opaque pale ice, hard edge vs open water |
| **Glacier / ice sheet** | LAND, thick enough that snow has compacted to flowing ice | `iceThickness` (10..4000 m) + `iceFlow` + `kFlagGlacier` | opaque, thickness-shaded, parabolic dome |

Snow and solid ice are different concepts: **snow is a surface layer; ice is a
body of solid ice.** Snow does not accumulate on water (frozen ocean is sea ice,
not snow). Sea ice and glaciers are opaque (you do not see water or ground under
them); permanent snow is a near-opaque year-round snowfield. Thin, see-through
*seasonal* snow is future work (see Season system, below) — the `snowCover` field
here is the permanent baseline.

---

## Data model (`libs/world/worldgen/data/WorldData.h`)

Per-tile SoA fields added for the cryosphere (IO format v4, `PlanetIO.cpp`):

- `iceThickness` (uint16, m): solid-ice depth — sea ice (~1-3 m) and ice sheets
  (up to `kMaxIceM = 4000`). Written by SnowStage (ocean) and GlacierStage (land).
- `iceFlow` (uint8, 0..5, 0xFF = none): neighbour index of steepest ice-surface
  descent. Written by GlacierStage.
- `snowCover` (uint8): permanent snow on LAND only. (Reused field; never set on
  ocean — snow does not fall on water.)
- Flags: `kFlagSeaIce` (0x20), `kFlagPermanentSnow` (0x10), `kFlagGlacier` (0x08).

Ownership / validFields: SnowStage claims `SnowCover`; **GlacierStage is the last
writer of `flags` and `iceThickness`, so it owns `Flags | IceThickness | IceFlow`.**
SnowStage writes sea-ice flags/thickness but does not claim those bits.

---

## Pipeline stages

### Sea ice — SnowStage (`stages/SnowStage.cpp`)

Land permanent snow and frozen ocean, from the already-computed temperature.

- **Permanent snow** (land): below `thresholdC = -10 - 3*(sqrt(atm) - 1)` clamped
  `[-16, -6]` C, ramps `snowCover` and sets `kFlagPermanentSnow`.
- **Sea ice** (ocean): below `kSeaIceThresholdC = -9 C` (colder than the -1.8 C
  salt-water point because `temperatureMean` is air temp), records a thin
  `iceThickness` (`kSeaIceMaxThicknessM = 3`) and sets `kFlagSeaIce`. The hard
  threshold gives a hard ice/water edge. A cold-enough world freezes its whole
  ocean; an Earth-like world freezes only a high-latitude cap.

### Glaciers / ice sheets — GlacierStage (`stages/GlacierStage.cpp`)

Real glaciology, in two physical pieces. Land only; ocean (sea ice) is left to
SnowStage. **This is a deterministic steady-state model, not a time-stepped
ice-flow PDE.**

**1. Surface mass balance (positive-degree-day model), parallel per land tile.**
Decides where perennial ice can exist.
- Annual positive-degree-days from the seasonal cycle `T(m) = T_ma + A·cos(2π·m/12)`
  (A = `temperatureRange`, the seasonal half-amplitude) with Gaussian sub-monthly
  variability `kSigmaC = 5`, via the Calov-Greve expectation
  `E[max(0,T)] = σ·φ(z) + μ·Φ(z)` summed over 12 months (`std`-free deterministic
  `exp` / normal-CDF). `PDD = (365.25/12)·Σ E`.
- Ablation: `melt = kDDF · PDD`, `kDDF = 3` mm w.e./(C·day) (snow degree-day factor).
- Accumulation: `acc = precip · snowFrac`, `snowFrac` = fraction of the annual
  cycle below `kSnowThreshC = 1 C`.
- `SMB = (acc - melt)/1000 · (ρ_w/ρ_i = 1.091)` m ice/yr. The **accumulation zone
  (ice mask) = land where SMB > 0** — the glaciological condition for perennial ice.

**2. Perfect-plastic thickness, deterministic.**
- Distance-to-margin `d` (m): multi-source Dijkstra (deterministic `(dist, TileId)`
  min-heap, the `DrainageRouting.cpp` pattern) seeded from every non-ice tile,
  relaxed into ice tiles with geodesic edge weights from `tileCenter` +
  `planetRadiusMeters`.
- Thickness (Nye, flat bed): `H = sqrt(2·τ0·d/(ρ_i·g))`, `τ0 = 1e5 Pa` (100 kPa,
  reproduces Greenland's ~3.2 km dome). Sets `iceThickness`, `kFlagGlacier` where
  `H >= 10 m`.
- `iceFlow` = steepest descent of the ice surface (`elevation + H`). Glaciers that
  reach the coast get the ocean as their margin (calving falls out for free).

---

## Ice → climate feedback (two-pass regeneration)

Ice cools its own climate; without this, a real melt model leaves the model's
warm-ish poles nearly ice-free. Implemented as a **fixed two passes** (not a
convergence loop, which would oscillate at the snowball-Earth tipping point):

1. Run the full pipeline (`PlanetGenerator::runPipeline`, pass 1, `iceFeedback=false`).
2. If pass 1 grew land ice (`worldHasLandIce`), re-run the temperature-dependent
   tail (Atmosphere → Precipitation → Ocean → Biome → Snow → Glacier) once with
   `StageContext::iceFeedback=true`, **reusing each stage's `deriveSeed(seed, i)`**
   so the re-run is bit-identical apart from the feedback. A glacier-free world
   skips pass 2 and pays nothing. OceanStage is part of the contiguous tail and so
   re-runs too: it reproduces identical ocean tiles (a pure function of elevation
   and sea level) but must re-run because it co-owns `waterDepth` with the lakes
   `PrecipitationStage` recomputes under the colder feedback climate.

Two feedbacks fire in pass-2 `AtmosphereStage`:
- **Elevation lapse:** the lapse rate acts on the ICE SURFACE (`elevation +
  iceThickness`), so a thick ice sheet's surface reads ~13 C colder (the
  elevation-mass-balance feedback). Rigorous, no new constant. Stabilizes/thickens
  existing ice.
- **Albedo:** reflective ice/snow runs colder, a damped Stefan-Boltzmann response
  `dT = -kAlbedoDamp · T/(4(1-a0)) · (a - a0)` (`a0 = 0.30`, `a_ice = 0.65`,
  `kAlbedoDamp = 0.40`, capped at `kMaxAlbedoCoolC = 20 C`). The damping stands in
  for the horizontal heat transport this per-tile balance omits; the cap keeps the
  fixed two-pass bounded. Cools snowy/icy tiles that are not yet glaciers, so it is
  the *expanding* feedback.

---

## Land/ocean thermal contrast (the polar-land fix)

Root cause of "no land ice on Earth-like worlds under a real melt model": the
baseline temperature curve `T(lat)` gave polar LAND an ocean-mild mean (~-14 C),
so its summers stayed above freezing and nothing seeded ice. Real cold continental
interiors (Antarctica, Siberia) radiate hard through the polar night and run far
colder.

`AtmosphereStage` now cools high-latitude land below same-latitude ocean:
`dT = -kPolarLandCoolC · sin⁴lat · (kPolarCoolCoastFrac + (1-kPolarCoolCoastFrac)·distNorm)`
(`kPolarLandCoolC = 16 C` at the pole interior, `kPolarCoolCoastFrac = 0.4`). This
is **heat redistribution, not net cooling**: the total is summed serially and added
back uniformly (`polarCoolComp`) so the global mean (the energy balance) is
unchanged. A side effect is that polar oceans stay warmer than polar land and so
carry a slightly smaller sea-ice cap, which is realistic (the Southern Ocean).

The latitude weight is `sin⁴`, not `sin²`: the polar-night radiative decoupling is a
genuinely high-latitude effect, so the cooling must stay concentrated poleward of
~60°. `sin²` bled ~8 C into 45° continental interiors and dragged the tundra belt far
equatorward of Earth's (the EarthLike across-seed mean ArcticTundra rose to ~19%);
`sin⁴` holds full strength at the pole while halving the cooling by 60° and quartering
it by 45°, so the caps still grow ice and the tundra belt pulls back to ~17.6%.

This is what makes polar continental summers fall below freezing so ice sheets form
where Earth has them; the feedback above then consolidates them.

---

## Determinism & performance

- All transcendentals go through `foundation::det_math` (cross-platform
  bit-identical) plus a local deterministic normal-CDF on `det_math::exp`; geodesic
  distances use chord length (sqrt only). `acos` is `π/2 - asin`.
- Parallel passes are pure per-tile or read a completed snapshot of a prior pass.
  The glacier distance transform is single-threaded Dijkstra. `worldHash` covers
  `iceThickness`/`iceFlow`; the across-thread-count determinism tests are the gate.
- Cost: the SMB pass is ~12 transcendentals over land tiles; the Dijkstra is
  O(N log N) over the ice region only (worst case a fully-glaciated cold world).
  The feedback adds one climate-tail re-run, only when land ice exists.

---

## Rendering (`libs/planet-view/PlanetTileColor.cpp`)

Ice is a render-time **overlay** (`applyIceOverlay`), never a biome, so Biome mode
and the histograms stay faithful. Wired into Terrain, Combined, and the Hydrology
ocean branch.

- **Solid ice** (sea ice or glacier): opaque, hard-edged, replaces the base. Thin
  ice reads pale icy blue `(206,226,240)`; a thick ice sheet reads bright white
  `(242,247,252)`, interpolated by `iceThickness/800`.
- **Permanent snow**: near-opaque white snowfield (`op = 0.85 + 0.15·cov`) so caps
  read solid. (Seasonal see-through snow is future.)
- Cold biomes were repaletted paler (`ColorMaps.h`) so Biome mode also shows pale
  poles.

`ColorMode::Ice` (right-click cycle) is the analyst view: sea-ice + glacier
thickness on a log ramp. WorldStats / the CLI report `seaIceFractionOfOcean`,
`frozenOceanFraction`, `iceCapMinLatitudeDeg`, `maxIceThicknessM`,
`glacierLandFraction`, `iceSheetLandFraction`. The landing details pane shows
sea-ice / snow / ice-sheet-thickness rows.

---

## Honest simplifications

- **Steady-state equilibrium**, not time-stepped flow; no planet-age dependence.
- Not modeled: marine/floating ice shelves, isostatic bed depression under ice
  load, basal sliding, ablation-zone overhang past the equilibrium line (margin =
  the SMB=0 boundary).
- The albedo feedback magnitude (`kAlbedoDamp`) and the land/ocean cooling
  (`kPolarLandCoolC`) are calibrated, physically-motivated parameters, not derived
  from first principles.
- **Climate-warmth caveat:** even with all of the above, an Earth-like preset grows
  ~4-5% of land ice (Earth is ~10%). The remaining gap is the climate model's
  coarse poles (large polar seasonal swing, no full multi-millennium feedback
  equilibrium), not the ice model. Colder presets glaciate heavily; the feedback
  responds to climate.

---

## Sources

- PDD model + degree-day factors (snow ~3, ice ~8 mm w.e./C/day) and the seasonal
  integral: Calov & Greve (2005), J. Glaciol.; antarcticglaciers.org.
- Perfect-plastic / Nye profile, τ0 ~ 100 kPa validated to Greenland: Nye (1951);
  Vialov (1958); Cuffey & Paterson, *The Physics of Glaciers*.

## Code

- `libs/world/worldgen/stages/SnowStage.cpp` — permanent snow + sea ice
- `libs/world/worldgen/stages/GlacierStage.cpp` — PDD mass balance + perfect-plastic
- `libs/world/worldgen/stages/AtmosphereStage.cpp` — lapse/albedo feedback + land/ocean contrast
- `libs/world/worldgen/pipeline/PlanetGenerator.cpp` — two-pass runner
- `libs/planet-view/PlanetTileColor.cpp` — ice/snow rendering + Ice mode
- See also `docs/technical/planet-view-rendering.md` (globe renderer).

---

## Season system (future)

Permanent snow / glaciers here are the *year-round* baseline. A planned season
slider will let the player view and select the time of year, driven by orbit
mechanics (axial tilt + orbital position). Seasonal snow cover ("how much snow is
on the ground" right now, distinct from glaciers) should be **dynamic and computed
at view/sim time, not baked into worldgen** — changing the season must not require a
full planet regeneration. See the planned epic in `docs/status.md`.
