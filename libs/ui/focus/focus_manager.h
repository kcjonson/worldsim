#pragma once

#include "focusable.h"
#include "input/input_types.h"
#include <vector>
#include <cstdint>

namespace UI {

/**
 * FocusManager
 *
 * Centralized keyboard focus management system.
 * Tracks which component has focus, handles Tab navigation, and routes keyboard input.
 *
 * Responsibilities:
 * - Maintain list of focusable components with tab order
 * - Track currently focused component
 * - Handle Tab/Shift+Tab navigation
 * - Manage focus scopes for modal dialogs
 * - Route keyboard input to focused component
 *
 * Usage:
 *   FocusManager focusManager;
 *
 *   // Components register themselves
 *   focusManager.RegisterFocusable(&button, 0);
 *   focusManager.RegisterFocusable(&textInput, 1);
 *
 *   // Tab key pressed
 *   focusManager.FocusNext();
 *
 *   // Route keyboard input to focused component
 *   focusManager.RouteKeyInput(key, shift, ctrl, alt);
 */
class FocusManager {
  public:
	// Singleton access
	static FocusManager& Get();
	static void			 SetInstance(FocusManager* instance);

	FocusManager() = default;
	~FocusManager();

	// Disable copy/move (singleton-like usage)
	FocusManager(const FocusManager&) = delete;
	FocusManager& operator=(const FocusManager&) = delete;
	FocusManager(FocusManager&&) = delete;
	FocusManager& operator=(FocusManager&&) = delete;

	// Registration
	/**
	 * Register a component for focus management.
	 *
	 * @param component - Pointer to component implementing IFocusable
	 * @param tabIndex - Explicit tab order (-1 for auto-assign based on registration order)
	 */
	void RegisterFocusable(IFocusable* component, int tabIndex = -1);

	/**
	 * Unregister a component (call in destructor).
	 *
	 * @param component - Pointer to previously registered component
	 */
	void UnregisterFocusable(IFocusable* component);

	// Focus control
	/**
	 * Give focus to specific component.
	 *
	 * @param component - Component to focus (must be registered)
	 */
	void SetFocus(IFocusable* component);

	/**
	 * Remove focus from current component.
	 */
	void ClearFocus();

	/**
	 * Move focus to next component in tab order (Tab key).
	 * Skips components where CanReceiveFocus() returns false.
	 * Wraps from last to first component.
	 */
	void FocusNext();

	/**
	 * Move focus to previous component in tab order (Shift+Tab).
	 * Skips components where CanReceiveFocus() returns false.
	 * Wraps from first to last component.
	 */
	void FocusPrevious();

	// Focus scope (for modals)
	/**
	 * Push a focus scope onto the stack (for modal dialogs).
	 * Tab navigation will be restricted to components in this scope.
	 *
	 * @param components - List of components in this scope
	 */
	void PushFocusScope(const std::vector<IFocusable*>& components);

	/**
	 * Pop the topmost focus scope and restore previous focus.
	 * Asserts if scope stack is empty (must match Push/Pop).
	 */
	void PopFocusScope();

	// Query
	/**
	 * Get currently focused component.
	 *
	 * @return Pointer to focused component, or nullptr if no focus
	 */
	IFocusable* GetFocused() const { return m_currentFocus; }

	/**
	 * Check if specific component has focus.
	 *
	 * @param component - Component to check
	 * @return True if component currently has focus
	 */
	bool HasFocus(IFocusable* component) const { return m_currentFocus == component; }

	// Input routing (called by InputManager)
	/**
	 * Route keyboard input to focused component (called by InputManager).
	 *
	 * @param key - Key pressed (from engine::Key enum)
	 * @param shift - True if Shift modifier is pressed
	 * @param ctrl - True if Ctrl/Cmd modifier is pressed
	 * @param alt - True if Alt/Option modifier is pressed
	 */
	void RouteKeyInput(engine::Key key, bool shift, bool ctrl, bool alt);

	/**
	 * Route character input to focused component (called by InputManager).
	 *
	 * @param codepoint - Unicode codepoint of character typed
	 */
	void RouteCharInput(char32_t codepoint);

  private:
	// Singleton instance pointer
	static FocusManager* s_instance;

	// Focus entry with tab order
	struct FocusEntry {
		IFocusable* component;
		int			tabIndex;

		bool operator<(const FocusEntry& other) const { return tabIndex < other.tabIndex; }
	};

	// Focus scope for modal dialogs
	struct FocusScope {
		std::vector<IFocusable*> components;
		IFocusable*				 previousFocus; // Focus to restore when scope pops
	};

	// State
	std::vector<FocusEntry>	  m_focusables;	  // All registered components (sorted by tabIndex)
	IFocusable*				  m_currentFocus{nullptr}; // Currently focused component
	std::vector<FocusScope>	  m_scopeStack;	  // Focus scope stack (for modals)
	int						  m_nextAutoTabIndex{0};  // Auto-increment for tabIndex=-1

	// Internal helpers
	void						 SortFocusables();
	std::vector<IFocusable*> GetActiveFocusables() const;
	int							 FindFocusIndex(IFocusable* component) const;
};

} // namespace UI
