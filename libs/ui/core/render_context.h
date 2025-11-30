// Render context for passing z-index and other rendering state through the pipeline
#pragma once

namespace UI {

	/**
	 * Thread-local rendering context
	 * Used to pass z-index from Component to shape Render() methods
	 */
	class RenderContext {
	  public:
		// Get the current rendering z-index
		static short GetZIndex() { return s_currentZIndex; }

		// Set the current rendering z-index (used by Component)
		static void setZIndex(short zIndex) { s_currentZIndex = zIndex; }

	  private:
		static thread_local short s_currentZIndex;
	};

} // namespace UI
