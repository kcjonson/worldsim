#pragma once

// A worldsim scene on salal's scene stack. Until salal's ui brings its own dispatch, the bridge
// (UiBridge.h) gives the top scene its pointer input as UI::InputEvents through handleInput, and the
// dev server's /api/ui/tree and /api/ui/lint walk its getUiRoots().

#include <scene/Scene.h>

#include <functional>
#include <memory>
#include <vector>

namespace UI {
	struct IComponent;
	struct InputEvent;
} // namespace UI

namespace engine {

	class UiScene : public app::Scene {
	  public:
		// True when the scene consumed the event.
		virtual bool							   handleInput(UI::InputEvent& /*event*/) { return false; }
		virtual std::vector<const UI::IComponent*> getUiRoots() const { return {}; }
	};

	// What a scene list registers by name. Every scene a worldsim app adds is a UiScene, which the
	// bridge relies on when it hands the top scene its input.
	using UiSceneFactory = std::function<std::unique_ptr<UiScene>()>;

} // namespace engine
