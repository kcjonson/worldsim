# 2024-12-04 - Documentation Reorganization

## Summary

Major reorganization of the `docs/` directory to address duplicate information, conflicts, and structural issues. Consolidated content, created single sources of truth, and established new documentation processes.

## Changes Made

### Structural Changes

**Merged `design/mechanics/` and `design/systems/` into `design/game-systems/`:**
- Topic-based organization: `colonists/`, `world/`, `features/`
- Clear separation of concerns within each topic

**Created development log entry system:**
- `development-log/README.md` as index
- `development-log/entries/` for individual dated entries
- Old monolithic log split into separate files

### New Files Created

| File | Purpose |
|------|---------|
| `design/mvp-scope.md` | Single source of truth for MVP scope |
| `technical/library-decisions.md` | Single source of truth for library choices |
| `development-log/README.md` | Development log index and process docs |
| `workflows.md` | Updated with documentation workflows |

### Consolidated Files

**Colonist-related docs merged:**
- `mechanics/colonists.md` (static attributes) → `game-systems/colonists/attributes.md`
- `mechanics/colonists.md` (needs/mood) + `systems/needs-system.md` → `game-systems/colonists/needs.md`
- `systems/colonist-ai.md` → `game-systems/colonists/ai-behavior.md`
- `systems/colonist-memory.md` → `game-systems/colonists/memory.md`
- `systems/work-priorities.md` → `game-systems/colonists/work-priorities.md`

**Removed duplicate MVP scope sections from:**
- colonist-ai.md
- needs-system.md
- colonist-memory.md
- work-priorities.md
- player-control.md
- entity-capabilities.md

All now reference central mvp-scope.md.

### Files to Delete (from old structure)

After migration is complete:
- `docs/technical/vector-asset-pipeline.md` (duplicate of `vector-graphics/asset-pipeline.md`)
- `docs/design/mechanics/` folder (merged into `game-systems/`)
- `docs/design/systems/` folder (merged into `game-systems/`)

## Issues Resolved

### Duplicates Fixed

1. ✅ Mood/breakdown thresholds — now only in `needs.md`
2. ✅ MVP scope sections — now only in `mvp-scope.md`
3. ✅ Basic needs list — now only in `needs.md`
4. ✅ Vector asset pipeline — single file in `vector-graphics/`

### Conflicts Resolved

1. ✅ Memory budget — using ~350 MB as canonical value
2. ✅ mechanics/ vs systems/ boundary — merged into `game-systems/`

### Deferred for Later

1. ⏳ Entity capabilities vs ECS components — marked as TODO in docs
2. ⏳ Work categories finalization — noted as work in progress

## Documentation Process Established

New process documented in `workflows.md`:

1. **Development log entries:** Create individual files in `entries/` folder
2. **MVP references:** Always link to central mvp-scope.md
3. **Historical addendums:** Preserve old content when consolidating

## Related Documentation

- [Documentation Audit Report](../docs-audit-report.md) — Full analysis that led to this reorganization

## Files Modified

All files in the new `docs-reorganized/` structure are new or significantly modified from originals.
