#include "coordinate_system/coordinate_system.h"
#include <GL/glew.h>
#include <GLFW/glfw3.h>

namespace Renderer {

	// COORDINATE SYSTEM DESIGN PHILOSOPHY:
	//
	// This coordinate system is designed to abstract away the complexity of high-DPI displays
	// (like Retina displays on macOS) from the rest of the application.
	//
	// Key concepts:
	// 1. LOGICAL PIXELS (Window Coordinates): What the user works with. These are the same
	//    regardless of display DPI. A 100x100 button is always 100x100 logical pixels.
	//
	// 2. PHYSICAL PIXELS (Framebuffer Coordinates): Actual pixels on the screen. On a 2x
	//    Retina display, a 100x100 logical pixel button is 200x200 physical pixels.
	//
	// 3. PIXEL RATIO: The ratio between physical and logical pixels (e.g., 2.0 on Retina).
	//
	// Design decisions:
	// - All public APIs use logical pixels (window coordinates)
	// - Only glViewport uses physical pixels (framebuffer size)
	// - Projection matrices use logical pixels to keep consistent coordinate spaces
	// - Mouse input is already in logical pixels from GLFW
	// - This abstraction is hidden from UI components - they just use logical pixels

	bool CoordinateSystem::Initialize(GLFWwindow* newWindow) {
		window = newWindow;
		return window != nullptr;
	}

	glm::mat4 CoordinateSystem::CreateScreenSpaceProjection() const {
		// IMPORTANT: We use window size (logical pixels) for projection matrices, NOT framebuffer size.
		// This ensures UI elements have consistent sizes regardless of display DPI.
		// The GPU will handle the scaling to physical pixels automatically.
		int width = 0;
		int height = 0;
		if (window != nullptr) {
			glfwGetWindowSize(window, &width, &height);
		} else {
			width = 1920; // Default fallback
			height = 1080;
		}

		// Screen-space orthographic projection: (0,0) at top-left, Y increases downward
		return glm::ortho(0.0F, static_cast<float>(width), static_cast<float>(height), 0.0F, -1.0F, 1.0F);
	}

	glm::mat4 CoordinateSystem::CreateWorldSpaceProjection() const {
		// Use window size for consistency with screen space projection
		int width = 0;
		int height = 0;
		if (window != nullptr) {
			glfwGetWindowSize(window, &width, &height);
		} else {
			width = 1920; // Default fallback
			height = 1080;
		}

		// World-space orthographic projection: (0,0) at center, Y increases upward
		float halfWidth = static_cast<float>(width) / 2.0F;
		float halfHeight = static_cast<float>(height) / 2.0F;
		return glm::ortho(-halfWidth, halfWidth, -halfHeight, halfHeight, -1.0F, 1.0F);
	}

	glm::vec2 CoordinateSystem::getWindowSize() const {
		// Return logical window size, not physical framebuffer size
		int width = 0;
		int height = 0;
		if (window != nullptr) {
			glfwGetWindowSize(window, &width, &height);
		} else {
			width = 1920; // Default fallback
			height = 1080;
		}
		return glm::vec2(static_cast<float>(width), static_cast<float>(height));
	}

	void CoordinateSystem::setFullViewport() const {
		// IMPORTANT: glViewport needs PHYSICAL pixels (framebuffer size), not logical pixels!
		// This is the only place where we use framebuffer size instead of window size.
		if (window != nullptr) {
			int width = 0;
			int height = 0;
			glfwGetFramebufferSize(window, &width, &height);
			glViewport(0, 0, width, height);
		}
	}

	void CoordinateSystem::updateWindowSize(int width, int height) {
		// NOTE: The width and height parameters are ignored.
		// Window size is tracked internally by GLFW, so this method does not update or validate the window size.
		// This method exists only for API compatibility with other systems that may expect such a function.
		// Mark pixel ratio as dirty so it gets recalculated on next access.
		pixelRatioDirty = true;
	}

	float CoordinateSystem::getPixelRatio() const {
		// Calculate and cache the pixel ratio for performance
		if (window == nullptr) {
			// Fallback: if window is null, return default pixel ratio
			return 1.0F;
		}
		if (pixelRatioDirty) {
			int windowWidth = 0;
			int windowHeight = 0;
			int framebufferWidth = 0;
			int framebufferHeight = 0;

			glfwGetWindowSize(window, &windowWidth, &windowHeight);
			glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);

			// Use the width ratio (should be same as height ratio)
			if (windowWidth > 0) {
				cachedPixelRatio = static_cast<float>(framebufferWidth) / static_cast<float>(windowWidth);
			} else {
				cachedPixelRatio = 1.0F;
			}

			pixelRatioDirty = false;
		}

		return cachedPixelRatio;
	}

	glm::vec2 CoordinateSystem::WindowToFramebuffer(const glm::vec2& windowCoords) const {
		// Convert logical pixels to physical pixels
		float ratio = getPixelRatio();
		return windowCoords * ratio;
	}

	glm::vec2 CoordinateSystem::FramebufferToWindow(const glm::vec2& fbCoords) const {
		// Convert physical pixels to logical pixels
		float ratio = getPixelRatio();
		return fbCoords / ratio;
	}

	float CoordinateSystem::PercentWidth(float percent) const { // NOLINT(readability-convert-member-functions-to-static)
		// Convert percentage to logical pixels
		glm::vec2 size = getWindowSize();
		return size.x * (percent / 100.0F);
	}

	float CoordinateSystem::PercentHeight(float percent) const { // NOLINT(readability-convert-member-functions-to-static)
		// Convert percentage to logical pixels
		glm::vec2 size = getWindowSize();
		return size.y * (percent / 100.0F);
	}

	glm::vec2
	CoordinateSystem::PercentSize(float widthPercent, float heightPercent) const { // NOLINT(readability-convert-member-functions-to-static)
		// Convert percentage dimensions to logical pixels
		glm::vec2 size = getWindowSize();
		return glm::vec2(size.x * (widthPercent / 100.0F), size.y * (heightPercent / 100.0F));
	}

	glm::vec2
	CoordinateSystem::PercentPosition(float xPercent, float yPercent) const { // NOLINT(readability-convert-member-functions-to-static)
		// Convert percentage position to logical pixels
		glm::vec2 size = getWindowSize();
		return glm::vec2(size.x * (xPercent / 100.0F), size.y * (yPercent / 100.0F));
	}

} // namespace Renderer
