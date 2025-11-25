#include "focus_manager.h"
#include <utils/log.h>
#include <algorithm>
#include <cassert>

namespace UI {

void FocusManager::RegisterFocusable(IFocusable* component, int tabIndex) {
	assert(component != nullptr);

	// Check if already registered
	for (const auto& entry : m_focusables) {
		if (entry.component == component) {
			LOG_WARNING(UI, "FocusManager: Component already registered");
			return;
		}
	}

	// Auto-assign tab index if -1
	if (tabIndex == -1) {
		tabIndex = m_nextAutoTabIndex++;
	}

	// Add to list
	m_focusables.push_back({component, tabIndex});

	// Sort by tab index (stable sort preserves registration order for equal tabIndex)
	SortFocusables();
}

void FocusManager::UnregisterFocusable(IFocusable* component) {
	assert(component != nullptr);

	// Remove from focusables list
	auto it = std::remove_if(m_focusables.begin(), m_focusables.end(), [component](const FocusEntry& entry) {
		return entry.component == component;
	});

	if (it != m_focusables.end()) {
		m_focusables.erase(it, m_focusables.end());
	}

	// Clear focus if this component has focus
	if (m_currentFocus == component) {
		m_currentFocus->OnFocusLost();
		m_currentFocus = nullptr;
	}

	// Remove from all focus scopes
	for (auto& scope : m_scopeStack) {
		auto scopeIt = std::remove(scope.components.begin(), scope.components.end(), component);
		if (scopeIt != scope.components.end()) {
			scope.components.erase(scopeIt, scope.components.end());
		}

		// Clear previous focus if it was this component
		if (scope.previousFocus == component) {
			scope.previousFocus = nullptr;
		}
	}
}

void FocusManager::SetFocus(IFocusable* component) {
	if (component == m_currentFocus) {
		return; // Already has focus
	}

	// Clear current focus
	if (m_currentFocus != nullptr) {
		m_currentFocus->OnFocusLost();
	}

	// Set new focus
	m_currentFocus = component;

	if (m_currentFocus != nullptr) {
		m_currentFocus->OnFocusGained();
	}
}

void FocusManager::ClearFocus() {
	if (m_currentFocus != nullptr) {
		m_currentFocus->OnFocusLost();
		m_currentFocus = nullptr;
	}
}

void FocusManager::FocusNext() {
	auto focusables = GetActiveFocusables();

	if (focusables.empty()) {
		ClearFocus();
		return;
	}

	// Find current focus index
	int currentIndex = FindFocusIndex(m_currentFocus);

	// Search for next focusable component (wrap around)
	int startIndex = (currentIndex + 1) % static_cast<int>(focusables.size());
	int index = startIndex;

	do {
		IFocusable* candidate = focusables[index];
		if (candidate->CanReceiveFocus()) {
			SetFocus(candidate);
			return;
		}

		index = (index + 1) % static_cast<int>(focusables.size());
	} while (index != startIndex);

	// No focusable component found - clear focus
	ClearFocus();
}

void FocusManager::FocusPrevious() {
	auto focusables = GetActiveFocusables();

	if (focusables.empty()) {
		ClearFocus();
		return;
	}

	// Find current focus index
	int currentIndex = FindFocusIndex(m_currentFocus);

	// Search for previous focusable component (wrap around)
	int startIndex = (currentIndex - 1 + static_cast<int>(focusables.size())) % static_cast<int>(focusables.size());
	int index = startIndex;

	do {
		IFocusable* candidate = focusables[index];
		if (candidate->CanReceiveFocus()) {
			SetFocus(candidate);
			return;
		}

		index = (index - 1 + static_cast<int>(focusables.size())) % static_cast<int>(focusables.size());
	} while (index != startIndex);

	// No focusable component found - clear focus
	ClearFocus();
}

void FocusManager::PushFocusScope(const std::vector<IFocusable*>& components) {
	FocusScope scope;
	scope.components = components;
	scope.previousFocus = m_currentFocus;

	m_scopeStack.push_back(scope);

	// Clear current focus (modal will set its own focus)
	ClearFocus();
}

void FocusManager::PopFocusScope() {
	assert(!m_scopeStack.empty() && "PopFocusScope called with empty stack");

	FocusScope scope = m_scopeStack.back();
	m_scopeStack.pop_back();

	// Restore previous focus (if component still exists)
	if (scope.previousFocus != nullptr) {
		// Check if component is still registered
		bool stillExists = false;
		for (const auto& entry : m_focusables) {
			if (entry.component == scope.previousFocus) {
				stillExists = true;
				break;
			}
		}

		if (stillExists) {
			SetFocus(scope.previousFocus);
		} else {
			ClearFocus();
		}
	}
}

void FocusManager::RouteKeyInput(engine::Key key, bool shift, bool ctrl, bool alt) {
	if (m_currentFocus != nullptr) {
		m_currentFocus->HandleKeyInput(key, shift, ctrl, alt);
	}
}

void FocusManager::RouteCharInput(char32_t codepoint) {
	if (m_currentFocus != nullptr) {
		m_currentFocus->HandleCharInput(codepoint);
	}
}

// Private methods

void FocusManager::SortFocusables() {
	std::stable_sort(m_focusables.begin(), m_focusables.end());
}

std::vector<IFocusable*> FocusManager::GetActiveFocusables() const {
	// If focus scope stack is not empty, use topmost scope's components
	if (!m_scopeStack.empty()) {
		return m_scopeStack.back().components;
	}

	// Otherwise, use all registered focusables
	std::vector<IFocusable*> result;
	result.reserve(m_focusables.size());
	for (const auto& entry : m_focusables) {
		result.push_back(entry.component);
	}
	return result;
}

int FocusManager::FindFocusIndex(IFocusable* component) const {
	auto focusables = GetActiveFocusables();

	for (size_t i = 0; i < focusables.size(); ++i) {
		if (focusables[i] == component) {
			return static_cast<int>(i);
		}
	}

	// Not found or no focus - return -1 (will wrap to 0 in FocusNext)
	return -1;
}

} // namespace UI
