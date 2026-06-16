# Development Log

This directory contains detailed records of implementation work, organized as individual entry files.

---

## How to Use This Log

### Reading Entries
Each entry is a separate file in `entries/` named by date and topic:
- `YYYY-MM-DD-topic-name.md`

Entries are listed below in reverse chronological order (newest first).

### Adding New Entries

When completing significant work:

1. **Create a new entry file** in `entries/`:
   ```
   entries/2025-01-15-feature-name.md
   ```

2. **Use this template:**
   ```markdown
   # [Date] - [Title]
   
   ## Summary
   Brief description of what was accomplished.
   
   ## Details
   - What was built/changed
   - Technical decisions made
   - Files created/modified
   
   ## Related Documentation
   - Links to specs created/updated
   - Links to relevant technical docs
   
   ## Next Steps
   What should happen next (if applicable)
   ```

3. **Add entry to this index** (below, newest first)

4. **Update status.md** with current project state

### What Goes Here vs. Other Docs

| Content Type | Location |
|--------------|----------|
| **What was built** (historical record) | Development log entries |
| **How something works** (current design) | Technical docs |
| **What to build** (requirements) | Design docs |
| **Current project state** | status.md |

Development log entries are **immutable history**. Don't update old entries — create new ones if things change.

---

## Entry Index

### 2026

#### June 2026

- [2026-06-15 - Fluvial erosion (stream-power valley carving)](./entries/2026-06-15-worldgen-fluvial-erosion.md)
- [2026-06-15 - Water availability + plate-boundary realism](./entries/2026-06-15-worldgen-water-and-plate-realism.md)
- [2026-06-14 - Construction Epic F1: openings (doors & windows)](./entries/2026-06-14-construction-epic-f1-openings.md)
- [2026-06-14 - Construction Epic E: rooms](./entries/2026-06-14-construction-epic-e-rooms.md)
- [2026-06-14 - Climate, biome, and shelf realism retune](./entries/2026-06-14-climate-biome-shelf-retune.md)
- [2026-06-13 - Construction Epic D: walls](./entries/2026-06-13-construction-epic-d-walls.md)
- [2026-06-13 - Tectonic history simulation](./entries/2026-06-13-tectonic-history-simulation.md)
- [2026-06-13 - Construction Epic C: foundations end-to-end](./entries/2026-06-13-construction-epic-c.md)
- [2026-06-12 - Geometry Foundations (libs/geometry)](./entries/2026-06-12-geometry-foundations.md)
- [2026-06-12 - Goal-Driven Task Generation (core)](./entries/2026-06-12-goal-driven-task-generation.md)
- [2026-06-12 - Building, Pathfinding, and Vision Specs](./entries/2026-06-12-building-pathfinding-vision-specs.md)
- [2026-06-12 - Goldberg hex grid + two-tier planet rendering](./entries/2026-06-12-worldgen-hex-goldberg.md)

### 2025

#### December 2025

- [2025-12-26 - Main Game UI: Primitives Foundation](./entries/2025-12-26-primitives-foundation.md)
- [2025-12-24 - FocusManager Simplification (CRTP Pattern)](./entries/2025-12-24-focusmanager-simplification.md)

#### October 2025

- [2025-10-29 - Vector Graphics Validation Plan Updates](./entries/2025-10-29-vector-graphics-validation.md)
- [2025-10-27 - RmlUI OpenGL Implementation Guide](./entries/2025-10-27-rmlui-opengl-guide.md)
- [2025-10-24 - Vector Graphics System Research](./entries/2025-10-24-vector-graphics-research.md)
- [2025-10-24 - Networking & Multiplayer Architecture](./entries/2025-10-24-multiplayer-architecture.md)

### 2024

#### December 2024

- [2024-12-04 - Colonist AI & Behavior Systems Design](./entries/2024-12-04-colonist-systems.md)
- [2024-12-04 - Documentation Reorganization](./entries/2024-12-04-docs-reorganization.md)

---

## Quick Stats

- **Total Entries:** 71 (entry files; the index below lists notable milestones)
- **Latest Entry:** 2026-06-15
- **Major Milestones:**
  - Fluvial erosion: stream-power valley carving so rivers land in valleys (2026-06-15)
  - Water availability + plate-boundary realism: real drainage, landing water signal, curved rift cuts (2026-06-15)
  - Construction Epic F1: openings — doors & windows (2026-06-14)
  - Construction Epic E: rooms (2026-06-14)
  - Climate/biome/shelf retune: Earth-like biome fractions + rain shadows (2026-06-14)
  - Construction Epic D: walls (2026-06-13)
  - Tectonic history simulation: realistic continents and mountains (2026-06-13)
  - libs/geometry exact-geometry library (2026-06-12)
  - Goal-driven task generation core (2026-06-12)
  - Goldberg hex grid + two-tier planet rendering (2026-06-12)
  - UI Primitives Foundation (ProgressBar, ScrollContainer) (2025-12-26)
  - FocusManager CRTP simplification (2025-12-24)
  - Vector graphics research complete (2025-10-24)
  - Multiplayer architecture defined (2025-10-24)
  - Colonist systems designed (2024-12-04)
