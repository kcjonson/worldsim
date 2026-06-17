// Main Menu Scene - Salvage title screen.
//
// Left-aligned identity column (diamond glyph + WORLD-SIM + tagline) over a
// kicker and a stack of hover-aware menu rows, with a footer. Rendered inline
// from the Salvage primitives + tokens; the diamond and selection bracket are
// vector-drawn, not font glyphs.

#include "GameStartConfig.h"
#include "SceneTypes.h"
#include <GL/glew.h>

#include <graphics/PrimitiveStyles.h>
#include <input/InputEvent.h>
#include <primitives/Primitives.h>
#include <scene/Scene.h>
#include <scene/SceneManager.h>
#include <theme/Tokens.h>
#include <theme/Variants.h>
#include <utils/Log.h>

#include <array>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace {

	constexpr const char* kSceneName = "main_menu";

	float textScale(float px) { return px / 16.0F; }

	class MainMenuScene : public engine::IScene {
	  public:
		void onEnter() override {
			LOG_INFO(Game, "MainMenuScene - Entering");

			items.clear();
			items.push_back({"New Game", "Begin a new expedition", [this]() {
								 sceneManager->switchTo(world_sim::toKey(world_sim::SceneType::WorldCreator));
							 }, true});
			items.push_back({"Quick Start", "Jump in on the cached planet", [this]() {
								 auto config = std::make_unique<world_sim::GameStartConfig>();
								 config->source = world_sim::GameStartConfig::Source::QuickStart;
								 world_sim::GameStartConfig::SetPending(std::move(config));
								 sceneManager->switchTo(world_sim::toKey(world_sim::SceneType::GameLoading));
							 }, false});
			items.push_back({"Settings", "Graphics, audio, controls", [this]() {
								 sceneManager->switchTo(world_sim::toKey(world_sim::SceneType::Settings));
							 }, false});
			items.push_back({"Exit", "Quit to desktop", [this]() { sceneManager->requestExit(); }, false});

			layoutMenu();
		}

		bool handleInput(UI::InputEvent& event) override {
			using UI::InputEvent;
			if (event.type == InputEvent::Type::MouseMove) {
				hoveredIndex = itemAtPoint(event.position);
				return false;
			}
			if (event.type == InputEvent::Type::MouseUp && event.button == engine::MouseButton::Left) {
				const int idx = itemAtPoint(event.position);
				if (idx >= 0 && items[static_cast<size_t>(idx)].action) {
					items[static_cast<size_t>(idx)].action();
					event.consume();
					return true;
				}
			}
			return false;
		}

		void update(float /*dt*/) override {}

		void render() override {
			using namespace UI;
			using Renderer::Primitives::drawRect;
			using Renderer::Primitives::drawText;

			glClearColor(bg_void.r, bg_void.g, bg_void.b, 1.0F);
			glClear(GL_COLOR_BUFFER_BIT);

			const float screenW = Renderer::Primitives::PercentWidth(100.0F);
			const float screenH = Renderer::Primitives::PercentHeight(100.0F);

			// Identity: diamond glyph + WORLD-SIM + tagline.
			const float diaR = 11.0F;
			drawDiamond(colX + diaR, headerY + 17.0F, diaR, accent);
			drawText(Renderer::Primitives::TextArgs{
				.text = "WORLD-SIM",
				.position = {colX + diaR * 2.0F + 16.0F, headerY},
				.scale = textScale(fs_3xl),
				.color = text_bright,
				.font = fontDisplay,
				.vAlign = Foundation::VerticalAlign::Top,
				.letterSpacing = fs_3xl * ls_wide});
			drawText(Renderer::Primitives::TextArgs{
				.text = "Prospecting Expedition 28-B",
				.position = {colX, headerY + 52.0F},
				.scale = textScale(fs_sm),
				.color = text_dim,
				.font = fontMono,
				.vAlign = Foundation::VerticalAlign::Top});

			// Kicker.
			drawText(Renderer::Primitives::TextArgs{
				.text = "// MAIN MENU",
				.position = {colX, kickerY},
				.scale = textScale(fs_2xs),
				.color = text_faint,
				.font = fontMono,
				.vAlign = Foundation::VerticalAlign::Top,
				.letterSpacing = fs_2xs * ls_wider});

			// Menu rows.
			for (size_t i = 0; i < items.size(); ++i) {
				const MenuItem&		   item = items[i];
				const Foundation::Rect rect = itemRects[i];
				const bool			   hovered = static_cast<int>(i) == hoveredIndex;
				const bool			   armed = hovered || item.primary;

				if (hovered) {
					drawRect(Renderer::Primitives::RectArgs{.bounds = rect, .style = {.fill = bg_hover}});
				}
				if (armed) {
					drawRect(Renderer::Primitives::RectArgs{
						.bounds = {rect.x, rect.y + 6.0F, 2.0F, rect.height - 12.0F},
						.style = {.fill = accent}});
				}

				const Foundation::Color labelColor = item.primary ? accent_bright : (hovered ? text_bright : text_dim);
				const float				labelX = rect.x + (hovered ? space_6 : space_4);
				drawText(Renderer::Primitives::TextArgs{
					.text = item.label,
					.position = {labelX, rect.y},
					.scale = textScale(fs_lg),
					.color = labelColor,
					.font = fontDisplay,
					.vAlign = Foundation::VerticalAlign::Middle,
					.boxHeight = rect.height});

				if (hovered) {
					drawText(Renderer::Primitives::TextArgs{
						.text = item.hint,
						.position = {rect.x, rect.y},
						.scale = textScale(fs_xs),
						.color = text_faint,
						.font = fontMono,
						.hAlign = Foundation::HorizontalAlign::Right,
						.vAlign = Foundation::VerticalAlign::Middle,
						.boxWidth = rect.width - space_4,
						.boxHeight = rect.height});
				}
			}

			// Footer.
			const float footerY = screenH - 28.0F;
			drawText(Renderer::Primitives::TextArgs{
				.text = "v0.1.0 - Development Build",
				.position = {colX, footerY},
				.scale = textScale(fs_2xs),
				.color = text_faint,
				.font = fontMono,
				.vAlign = Foundation::VerticalAlign::Top});
			drawText(Renderer::Primitives::TextArgs{
				.text = "Star system: Maed - Sector 28-B",
				.position = {colX, footerY},
				.scale = textScale(fs_2xs),
				.color = text_faint,
				.font = fontMono,
				.hAlign = Foundation::HorizontalAlign::Right,
				.vAlign = Foundation::VerticalAlign::Top,
				.boxWidth = screenW - colX * 2.0F});
		}

		void onExit() override {
			LOG_INFO(Game, "MainMenuScene - Exiting");
			items.clear();
			itemRects.clear();
		}

		std::string exportState() override { return R"({"scene": "main_menu"})"; }
		const char* getName() const override { return kSceneName; }

	  private:
		struct MenuItem {
			std::string			  label;
			std::string			  hint;
			std::function<void()> action;
			bool				  primary = false;
		};

		static void drawDiamond(float cx, float cy, float r, Foundation::Color color) {
			const std::array<Foundation::Vec2, 4> v{{{cx, cy - r}, {cx + r, cy}, {cx, cy + r}, {cx - r, cy}}};
			const std::array<uint16_t, 6>		  idx{0, 1, 2, 0, 2, 3};
			Renderer::Primitives::drawTriangles(Renderer::Primitives::TrianglesArgs{
				.vertices = v.data(), .indices = idx.data(), .vertexCount = 4, .indexCount = 6, .color = color});
		}

		void layoutMenu() {
			const float screenH = Renderer::Primitives::PercentHeight(100.0F);
			constexpr float kItemH = 44.0F;
			const float blockH = 70.0F + 46.0F + static_cast<float>(items.size()) * kItemH;
			float y = (screenH - blockH) * 0.5F;
			if (y < 80.0F) { y = 80.0F; }

			headerY = y;
			kickerY = y + 92.0F;
			const float itemsTop = kickerY + 30.0F;

			itemRects.clear();
			for (size_t i = 0; i < items.size(); ++i) {
				itemRects.push_back({colX, itemsTop + static_cast<float>(i) * kItemH, kRowWidth, kItemH});
			}
		}

		int itemAtPoint(Foundation::Vec2 p) const {
			for (size_t i = 0; i < itemRects.size(); ++i) {
				const Foundation::Rect& r = itemRects[i];
				if (p.x >= r.x && p.x <= r.x + r.width && p.y >= r.y && p.y <= r.y + r.height) {
					return static_cast<int>(i);
				}
			}
			return -1;
		}

		static constexpr float colX = 80.0F;
		static constexpr float kRowWidth = 380.0F;

		std::vector<MenuItem>		  items;
		std::vector<Foundation::Rect> itemRects;
		int							  hoveredIndex = -1;
		float						  headerY = 0.0F;
		float						  kickerY = 0.0F;
	};

} // namespace

// Export scene info for registry
namespace world_sim::scenes {
	extern const world_sim::SceneInfo MainMenu = {kSceneName, []() { return std::make_unique<MainMenuScene>(); }};
} // namespace world_sim::scenes
