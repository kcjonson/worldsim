#include "focus_manager.h"
#include <utils/log.h>
#include <algorithm>
#include <cassert>
#include <stdexcept>

namespace UI {

// Static member initialization
FocusManager* FocusManager::s_instance = nullptr;

FocusManager& FocusManager::Get() {
	if (!s_instance) {
		LOG_ERROR(UI, "FocusManager::Get() called before FocusManager was created");
		throw std::runtime_error("FocusManager not initialized");
	}
	return *s_instance;
}

void FocusManager::setInstance(FocusManager* instance) {
	s_instance = instance;
	LOG_INFO(UI, "FocusManager singleton instance set");
}

FocusManager::~FocusManager() {
	if (s_instance == this) {
		s_instance = nullptr;
	}
}

void FocusManager::registerFocusable(IFocusable* component, int tabIndex) {
	assert(component != nullptr);

	// Check if already registered
	for (const auto& entry : focusables) {
		if (entry.component == component) {
			LOG_WARNING(UI, "FocusManager: Component already registered");
			return;
		}
	}

	// Auto-assign tab index if -1
	if (tabIndex == -1) {
		tabIndex = nextAutoTabIndex++;
	}

	// Add to list
	focusables.push_back({component, tabIndex});

	// Sort by tab index (stable sort preserves registration order for equal tabIndex)
	sortFocusables();
}

void FocusManager::unregisterFocusable(IFocusable* component) {
	assert(component != nullptr);

	// Remove from focusables list
	auto it = std::remove_if(focusables.begin(), focusables.end(), [component](const FocusEntry& entry) {
		return entry.component == component;
	});

	if (it != focusables.end()) {
		focusables.erase(it, focusables.end());
	}

	// Clear focus if this component has focus
	if (currentFocus == component) {
		currentFocus->onFocusLost();
		currentFocus = nullptr;
	}

	// Remove from all focus scopes
	for (auto& scope : scopeStack) {
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

void FocusManager::setFocus(IFocusable* component) {
	if (component == currentFocus) {
		return; // Already has focus
	}

	// Clear current focus
	if (currentFocus != nullptr) {
		currentFocus->onFocusLost();
	}

	// Set new focus
	currentFocus = component;

	if (currentFocus != nullptr) {
		currentFocus->onFocusGained();
	}
}

void FocusManager::clearFocus() {
	if (currentFocus != nullptr) {
		currentFocus->onFocusLost();
		currentFocus = nullptr;
	}
}

void FocusManager::focusNext() {
	auto focusables = getActiveFocusables();

	if (focusables.empty()) {
		clearFocus();
		return;
	}

	// Find current focus index
	int currentIndex = findFocusIndex(currentFocus);

	// Search for next focusable component (wrap around)
	int startIndex = (currentIndex + 1) % static_cast<int>(focusables.size());
	int index = startIndex;

	do {
		IFocusable* candidate = focusables[index];
		if (candidate->canReceiveFocus()) {
			setFocus(candidate);
			return;
		}

		index = (index + 1) % static_cast<int>(focusables.size());
	} while (index != startIndex);

	// No focusable component found - clear focus
	clearFocus();
}

void FocusManager::focusPrevious() {
	auto focusables = getActiveFocusables();

	if (focusables.empty()) {
		clearFocus();
		return;
	}

	// Find current focus index
	int currentIndex = findFocusIndex(currentFocus);

	// Search for previous focusable component (wrap around)
	int startIndex = (currentIndex - 1 + static_cast<int>(focusables.size())) % static_cast<int>(focusables.size());
	int index = startIndex;

	do {
		IFocusable* candidate = focusables[index];
		if (candidate->canReceiveFocus()) {
			setFocus(candidate);
			return;
		}

		index = (index - 1 + static_cast<int>(focusables.size())) % static_cast<int>(focusables.size());
	} while (index != startIndex);

	// No focusable component found - clear focus
	clearFocus();
}

void FocusManager::pushFocusScope(const std::vector<IFocusable*>& components) {
	FocusScope scope;
	scope.components = components;
	scope.previousFocus = currentFocus;

	scopeStack.push_back(scope);

	// Clear current focus (modal will set its own focus)
	clearFocus();
}

void FocusManager::popFocusScope() {
	assert(!scopeStack.empty() && "PopFocusScope called with empty stack");

	FocusScope scope = scopeStack.back();
	scopeStack.pop_back();

	// Restore previous focus (if component still exists)
	if (scope.previousFocus != nullptr) {
		// Check if component is still registered
		bool stillExists = false;
		for (const auto& entry : focusables) {
			if (entry.component == scope.previousFocus) {
				stillExists = true;
				break;
			}
		}

		if (stillExists) {
			setFocus(scope.previousFocus);
		} else {
			clearFocus();
		}
	}
}

void FocusManager::routeKeyInput(engine::Key key, bool shift, bool ctrl, bool alt) {
	if (currentFocus != nullptr) {
		currentFocus->handleKeyInput(key, shift, ctrl, alt);
	}
}

void FocusManager::routeCharInput(char32_t codepoint) {
	if (currentFocus != nullptr) {
		currentFocus->handleCharInput(codepoint);
	}
}

// Private methods

void FocusManager::sortFocusables() {
	std::stable_sort(focusables.begin(), focusables.end());
}

std::vector<IFocusable*> FocusManager::getActiveFocusables() const {
	// If focus scope stack is not empty, use topmost scope's components
	if (!scopeStack.empty()) {
		return scopeStack.back().components;
	}

	// Otherwise, use all registered focusables
	std::vector<IFocusable*> result;
	result.reserve(focusables.size());
	for (const auto& entry : focusables) {
		result.push_back(entry.component);
	}
	return result;
}

int FocusManager::findFocusIndex(IFocusable* component) const {
	auto focusables = getActiveFocusables();

	for (size_t i = 0; i < focusables.size(); ++i) {
		if (focusables[i] == component) {
			return static_cast<int>(i);
		}
	}

	// Not found or no focus - return -1 (will wrap to 0 in FocusNext)
	return -1;
}

} // namespace UI
