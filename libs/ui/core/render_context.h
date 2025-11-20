// Render context for passing z-index and other rendering state through the pipeline
#pragma once

namespace UI {

	/**
	 * Thread-local rendering context
	 * Used to pass z-index from LayerManager to shape Render() methods
	 */
	class RenderContext {
	  public:
		// Get the current rendering z-index
		static float GetZIndex() { return s_currentZIndex; }

		// Set the current rendering z-index (used by LayerManager)
		static void SetZIndex(float zIndex) { s_currentZIndex = zIndex; }

	  private:
		static thread_local float s_currentZIndex;
	};

} // namespace UI
