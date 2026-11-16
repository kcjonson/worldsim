// gallerydialog: a modal dialog in worldsim's theme over two of its panels, so the scrim's dimming
// shows, with a column of list rows in it.

#include "SceneTypes.h"

#include <theme/Panel.h>
#include <theme/Theme.h>
#include <theme/Tokens.h>

#include <primitives/Primitives.h>
#include <scene/Scene.h>
#include <ui/components/Dialog.h>
#include <ui/components/ListRow.h>

#include <array>
#include <cstddef>
#include <memory>
#include <string>

namespace {

	constexpr const char* kSceneName = "gallerydialog";

	class GalleryDialogScene : public app::Scene {
	  public:
		const char* name() const override { return kSceneName; }

		void onEnter(app::App& /*app*/) override {
			theme::apply(ui());
			root().addChild<theme::Panel>({.position = {60.0F, 60.0F}, .size = {320.0F, 220.0F}, .title = "REGION", .kicker = "SURVEY"});
			root().addChild<theme::Panel>(
				{.position = {60.0F, 300.0F},
				 .size = {320.0F, 180.0F},
				 .title = "STORAGE",
				 .kicker = "HOLD",
				 .variant = theme::PanelVariant::Raised,
				 .accent = theme::PanelAccent::Data}
			);

			ui::Ref<ui::Dialog> dialog = ui::Dialog::open(*root().get(), {.title = "MARA VANCE", .panelSize = {760.0F, 420.0F}});
			constexpr std::array<const char*, 6> kItems{
				"Wooden Wall", "Stone Wall", "Campfire", "Crafting Spot", "Wooden Door", "Storage Shelf"
			};
			for (std::size_t i = 0; i < kItems.size(); ++i) {
				dialog.get()->content().addChild<ui::ListRow>({
					.id = "demo_row_" + std::to_string(i),
					.size = {340.0F, ui::kAuto},
					.label = kItems[i],
					.trailing = i == 0 ? "x4" : "",
					.selected = i == 1,
					.dim = i == 3,
					.indent = i == 0 ? 0.0F : 16.0F,
				});
			}
		}

		void render(float /*alpha*/) override { Renderer::Primitives::clear(ui().getTheme().get(theme::tokens::theme::color::bgVoid)); }
	};

} // namespace

namespace ui_sandbox::scenes {
	extern const ui_sandbox::SceneInfo GalleryDialog = {kSceneName, []() { return std::make_unique<GalleryDialogScene>(); }};
} // namespace ui_sandbox::scenes
