#pragma once

#include "component/Component.h"
#include "graphics/ClipTypes.h"
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
//   container.setClip(ClipSettings{.shape = ClipRect{.bounds = viewport}});
//   container.setContentOffset({0, -scrollY});  // Negative = scrolled down
//   container.addChild(Rectangle{...});
//   container.addChild(Button{...});

class Container : public Component {
  public:
	Container() = default;

	// Handle input events by dispatching to children.
	// This is the core of the container pattern - events flow down the tree.
	bool handleEvent(InputEvent& event) override {
		return dispatchEvent(event);
	}

	// Set clip region for this container.
	// All children will be visually masked to this region.
	// Pass std::nullopt to disable clipping.
	void setClip(std::optional<Foundation::ClipSettings> clipSettings) { m_clip = clipSettings; }

	// Get current clip settings.
	[[nodiscard]] const std::optional<Foundation::ClipSettings>& getClip() const { return m_clip; }

	// Set content offset (for scrolling).
	// Children are translated by this amount BEFORE clipping.
	// Negative Y scrolls content up (viewport moves down into content).
	void setContentOffset(Foundation::Vec2 offset) { m_contentOffset = offset; }

	// Get current content offset.
	[[nodiscard]] Foundation::Vec2 getContentOffset() const { return m_contentOffset; }

	// Override render to apply clipping and content offset.
	// Order: Push transform → Push clip → Render children → Pop clip → Pop transform
	void render() override {
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

		// Apply clipping (after transform, so clip region stays fixed)
		if (m_clip.has_value()) {
			Renderer::Primitives::pushClip(m_clip.value());
		}

		// Render children (base class handles sorting and iteration)
		Component::render();

		// Pop in reverse order
		if (m_clip.has_value()) {
			Renderer::Primitives::popClip();
		}

		if (hasOffset) {
			Renderer::Primitives::PopTransform();
		}
	}

  private:
	std::optional<Foundation::ClipSettings> m_clip;
	Foundation::Vec2						m_contentOffset{0.0F, 0.0F};
};

} // namespace UI
