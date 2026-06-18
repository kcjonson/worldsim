// Salvage Dialog Scene - an interactive Dialog over a dimmed mini-HUD, to verify
// the Salvage dialog frame (scrim + bracketed panel + glow) against the prototype.

#include <components/dialog/Dialog.h>
#include <components/panel/Panel.h>
#include <theme/Tokens.h>
#include <theme/Variants.h>

#include <graphics/Color.h>
#include <primitives/Primitives.h>
#include <scene/Scene.h>
#include "SceneTypes.h"

#include <GL/glew.h>

#include <memory>
#include <string>

namespace {

constexpr const char* kSceneName = "salvagedialog";

class SalvageDialogScene : public engine::IScene {
  public:
	void onEnter() override {
		dialog = std::make_unique<UI::Dialog>(UI::Dialog::Args{.title = "MARA VANCE", .size = {760.0F, 420.0F}, .modal = true});
	}

	void update(float dt) override {
		// Open once the viewport is known, then drive the fade.
		if (dialog && !opened) {
			int vw = 0;
			int vh = 0;
			Renderer::Primitives::getLogicalViewport(vw, vh);
			if (vw > 0 && vh > 0) {
				dialog->open(static_cast<float>(vw), static_cast<float>(vh));
				opened = true;
			}
		}
		if (dialog) {
			dialog->update(dt);
		}
	}

	void onExit() override {}

	std::string exportState() override { // NOLINT(readability-convert-member-functions-to-static)
		return R"({"scene": "salvagedialog"})";
	}

	const char* getName() const override { return kSceneName; }

	void render() override {
		using namespace UI;

		glClearColor(bg_void.r, bg_void.g, bg_void.b, bg_void.a);
		glClear(GL_COLOR_BUFFER_BIT);

		// A couple of panels behind, so the scrim's dimming is visible.
		Panel({.position = {60.0F, 60.0F}, .size = {320.0F, 220.0F}, .title = "REGION", .kicker = "SURVEY"}).render();
		Panel({.position = {60.0F, 300.0F}, .size = {320.0F, 180.0F}, .title = "STORAGE", .kicker = "HOLD", .variant = PanelVariant::Raised, .accent = PanelAccent::Data}).render();

		if (dialog) {
			dialog->render();
		}
	}

  private:
	std::unique_ptr<UI::Dialog> dialog;
	bool						opened = false;
};

} // anonymous namespace

namespace ui_sandbox::scenes {
	extern const ui_sandbox::SceneInfo SalvageDialog = {kSceneName, []() { return std::make_unique<SalvageDialogScene>(); }};
}
