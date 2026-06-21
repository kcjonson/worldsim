# Season System

**Status:** Draft (stub)
**Created:** 2026-06-21

## Overview

A season slider lets the player view and select the time of year. The slider drives real orbit mechanics (axial tilt plus orbital position), which set per-tile insolation and temperature for the chosen date. Seasonal snow cover, the snow on the ground right now, follows from that date's temperature and is computed dynamically at sim/view time. Moving the slider must not trigger a planet regeneration.

## The hard constraint: dynamic, not baked

Worldgen owns the year-round baseline: glaciers (real thickness and flow) and the permanent-snow snowfield. Those are frozen into the planet at generation time. See `docs/technical/cryosphere-ice-and-glaciers.md`.

The season system owns the thin, see-through layer on top: how much seasonal snow sits on the ground for a given date. This is derived state. It reads the date, the per-tile climate, and the worldgen baseline, and produces a coverage value. Changing the date recomputes this layer; it does not touch the planet. A full regen for a season change is the failure case this spec exists to prevent.

## Player experience

- A season slider in the globe/world view and in-game (placement TBD).
- Dragging it changes the date; the globe and the 2D chunk views update to show that date's snow and, where surfaced, temperature.
- The change is cheap and immediate. No loading, no regen.

## What already exists

The climate model already carries a seasonal temperature cycle per tile: `T(month) = T_mean_annual + A*cos(2*pi*month/12)`, where `A` is the seasonal half-amplitude (`temperatureRange`). The cryosphere's positive-degree-day model integrates over this cycle to decide where perennial ice can exist. The season system reuses this signal: instead of integrating the whole year, it samples a specific date.

## Functional requirements

1. Date to climate. Map the slider position to a date, and the date (plus axial tilt and orbital position) to per-tile insolation and temperature. Reuse the existing seasonal cycle where possible.
2. Dynamic seasonal snow. From the date's temperature, compute a per-tile seasonal snow coverage that accumulates and melts. It is separate from, and layered over, the worldgen permanent snow and glaciers. Decide where it lives (sim state vs render-time derivation) and what, if anything, is cached.
3. Regen-free consumption. The globe renderer and the 2D chunk renderer both read the seasonal layer for the current date without a planet regen. Define the read path and the invalidation on a date change.
4. Render. Draw seasonal snow as the thin, see-through layer the cryosphere doc explicitly defers, distinct from the opaque permanent snow and ice.

## Layering (who owns what)

| Layer | Owner | When computed |
|---|---|---|
| Glaciers (thickness, flow) | worldgen | generation |
| Permanent snow baseline | worldgen | generation |
| Seasonal snow (this date) | season system | sim/view time |
| Insolation / temperature by date | season system | sim/view time |

## Open questions

These are the decisions the stub does not make yet; they map to the epic's three "Spec:" tasks.

- Orbit fidelity: how much of axial tilt and orbital eccentricity do we simulate, and how does a date map to slider position (linear by day, or by solar declination)?
- Snow layer home: pure render-time function of (date, tile, baseline), or cached sim state with incremental accumulate/melt? Weigh memory against recompute cost at globe scale.
- Consumption: what does the globe need versus the 2D chunk, and how is the seasonal layer invalidated and recomputed on a slider move without stalling a frame?
- Reach: in v1, does season affect anything beyond snow and display (plant growth, water, colonist behavior), or is that out of scope?

## Dependencies

- Cryosphere / ice (landed) — provides the permanent-snow and glacier baseline this layers over.

## Out of scope (v1)

- Gameplay effects of season beyond visual snow and display, unless a question above promotes one.
- Fog of war, and any change to worldgen.

## References

- `docs/technical/cryosphere-ice-and-glaciers.md` — the "Season system (future)" section and the PDD/temperature model.
- Epic "Season System (season slider + dynamic seasonal snow)" on the Specboard Worldsim board.
