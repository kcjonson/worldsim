# Worldgen Lakes and Rivers: Physically Grounded Rules

**Status:** Draft (planned, not implemented)
**Created:** 2026-11-17
**Epic:** Lakes and rivers that follow the water budget (see Specboard)
**Related:** [generation-phases.md](../design/features/world-generation/generation-phases.md) phase 5C, [continental-margins.md](./continental-margins.md), `libs/worldgen/worldgen/sampling/RiverNetwork2D.h`, `PondNetwork2D.h`, `stages/DrainageRouting.h`

## Problem

Lakes come from depressions in the drainage and rivers are a flow-accumulation quantile (`kRiverLandFraction`). Neither knows whether there is enough water to do what it is drawn doing:

- A depression becomes a lake whether or not its inflow could fill it against evaporation.
- River channel width at landing scale is not tied to discharge.
- River paths follow steepest descent, so they can be either too straight or too jagged at landing scale depending on the grid.
- Shallow sills between a near-sea basin and the ocean stay closed, leaving odd dry basins below sea level or isolated seas a few metres from the coast.

## Goal

Lakes and rivers obey a water budget and simple geometric scaling laws, so their size and shape follow from the climate and terrain of the planet they are on.

## Non-Earth requirement

Every rule here is a balance between inflow and loss, so it works on any planet: a desert world gets few, salty, shrinking lakes; an ocean world's islands get short, steep rivers; a frozen world's lakes are bounded by melt, not rain. No rule may hard-code Earth discharge values. Constants are expressed as ratios (lake area per unit of inflow, width per root of discharge) with their physical reasoning, and absolute scales come from the planet's own precipitation and evaporation fields.

## Rules

### 1. Lakes must be filled

A closed depression becomes a lake only if its steady-state area, the area at which evaporation from the surface equals inflow (precipitation runoff from its catchment), reaches a minimum visible size. The lake's surface is then the level at which that area is reached, not the spill level:

- Inflow > evaporation at the spill level: the lake fills to the spill point and overflows; downstream river continues (an open lake).
- Inflow < evaporation at the spill level: the lake stops at the balancing level and becomes terminal (endorheic, and flagged saline for later gameplay).
- Balancing area below the minimum: no lake; the basin is a dry pan (playa) and the 2D layer may draw a seasonal or dry bed.

Lake minimum area scales with the square root of inflow, so a large river crossing a small dip does not spawn a lake, while a modest stream into a real hollow does.

### 2. Lakes sit in real hollows

Shallow numeric pits (a few metres, from noise or upsampling) are filled before lake detection; only depressions deeper than a planet-scaled tectonic threshold (relative to local relief) are lake candidates. Where a basin cannot fill and the river must cross its rim, the drainage cuts a gorge through the rim instead of ponding.

### 3. Shallow sills open to the sea

A below-sea-level basin separated from the ocean by a sill that is both shallow and narrow is opened as a strait. Thresholds are planet-scaled (in relation to sea-level variation from the cryosphere's lowstand, not fixed metres). Deep isolated basins stay inland seas. **Boundary with WOR-571:** WOR-571 owns how inland seas form (rifts, glacial depression, dynamic topography). This rule owns only whether an existing basin connects to the ocean, and runs after WOR-571's terrain is final.

### 4. Channel width from discharge

At landing scale (`RiverNetwork2D`), channel width is proportional to the square root of discharge (the standard hydraulic geometry exponent), with the coefficient chosen so the quickstart planet's main river keeps its current width. With the seasonal climate epic, width is set by bankfull (high-sample) discharge and the drawn water level by the current date's discharge.

### 5. River directness

At landing scale, rivers meander only when the detour is worth it: a channel takes a bend when the alternative straight path is steeper than the bend by a tunable ratio (the reference implementation used about 10x). This gives gentle sinuosity on plains and straight runs in steep country, from one parameter instead of noise amplitude tuning.

## Determinism

All rules run in the existing deterministic drainage order. Lake fill is a priority-flood with the balance test, single-threaded. worldHash changes; golden hashes re-pin.

## Out of scope

- Lake salinity as a resource or hazard (flag only; gameplay use is separate).
- Delta and estuary shapes.
- Groundwater as a modelled field (the seasonal-climate epic adds a simple reservoir for river baseflow only).

## Reference

Ideas from World Orogen (orogen.studio, GPL-3.0): inflow-scaled lake survival, tectonic-hollow requirement, sill opening, square-root valley width, and a single river-directness knob. Ideas only; no code is copied.
