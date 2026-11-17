#include "DrawingKeyGuide.h"

#include "scenes/game/world/construction/DrawingSystem.h"
#include "scenes/shared/SalalUi.h"

#include <assets/ConstructionRegistry.h>

#include <ui/Context.h>
#include <ui/components/Control.h>
#include <ui/components/KeyCap.h>
#include <ui/shapes/Rectangle.h>
#include <ui/shapes/Text.h>

#include <utility>

namespace world_sim {

	namespace {

		constexpr char32_t kToggleCharacter = U'?';
		constexpr const char* kChipText = "Keys ?";

		KeyGuidePart key(std::string text) {
			return {.text = std::move(text), .chip = true};
		}

		KeyGuidePart words(std::string text) {
			return {.text = std::move(text), .chip = false};
		}

		// The design spec's key rows (UI & Selection > Key guide), each tool's in its order.
		std::vector<KeyGuideKeyRow> keyRowsOf(ToolKind tool) {
			const KeyGuidePart either = words("/");
			switch (tool) {
				case ToolKind::FoundationRectangle:
					return {
						{{key("Click")}, "Place two opposite corners (or drag)"},
						{{key("Backspace"), either, key("Esc"), either, key("Right-click")}, "Drop the first corner"},
						{{key("Esc"), words("again")}, "Leave the tool"},
						{{key("Alt")}, "Hold to place the raw cursor (no grid, no snapping)"},
					};
				case ToolKind::Foundation:
					return {
						{{key("Click")}, "Place a point"},
						{{key("Click"), words("point 1")}, "Close the shape"},
						{{key("Backspace")}, "Undo a point"},
						{{key("Esc"), either, key("Right-click")}, "Cancel the shape"},
						{{key("Esc"), words("again")}, "Leave the tool"},
						{{key("Alt")}, "Hold to turn snapping off"},
					};
				case ToolKind::Wall:
					return {
						{{key("Click")}, "Place a point"},
						{{key("Click"), words("point 1")}, "Close the loop"},
						{{key("Double-click"), either, key("Enter"), either, key("Right-click")}, "End the wall"},
						{{key("Backspace")}, "Undo a point"},
						{{key("Esc")}, "End the wall"},
						{{key("Esc"), words("again")}, "Leave the tool"},
						{{key("Ctrl+click"), words("a foundation edge")}, "Wall the whole edge"},
						{{key("Alt")}, "Hold to turn snapping off"},
					};
				case ToolKind::Opening:
					return {
						{{key("Click"), words("a wall")}, "Place it"},
						{{key("Esc"), either, key("Right-click")}, "Leave the tool"},
					};
				case ToolKind::FoundationEdit:
					return {
						{{key("Click")}, "Place a point"},
						{{key("Click"), words("point 1")}, "Close the shape and apply it (the tool stays on for the next)"},
						{{key("Backspace")}, "Undo a point"},
						{{key("Esc"), either, key("Right-click")}, "Cancel the shape"},
						{{key("Esc"), words("again")}, "Leave the tool"},
						{{key("Alt")}, "Hold to turn snapping off"},
					};
			}
			return {};
		}

		// The tools that draw the helpers' marks: those that run the snap engine's guides.
		bool drawsHelpers(ToolKind tool) {
			switch (tool) {
				case ToolKind::Foundation:
				case ToolKind::Wall:
				case ToolKind::FoundationEdit:
					return true;
				case ToolKind::FoundationRectangle:
				case ToolKind::Opening:
					return false;
			}
			return false;
		}

		// The design spec's legend lines, each shown while its helpers are on.
		std::vector<KeyGuideLegendRow> legendRowsOf(ToolKind tool, const DrawingSettings& settings) {
			std::vector<KeyGuideLegendRow> rows;
			if (!drawsHelpers(tool)) {
				return rows;
			}
			const bool guides = settings.angleSteps || settings.guidesFromShape || settings.alignToExistingBuildings || settings.guidesFromWorldAxes;
			if (guides) {
				rows.push_back({LegendGlyph::Guide, "Lined up with something"});
			}
			if (guides || settings.angleReadout) {
				rows.push_back({LegendGlyph::RightAngle, "A square corner"});
			}
			if (guides && settings.guideCrossings) {
				rows.push_back({LegendGlyph::Crossing, "Two guides cross"});
			}
			if (settings.commonLengths) {
				rows.push_back({LegendGlyph::CommonLength, "= and ticks: same length as an edge already built"});
			}
			if (settings.angleReadout) {
				rows.push_back({LegendGlyph::AngleArc, "Arc and degrees: the corner's angle"});
			}
			return rows;
		}

		// A legend line's glyph: the plan of its mark, drawn in its box.
		class LegendGlyphView : public ui::Component {
		  public:
			struct Args {
				UI_COMMON_ARGS
				LegendGlyph glyph = LegendGlyph::Guide;
			};

			explicit LegendGlyphView(const Args& args)
				: glyph(args.glyph) {}

			[[nodiscard]] const char* typeName() const override { return "LegendGlyphView"; }

		  protected:
			void onMount() override {
				plan = planLegendGlyph(
					glyph, assets::ConstructionRegistry::Get().rendering().preview, getContext()->getTheme().get(tokens::color::statusOk)
				);
			}

			void render() override { drawFeedbackPlan(plan, drawKey(), opacity()); }

			[[nodiscard]] std::optional<Foundation::Vec2> intrinsicSize(std::optional<float> /*width*/, float /*availableWidth*/) const override {
				return Foundation::Vec2{kLegendGlyphWidth, kLegendGlyphHeight};
			}

		  private:
			LegendGlyph			glyph;
			DrawingFeedbackPlan plan;
		};

		// The collapsed key guide: a press opens it.
		class KeyGuideChip : public ui::Control {
		  public:
			struct Args {
				UI_COMMON_ARGS
				std::function<void()> onOpen;
			};

			explicit KeyGuideChip(const Args& args)
				: onOpen(args.onOpen) {}

			[[nodiscard]] const char* typeName() const override { return "KeyGuideChip"; }

		  protected:
			void onMount() override {
				Control::onMount();
				const ui::Theme& theme = getContext()->getTheme();
				ui::Layout		 box = getLayout();
				box.display = ui::Display::Flex;
				box.alignItems = ui::AlignItems::Center;
				box.padding = {theme.get(tokens::spacing::space05), theme.get(tokens::spacing::space2)};
				setLayout(box);
				setStyle({
					.backgroundColor = theme.get(tokens::color::bgPanel),
					.borderColor = theme.get(tokens::color::lineEdge),
					.borderWidth = theme.get(tokens::border::bw),
					.borderRadius = theme.get(tokens::radius::radiusSm),
				});
				addChild<ui::Text>({
					.style = world_sim::text(theme, {.size = tokens::fontSize::fsSm, .color = tokens::color::textBright}),
					.text = kChipText,
				});
			}

			void activate() override {
				if (onOpen) {
					onOpen();
				}
			}

		  private:
			std::function<void()> onOpen;
		};

		// A row's keys: each chip a key cap, the words between them dim.
		void addParts(ui::Component& into, const std::vector<KeyGuidePart>& parts, const ui::Theme& theme) {
			const ui::Style dim = world_sim::text(theme, {.size = tokens::fontSize::fsXs, .color = tokens::color::textDim});
			for (const KeyGuidePart& part : parts) {
				if (part.chip) {
					into.addChild<ui::KeyCap>({.label = part.text});
				} else {
					into.addChild<ui::Text>({.style = dim, .text = part.text});
				}
			}
		}

	} // namespace

	KeyGuideContent keyGuideContent(ToolKind tool, const DrawingSettings& settings) {
		return {
			.keyRows = keyRowsOf(tool),
			.legendRows = legendRowsOf(tool, settings),
			.foot = {key("?"), words("or"), key("H"), words("hides this")},
		};
	}

	bool togglesKeyGuide(const foundation::InputEvent& event) {
		using Type = foundation::InputEvent::Type;
		if (event.type == Type::Char) {
			return event.codepoint == kToggleCharacter;
		}
		return event.type == Type::KeyDown && !event.repeat && event.key == foundation::Key::H;
	}

	DrawingKeyGuide::DrawingKeyGuide(const Args& args)
		: onOpen(args.onOpen) {}

	void DrawingKeyGuide::onMount() {
		const ui::Theme& theme = getContext()->getTheme();
		setVisible(false);
		const float gap = theme.get(tokens::spacing::space1);
		panel = addChild<ui::Rectangle>({
			.id = "key_guide_panel",
			.layout =
				{
					.display = ui::Display::Flex,
					.flexDirection = ui::FlexDirection::Column,
					.gap = gap,
					.padding = {gap, theme.get(tokens::spacing::space2)},
					.alignItems = ui::AlignItems::Start,
				},
			.style =
				{
					.backgroundColor = theme.get(tokens::color::bgPanel),
					.borderColor = theme.get(tokens::color::lineEdge),
					.borderWidth = theme.get(tokens::border::bw),
					.borderRadius = theme.get(tokens::radius::radiusSm),
				},
		});
		chip = addChild<KeyGuideChip>({.id = "key_guide_chip", .visible = false, .onOpen = [this] {
										   if (onOpen) {
											   onOpen();
										   }
									   }});
	}

	void DrawingKeyGuide::show(std::optional<ToolKind> tool, const DrawingSettings& settings, bool collapsed) {
		const Shown now{.tool = tool, .settings = settings, .collapsed = collapsed};
		if (shown == now) {
			return;
		}
		shown = now;
		setVisible(tool.has_value() && settings.showKeyGuide);
		if (isVisible()) {
			build();
		}
	}

	void DrawingKeyGuide::build() {
		ui::Component* box = panel.get();
		ui::Component* collapsedChip = chip.get();
		if (box == nullptr || collapsedChip == nullptr || !shown || !shown->tool) {
			return;
		}
		box->setVisible(!shown->collapsed);
		collapsedChip->setVisible(shown->collapsed);
		const std::vector<ui::ComponentId> old = box->getChildren();
		for (const ui::ComponentId id : old) {
			box->removeChild(getContext()->find(id)->self());
		}
		if (shown->collapsed) {
			return;
		}

		const ui::Theme&	  theme = getContext()->getTheme();
		const KeyGuideContent content = keyGuideContent(*shown->tool, shown->settings);
		const float			  gap = theme.get(tokens::spacing::space1);
		const ui::Layout	  grid{
				 .display = ui::Display::Grid,
				 .gap = gap,
				 .alignItems = ui::AlignItems::Center,
				 .gridTemplateColumns = {ui::kAuto, ui::kAuto},
		 };
		const ui::Layout keyRun{.display = ui::Display::Flex, .gap = theme.get(tokens::spacing::space05), .alignItems = ui::AlignItems::Center};
		const ui::Style	 action = world_sim::text(theme, {.size = tokens::fontSize::fsSm, .color = tokens::color::text});

		ui::Ref<ui::Component> keys = box->addChild<ui::Component>({.id = "key_guide_keys", .layout = grid});
		for (const KeyGuideKeyRow& row : content.keyRows) {
			ui::Ref<ui::Component> cell = keys.addChild<ui::Component>({.id = "key_guide_key_row", .layout = keyRun});
			addParts(*cell.get(), row.keys, theme);
			keys.addChild<ui::Text>({.style = action, .text = row.action});
		}
		if (!content.legendRows.empty()) {
			ui::Ref<ui::Component> legend = box->addChild<ui::Component>({.id = "key_guide_legend", .layout = grid});
			for (const KeyGuideLegendRow& row : content.legendRows) {
				legend.addChild<LegendGlyphView>({.id = "key_guide_glyph", .glyph = row.glyph});
				legend.addChild<ui::Text>({.style = action, .text = row.text});
			}
		}
		ui::Ref<ui::Component> foot = box->addChild<ui::Component>({.id = "key_guide_foot", .layout = keyRun});
		addParts(*foot.get(), content.foot, theme);
	}

} // namespace world_sim
