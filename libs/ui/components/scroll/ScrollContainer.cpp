#include "ScrollContainer.h"

#include "primitives/Primitives.h"
#include "theme/Theme.h"

#include <algorithm>

namespace UI {

ScrollContainer::ScrollContainer(const Args& args)
	: viewportSize(args.size) {

	// Set base class members
	position = args.position;
	size = args.size;
	margin = args.margin;

	// Initialize scrollbar geometry
	scrollbarX = position.x + viewportSize.x - kScrollbarWidth;
	scrollbarY = position.y;
	trackHeight = viewportSize.y;

	// Set up initial clipping and offset
	updateClipAndOffset();
}

void ScrollContainer::scrollTo(float y) {
	scrollY = std::clamp(y, 0.0F, maxScroll);
	updateClipAndOffset();
	updateScrollbar();
}

void ScrollContainer::scrollBy(float delta) {
	scrollTo(scrollY + delta);
}

void ScrollContainer::scrollToTop() {
	scrollTo(0.0F);
}

void ScrollContainer::scrollToBottom() {
	scrollTo(maxScroll);
}

void ScrollContainer::setContentHeight(float height) {
	contentHeight = std::max(0.0F, height);
	contentHeightSet = true;
	updateScrollBounds();
	updateScrollbar();
}

void ScrollContainer::setViewportSize(Foundation::Vec2 newSize) {
	viewportSize = newSize;
	size = newSize;
	scrollbarX = position.x + viewportSize.x - kScrollbarWidth;
	trackHeight = viewportSize.y;
	updateScrollBounds();
	updateClipAndOffset();
	updateScrollbar();
}

void ScrollContainer::setPosition(float x, float y) {
	position = {x, y};
	scrollbarX = x + viewportSize.x - kScrollbarWidth;
	scrollbarY = y;
	updateClipAndOffset();
	updateScrollbar();
}

void ScrollContainer::update(float /*deltaTime*/) {
	// No animation/physics yet - could add smooth scrolling here later
}

void ScrollContainer::render() {
	// Auto-detect content height from first child if not manually set
	if (!contentHeightSet && !children.empty()) {
		float detectedHeight = children[0]->getHeight();
		if (detectedHeight != contentHeight) {
			contentHeight = detectedHeight;
			updateScrollBounds();
			updateScrollbar();
		}
	}

	// Render children with clipping and offset (Container::render handles this)
	Container::render();

	// Render scrollbar on top (only if content overflows)
	if (maxScroll > 0.0F) {
		Foundation::Vec2 contentPos = getContentPosition();

		// Track background
		Renderer::Primitives::drawRect({
			.bounds = Foundation::Rect{
				contentPos.x + viewportSize.x - kScrollbarWidth,
				contentPos.y,
				kScrollbarWidth,
				viewportSize.y
			},
			.style = {
				.fill = Theme::Colors::scrollbarTrack
			}
		});

		// Thumb
		float currentThumbY = contentPos.y + thumbY;
		Foundation::Color thumbColor = isDraggingThumb
			? Theme::Colors::scrollbarThumbActive
			: Theme::Colors::scrollbarThumb;

		Renderer::Primitives::drawRect({
			.bounds = Foundation::Rect{
				contentPos.x + viewportSize.x - kScrollbarWidth,
				currentThumbY,
				kScrollbarWidth,
				thumbHeight
			},
			.style = {
				.fill = thumbColor
			}
		});
	}
}

bool ScrollContainer::handleEvent(InputEvent& event) {
	// Handle thumb drag release first (regardless of position)
	if (isDraggingThumb && event.type == InputEvent::Type::MouseUp) {
		isDraggingThumb = false;
		event.consume();
		return true;
	}

	// Handle thumb dragging (mouse move while dragging)
	if (isDraggingThumb && event.type == InputEvent::Type::MouseMove) {
		float deltaY = event.position.y - dragStartY;
		// Convert screen delta to scroll delta
		// thumbDelta / (trackHeight - thumbHeight) = scrollDelta / maxScroll
		float scrollableTrack = trackHeight - thumbHeight;
		if (scrollableTrack > 0.0F) {
			float scrollDelta = (deltaY / scrollableTrack) * maxScroll;
			scrollTo(dragStartScroll + scrollDelta);
		}
		event.consume();
		return true;
	}

	// Check if event is within our bounds
	if (!containsPoint(event.position)) {
		return false;
	}

	// Handle scroll wheel
	if (event.type == InputEvent::Type::Scroll) {
		// scrollDelta < 0: scroll wheel up   -> see higher content (scroll position decreases)
		// scrollDelta > 0: scroll wheel down -> see lower content  (scroll position increases)
		// Negation implements natural scrolling direction
		scrollBy(-event.scrollDelta * kScrollSpeed);
		event.consume();
		return true;
	}

	// Handle mouse down on scrollbar
	if (event.type == InputEvent::Type::MouseDown) {
		if (isPointOverThumb(event.position)) {
			// Start dragging thumb
			isDraggingThumb = true;
			dragStartY = event.position.y;
			dragStartScroll = scrollY;
			event.consume();
			return true;
		}

		if (isPointOverTrack(event.position)) {
			// Click on track - jump to position
			Foundation::Vec2 contentPos = getContentPosition();
			float clickY = event.position.y - contentPos.y;
			float targetRatio = clickY / trackHeight;
			scrollTo(targetRatio * maxScroll);
			event.consume();
			return true;
		}
	}

	// Dispatch to children for other events
	// Transform event coordinates to account for content offset
	Foundation::Vec2 contentPos = getContentPosition();
	Foundation::Vec2 originalPos = event.position;

	// Transform: subtract container position and add scroll offset
	event.position.x -= contentPos.x;
	event.position.y -= contentPos.y;
	event.position.y += scrollY; // Add back scroll offset

	bool handled = Container::handleEvent(event);

	// Restore original position
	event.position = originalPos;

	return handled;
}

bool ScrollContainer::containsPoint(Foundation::Vec2 point) const {
	Foundation::Vec2 contentPos = getContentPosition();
	return point.x >= contentPos.x &&
		   point.x <= contentPos.x + viewportSize.x &&
		   point.y >= contentPos.y &&
		   point.y <= contentPos.y + viewportSize.y;
}

void ScrollContainer::updateScrollBounds() {
	maxScroll = std::max(0.0F, contentHeight - viewportSize.y);
	scrollY = std::clamp(scrollY, 0.0F, maxScroll);
	updateClipAndOffset();
}

void ScrollContainer::updateScrollbar() {
	if (contentHeight <= 0.0F || maxScroll <= 0.0F) {
		thumbHeight = 0.0F;
		thumbY = 0.0F;
		return;
	}

	// Thumb height proportional to visible content
	thumbHeight = (viewportSize.y / contentHeight) * trackHeight;
	thumbHeight = std::max(kMinThumbHeight, thumbHeight);

	// Thumb position based on scroll position
	float scrollableTrack = trackHeight - thumbHeight;
	if (maxScroll > 0.0F) {
		thumbY = (scrollY / maxScroll) * scrollableTrack;
	} else {
		thumbY = 0.0F;
	}
}

void ScrollContainer::updateClipAndOffset() {
	Foundation::Vec2 contentPos = getContentPosition();

	// Set clip region to viewport bounds
	Foundation::ClipSettings clipSettings;
	clipSettings.shape = Foundation::ClipRect{
		.bounds = Foundation::Rect{
			contentPos.x,
			contentPos.y,
			viewportSize.x - kScrollbarWidth, // Leave room for scrollbar
			viewportSize.y
		}
	};
	clipSettings.mode = Foundation::ClipMode::Inside;
	setClip(clipSettings);

	// Content offset: position + scroll offset
	// Subtracting scrollY moves content up (scroll down = see content further down)
	setContentOffset({contentPos.x, contentPos.y - scrollY});
}

bool ScrollContainer::isPointOverThumb(Foundation::Vec2 point) const {
	if (maxScroll <= 0.0F) {
		return false;
	}

	Foundation::Vec2 contentPos = getContentPosition();
	float thumbLeft = contentPos.x + viewportSize.x - kScrollbarWidth;
	float thumbTop = contentPos.y + thumbY;

	return point.x >= thumbLeft &&
		   point.x <= thumbLeft + kScrollbarWidth &&
		   point.y >= thumbTop &&
		   point.y <= thumbTop + thumbHeight;
}

bool ScrollContainer::isPointOverTrack(Foundation::Vec2 point) const {
	if (maxScroll <= 0.0F) {
		return false;
	}

	Foundation::Vec2 contentPos = getContentPosition();
	float trackLeft = contentPos.x + viewportSize.x - kScrollbarWidth;

	return point.x >= trackLeft &&
		   point.x <= trackLeft + kScrollbarWidth &&
		   point.y >= contentPos.y &&
		   point.y <= contentPos.y + trackHeight;
}

} // namespace UI
