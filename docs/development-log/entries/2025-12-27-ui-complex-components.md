# Main Game UI: Complex Components

**Date:** 2025-12-27
**Epic:** Main Game UI: Complex Components
**Status:** Complete

## Summary

Built a suite of complex UI components for information-dense screens including icons, tree views, dropdown menus, select controls, and toast notifications. Total of 208 unit tests covering all components.

## What Was Accomplished

### Components Created

1. **Icon** (`libs/ui/components/icon/`)
   - SVG-based icon rendering with tinting
   - Tessellated mesh caching for performance
   - Resize without disk I/O (transforms cached vertices)
   - 11 tests

2. **Menu** (`libs/ui/components/menu/`)
   - "Dumb" visual component for rendering menu items
   - Parent controls visibility, hover highlighting
   - Disabled item styling
   - 26 tests

3. **Select** (`libs/ui/components/select/`)
   - Controlled form element (parent provides value)
   - Fires onChange when user selects different option
   - Uses Menu internally via addChild()
   - Keyboard navigation (Up/Down/Enter/Escape)
   - 23 tests

4. **DropdownButton** (`libs/ui/components/dropdown/`)
   - Action menu button with dropdown
   - Composes Button + Menu pattern
   - Focus integration for mutual exclusivity
   - 17 tests

5. **Toast + ToastStack** (`libs/ui/components/toast/`)
   - Notification popups with severity styling (Info/Warning/Critical)
   - Auto-dismiss timer or persistent with manual dismiss
   - Fade in/out animations
   - Stack container with vertical arrangement
   - 23 tests total

6. **TreeView** (`libs/ui/components/treeview/`)
   - Expandable/collapsible nodes with expand indicator (v/> symbols)
   - Nested hierarchy with indentation
   - Auto-height mode for integration with ScrollContainer
   - onExpand/onCollapse callbacks
   - 16 tests

### Demo Scenes

Added 4 new scenes to ui-sandbox:
- `IconScene` - Icon sizes, tinting, SVG loading
- `DropdownScene` - Select + DropdownButton demos
- `ToastScene` - Toast notifications with all severity levels
- `TreeViewScene` - Standalone + scrollable tree demos

### Bug Fixes (from PR feedback)

- TreeView auto-height mode (rows not showing when size.y = 0)
- ScrollContainer event coordinate transformation for children
- Lambda capture safety (capture values not indices)
- Icon vertex caching for list performance

## Technical Decisions

1. **Menu as Building Block** - Menu is a "dumb" visual component that just renders items and handles hover/click. Select and DropdownButton compose Menu via addChild() rather than duplicating rendering logic.

2. **Controlled vs Uncontrolled** - Select uses the controlled pattern (parent provides value, component fires onChange). This matches React conventions and makes state management predictable.

3. **Icon Vertex Caching** - Original tessellated vertices are cached after SVG load. Resize operations transform cached vertices instead of reloading from disk. Critical for lists with many icons.

4. **Auto-Height Mode** - TreeView calculates height from content when size.y = 0, enabling integration with ScrollContainer for variable-height trees.

## Files Modified

### New Files (31 total)
- `libs/ui/components/icon/` (Icon.h, Icon.cpp, Icon.test.cpp)
- `libs/ui/components/menu/` (Menu.h, Menu.cpp, Menu.test.cpp)
- `libs/ui/components/select/` (Select.h, Select.cpp, Select.test.cpp)
- `libs/ui/components/dropdown/` (DropdownButton.h, DropdownButton.cpp, DropdownButton.test.cpp)
- `libs/ui/components/toast/` (Toast.h, Toast.cpp, Toast.test.cpp, ToastStack.h, ToastStack.cpp)
- `libs/ui/components/treeview/` (TreeView.h, TreeView.cpp, TreeView.test.cpp)
- `apps/ui-sandbox/scenes/` (IconScene.cpp, DropdownScene.cpp, ToastScene.cpp, TreeViewScene.cpp)

### Modified Files
- `libs/ui/CMakeLists.txt` - Added new component directories
- `libs/ui/theme/Theme.h` - Added Toast and TreeView theme constants
- `libs/ui/component/Component.h` - Added LayerHandle and getChild() template
- `libs/ui/components/scroll/ScrollContainer.cpp` - Event coordinate transformation fix
- `apps/ui-sandbox/CMakeLists.txt` - Added demo scenes
- `apps/ui-sandbox/SceneTypes.h` - Registered new scenes

## Known Issues / Deferred

- **Z-index overlay issue**: Menus render behind other components because z-index is local context only. Proper fix requires an overlay/portal system.
- **Text measurement**: DropdownButton uses magic number for text width approximation. Proper text measurement API needed.

## Related Documentation

- Plan: `/docs/development-log/plans/2025-12-27-dropdown-menu-refactor.md`
- Spec: `/docs/design/main-game-ui-design.md` (Section 17)

## Next Steps

The following epics can now proceed:
- **Main Game UI: Interaction Components** - Modal, Tooltip, Context Menu
- **Main Game UI: Information Systems** - Minimap, Resources Panel (depends on TreeView)
