#pragma once

#include "component/Component.h"
#include "core/ScreenClip.h"
#include "graphics/Rect.h"
#include "math/Types.h"
#include "primitives/Primitives.h"

#include <glm/gtc/matrix_transform.hpp>
#include <optional>

namespace UI {

// Container - Organizational component with optional clipping and content offset.
//
// Extends Component with:
// - Clipping: Visually masks all children to a specified region
// - Content Offset: Translates all children (for scrolling behavior)
//
// These are independent concepts (Flutter/Unity pattern):
// - A container can have clipping without scrolling (overflow hidden)
// - A container can have scrolling without clipping (parallax effects)
// - Both together create scrollable viewports
//
// Usage:
//   auto container = Container{};
//   container.setClip(viewport);  // In the container's parent's space
//   container.setContentOffset({0, -scrollY});  // Negative = scrolled down
//   container.addChild(Rectangle{...});
//   container.addChild(Button{...});

class Container : public Component {
  public:
	Container() = default;

	// Handle input events by dispatching to children.
	// This is the core of the container pattern - events flow down the tree.
	// Coordinates are transformed to account for content offset (reverse of render transform).
	bool handleEvent(InputEvent& event) override {
		bool hasOffset = (m_contentOffset.x != 0.0F || m_contentOffset.y != 0.0F);
		Foundation::Vec2 originalPos = event.position;

		if (hasOffset) {
			// Reverse the render transform: subtract content offset to get child coords
			// Render translates by +offset, so events need -offset
			event.position.x -= m_contentOffset.x;
			event.position.y -= m_contentOffset.y;
		}

		bool handled = dispatchEvent(event);

		// Restore original position for parent/sibling event handling
		event.position = originalPos;

		return handled;
	}

	// Set clip region for this container, in the space its position is in (its
	// parent's), so the content offset doesn't move it. render() takes it to screen
	// space through the transform current then. All children will be visually masked
	// to this region. Pass std::nullopt to disable clipping.
	void setClip(std::optional<Foundation::Rect> clipBounds) { m_clip = clipBounds; }

	// Set content offset (for scrolling).
	// Children are translated by this amount BEFORE clipping.
	// Negative Y scrolls content up (viewport moves down into content).
	void setContentOffset(Foundation::Vec2 offset) { m_contentOffset = offset; }

	// Get current content offset.
	[[nodiscard]] Foundation::Vec2 getContentOffset() const { return m_contentOffset; }

	const char* debugTypeName() const override { return "Container"; }

	// Override render to apply clipping and content offset.
	// Order: map the clip, push the transform, push the clip, render children, pop the clip, pop the transform
	void render() override {
		// The clip goes to screen space before the content offset is pushed, which must not move it.
		const std::optional<Foundation::Rect> clip = m_clip.has_value() ? std::optional(screenClip(*m_clip)) : std::nullopt;

		// Apply content offset as translation (moves content within fixed clip region)
		bool hasOffset = (m_contentOffset.x != 0.0F || m_contentOffset.y != 0.0F);
		if (hasOffset) {
			// Note: The offset is applied as-is to child positions.
			// For scrolling semantics (positive scroll moves content up),
			// the caller should negate the scroll value when calling setContentOffset.
			Foundation::Mat4 translation =
				glm::translate(Foundation::Mat4(1.0F), Foundation::Vec3(m_contentOffset.x, m_contentOffset.y, 0.0F));
			Renderer::Primitives::PushTransform(translation);
		}

		if (clip.has_value()) {
			Renderer::Primitives::pushClip(*clip);
		}

		// Render children (base class handles sorting and iteration)
		Component::render();

		// Pop in reverse order
		if (clip.has_value()) {
			Renderer::Primitives::popClip();
		}

		if (hasOffset) {
			Renderer::Primitives::PopTransform();
		}
	}

  private:
	std::optional<Foundation::Rect> m_clip;
	Foundation::Vec2				m_contentOffset{0.0F, 0.0F};
};

} // namespace UI
