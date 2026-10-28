#pragma once

// worldsim's ui on salal's App, until salal's ui brings its own dispatch. The bridge is the App's
// dispatch (App::setDispatch), and each frame it turns app.input() into what the ui takes: the
// frame's keys and characters go to the focus manager, paused or not; then, unless paused, the
// overlays (last added first) and then the top scene get UI::InputEvents: a pointer move from the
// frame's pointer, every frame, so hover holds under a still cursor; the Left and Right presses and
// releases; and the last scroll, sent every frame until something consumes it. Their modifiers come
// from the Shift, Control and Alt keys held, with no Super bit.
//
// It also gives the ui the App's clipboard and answers /api/ui/tree and /api/ui/lint from the top
// scene's component roots at the logical viewport. Make one after App::create, before the first
// frame, and destroy it after App::shutdown: components unregister from its focus manager as the
// scenes and overlays go.

#include "app/UiClipboard.h"
#include "app/UiOverlay.h"

#include <DevServer/DevServer.h>
#include <focus/FocusManager.h>
#include <input/InputTypes.h>

#include <vector>

namespace app {
	class App;
} // namespace app

namespace UI {
	struct IComponent;
	struct InputEvent;
} // namespace UI

namespace engine {

	class UiBridge {
	  public:
		explicit UiBridge(app::App& app);
		~UiBridge();

		UiBridge(const UiBridge&) = delete;
		UiBridge& operator=(const UiBridge&) = delete;
		UiBridge(UiBridge&&) = delete;
		UiBridge& operator=(UiBridge&&) = delete;

		// An overlay the App owns (App::addOverlay), to get UI::InputEvents before the top scene.
		void addOverlay(UiOverlay& overlay);

		// The scroll nothing in the ui consumed, which stops it being sent again; 0 when there's none.
		// GameScene zooms the camera with it, so a scroll a list takes doesn't zoom too.
		float consumeScroll();

	  private:
		void dispatch();
		void routeKey(const foundation::InputEvent& event);
		void dispatchPointer(const foundation::InputSnapshot& input);
		bool send(UI::InputEvent& event);

		std::vector<const UI::IComponent*> uiRoots() const;

		app::App&					 owner;
		UI::FocusManager			 focus;
		UiClipboard					 clipboard;
		std::vector<UiOverlay*>		 overlays;
		float						 scrollDelta = 0.0F; // the last wheel event's, until something consumes it
		app::DevServer::Registration uiTree;
		app::DevServer::Registration uiLint;
	};

} // namespace engine
