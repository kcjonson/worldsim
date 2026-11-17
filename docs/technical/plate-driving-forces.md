# Plate Motion from Driving Forces

**Status:** Draft (planned, not implemented)
**Created:** 2026-11-17
**Epic:** Force-driven plate motion (see Specboard)
**Related:** [generation-phases.md](../design/features/world-generation/generation-phases.md) phase 1, `libs/worldgen/worldgen/tectonics/TectonicParams.h`, `PlateSim.h`

## Problem

Plate speed today is sampled, not derived: each plate draws a speed from Earth bands (`kOceanicSpeedMinCmYr` 4 to 10, `kContinentalSpeedMinCmYr` 1 to 4), a slab-pull multiplier scales it (M-T2.6), and `evolvePoles()` drifts Euler poles on a schedule with jitter (`kPoleEvolutionSpeedJitter`). This has two problems:

1. **It is Earth-anchored.** The bands are Earth's observed plate speeds. A young, hot planet or a high-gravity planet has no way to move faster or slower, and nothing lets a planet approach a stagnant lid.
2. **Motion is scripted.** Pole drift on a timer means plates turn because the clock says so, not because a collision locked them or a new trench opened.

## Goal

Each step, every plate's angular velocity comes from a balance of torques on that plate, so speeds and turns emerge from the boundaries the plate has.

## Model

Per plate, solve `D * omega = T` where:

- **T (driving torque)** is the sum, over the plate's boundary and base, of:
  - **Slab pull** at subducting edges, scaled by slab age (the existing age factor carries over) and by gravity.
  - **Ridge push** at spreading edges, scaled by the age contrast across the ridge and by gravity.
  - **Mantle drag** toward large-scale downwellings (where slabs sink) on continental keels.
- **D (resistance)** is basal drag (proportional to plate area, more for thick continental keels) plus a **collision lock** term at continent-continent boundaries that grows as the collision matures.

Speed then passes through a soft saturation, `v' = v / sqrt(1 + (v / S)^2)`, where S is a planet-derived soft cap, so no single strong slab produces runaway speeds while weak forces stay linear.

## Non-Earth requirement

The force scales come from `PlanetParams` and `DerivedPlanetValues`, not from Earth's observed speeds:

- Gravity scales slab pull and ridge push.
- Mantle vigor (a derived value from planet age, mass and, later, internal heating) scales the overall driving force and lowers basal drag on young hot planets.
- A planet whose mantle vigor is too low for slab pull to beat basal drag ends up nearly static: a stagnant-lid world, which is a legitimate result, not a failure. The world creator should then show very few active belts.

Earth's 1 to 10 cm/yr bands become a **check on the EarthLike preset** in WorldStats, not an input.

## What gets deleted

The sampled speed bands, the speed-age jitter, and the scheduled pole evolution (`evolvePoles` and its constants) are deleted in the same change. The slab-age factor moves into the slab-pull torque.

## Verification

- WorldStats gains plate-speed distribution and the fraction of plates with active slabs; EarthLike should land in Earth's ranges without being told them.
- All presets still pass the existing plate-size, belt-aspect and continent-shape gates (re-anchored if the new motion legitimately changes them).
- Determinism unchanged: single-threaded sim, `det_math` for every transcendental, golden hash re-pinned.

## Reference

Torque balance with basal drag, slab resistance and collision lock, and the soft speed saturation, follow World Orogen's Evolution model (orogen.studio, GPL-3.0), itself built on Forsyth and Uyeda (1975) and Conrad and Lithgow-Bertelloni (2002). Ideas only; no code is copied.
