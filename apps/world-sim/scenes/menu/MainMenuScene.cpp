// Main Menu Scene - Salvage title screen.
//
// Left-aligned identity column (diamond glyph + WORLD-SIM + tagline) over a
// kicker and a LayoutContainer menu column of hover-aware rows (icon column,
// hover bracket + hint, disabled dimming), with a footer. The decorative
// planet docks off the right screen edge behind a scrim.

#include "GameStartConfig.h"
#include "NewGameSetup.h"
#include "SceneTypes.h"
#include "scenes/shared/DecorativePlanet.h"
#include "scenes/shared/Starfield.h"
#include "scenes/shared/UiStateDrain.h"
#include "scenes/shared/Widgets.h"
#include <GL/glew.h>

#include <components/icon/Icon.h>
#include <graphics/PrimitiveStyles.h>
#include <input/InputEvent.h>
#include <layout/LayoutContainer.h>
#include <primitives/Primitives.h>
#include <scene/Scene.h>
#include <scene/SceneManager.h>
#include <theme/Tokens.h>
#include <theme/Variants.h>
#include <utils/Log.h>
#include <utils/ResourcePath.h>

#include <algorithm>
#include <array>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace {

	constexpr const char* kSceneName = "main_menu";
	constexpr float		  kColX = 80.0F;
	constexpr float		  kRowWidth = 500.0F;
	constexpr float		  kRowHeight = 44.0F;

	float textScale(float px) { return px / 16.0F; }

	class MenuItemRow : public world_sim::Widget {
	  public:
		struct Args {
			std::string			  label;
			std::string			  hint;
			const char*			  glyph{nullptr};
			std::function<void()> action;
			bool				  primary{false};
			bool				  enabled{true};
			const char*			  id{nullptr};
		};

		explicit MenuItemRow(Args args)
			: label(std::move(args.label)),
			  hint(std::move(args.hint)),
			  action(std::move(args.action)),
			  primary(args.primary),
			  enabled(args.enabled),
			  id(args.id) {
			widthMode = UI::SizeMode::Hug;
			size.y = kRowHeight;
			icon = std::make_unique<UI::Icon>(UI::Icon::Args{
				.size = 18.0F, .glyph = args.glyph != nullptr ? args.glyph : "", .tint = UI::text_faint});
			bracket = std::make_unique<UI::Icon>(UI::Icon::Args{.size = 14.0F, .glyph = "chevronRight", .tint = UI::accent});
		}

		bool handleEvent(UI::InputEvent& event) override {
			using UI::InputEvent;
			if (event.type == InputEvent::Type::MouseMove) {
				hovered = enabled && containsPoint(event.position);
				return false;
			}
			if (event.type == InputEvent::Type::MouseUp && event.button == engine::MouseButton::Left &&
			    containsPoint(event.position)) {
				if (enabled && action) {
					action();
					event.consume();
					return true;
				}
			}
			return false;
		}

		bool containsPoint(Foundation::Vec2 p) const override {
			return p.x >= position.x && p.x <= position.x + size.x && p.y >= position.y &&
				   p.y <= position.y + size.y;
		}

		void render() override {
			using Renderer::Primitives::drawRect;
			using Renderer::Primitives::drawText;

			const Foundation::Rect r{position.x, position.y, size.x, size.y};
			// Disabled rows render at 40% alpha with no hover response.
			const float alpha = enabled ? 1.0F : 0.4F;
			const auto	dim = [alpha](Foundation::Color c) { return UI::withAlpha(c, c.a * alpha); };

			if (hovered) {
				drawRect({.bounds = r, .style = {.fill = UI::bg_hover}});
				drawRect({.bounds = {r.x, r.y, 2.0F, r.height}, .style = {.fill = UI::accent}});
			}

			const float insetX = r.x + (hovered ? UI::space_6 : UI::space_4);
			// The bracket is the hover cursor, shown only on the hovered row. The
			// primary row is distinguished by its accent label color, not the bracket.
			if (hovered) {
				bracket->setPosition(insetX, r.y + (r.height - 14.0F) * 0.5F);
				bracket->render();
			}

			icon->setTint(dim(hovered ? UI::accent : UI::text_faint));
			icon->setPosition(insetX + 18.0F, r.y + (r.height - 18.0F) * 0.5F);
			icon->render();

			const Foundation::Color labelColor =
				!enabled ? dim(UI::text_dim)
						 : (primary ? UI::accent_bright : (hovered ? UI::text_bright : UI::text_dim));
			drawText({.text = label,
					  .position = {insetX + 18.0F + 26.0F, r.y},
					  .scale = textScale(UI::fs_xl),
					  .color = labelColor,
					  .font = UI::fontDisplay,
					  .vAlign = Foundation::VerticalAlign::Middle,
					  .boxHeight = r.height,
					  .letterSpacing = UI::fs_xl * UI::ls_wide});

			// Hint: right-aligned to the row's right edge, but its box starts after
			// the measured label end + a gap so it can never collide with the label.
			if (hovered && !hint.empty()) {
				const float labelX = insetX + 18.0F + 26.0F;
				float		labelW = 0.0F;
				if (auto* fontRenderer = Renderer::Primitives::getFontRenderer()) {
					labelW = fontRenderer->MeasureText(label, textScale(UI::fs_xl), UI::fontDisplay, UI::fs_xl * UI::ls_wide).x;
				}
				const float hintLeft = labelX + labelW + UI::space_4;
				const float hintRight = r.x + r.width - UI::space_4;
				if (hintRight > hintLeft) {
					drawText({.text = hint,
							  .position = {hintLeft, r.y},
							  .scale = textScale(UI::fs_xs),
							  .color = UI::text_faint,
							  .font = UI::fontMono,
							  .hAlign = Foundation::HorizontalAlign::Right,
							  .vAlign = Foundation::VerticalAlign::Middle,
							  .boxWidth = hintRight - hintLeft,
							  .boxHeight = r.height});
				}
			}
		}

		const char* debugTypeName() const override { return "MenuItemRow"; }
		const char* debugId() const override { return id; }

	  private:
		std::string				  label;
		std::string				  hint;
		std::function<void()>	  action;
		bool					  primary{false};
		bool					  enabled{true};
		const char*				  id{nullptr};
		std::unique_ptr<UI::Icon> icon;
		std::unique_ptr<UI::Icon> bracket;
		bool					  hovered{false};
	};

	class MainMenuScene : public engine::IScene {
	  public:
		void onEnter() override {
			LOG_INFO(Game, "MainMenuScene - Entering");

			menu = std::make_unique<UI::LayoutContainer>(UI::LayoutContainer::Args{
				.size = {kRowWidth, 0.0F},
				.direction = UI::Direction::Vertical,
				.gap = 2.0F,
				.crossAlign = UI::CrossAlign::Stretch,
				.id = "main_menu_items"});

			menu->addChild(MenuItemRow({.label = "New Game",
										.hint = "Begin a new expedition",
										.glyph = "play",
										.action = [this]() {
											world_sim::NewGameSetup::Reset();
											sceneManager->switchTo(world_sim::toKey(world_sim::SceneType::ScenarioSelect));
										},
										.primary = true,
										.id = "menu_new_game"}));
			menu->addChild(MenuItemRow({.label = "Continue",
										.hint = "Resume your last colony",
										.glyph = "refresh",
										.enabled = false,
										.id = "menu_continue"}));
			menu->addChild(MenuItemRow({.label = "Load Game",
										.hint = "Restore a saved expedition",
										.glyph = "save",
										.enabled = false,
										.id = "menu_load_game"}));

			// Quick Start needs the prebuilt planet shipped next to the exe; if
			// the build step that bakes it hasn't run, disable the entry rather
			// than drop the player into a dead loading screen.
			const bool quickstartReady =
				Foundation::findResource(world_sim::kQuickstartPlanetResource).has_value();
			menu->addChild(MenuItemRow({.label = "Quick Start",
										.hint = quickstartReady
													? "Jump in on the prebuilt planet"
													: "Unavailable - run the quickstart-planet build target",
										.glyph = "bolt",
										.action = [this]() {
											auto config = std::make_unique<world_sim::GameStartConfig>();
											config->source = world_sim::GameStartConfig::Source::QuickStart;
											world_sim::GameStartConfig::SetPending(std::move(config));
											sceneManager->switchTo(world_sim::toKey(world_sim::SceneType::GameLoading));
										},
										.enabled = quickstartReady,
										.id = "menu_quick_start"}));
			menu->addChild(MenuItemRow({.label = "Settings",
										.hint = "Graphics, audio, controls",
										.glyph = "gear",
										.action = [this]() {
											sceneManager->switchTo(world_sim::toKey(world_sim::SceneType::Settings));
										},
										.id = "menu_settings"}));
			menu->addChild(MenuItemRow({.label = "Credits",
										.hint = "The crew behind the crew",
										.glyph = "users",
										.enabled = false,
										.id = "menu_credits"}));
			menu->addChild(MenuItemRow({.label = "Exit",
										.hint = "Quit to desktop",
										.glyph = "close",
										.action = [this]() { sceneManager->requestExit(); },
										.id = "menu_exit"}));
			itemCount = 7;
		}

		bool handleInput(UI::InputEvent& event) override {
			return menu && menu->handleEvent(event);
		}

		void update(float dt) override {
			planet.update(dt);
			if (menu) menu->update(dt);
		}

		void render() override {
			using namespace UI;
			using Renderer::Primitives::drawText;

			glClearColor(bg_void.r, bg_void.g, bg_void.b, 1.0F);
			glClear(GL_COLOR_BUFFER_BIT);

			const float screenW = Renderer::Primitives::PercentWidth(100.0F);
			const float screenH = Renderer::Primitives::PercentHeight(100.0F);
			world_sim::renderStarfield(static_cast<int>(screenW), static_cast<int>(screenH), 11U);

			// Decorative planet docked off the right screen edge, behind the
			// menu content (its scrim keeps the text legible). Sized against
			// the viewport (mock proportion) with a floor for small windows;
			// a quarter of it hangs off the edge.
			const float planetSize = std::max(kPlanetMinSize, screenH * 0.55F);
			const float overhang = planetSize * 0.25F;
			planet.render({screenW - (planetSize - overhang), (screenH - planetSize) * 0.5F,
			               planetSize, planetSize},
			              screenW, screenH);

			// Vertically center the identity + menu block.
			const float blockH = 92.0F + 16.0F + space_3 + static_cast<float>(itemCount) * (kRowHeight + 2.0F);
			const float y = std::max(80.0F, (screenH - blockH) * 0.5F);
			const float headerY = y;
			const float kickerY = y + 92.0F;

			// Identity: diamond glyph + WORLD-SIM + tagline.
			const float diaR = 11.0F;
			drawDiamond(kColX + diaR, headerY + 17.0F, diaR, accent);
			drawText(Renderer::Primitives::TextArgs{
				.text = "WORLD-SIM",
				.position = {kColX + diaR * 2.0F + 16.0F, headerY},
				.scale = textScale(fs_4xl),
				.color = text_bright,
				.font = fontDisplay,
				.vAlign = Foundation::VerticalAlign::Top,
				.letterSpacing = fs_4xl * ls_wider});
			drawText(Renderer::Primitives::TextArgs{
				.text = "Prospecting Expedition 28-B",
				.position = {kColX, headerY + 52.0F},
				.scale = textScale(fs_sm),
				.color = accent,
				.font = fontMono,
				.vAlign = Foundation::VerticalAlign::Top,
				.letterSpacing = fs_sm * ls_wide,
				.transform = Foundation::TextTransform::Uppercase});

			// Kicker, then the menu column space_3 below it.
			drawText(Renderer::Primitives::TextArgs{
				.text = "// MAIN MENU",
				.position = {kColX, kickerY},
				.scale = textScale(fs_2xs),
				.color = text_faint,
				.font = fontMono,
				.vAlign = Foundation::VerticalAlign::Top,
				.letterSpacing = fs_2xs * ls_wider});

			if (menu) {
				menu->setPosition(kColX, kickerY + 16.0F + space_3);
				menu->render();
			}

			// Footer.
			const float footerY = screenH - 28.0F;
			drawText(Renderer::Primitives::TextArgs{
				.text = "v0.1.0 - Development Build",
				.position = {kColX, footerY},
				.scale = textScale(fs_2xs),
				.color = text_faint,
				.font = fontMono,
				.vAlign = Foundation::VerticalAlign::Top});
			drawText(Renderer::Primitives::TextArgs{
				.text = "Star system: Maed - Sector 28-B",
				.position = {kColX, footerY},
				.scale = textScale(fs_2xs),
				.color = text_faint,
				.font = fontMono,
				.hAlign = Foundation::HorizontalAlign::Right,
				.vAlign = Foundation::VerticalAlign::Top,
				.boxWidth = screenW - kColX * 2.0F});

			world_sim::serveUiStateRequests(*this);
		}

		void onExit() override {
			LOG_INFO(Game, "MainMenuScene - Exiting");
			menu.reset();
		}

		std::string exportState() override { return R"({"scene": "main_menu"})"; }
		const char* getName() const override { return kSceneName; }

		std::vector<const UI::IComponent*> getUiRoots() const override {
			if (!menu) return {};
			return {menu.get()};
		}

	  private:
		static void drawDiamond(float cx, float cy, float r, Foundation::Color color) {
			const std::array<Foundation::Vec2, 4> v{{{cx, cy - r}, {cx + r, cy}, {cx, cy + r}, {cx - r, cy}}};
			const std::array<uint16_t, 6>		  idx{0, 1, 2, 0, 2, 3};
			Renderer::Primitives::drawTriangles(Renderer::Primitives::TrianglesArgs{
				.vertices = v.data(), .indices = idx.data(), .vertexCount = 4, .indexCount = 6, .color = color});
		}

		static constexpr float kPlanetMinSize = 640.0F;

		world_sim::DecorativePlanet			 planet;
		std::unique_ptr<UI::LayoutContainer> menu;
		int									 itemCount = 0;
	};

} // namespace

// Export scene info for registry
namespace world_sim::scenes {
	extern const world_sim::SceneInfo MainMenu = {kSceneName, []() { return std::make_unique<MainMenuScene>(); }};
} // namespace world_sim::scenes
