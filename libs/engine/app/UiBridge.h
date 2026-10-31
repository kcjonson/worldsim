#pragma once

// worldsim's ui on salal's App, until salal's ui brings its own dispatch. The bridge is the App's
// dispatch (App::setDispatch). Each frame it walks app.input().events() in order: keys and characters
// go to the focus manager, paused or not, and unless paused each Left or Right press and release goes
// to the top scene as a UI::InputEvent at its own position, with a move there first, and each wheel
// event as a scroll, which is sent again every frame until something consumes it. Then, unless paused, a move at the frame's pointer, every frame, so hover
// holds under a still cursor; there's none until an event has carried a pointer position. Modifiers
// come from the Shift, Control and Alt keys held at the end of the frame, with no Super bit.
//
// It also gives the ui the App's clipboard and answers /api/ui/tree and /api/ui/lint from the top
// scene's component roots at the logical viewport. Make one after App::create, before the first
// frame, and destroy it after App::shutdown: components unregister from its focus manager as the
// scenes go, and its focus manager and clipboard clear the ui's singletons as they go.
//
// While scenes are moving onto salal's ui, the bridge is installed per scene: a scene on salal's ui
// uninstalls it as it enters, so App's own dispatch and tree and lint providers reach its ui, and a
// UiScene installs it again. It starts installed.

#include "app/UiClipboard.h"

#include <DevServer/DevServer.h>
#include <focus/FocusManager.h>
#include <input/InputTypes.h>
#include <math/Types.h>

#include <vector>

namespace app {
	class App;
} // namespace app

namespace UI {
	struct IComponent;
	struct InputEvent;
} // namespace UI

namespace engine {

	class UiScene;

	class UiBridge {
	  public:
		explicit UiBridge(app::App& app);
		~UiBridge();

		UiBridge(const UiBridge&) = delete;
		UiBridge& operator=(const UiBridge&) = delete;
		UiBridge(UiBridge&&) = delete;
		UiBridge& operator=(UiBridge&&) = delete;

		// The App's dispatch and the tree and lint providers, or App's own again. Idempotent.
		void			   install();
		void			   uninstall();
		[[nodiscard]] bool installed() const { return isInstalled; }

		// The scroll nothing in the ui consumed, which stops it being sent again; 0 when there's none.
		// GameScene zooms the camera with it, so a scroll a list takes doesn't zoom too.
		float consumeScroll();

	  private:
		void dispatch();
		void routeKey(const foundation::InputEvent& event);
		void sendButton(const foundation::InputEvent& event, int mods);
		bool sendScroll(Foundation::Vec2 position);
		bool send(UI::InputEvent& event);

		UiScene*						   topScene() const;
		std::vector<const UI::IComponent*> uiRoots() const;

		app::App&					 owner;
		UI::FocusManager			 focus;
		UiClipboard					 clipboard;
		float						 scrollDelta = 0.0F; // the last wheel event's, until something consumes it
		bool						 pointerSeen = false;
		bool						 isInstalled = false;
		app::DevServer::Registration uiTree;
		app::DevServer::Registration uiLint;
	};

} // namespace engine
