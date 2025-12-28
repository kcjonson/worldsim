# EntityInfoView Rebuild: Two-Column Layout

**Date:** 2025-12-28
**Status:** Complete
**PR:** #95

## Summary

Rebuilt the EntityInfoView panel with a unified two-column layout for colonists and improved slot-based architecture. Added three-tier update system for performance optimization, header section with portrait/mood bar, and Details button support.

## What Was Accomplished

### Two-Column Layout for Colonists
- Header area with portrait placeholder (64×64), name, and compact mood bar
- Left column: Task/Action status
- Right column: "Needs:" section header + 8 need bars
- Fixed height calculation: 280px for consistent panel sizing

### Single-Column Layout for Other Entities
- Centered icon placeholder (48×48) with label below
- Remaining slots rendered below icon/label area
- Used for items, flora, fauna, crafting stations

### Three-Tier Update System
- **Visibility tier**: O(1) toggle when selection changes to/from NoSelection
- **Structure tier**: Full relayout when different entity selected
- **Value tier**: O(dynamic) update only for progress bars when same entity
- Significant performance improvement by skipping layout calculations for value-only updates

### New Slot Types
- `IconSlot`: Centered icon with label (for entity icons)
- Extended slot rendering with xOffset and maxWidth parameters for column layouts

### Header Mood Bar
- Uses NeedBar component internally for consistent color gradient
- Compact 8px height (vs 16px for need bars)
- Mood percentage label to the right ("72% Content")

### Details Button
- Added [Details] button in header area for colonists
- Button only renders when `onDetails` callback is provided
- Prepares for future Colonist Details Modal epic

## Files Modified

**Key Files:**
- `EntityInfoView.h/cpp` - Complete rebuild with two-column layout
- `EntityInfoModel.h/cpp` - Added UpdateType enum, removed unused callbacks
- `SelectionAdapter.h/cpp` - Added header content for colonist/world entities
- `NeedBar.h/cpp` - Made height/labelWidth const, added size variants
- `InfoSlot.h` - Added IconSlot, ColonistHeader struct
- `GameUI.cpp` - Removed unused onTaskListToggle

**Constants Added:**
- `kPortraitSize`, `kEntityIconSize`, `kHeaderMoodBarWidth/Height`
- `kButtonGap`, `kIconLabelGap`, `kHeaderMoodBarOffset`, `kBorderWidth`
- `kMoodLabelFontSize`, `kLeftColumnWidth`, `kColumnGap`

## Technical Decisions

### PanelContent.layout Field
Added `PanelLayout` enum (SingleColumn/TwoColumn) to PanelContent. The view dispatches to `renderSingleColumnLayout()` or `renderTwoColumnLayout()` based on this field. Adapters set the layout based on selection type.

### Slot Architecture
Kept the slot-based approach but enhanced it with column support. Slots can now specify xOffset and maxWidth for flexible positioning within columns.

### NeedBar Component Reuse
The header mood bar uses NeedBar (with Compact size) internally. This ensures consistent color gradient logic (red→yellow→green) without duplicating the valueToColor calculation.

### String Allocation Optimization
Applied reserve/append pattern for string concatenation in hot paths (updateValues, renderTextSlot, renderTwoColumnLayout) to avoid temporary allocations.

## PR Review Feedback Addressed

- Made NeedBar.height/labelWidth const with initializer list
- Used needLabel() helper for bounds-checked need type lookup
- Removed entire unused onTaskListToggle callback chain (7 files)
- Extracted 12+ magic numbers to named constants
- Computed mood label vertical offset programmatically
- Consolidated TODO comments into NOTE
- Clarified kHeaderMoodBarHeight differs from kCompactHeight

## Next Steps

- Wire up `onDetails` callback when Colonist Details Modal is implemented
- Add portrait rendering (currently placeholder rectangle)
- Populate item/flora info with actual entity data from ECS
