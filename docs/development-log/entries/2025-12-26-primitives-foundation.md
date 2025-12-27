# 2025-12-26 - Main Game UI: Primitives Foundation

## Summary

Added two foundational UI primitives to `libs/ui/`: ProgressBar (generic progress bar) and ScrollContainer (scrollable viewport with scrollbar). Also refactored NeedBar to use ProgressBar internally.

## Details

### ProgressBar Component

**Location:** `libs/ui/components/progress/`

Created a generic progress bar with:
- Normalized 0.0-1.0 value range (industry standard)
- Configurable fill color (any color, not value-based gradient)
- Optional label support (label left, bar right layout)
- Full layout API integration (margin, getWidth/getHeight)
- 10 unit tests covering all scenarios

**Key API:**
```cpp
ProgressBar bar({
    .size = {100.0F, 12.0F},
    .value = 0.75F,
    .fillColor = Theme::Colors::statusActive,
    .label = "Health",      // Optional
    .labelWidth = 50.0F,    // If label provided
});
bar.setValue(0.5F);         // 0.0 to 1.0, clamped
bar.setFillColor(Color::red());
```

### NeedBar Refactor

**Location:** `apps/world-sim/scenes/game/ui/components/NeedBar.h/.cpp`

NeedBar is now a thin wrapper around ProgressBar:
- Keeps the same 0-100 API for backwards compatibility
- Internally converts to 0-1 for ProgressBar
- valueToColor() gradient logic stays in NeedBar
- Calls `progressBar.setFillColor(valueToColor(value))` on each setValue

### ScrollContainer Component

**Location:** `libs/ui/components/scroll/ScrollContainer.h/.cpp`

Created a scrollable viewport that encapsulates:
- Clipping: Content masked to viewport bounds (uses Container's setClip)
- Content offset: Children scroll within viewport (uses Container's setContentOffset)
- Mouse wheel handling: Scroll events update position
- Scrollbar: Visual track + draggable thumb
- Click-on-track: Jump to position
- Auto content height detection from LayoutContainer child

**Key API:**
```cpp
ScrollContainer scroll({
    .position = {50.0F, 95.0F},
    .size = {200.0F, 200.0F},  // Viewport size
});
scroll.addChild(LayoutContainer{...});  // Content can be taller
scroll.scrollTo(100.0F);                // Absolute position
scroll.scrollBy(-50.0F);                // Relative scroll
```

### ScrollScene Demo

**Location:** `apps/ui-sandbox/scenes/ScrollScene.cpp`

Added demo scene showing:
1. Basic ScrollContainer with list items
2. ScrollContainer + LayoutContainer + Buttons
3. ProgressBar showcase with different colors and labels

## Files Created/Modified

**New Files:**
- `libs/ui/components/progress/ProgressBar.h`
- `libs/ui/components/progress/ProgressBar.cpp`
- `libs/ui/components/progress/ProgressBar.test.cpp` (10 tests)
- `libs/ui/components/scroll/ScrollContainer.h`
- `libs/ui/components/scroll/ScrollContainer.cpp`
- `libs/ui/components/scroll/ScrollContainer.test.cpp` (15 tests)
- `apps/ui-sandbox/scenes/ScrollScene.cpp`

**Modified Files:**
- `libs/ui/CMakeLists.txt` - Added new source files
- `libs/ui/theme/Theme.h` - Added scrollbar colors (scrollbarTrack, scrollbarThumb, scrollbarThumbActive)
- `libs/ui/components/button/Button.h/.cpp` - Added setPosition() override to fix text positioning in layouts
- `apps/ui-sandbox/CMakeLists.txt` - Added ScrollScene.cpp
- `apps/ui-sandbox/SceneTypes.h` - Added Scroll to scene list
- `apps/world-sim/scenes/game/ui/components/NeedBar.h` - Refactored to use ProgressBar
- `apps/world-sim/scenes/game/ui/components/NeedBar.cpp` - Refactored implementation

## Design Decisions

1. **ProgressBar uses normalized 0-1 values** - Industry standard, more flexible than NeedBar's 0-100
2. **ScrollContainer inherits from Container** (not LayoutContainer) - Gets clipping + offset, users can nest LayoutContainer as child
3. **Scrollbar styling is internal** - Uses constants/Theme colors, not configurable per-instance
4. **Vertical-only scroll initially** - Horizontal can be added later when needed

## Related Documentation

- Plan file: `~/.claude/plans/frolicking-pondering-flamingo.md`
- Design spec: `/docs/design/main-game-ui-design.md` (Section 17)

## Next Steps

- Main Game UI: Complex Components (Tree View, Dropdown, Toast) - now unblocked
- Main Game UI: Interaction Components (Modal, Tooltip, Context Menu) - now unblocked
