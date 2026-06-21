// Settings Scene - game options.
//
// Minimal options screen: a list of clickable toggle rows mirroring the main
// menu's inline hit-tested row pattern (no Button widgets). Rows read/write
// world_sim::UserSettings; changes apply immediately for the session (no
// cross-launch persistence yet). ESC returns to the main menu.

#include "SceneTypes.h"
#include "UserSettings.h"
#include <GL/glew.h>

#include <graphics/PrimitiveStyles.h>
#include <input/InputEvent.h>
#include <input/InputManager.h>
#include <primitives/Primitives.h>
#include <scene/Scene.h>
#include <scene/SceneManager.h>
#include <theme/Tokens.h>
#include <theme/Variants.h>
#include <utils/Log.h>

#include <memory>
#include <string>

namespace {

	constexpr const char* kSceneName = "settings";

	float textScale(float px) { return px / 16.0F; }

	class SettingsScene : public engine::IScene {
	  public:
		void onEnter() override {
			LOG_INFO(Game, "SettingsScene - Entering");
			layout();
		}

		bool handleInput(UI::InputEvent& event) override {
			using UI::InputEvent;
			if (event.type == InputEvent::Type::MouseMove) {
				hoverAlign = inRect(event.position, alignRect);
				return false;
			}
			if (event.type == InputEvent::Type::MouseUp && event.button == engine::MouseButton::Left) {
				if (inRect(event.position, alignRect)) {
					auto& settings = world_sim::UserSettings::Get();
					settings.alignSnapToExistingFoundations = !settings.alignSnapToExistingFoundations;
					LOG_INFO(Game, "Setting: align snap to existing foundations -> %s",
							 settings.alignSnapToExistingFoundations ? "on" : "off");
					event.consume();
					return true;
				}
			}
			return false;
		}

		void update(float /*dt*/) override {
			if (engine::InputManager::Get().isKeyPressed(engine::Key::Escape)) {
				sceneManager->switchTo(world_sim::toKey(world_sim::SceneType::MainMenu));
			}
		}

		void render() override {
			using namespace UI;
			using Renderer::Primitives::drawRect;
			using Renderer::Primitives::drawText;

			glClearColor(bg_void.r, bg_void.g, bg_void.b, 1.0F);
			glClear(GL_COLOR_BUFFER_BIT);

			// Title + kicker.
			drawText(Renderer::Primitives::TextArgs{
				.text = "Settings",
				.position = {colX, headerY},
				.scale = textScale(fs_4xl),
				.color = text_bright,
				.font = fontDisplay,
				.vAlign = Foundation::VerticalAlign::Top,
				.letterSpacing = fs_4xl * ls_wider});
			drawText(Renderer::Primitives::TextArgs{
				.text = "// CONSTRUCTION",
				.position = {colX, kickerY},
				.scale = textScale(fs_2xs),
				.color = text_faint,
				.font = fontMono,
				.vAlign = Foundation::VerticalAlign::Top,
				.letterSpacing = fs_2xs * ls_wider});

			// Alignment-guides toggle row.
			const bool on = world_sim::UserSettings::Get().alignSnapToExistingFoundations;
			if (hoverAlign) {
				drawRect(Renderer::Primitives::RectArgs{.bounds = alignRect, .style = {.fill = bg_hover}});
			}
			drawText(Renderer::Primitives::TextArgs{
				.text = "Align to existing foundations",
				.position = {alignRect.x + space_4, alignRect.y},
				.scale = textScale(fs_lg),
				.color = hoverAlign ? text_bright : text_dim,
				.font = fontDisplay,
				.vAlign = Foundation::VerticalAlign::Middle,
				.boxHeight = alignRect.height});
			drawText(Renderer::Primitives::TextArgs{
				.text = on ? "ON" : "OFF",
				.position = {alignRect.x, alignRect.y},
				.scale = textScale(fs_lg),
				.color = on ? accent_bright : text_faint,
				.font = fontMono,
				.hAlign = Foundation::HorizontalAlign::Right,
				.vAlign = Foundation::VerticalAlign::Middle,
				.boxWidth = alignRect.width - space_4,
				.boxHeight = alignRect.height});

			// Footer hint.
			const float footerY = Renderer::Primitives::PercentHeight(100.0F) - 28.0F;
			drawText(Renderer::Primitives::TextArgs{
				.text = "Press ESC to return to menu",
				.position = {colX, footerY},
				.scale = textScale(fs_2xs),
				.color = text_faint,
				.font = fontMono,
				.vAlign = Foundation::VerticalAlign::Top});
		}

		void onExit() override { LOG_INFO(Game, "SettingsScene - Exiting"); }

		std::string exportState() override { return R"({"scene": "settings"})"; }
		const char* getName() const override { return kSceneName; }

	  private:
		void layout() {
			const float screenH = Renderer::Primitives::PercentHeight(100.0F);
			headerY = (screenH - 200.0F) * 0.5F;
			if (headerY < 80.0F) {
				headerY = 80.0F;
			}
			kickerY = headerY + 92.0F;
			alignRect = {colX, kickerY + 30.0F, kRowWidth, 44.0F};
		}

		static bool inRect(Foundation::Vec2 p, const Foundation::Rect& r) {
			return p.x >= r.x && p.x <= r.x + r.width && p.y >= r.y && p.y <= r.y + r.height;
		}

		static constexpr float colX = 80.0F;
		static constexpr float kRowWidth = 480.0F;

		Foundation::Rect alignRect{};
		bool			 hoverAlign = false;
		float			 headerY = 0.0F;
		float			 kickerY = 0.0F;
	};

} // namespace

// Export scene info for registry
namespace world_sim::scenes {
	extern const world_sim::SceneInfo Settings = {kSceneName, []() { return std::make_unique<SettingsScene>(); }};
}
