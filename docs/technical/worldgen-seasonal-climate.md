# Worldgen Seasonal Climate

**Status:** Draft (planned, not implemented)
**Created:** 2026-11-17
**Epic:** Seasonal climate samples in worldgen (see Specboard)
**Related:** [season-system.md](../design/features/season-system.md), [cryosphere-ice-and-glaciers.md](./cryosphere-ice-and-glaciers.md), [generation-phases.md](../design/features/world-generation/generation-phases.md) phases 4 and 5

## Problem

Worldgen stores climate as annual summaries: `temperatureMean`, `temperatureRange` (a cosine half-amplitude) and `precipitation` (mm/yr). Everything downstream that cares about *when* things happen has to guess from those three numbers:

- Rivers have one discharge (`flowAccum`), so there is no difference between a river that runs all year, one that floods every spring from snowmelt, and a wadi that is dry ten months of the year.
- Precipitation has no season, so a monsoon savanna and an evenly wet grassland with the same annual total look identical to the biome stage.
- Growing season length (the most important number a colony sim can show for a landing site) is not computed at all.
- The season system (WOR-50, WOR-771) has to reconstruct the year from a single cosine.

## Goal

Worldgen computes climate at **S seasonal samples across the planet's year** and stores enough per tile for the biome stage, rivers, landing-site rating and the in-game season system to read real seasonality.

## Non-Earth requirement

This is the hard constraint. Planets will be far from Earth-like, so nothing here may assume 12 months, a 365-day year, 23.4 degrees of tilt or Earth's seasons.

- The year is the planet's orbital period from `PlanetParams` (semi-major axis, star mass). Samples are evenly spaced in orbital phase, not calendar months. S is a fixed generation constant (start at 12 for storage sizing; it is a count of samples, not "months").
- Insolation per sample comes from axial tilt, orbital phase and eccentricity. Tilt is not in `PlanetParams` today; WOR-50 owns the orbit model and adds it. This epic consumes WOR-50's insolation function rather than writing a second one (one path rule). Zero tilt with a circular orbit must produce no seasons; high tilt (Uranus-like) must produce polar day/night extremes; high eccentricity must produce a global hot/cold season independent of latitude.
- Thermal inertia (how far temperature lags and damps insolation) depends on ocean fraction, distance to ocean and atmosphere strength, the same continentality inputs AtmosphereStage already uses.
- Seasonal precipitation shifts come from the wind cells moving with the subsolar latitude (ITCZ migration), derived from tilt, not from a fixed Earth monsoon band.
- Acceptance is judged across all six presets (EarthLike, DesertWorld, OceanWorld, FrozenWorld, VolcanicWorld, AncientGarden) plus extreme-tilt and high-eccentricity test planets, by internal consistency and the globe view, never by matching Earth observations.

## Design

### Per-sample climate (AtmosphereStage, PrecipitationStage)

- AtmosphereStage computes temperature per tile per sample: the existing mean model plus a seasonal term from the sample's insolation anomaly, damped and lagged by thermal inertia. The existing `temperatureMean` and `temperatureRange` become derived summaries of the samples (mean, and half of max minus min) so current consumers keep working unchanged.
- PrecipitationStage runs its moisture sweep per sample with that sample's wind-cell edges. Annual `precipitation` becomes the sum of the samples.
- Cost: the sweep is serial. S sweeps is S times the stage cost; the spike task measures it at n=2048 and decides whether the sweep runs at full resolution per sample or on a coarser grid with per-tile scaling.

### Stored fields

Storing S floats per tile per field at n=2048 is too large for the planet file. Store compact derived products instead, each a few bytes per tile:

| Field | Meaning |
|---|---|
| `temperatureSeasonPhase` | orbital phase of the warmest sample (lets the season system recompute any date with the right lag) |
| `precipSeasonality` | 0..1 concentration of rain in the wettest part of the year |
| `precipPeakPhase` | orbital phase of the wettest sample |
| `growingSeasonSamples` | number of samples warm enough and wet enough to grow (thresholds below) |
| `riverRegime` | enum: Permanent, PermanentWithFloodSeason, Seasonal, Ephemeral |
| `floodPhase` | orbital phase of peak discharge, for Permanent-with-flood and Seasonal rivers |

PlanetIO format version bumps; the quickstart planet re-bakes.

### Growing season

A sample counts as growing when mean temperature is above a growth threshold and water supply (rain plus snowmelt plus a fixed soil-water bucket carried between samples) exceeds about half of potential evaporation. Thresholds live in one constants header with their reasoning. They describe what Earth-like plants need; when flora for other biochemistries arrives, the thresholds move onto the flora/biome definitions, not into worldgen branches.

### River regimes

Discharge per sample = upstream runoff for that sample, where runoff is rain minus evaporation plus snowmelt (snow accumulates in samples below freezing and melts in the first warm ones) plus slow groundwater release (a per-tile reservoir with a fixed drain rate, so rivers do not drop to zero the moment rain stops). Classify each river tile by its minimum-to-mean and peak-to-mean discharge ratios. Discharge per sample is computed during the drainage pass and only the regime and flood phase are stored.

### Biomes

BiomeStage gains precipitation seasonality as an input: strong seasonality pushes forest toward seasonal forest and savanna; aseasonal wet pushes toward rainforest. This replaces any latitude proxies for "monsoon" now in BiomeStage.

## Consumers

- **Season system (WOR-50, WOR-771):** reads `temperatureSeasonPhase` and the samples' amplitude instead of assuming a pure cosine with zero lag. The "What already exists" section of season-system.md is updated when this lands.
- **Landing site rating (WOR-369):** growing season and river regime are the headline numbers ("Growing season: 5 of 12 parts of the year", "River floods in late spring").
- **2D rivers and ponds:** `RiverNetwork2D` and `PondNetwork2D` read the regime to size channels and decide whether an ephemeral bed draws as water or dry sand for the current date.

## Out of scope

- In-game weather events (storms, droughts). This is climate, not weather.
- Year-to-year variability.
- Ocean currents. Coastal temperature still comes from continentality only.

## Open questions

- S: 12 samples is enough to place a flood season. Fewer (8) may be enough and halves cost. The spike decides.
- Whether the per-sample sweep needs full resolution (see Cost).
