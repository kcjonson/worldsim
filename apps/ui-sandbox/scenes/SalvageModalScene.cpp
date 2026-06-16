// Salvage Modal Scene - a modal over a dimmed mini-HUD, to verify the Modal
// primitive (scrim + centered bracketed Panel) against the prototype.

#include <design-system/Modal.h>
#include <design-system/Panel.h>
#include <design-system/Stat.h>
#include <design-system/Tokens.h>
#include <design-system/Variants.h>
#include <graphics/Color.h>
#include <primitives/Primitives.h>
#include <scene/Scene.h>
#include "SceneTypes.h"

#include <GL/glew.h>

#include <memory>
#include <string>

namespace {

constexpr const char* kSceneName = "salvagemodal";

class SalvageModalScene : public engine::IScene {
  public:
	void onEnter() override {}
	void update(float /*dt*/) override {}
	void onExit() override {}

	std::string exportState() override { // NOLINT(readability-convert-member-functions-to-static)
		return R"({"scene": "salvagemodal"})";
	}

	const char* getName() const override { return kSceneName; }

	void render() override {
		using namespace UI::DS;

		glClearColor(bg_void.r, bg_void.g, bg_void.b, bg_void.a);
		glClear(GL_COLOR_BUFFER_BIT);

		int vw = 0;
		int vh = 0;
		Renderer::Primitives::getLogicalViewport(vw, vh);

		// A couple of panels behind, so the scrim's dimming is visible.
		Panel({.position = {60.0F, 60.0F}, .size = {320.0F, 220.0F}, .title = "REGION", .kicker = "SURVEY"}).render();
		Panel({.position = {60.0F, 300.0F}, .size = {320.0F, 180.0F}, .title = "STORAGE", .kicker = "HOLD", .variant = PanelVariant::Raised, .accent = PanelAccent::Data}).render();

		// The modal on top.
		Modal({.viewport = {static_cast<float>(vw), static_cast<float>(vh)},
			   .title = "MARA VANCE",
			   .kicker = "PERSONNEL FILE",
			   .size = Size::Lg,
			   .accent = PanelAccent::Accent,
			   .body = "Flight Engineer - Outpost 28-B"})
			.render();
	}
};

} // anonymous namespace

namespace ui_sandbox::scenes {
	extern const ui_sandbox::SceneInfo SalvageModal = {kSceneName, []() { return std::make_unique<SalvageModalScene>(); }};
}
