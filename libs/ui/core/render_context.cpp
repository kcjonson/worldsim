// Render context implementation
#include "core/render_context.h"

namespace UI {

	// Thread-local storage for current z-index during rendering
	thread_local short RenderContext::s_currentZIndex = 0;

} // namespace UI
