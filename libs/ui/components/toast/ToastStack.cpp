#include "ToastStack.h"

#include <algorithm>

namespace UI {

ToastStack::ToastStack(const Args& args)
	: anchor(args.anchor),
	  spacing(args.spacing),
	  maxToasts(args.maxToasts),
	  toastWidth(args.toastWidth) {
	position = args.position;
	// Size is dynamic based on toasts
	size = {toastWidth, 0.0F};
}

void ToastStack::addToast(const std::string& title, const std::string& message,
						   ToastSeverity severity, float autoDismissTime) {
	addToast(Toast::Args{
		.title = title,
		.message = message,
		.severity = severity,
		.autoDismissTime = autoDismissTime,
		.width = toastWidth,
	});
}

void ToastStack::addToast(const std::string& title, const std::string& message,
						   ToastSeverity severity, float autoDismissTime,
						   std::function<void()> onClick) {
	addToast(Toast::Args{
		.title = title,
		.message = message,
		.severity = severity,
		.autoDismissTime = autoDismissTime,
		.onClick = std::move(onClick),
		.width = toastWidth,
	});
}

void ToastStack::addToast(Toast::Args args) {
	// If at max capacity, dismiss the oldest toast
	if (toasts.size() >= maxToasts) {
		// Find the oldest non-dismissing toast
		for (auto& toast : toasts) {
			if (!toast->isDismissing() && !toast->isFinished()) {
				toast->dismiss();
				break;
			}
		}
	}

	// Set width if not specified
	if (args.width == 0.0F) {
		args.width = toastWidth;
	}

	// Create the toast with temporary position (will be repositioned)
	args.position = position;
	auto toast = std::make_unique<Toast>(args);
	toast->zIndex = zIndex + static_cast<int>(toasts.size());

	toasts.push_back(std::move(toast));
	repositionToasts();
}

void ToastStack::dismissAll() {
	for (auto& toast : toasts) {
		toast->dismiss();
	}
}

size_t ToastStack::getVisibleToastCount() const {
	size_t count = 0;
	for (const auto& toast : toasts) {
		if (!toast->isFinished()) {
			++count;
		}
	}
	return count;
}

void ToastStack::repositionToasts() {
	if (toasts.empty()) {
		return;
	}

	// Calculate positions based on anchor
	float currentY = position.y;

	// For bottom anchors, we stack upward (newest at bottom)
	// For top anchors, we stack downward (newest at top)
	bool stackUpward = (anchor == ToastAnchor::BottomRight || anchor == ToastAnchor::BottomLeft);

	// Calculate X position based on anchor
	float toastX = position.x;
	if (anchor == ToastAnchor::TopRight || anchor == ToastAnchor::BottomRight) {
		toastX = position.x - toastWidth;
	}

	if (stackUpward) {
		// Start from anchor and go upward
		// Process in reverse order so newest is at the anchor point
		for (auto it = toasts.rbegin(); it != toasts.rend(); ++it) {
			auto& toast = *it;
			if (toast->isFinished()) {
				continue;
			}

			float toastHeight = toast->getHeight();
			currentY -= toastHeight;
			toast->setPosition(toastX, currentY);
			currentY -= spacing;
		}
	} else {
		// Start from anchor and go downward
		for (auto& toast : toasts) {
			if (toast->isFinished()) {
				continue;
			}

			toast->setPosition(toastX, currentY);
			float toastHeight = toast->getHeight();
			currentY += toastHeight + spacing;
		}
	}
}

void ToastStack::removeFinishedToasts() {
	bool removed = false;
	auto it = toasts.begin();
	while (it != toasts.end()) {
		if ((*it)->isFinished()) {
			it = toasts.erase(it);
			removed = true;
		} else {
			++it;
		}
	}

	if (removed) {
		repositionToasts();
	}
}

void ToastStack::setPosition(float x, float y) {
	position = {x, y};
	repositionToasts();
}

bool ToastStack::containsPoint(Foundation::Vec2 point) const {
	for (const auto& toast : toasts) {
		if (toast->containsPoint(point)) {
			return true;
		}
	}
	return false;
}

bool ToastStack::handleEvent(InputEvent& event) {
	if (!visible) {
		return false;
	}

	// Dispatch to toasts in reverse order (newest first / on top)
	for (auto it = toasts.rbegin(); it != toasts.rend(); ++it) {
		if ((*it)->handleEvent(event)) {
			return true;
		}
	}

	return false;
}

void ToastStack::update(float deltaTime) {
	// Update all toasts
	for (auto& toast : toasts) {
		toast->update(deltaTime);
	}

	// Remove finished toasts
	removeFinishedToasts();

	// An empty stack draws nothing and has no extent; hiding it keeps the debug
	// UI-tree/lint from seeing a zero-size box parked at the anchor point.
	visible = getVisibleToastCount() > 0;
}

Foundation::Vec2 ToastStack::getPosition() const {
	Foundation::Vec2 minPos = position;
	bool			 any = false;
	for (const auto& toast : toasts) {
		if (toast->isFinished()) {
			continue;
		}
		const Foundation::Vec2 p = toast->getPosition();
		if (!any) {
			minPos = p;
			any = true;
		} else {
			minPos.x = std::min(minPos.x, p.x);
			minPos.y = std::min(minPos.y, p.y);
		}
	}
	return minPos;
}

float ToastStack::getWidth() const {
	const Foundation::Vec2 origin = getPosition();
	float				   right = origin.x;
	for (const auto& toast : toasts) {
		if (!toast->isFinished()) {
			right = std::max(right, toast->getPosition().x + toast->getWidth());
		}
	}
	return right - origin.x;
}

float ToastStack::getHeight() const {
	const Foundation::Vec2 origin = getPosition();
	float				   bottom = origin.y;
	for (const auto& toast : toasts) {
		if (!toast->isFinished()) {
			bottom = std::max(bottom, toast->getPosition().y + toast->getHeight());
		}
	}
	return bottom - origin.y;
}

void ToastStack::render() {
	if (!visible) {
		return;
	}

	// Render all toasts (oldest first so newest appears on top)
	for (auto& toast : toasts) {
		toast->render();
	}
}

} // namespace UI
