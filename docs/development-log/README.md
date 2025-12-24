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

### 2025

#### December 2025

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

- **Total Entries:** 7
- **Latest Entry:** 2025-12-24
- **Major Milestones:**
  - FocusManager CRTP simplification (2025-12-24)
  - Vector graphics research complete (2025-10-24)
  - Multiplayer architecture defined (2025-10-24)
  - Colonist systems designed (2024-12-04)
