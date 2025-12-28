# Main Game UI: Information Systems

**Date:** 2025-12-28
**Status:** Complete
**PR:** #94

## Summary

Added resources panel and notifications system to the main game UI. Also fixed a z-order rendering bug in TopBar and GameplayBar where backgrounds were rendering on top of their children.

## What Was Accomplished

### Resources Panel
- Created `ResourcesPanel` component in `apps/world-sim/scenes/game/ui/views/`
- Collapsible button showing "Storage ▼" / "Storage ▲"
- Empty state message when expanded ("No stockpiles built...")
- Positioned in top-right, below zoom controls (Y=120)
- Uses Component's `addChild()` pattern for proper layering

### Notifications System
- Integrated existing `ToastStack` component into `GameUI`
- Added `onClick` callback to `Toast` for click-to-navigate functionality
- Added convenience overload to `ToastStack::addToast()` with onClick parameter
- New `GameUI::pushNotification()` API for game systems to show notifications
- Deleted old `NotificationManager.h` (replaced by ToastStack)
- Position: bottom-right, above gameplay bar

### Z-Order Fix
- Removed explicit `zIndex` from TopBar and GameplayBar background rectangles
- Background was set to zIndex=500/400, causing it to render AFTER children with default zIndex=0
- Fix: rely on insertion order - backgrounds added first via `addChild()` render first (behind)
- Component's `render()` uses `stable_sort` by zIndex, preserving insertion order for equal values

## Files Modified

**New Files:**
- `apps/world-sim/scenes/game/ui/views/ResourcesPanel.h`
- `apps/world-sim/scenes/game/ui/views/ResourcesPanel.cpp`

**Modified Files:**
- `libs/ui/components/toast/Toast.h` - Added onClick callback
- `libs/ui/components/toast/Toast.cpp` - Handle onClick in MouseUp event
- `libs/ui/components/toast/ToastStack.h` - Added onClick overload
- `libs/ui/components/toast/ToastStack.cpp` - Implement onClick overload
- `apps/world-sim/scenes/game/ui/GameUI.h` - Added resourcesPanel, toastStack, pushNotification()
- `apps/world-sim/scenes/game/ui/GameUI.cpp` - Full integration
- `apps/world-sim/scenes/game/GameScene.cpp` - Updated notification calls
- `apps/world-sim/scenes/game/ui/views/TopBar.cpp` - Removed zIndex from background
- `apps/world-sim/scenes/game/ui/views/GameplayBar.cpp` - Removed zIndex from background

**Deleted Files:**
- `apps/world-sim/scenes/game/world/NotificationManager.h`

## Technical Decisions

### Z-Index vs Insertion Order
The Component base class sorts children by zIndex before rendering, using stable sort. This means:
- Equal zIndex values preserve insertion order
- Explicitly setting high zIndex on backgrounds caused them to render last (on top)
- Solution: don't set zIndex on backgrounds, let insertion order handle parent/child layering

### Empty State for Resources Panel
Stockpiles aren't implemented yet, so the resources panel shows an empty state message. The TreeView population will be added when the stockpile system is built.

## Deferred Work

- **Minimap** - Split into its own epic due to GPU complexity (render-to-texture optimization needed)
- **Resources TreeView** - Requires stockpile system implementation
- **Pin-to-always-show** - Can be added when resources panel has content

## Next Steps

Potential next epics:
- Main Game UI: Minimap
- Main Game UI: Management Screens
- Flora Content Pack
