# Worldgen Geology: Rock Types and Resource Deposits

**Status:** Draft (planned, not implemented)
**Created:** 2026-11-17
**Epic:** Tectonic rock types and resource deposits (see Specboard)
**Related:** [generation-phases.md](../design/features/world-generation/generation-phases.md) phase 1, `libs/worldgen/worldgen/tectonics/`, [crafting.md](../design/game-systems/world/crafting.md)

## Problem

The tectonic history already knows a great deal about each tile: crust type, `crustAge`, `orogenyAge`, volcanism (arc and hotspot), rifts and sutures. None of it reaches gameplay. When resources such as stone, ore and clay are placed, they will otherwise be scattered by noise and biome, which makes prospecting meaningless: there is no reason to look in one place over another.

## Goal

Worldgen derives a **rock type** and a set of **deposit richness** values per tile from the tectonic history, so where things are found follows from the planet's geological story. A player who learns "old mountain belts carry metal" can use it on every planet.

## Non-Earth requirement

Deposit rules are written in terms of processes (arc magmatism, rifting, long-term erosion, sedimentary basins), not Earth's mineral list. Which concrete materials a process produces is **data**: a deposit-class table in assets maps each process class to material defs, so an exotic planet or a mod can swap materials without touching worldgen code. Worldgen emits classes and richness; the asset layer names the stuff.

## Rock type (one byte per tile)

Classified from the history, in priority order:

| Class | From |
|---|---|
| Volcanic (young) | volcanism accumulator high, crust young |
| Volcanic (old) / flood basalt | hotspot or large-igneous events, old |
| Granitic / plutonic | eroded old arc and orogen cores (orogeny old, high past thickening) |
| Metamorphic | suture zones and old collisional belts |
| Sedimentary | low-lying continental crust with long drainage accumulation, foreland basins |
| Oceanic basalt | oceanic crust (seafloor, and any obducted slivers) |
| Glacial / unconsolidated | cover where GlacierStage reports past or present ice |

Fed into the biome and ground layers so rock outcrops at landing scale match (granite tors vs sandstone vs basalt).

## Deposit classes (a few bytes per tile, richness 0..255 each)

| Process class | Where | Typical Earth-like materials (data, not code) |
|---|---|---|
| Arc magmatic | over subduction arcs, active and old | copper, gold, silver |
| Orogenic | old suture and collision belts | gold, metals in veins |
| Rift / basin | failed rifts, inland basins | salts, evaporites, oil-like fuels |
| Craton / ancient crust | oldest stable continental interiors | iron, rare high-value deposits |
| Sedimentary / alluvial | river deltas, floodplains downstream of orogens | clay, sand, placer metals |
| Volcanic | hotspots, young volcanic fields | sulfur, obsidian, geothermal |

Richness is smooth over tiles, with sparse high peaks so there are discoverable "good spots". Every class has a seeded per-planet stream.

## Storage and access

- `WorldData` gains `rockType` (u8) and a fixed-size deposit richness array (u8 per class). PlanetIO version bumps.
- `PlanetSampler` exposes rock type and richness at a world position; chunk generation and placement read them.
- A globe color mode per deposit class for the world creator, and the landing-site rating lists the best nearby deposits.

## Out of scope

- Mining gameplay, prospecting tools, deposit depletion. This epic only places the data.
- Underground layers or 3D geology.

## Reference

The idea of a geology layer derived from tectonic history (rock types, deposit masks, resource summaries) comes from World Orogen (orogen.studio, GPL-3.0). Ideas only; no code is copied.
