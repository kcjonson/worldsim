#include "TooltipManager.h"

#include <algorithm>
#include <stdexcept>
#include <utils/Log.h>

namespace UI {

TooltipManager* TooltipManager::s_instance = nullptr;

TooltipManager& TooltipManager::Get() {
	if (s_instance == nullptr) {
		LOG_ERROR(UI, "TooltipManager::Get() called before TooltipManager was created");
		throw std::runtime_error("TooltipManager not initialized");
	}
	return *s_instance;
}

void TooltipManager::setInstance(TooltipManager* instance) {
	s_instance = instance;
}

void TooltipManager::startHover(const TooltipContent& tooltipContent, Foundation::Vec2 newCursor) {
	// Warn once if screen bounds haven't been properly initialized
	static bool warnedAboutBounds = false;
	if (!warnedAboutBounds && screenWidth == 800.0F && screenHeight == 600.0F) {
		LOG_WARNING(UI, "TooltipManager: using default screen bounds (800x600). Call setScreenBounds() for proper positioning.");
		warnedAboutBounds = true;
	}

	pendingContent = tooltipContent;
	cursorPosition = newCursor;

	if (state == State::Idle || state == State::Hiding) {
		// Start the hover delay
		state = State::Waiting;
		stateTimer = 0.0F;
	} else if (state == State::Showing || state == State::Visible) {
		// Already showing a tooltip, update content immediately
		if (activeTooltip) {
			activeTooltip->setContent(tooltipContent);
			// Reposition for new content size
			Foundation::Vec2 pos = calculateTooltipPosition(
				cursorPosition, activeTooltip->getTooltipWidth(), activeTooltip->getTooltipHeight());
			activeTooltip->setPosition(pos.x, pos.y);
		}
	}
	// If Waiting, just update the pending content (timer continues)
}

void TooltipManager::endHover() {
	if (state == State::Waiting) {
		// Cancel the hover delay
		state = State::Idle;
		stateTimer = 0.0F;
	} else if (state == State::Showing || state == State::Visible) {
		// Start fade out
		state = State::Hiding;
		stateTimer = 0.0F;
	}
}

void TooltipManager::updateCursorPosition(Foundation::Vec2 newCursor) {
	// Only update if cursor moved beyond threshold (reduces repositioning overhead)
	float dx = newCursor.x - cursorPosition.x;
	float dy = newCursor.y - cursorPosition.y;
	float distanceSquared = dx * dx + dy * dy;

	if (distanceSquared < kMinCursorMoveDistance * kMinCursorMoveDistance) {
		return; // Cursor hasn't moved enough to warrant repositioning
	}

	cursorPosition = newCursor;

	// Update tooltip position if visible
	if (activeTooltip && (state == State::Showing || state == State::Visible)) {
		Foundation::Vec2 pos = calculateTooltipPosition(
			cursorPosition, activeTooltip->getTooltipWidth(), activeTooltip->getTooltipHeight());
		activeTooltip->setPosition(pos.x, pos.y);
	}
}

void TooltipManager::setScreenBounds(float width, float height) {
	screenWidth = width;
	screenHeight = height;
}

void TooltipManager::update(float deltaTime) {
	stateTimer += deltaTime;

	switch (state) {
		case State::Idle:
			// Nothing to do
			break;

		case State::Waiting:
			// Wait for hover delay
			if (stateTimer >= Theme::Tooltip::hoverDelay) {
				// Create and show tooltip
				// Use estimated dimensions for initial positioning
				float estimatedWidth = estimateTooltipWidth(pendingContent);
				float estimatedHeight = estimateTooltipHeight(pendingContent);
				Foundation::Vec2 pos = calculateTooltipPosition(
					cursorPosition, estimatedWidth, estimatedHeight);

				activeTooltip = std::make_unique<Tooltip>(Tooltip::Args{
					.content = pendingContent,
					.position = pos,
				});
				activeTooltip->setOpacity(0.0F);
				activeTooltip->visible = true;
				activeTooltip->zIndex = 500; // Above normal UI, below dialogs

				// Reposition with actual height
				pos = calculateTooltipPosition(
					cursorPosition, activeTooltip->getTooltipWidth(), activeTooltip->getTooltipHeight());
				activeTooltip->setPosition(pos.x, pos.y);

				state = State::Showing;
				stateTimer = 0.0F;
			}
			break;

		case State::Showing:
			// Fade in
			if (activeTooltip) {
				float opacity = std::min(1.0F, stateTimer / kFadeInDuration);
				activeTooltip->setOpacity(opacity);

				if (stateTimer >= kFadeInDuration) {
					activeTooltip->setOpacity(1.0F);
					state = State::Visible;
					stateTimer = 0.0F;
				}
			}
			break;

		case State::Visible:
			// Fully visible, nothing to do
			break;

		case State::Hiding:
			// Fade out
			if (activeTooltip) {
				float opacity = std::max(0.0F, 1.0F - stateTimer / kFadeOutDuration);
				activeTooltip->setOpacity(opacity);

				if (stateTimer >= kFadeOutDuration) {
					activeTooltip.reset();
					state = State::Idle;
					stateTimer = 0.0F;
				}
			} else {
				state = State::Idle;
				stateTimer = 0.0F;
			}
			break;
	}
}

void TooltipManager::render() {
	if (activeTooltip) {
		activeTooltip->render();
	}
}

bool TooltipManager::isTooltipVisible() const {
	return state == State::Showing || state == State::Visible;
}

Foundation::Vec2 TooltipManager::calculateTooltipPosition(
	Foundation::Vec2 cursor, float tooltipWidth, float tooltipHeight) const {

	// Default position: below and to the right of cursor
	float x = cursor.x + Theme::Tooltip::cursorOffset;
	float y = cursor.y + Theme::Tooltip::cursorOffset;

	// Clamp to screen bounds
	// If tooltip would go off the right edge, flip to left of cursor
	if (x + tooltipWidth > screenWidth) {
		x = cursor.x - tooltipWidth - Theme::Tooltip::cursorOffset / 2.0F;
	}

	// If tooltip would go off the bottom edge, flip to above cursor
	if (y + tooltipHeight > screenHeight) {
		y = cursor.y - tooltipHeight - Theme::Tooltip::cursorOffset / 2.0F;
	}

	// Ensure we don't go off the left or top edges
	x = std::max(0.0F, x);
	y = std::max(0.0F, y);

	return {x, y};
}

float TooltipManager::estimateTooltipHeight(const TooltipContent& tooltipContent) const {
	float height = Theme::Tooltip::padding * 2;

	// Title is always present
	height += Tooltip::kTitleFontSize;

	// Description (optional)
	if (!tooltipContent.description.empty()) {
		height += Tooltip::kLineSpacing + Tooltip::kDescFontSize;
	}

	// Hotkey (optional)
	if (!tooltipContent.hotkey.empty()) {
		height += Tooltip::kLineSpacing + Tooltip::kHotkeyFontSize;
	}

	return height;
}

float TooltipManager::estimateTooltipWidth(const TooltipContent& tooltipContent) const {
	// Find the longest line
	size_t maxChars = tooltipContent.title.length();

	if (!tooltipContent.description.empty()) {
		maxChars = std::max(maxChars, tooltipContent.description.length());
	}

	if (!tooltipContent.hotkey.empty()) {
		// Hotkey is displayed as "[hotkey]", so add 2 for brackets
		maxChars = std::max(maxChars, tooltipContent.hotkey.length() + 2);
	}

	// Estimate width: padding + chars * estimated char width
	float estimatedWidth = Theme::Tooltip::padding * 2 +
						   static_cast<float>(maxChars) * Tooltip::kEstimatedCharWidth;

	// Clamp to max width
	return std::min(estimatedWidth, Theme::Tooltip::maxWidth);
}

} // namespace UI
