#pragma once

// An overlay above every scene (app::Overlay, owned by the App) that the bridge (UiBridge.h) gives
// pointer input to as UI::InputEvents before the top scene.

#include <scene/Overlay.h>

namespace UI {
	struct InputEvent;
} // namespace UI

namespace engine {

	class UiOverlay : public app::Overlay {
	  public:
		// True when the overlay consumed the event, so nothing under it sees it.
		virtual bool handleInput(UI::InputEvent& event) = 0;
	};

} // namespace engine
