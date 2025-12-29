#include "layout/LayoutContainer.h"

namespace UI {

LayoutContainer::LayoutContainer(const Args& args)
	: direction(args.direction), hAlign(args.hAlign), vAlign(args.vAlign), id(args.id) {
	// Initialize base class members
	position = args.position;
	size = args.size;
	margin = args.margin;
}

void LayoutContainer::update(float deltaTime) {
	// Forward to Container (which forwards to all children)
	Container::update(deltaTime);
}

void LayoutContainer::render() {
	// Ensure layout is computed before rendering
	if (layoutDirty) {
		computeLayout();
		layoutDirty = false;
	}

	// Forward to Container (handles clipping, transforms, child rendering)
	Container::render();
}

void LayoutContainer::layout(const Foundation::Rect& newBounds) {
	// Check if bounds have changed
	if (newBounds.x != lastBounds.x || newBounds.y != lastBounds.y ||
		newBounds.width != lastBounds.width || newBounds.height != lastBounds.height) {
		lastBounds = newBounds;
		layoutDirty = true;
	}

	// Update our position from bounds
	position.x = newBounds.x;
	position.y = newBounds.y;

	// Don't call Container::layout - we handle child positioning ourselves in computeLayout
}

void LayoutContainer::computeLayout() {
	// Content area starts at position + margin
	Foundation::Vec2 contentPos = getContentPosition();
	float			 contentWidth = size.x;
	float			 contentHeight = size.y;

	if (direction == Direction::Vertical) {
		// Vertical layout: stack children top to bottom
		float currentY = contentPos.y;

		for (auto* child : children) {
			if (!child->visible) {
				continue;
			}

			float childWidth = child->getWidth();
			float childHeight = child->getHeight();

			// Calculate X position based on horizontal alignment
			float childX = contentPos.x;
			switch (hAlign) {
				case HAlign::Left:
					childX = contentPos.x;
					break;
				case HAlign::Center:
					childX = contentPos.x + (contentWidth - childWidth) * 0.5F;
					break;
				case HAlign::Right:
					childX = contentPos.x + contentWidth - childWidth;
					break;
			}

			// Position child
			child->setPosition(childX, currentY);

			// Advance Y by child's full height (includes margin)
			currentY += childHeight;
		}
	} else {
		// Horizontal layout: stack children left to right
		float currentX = contentPos.x;

		for (auto* child : children) {
			if (!child->visible) {
				continue;
			}

			float childWidth = child->getWidth();
			float childHeight = child->getHeight();

			// Calculate Y position based on vertical alignment
			float childY = contentPos.y;
			switch (vAlign) {
				case VAlign::Top:
					childY = contentPos.y;
					break;
				case VAlign::Center:
					childY = contentPos.y + (contentHeight - childHeight) * 0.5F;
					break;
				case VAlign::Bottom:
					childY = contentPos.y + contentHeight - childHeight;
					break;
			}

			// Position child
			child->setPosition(currentX, childY);

			// Advance X by child's full width (includes margin)
			currentX += childWidth;
		}
	}

	// Propagate layout to nested containers
	// Note: We only call layout() for size updates, position was already set via setPosition()
	for (auto* child : children) {
		if (!child->visible) {
			continue;
		}
		if (auto* nestedContainer = dynamic_cast<LayoutContainer*>(child)) {
			// For nested LayoutContainers, just mark them dirty so they recompute their children
			// The position was already set via setPosition() above - don't overwrite it
			nestedContainer->layoutDirty = true;
		}
	}
}

} // namespace UI
