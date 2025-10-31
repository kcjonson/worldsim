#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

struct GLFWwindow;

namespace Renderer {

	/**
	 * CoordinateSystem abstracts high-DPI display complexity and provides coordinate system management.
	 *
	 * Key concepts:
	 * - LOGICAL PIXELS (Window Coordinates): What developers work with. Consistent regardless of DPI.
	 * - PHYSICAL PIXELS (Framebuffer Coordinates): Actual pixels on screen. On 2x Retina display,
	 *   100x100 logical pixels = 200x200 physical pixels.
	 * - PIXEL RATIO: Ratio between physical and logical pixels (e.g., 2.0 on Retina).
	 *
	 * Design:
	 * - All public APIs use logical pixels (window coordinates)
	 * - Only glViewport uses physical pixels (framebuffer size)
	 * - Projection matrices use logical pixels for consistent coordinate spaces
	 * - Mouse input is already in logical pixels from GLFW
	 */
	class CoordinateSystem {
	  public:
		CoordinateSystem() = default;
		~CoordinateSystem() = default;

		// Non-copyable, non-movable
		CoordinateSystem(const CoordinateSystem&) = delete;
		CoordinateSystem& operator=(const CoordinateSystem&) = delete;
		CoordinateSystem(CoordinateSystem&&) = delete;
		CoordinateSystem& operator=(CoordinateSystem&&) = delete;

		/**
		 * Initialize the coordinate system with a GLFW window.
		 * @param window The GLFW window to use for coordinate calculations
		 * @return true if initialization succeeded
		 */
		bool Initialize(GLFWwindow* window);

		/**
		 * Create screen-space orthographic projection matrix.
		 * (0,0) at top-left, Y increases downward.
		 * Uses logical pixels (window size), not physical pixels.
		 */
		glm::mat4 CreateScreenSpaceProjection() const;

		/**
		 * Create world-space orthographic projection matrix.
		 * (0,0) at center, Y increases upward.
		 * Uses logical pixels (window size), not physical pixels.
		 */
		glm::mat4 CreateWorldSpaceProjection() const;

		/**
		 * Get window size in logical pixels.
		 */
		glm::vec2 GetWindowSize() const;

		/**
		 * Set OpenGL viewport to full framebuffer size (physical pixels).
		 * This is the only method that uses physical pixels.
		 */
		void SetFullViewport() const;

		/**
		 * Update internal state when window is resized.
		 * @param width New window width in logical pixels
		 * @param height New window height in logical pixels
		 */
		void UpdateWindowSize(int width, int height);

		/**
		 * Get the pixel ratio (physical pixels / logical pixels).
		 * Cached for performance.
		 */
		float GetPixelRatio() const;

		/**
		 * Convert window coordinates (logical pixels) to framebuffer coordinates (physical pixels).
		 */
		glm::vec2 WindowToFramebuffer(const glm::vec2& windowCoords) const;

		/**
		 * Convert framebuffer coordinates (physical pixels) to window coordinates (logical pixels).
		 */
		glm::vec2 FramebufferToWindow(const glm::vec2& fbCoords) const;

		// Percentage-based layout helpers
		// These allow UI elements to use relative sizing (e.g., "50%" of screen width)

		/**
		 * Convert percentage of window width to logical pixels.
		 * @param percent Percentage (0-100)
		 */
		float PercentWidth(float percent) const;

		/**
		 * Convert percentage of window height to logical pixels.
		 * @param percent Percentage (0-100)
		 */
		float PercentHeight(float percent) const;

		/**
		 * Convert percentage dimensions to logical pixel size.
		 * @param widthPercent Width percentage (0-100)
		 * @param heightPercent Height percentage (0-100)
		 */
		glm::vec2 PercentSize(float widthPercent, float heightPercent) const;

		/**
		 * Convert percentage position to logical pixel position.
		 * @param xPercent X percentage (0-100)
		 * @param yPercent Y percentage (0-100)
		 */
		glm::vec2 PercentPosition(float xPercent, float yPercent) const;

	  private:
		GLFWwindow*	  m_window = nullptr;
		mutable float m_cachedPixelRatio = 1.0f;
		mutable bool  m_pixelRatioDirty = true;
	};

} // namespace Renderer
