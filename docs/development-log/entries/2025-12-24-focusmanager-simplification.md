# 2025-12-24 - FocusManager Simplification (CRTP Pattern)

## Summary

Reduced ~180 lines of FocusManager registration boilerplate across 3 focusable UI components by introducing a CRTP base class `FocusableBase<T>`.

## Details

### Problem

Each focusable component (Button, TabBar, TextInput) required ~60 lines of nearly identical code:
- Constructor: register with FocusManager
- Destructor: unregister from FocusManager
- Move constructor: unregister old address, register new address
- Move assignment: same as move constructor
- `tabIndex` member variable

### Solution

Created `FocusableBase<T>` CRTP template that handles all FocusManager lifecycle automatically:

```cpp
template <typename Derived>
class FocusableBase : public IFocusable {
public:
    explicit FocusableBase(int tabIndex = -1);  // Auto-registers
    ~FocusableBase() override;                   // Auto-unregisters
    FocusableBase(FocusableBase&& other) noexcept;  // Re-registers at new address
    FocusableBase& operator=(FocusableBase&& other) noexcept;
protected:
    int focusTabIndex{-1};  // -2 = moved-from sentinel
};
```

Components now:
1. Inherit from `FocusableBase<ComponentName>`
2. Call `FocusableBase<T>(args.tabIndex)` in constructor
3. Use `= default` for destructor and move operations

### Bug Fix

During testing, discovered crash: "Pure virtual function called!" when ui-sandbox shut down.

**Root cause:** `FocusManager::unregisterFocusable()` called `onFocusLost()` on the component, but when called from `FocusableBase` destructor, the derived class is already destroyed. Calling virtual functions on partially-destroyed objects is undefined behavior.

**Fix:** Removed `onFocusLost()` call from `unregisterFocusable()`. When a component is being destroyed, it doesn't need a callback - just clear the focus pointer.

### Files Changed

- **New:** `libs/ui/focus/FocusableBase.h` - CRTP template
- **Modified:** `libs/ui/focus/FocusManager.cpp` - Removed unsafe onFocusLost call
- **Modified:** `libs/ui/focus/FocusManager.test.cpp` - Updated test expectations
- **Modified:** `libs/ui/components/button/Button.h/cpp` - Migrated to FocusableBase
- **Modified:** `libs/ui/components/tabbar/TabBar.h/cpp` - Migrated to FocusableBase
- **Modified:** `libs/ui/components/TextInput/TextInput.h/cpp` - Migrated to FocusableBase
- **Modified:** `docs/technical/ui-framework/focus-management.md` - Updated with new pattern

### Design Decision: Move Focus Preservation

Copilot review asked whether moves should preserve focus state. Decision: **No**.

Rationale:
- UI components are typically constructed in place (vectors use emplace_back)
- Components are rarely moved after construction
- Adding focus preservation doubles the complexity of move operations
- Documented as known limitation in header comment

## Related Documentation

- `/docs/technical/ui-framework/focus-management.md` - Updated with FocusableBase usage
- `/docs/technical/cpp-coding-standards.md` - Established `= default` move pattern

## Next Steps

None - epic complete. Future focusable components should follow the same pattern.
