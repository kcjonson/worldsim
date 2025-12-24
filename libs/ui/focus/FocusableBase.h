#pragma once

#include "focus/FocusManager.h"
#include "focus/Focusable.h"

namespace UI {

/**
 * FocusableBase<T> - CRTP base class for automatic FocusManager registration.
 *
 * This eliminates ~40 lines of boilerplate per focusable component by handling:
 * - Constructor registration with FocusManager
 * - Destructor unregistration
 * - Move constructor (unregister old, register new)
 * - Move assignment (unregister both, re-register new)
 *
 * Usage:
 *   class Button : public Component, public FocusableBase<Button> {
 *   public:
 *       Button(const Args& args)
 *           : FocusableBase<Button>(args.tabIndex),  // Just pass tabIndex
 *             position(args.position), ... { }
 *
 *       // IFocusable methods still required (differ per component):
 *       void onFocusGained() override { focused = true; }
 *       void onFocusLost() override { focused = false; }
 *       void handleKeyInput(...) override { ... }
 *       void handleCharInput(...) override { }
 *       bool canReceiveFocus() const override { return !disabled; }
 *   };
 *
 * Move semantics:
 *   The derived class can use `= default` for move constructor/assignment.
 *   FocusableBase handles the FocusManager registration correctly.
 *
 * See: /docs/technical/ui-framework/focus-management.md
 */
template <typename Derived>
class FocusableBase : public IFocusable {
  public:
	explicit FocusableBase(int tabIndex = -1) : focusTabIndex(tabIndex) {
		FocusManager::Get().registerFocusable(this, focusTabIndex);
	}

	~FocusableBase() override {
		// Only unregister if not moved-from (-2 sentinel)
		if (focusTabIndex != -2) {
			FocusManager::Get().unregisterFocusable(this);
		}
	}

	// Move constructor: re-register at new address
	FocusableBase(FocusableBase&& other) noexcept : focusTabIndex(other.focusTabIndex) {
		FocusManager::Get().unregisterFocusable(&other);
		FocusManager::Get().registerFocusable(this, focusTabIndex);
		other.focusTabIndex = -2; // Mark moved-from
	}

	// Move assignment: unregister both, re-register this
	FocusableBase& operator=(FocusableBase&& other) noexcept {
		if (this != &other) {
			FocusManager::Get().unregisterFocusable(this);
			focusTabIndex = other.focusTabIndex;
			FocusManager::Get().unregisterFocusable(&other);
			FocusManager::Get().registerFocusable(this, focusTabIndex);
			other.focusTabIndex = -2; // Mark moved-from
		}
		return *this;
	}

	// Delete copy (focusables shouldn't be copied - each needs unique registration)
	FocusableBase(const FocusableBase&) = delete;
	FocusableBase& operator=(const FocusableBase&) = delete;

  protected:
	// Tab index for focus order. Exposed to derived classes if needed.
	// -1 = auto-assign, -2 = moved-from (don't unregister)
	int focusTabIndex{-1};
};

} // namespace UI
