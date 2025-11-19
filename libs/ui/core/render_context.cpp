// Render context implementation
#include "core/render_context.h"

namespace UI {

	// Thread-local storage for current z-index during rendering
	thread_local float RenderContext::s_currentZIndex = 0.0f;

} // namespace UI
