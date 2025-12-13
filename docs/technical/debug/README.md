# Debug Investigation Archive

This directory contains historical debug investigation logs for reference. These documents capture the debugging process and learnings from complex multi-session bugs.

## Documents

| File | Issue | Resolution |
|------|-------|------------|
| [water-detection-investigation.md](./water-detection-investigation.md) | Colonist drinking from grass, flora spawning on water | Root cause: Lazy tile generation via noise led to mismatched data between systems. Fixed via Flat Tile Storage Refactor. |

## Purpose

These logs are preserved for:
1. **Historical context** - Understanding why certain architectural decisions were made
2. **Learning** - Patterns of investigation that led to root cause discovery
3. **Reference** - If similar symptoms reappear, these logs can guide diagnosis
