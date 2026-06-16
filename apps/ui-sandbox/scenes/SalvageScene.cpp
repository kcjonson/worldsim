// Salvage Scene - design-system primitive gallery.
// Renders every Salvage primitive in its variants so the C++ implementation can
// be eyeballed against the React prototype's component gallery.

#include <components/avatar/Avatar.h>
#include <components/badge/Badge.h>
#include <design-system/Button.h>
#include <components/divider/Divider.h>
#include <design-system/Icon.h>
#include <theme/IconGlyphs.h>
#include <components/keycap/KeyCap.h>
#include <design-system/Meter.h>
#include <components/panel/Panel.h>
#include <components/segmentedcontrol/SegmentedControl.h>
#include <design-system/Slider.h>
#include <components/stat/Stat.h>
#include <design-system/Tabs.h>
#include <theme/Tokens.h>
#include <design-system/Tooltip.h>
#include <theme/Variants.h>
#include <graphics/Color.h>
#include <graphics/Rect.h>
#include <primitives/Primitives.h>
#include <scene/Scene.h>
#include "SceneTypes.h"

#include <GL/glew.h>

#include <array>

namespace {

constexpr const char* kSceneName = "salvage";

class SalvageScene : public engine::IScene {
  public:
	void onEnter() override {}
	void update(float /*dt*/) override {}
	void onExit() override {}

	std::string exportState() override { // NOLINT(readability-convert-member-functions-to-static)
		return R"({"scene": "salvage", "description": "Salvage design-system gallery"})";
	}

	const char* getName() const override { return kSceneName; }

	void render() override {
		using namespace UI::DS;
		using Renderer::Primitives::drawText;

		glClearColor(bg_void.r, bg_void.g, bg_void.b, bg_void.a);
		glClear(GL_COLOR_BUFFER_BIT);

		const float left = space_6;

		drawText({.text = "SALVAGE  -  DESIGN SYSTEM",
				  .position = {left, space_4},
				  .scale = fs_xl / 16.0F,
				  .color = text_bright,
				  .font = UI::DS::fontDisplay,
				  .id = "title"});

		const auto header = [&](const char* label, float y) {
			drawText({.text = label, .position = {left, y}, .scale = fs_2xs / 16.0F, .color = accent, .font = UI::DS::fontMono, .id = "section"});
		};

		float y = 60.0F;

		// --- Buttons ---
		header("BUTTONS", y);
		y += 18.0F;
		{
			const std::array<std::pair<const char*, ButtonVariant>, 5> btns = {{{"PRIMARY", ButtonVariant::Primary},
																				 {"SECONDARY", ButtonVariant::Secondary},
																				 {"GHOST", ButtonVariant::Ghost},
																				 {"DANGER", ButtonVariant::Danger},
																				 {"DATA", ButtonVariant::Data}}};
			float x = left;
			for (const auto& [label, variant] : btns) {
				Button({.position = {x, y}, .size = {150.0F, 34.0F}, .label = label, .variant = variant, .sizeVariant = Size::Md}).render();
				x += 150.0F + space_3;
			}
		}
		y += 34.0F + space_5;

		// --- Stats ---
		header("STATS", y);
		y += 18.0F;
		Stat({.position = {left, y}, .label = "POPULATION", .value = "12", .tone = Tone::Default, .size = Size::Md}).render();
		Stat({.position = {left + 220.0F, y}, .label = "FOOD", .value = "86", .unit = "%", .tone = Tone::Ok, .size = Size::Md}).render();
		Stat({.position = {left + 440.0F, y}, .label = "THREAT", .value = "2", .tone = Tone::Crit, .size = Size::Md}).render();
		Stat({.position = {left + 640.0F, y}, .label = "POWER", .value = "1.8", .unit = "kW", .tone = Tone::Data, .size = Size::Md}).render();
		y += 56.0F + space_5;

		// --- Meters ---
		header("METERS", y);
		y += 18.0F;
		{
			const float w = 360.0F;
			Meter({.position = {left, y}, .width = w, .value = 0.72F, .label = "MOOD", .valueText = "72%", .tone = Tone::Auto}).render();
			y += 32.0F;
			Meter({.position = {left, y}, .width = w, .value = 0.30F, .label = "HULL", .valueText = "30%", .tone = Tone::Auto}).render();
			y += 32.0F;
			Meter({.position = {left, y}, .width = w, .value = 0.85F, .label = "POWER", .valueText = "85%", .tone = Tone::Data, .segmented = true}).render();
			y += 32.0F;
			Meter({.position = {left, y}, .width = w, .value = 0.62F, .label = "BUILD FOUNDATION", .valueText = "62%", .tone = Tone::Accent, .inlineLabel = true}).render();
			y += 28.0F;
		}
		y += space_5;

		// --- Badges ---
		header("BADGES", y);
		y += 18.0F;
		{
			const std::array<std::tuple<const char*, Tone, bool>, 7> badges = {{{"DEFAULT", Tone::Default, false},
																				{"ACCENT", Tone::Accent, false},
																				{"DATA", Tone::Data, false},
																				{"OK", Tone::Ok, false},
																				{"WARN", Tone::Warn, false},
																				{"CRIT", Tone::Crit, false},
																				{"ONLINE", Tone::Ok, true}}};
			float x = left;
			for (const auto& [label, tone, dot] : badges) {
				Badge({.position = {x, y}, .label = label, .tone = tone, .dot = dot}).render();
				x += 130.0F;
			}
		}
		y += 24.0F + space_5;

		// --- Controls ---
		header("CONTROLS", y);
		y += 18.0F;
		SegmentedControl({.position = {left, y}, .width = 380.0F, .options = {"TERRAIN", "BIOMES", "TEMP", "RAIN"}, .selected = 0, .size = Size::Md, .tone = Tone::Data}).render();
		Tabs({.position = {left + 440.0F, y}, .tabs = {"BIO", "NEEDS", "SKILLS", "GEAR"}, .selected = 1}).render();
		y += 42.0F;
		{
			float x = left;
			for (const char* key : {"B", "M", "ESC", "SPACE"}) {
				KeyCap({.position = {x, y}, .label = key}).render();
				x += 80.0F;
			}
		}
		y += 30.0F;
		Divider({.position = {left, y}, .width = 380.0F, .label = "FIELD REPORT"}).render();
		y += 28.0F;

		// --- People / inputs (middle column) ---
		const float mx = left + 540.0F;
		drawText({.text = "PEOPLE", .position = {mx, 60.0F}, .scale = fs_2xs / 16.0F, .color = accent, .font = fontMono, .id = "section"});
		Avatar({.position = {mx, 78.0F}, .size = 44.0F, .seed = "Mara Vance", .mood = 0.72F}).render();
		Avatar({.position = {mx + 54.0F, 78.0F}, .size = 44.0F, .seed = "Idris Okonkwo", .mood = 0.45F}).render();
		Avatar({.position = {mx + 108.0F, 78.0F}, .size = 44.0F, .seed = "Rin Calloway", .mood = 0.2F}).render();
		Avatar({.position = {mx + 162.0F, 78.0F}, .size = 44.0F, .seed = "Vale", .mood = 0.9F, .selected = true}).render();

		drawText({.text = "SLIDERS", .position = {mx, 142.0F}, .scale = fs_2xs / 16.0F, .color = accent, .font = fontMono, .id = "section"});
		Slider({.position = {mx, 160.0F}, .width = 220.0F, .value = 0.62F, .label = "WATER", .valueText = "62%", .detent = 0.62F}).render();
		Slider({.position = {mx, 200.0F}, .width = 220.0F, .value = 0.31F, .label = "ATMOSPHERE", .valueText = "0.7 atm", .detent = 0.31F}).render();

		drawText({.text = "TOOLTIP", .position = {mx, 244.0F}, .scale = fs_2xs / 16.0F, .color = accent, .font = fontMono, .id = "section"});
		Tooltip({.position = {mx, 260.0F}, .content = "Double-click for full dossier"}).render();

		// --- Panels (right column) ---
		const float px = left + 820.0F;
		Panel({.position = {px, 60.0F}, .size = {300.0F, 180.0F}, .title = "REGION", .kicker = "SURVEY", .variant = PanelVariant::Panel, .accent = PanelAccent::Accent}).render();
		Panel({.position = {px + 320.0F, 60.0F}, .size = {300.0F, 180.0F}, .title = "TELEMETRY", .kicker = "FEED", .variant = PanelVariant::Raised, .accent = PanelAccent::Data}).render();
		Panel({.position = {px, 260.0F}, .size = {300.0F, 150.0F}, .title = "READOUT", .variant = PanelVariant::Inset, .accent = PanelAccent::None, .corners = false}).render();
		Panel({.position = {px + 320.0F, 260.0F}, .size = {300.0F, 150.0F}, .title = "HUD", .kicker = "COMPACT", .variant = PanelVariant::Raised, .accent = PanelAccent::Accent, .compact = true}).render();

		// --- Palette strip ---
		const float paletteY = y + space_2;
		drawPalette(left, paletteY);

		// --- Icons (all glyphs) ---
		const float iconsTop = paletteY + 60.0F;
		drawText({.text = "ICONS", .position = {left, iconsTop}, .scale = fs_2xs / 16.0F, .color = accent, .font = UI::DS::fontMono, .id = "section"});
		{
			const float cell = 42.0F;
			const int	cols = 18;
			for (int i = 0; i < Icons::count; ++i) {
				const float gx = left + static_cast<float>(i % cols) * cell;
				const float gy = iconsTop + 20.0F + static_cast<float>(i / cols) * cell;
				Icon({.position = {gx, gy}, .glyph = std::string(Icons::registry[i].name), .size = 24.0F, .color = text}).render();
			}
		}
	}

  private:
	static void drawPalette(float originX, float originY) {
		using namespace UI::DS;
		const std::array<Foundation::Color, 11> swatches = {
			accent, accent_bright, data, data_bright, status_ok, status_warn, status_crit, text_bright, bg_panel, bg_panel_raised, bg_inset};
		float x = originX;
		for (const Foundation::Color& color : swatches) {
			Renderer::Primitives::drawRect({.bounds = {x, originY, 36.0F, 36.0F},
											.style = {.fill = color, .border = Foundation::BorderStyle{.color = line_edge, .width = bw}}});
			x += 36.0F + space_2;
		}
	}
};

} // anonymous namespace

namespace ui_sandbox::scenes {
	extern const ui_sandbox::SceneInfo Salvage = {kSceneName, []() { return std::make_unique<SalvageScene>(); }};
}
