#include "ConstructionConfigStrip.h"

#include "scenes/game/ui/Look.h"

#include <ui/Context.h>
#include <ui/components/Control.h>

#include <format>

namespace world_sim {

	namespace {

		constexpr float kCardWidth = 120.0F;
		constexpr float kPresetCardWidth = 88.0F;
		constexpr float kCardHeight = 40.0F;
		constexpr float kCardSpacing = 8.0F;
		constexpr float kPadding = 12.0F;
		constexpr float kReadoutGap = 8.0F; // past the gap, before the readouts
		// Where the validity line starts, from the readouts' left edge.
		constexpr float kMessageOffset = 180.0F;
		constexpr float kEditMessageOffset = 280.0F;

		bool sameColor(ui::ColorToken a, ui::ColorToken b) {
			return a.table == b.table && a.index == b.index;
		}

		std::string costText(float costPerSquareMeter) {
			return std::format("{:.1f} /m\xC2\xB2", costPerSquareMeter);
		}

		std::string thicknessText(float meters) {
			return std::format("{:.2f} m", meters);
		}

		// A material or thickness card: its name over a detail line. Picked, it takes the active wash and
		// a thick accent border. A press picks it.
		class StripCard : public ui::Control {
		  public:
			struct Args {
				UI_COMMON_ARGS
				std::string								name;
				std::string								detail;
				bool									picked = false;
				std::function<void(const std::string&)> onPick;
			};

			explicit StripCard(const Args& args)
				: name(args.name),
				  detail(args.detail),
				  picked(args.picked),
				  onPick(args.onPick) {}

			void show(std::string detailText, bool isPicked) {
				detail = std::move(detailText);
				if (ui::Text* shown = detailLine.get()) {
					shown->setText(detail);
				}
				if (picked != isPicked) {
					picked = isPicked;
					tint();
				}
			}

			[[nodiscard]] const char* typeName() const override { return "StripCard"; }

		  protected:
			void onMount() override {
				Control::onMount();
				const ui::Theme& theme = getContext()->getTheme();
				ui::Layout		 column = getLayout();
				column.display = ui::Display::Flex;
				column.flexDirection = ui::FlexDirection::Column;
				column.gap = theme.get(look::t::spacing::space05);
				column.padding = {5.0F, 8.0F};
				setLayout(column);
				addChild<ui::Text>({.style = look::text(theme, look::t::color::textBright, look::t::fontSize::fsBase), .text = name});
				detailLine =
					addChild<ui::Text>({.style = look::text(theme, look::t::color::textDim, look::t::fontSize::fsXs), .text = detail});
				tint();
			}

			void activate() override {
				if (onPick) {
					onPick(name);
				}
			}

		  private:
			void tint() {
				const ui::Theme& theme = getContext()->getTheme();
				setStyle({
					.backgroundColor = theme.get(picked ? look::t::color::bgActive : look::t::color::bgInset),
					.borderColor = theme.get(picked ? look::t::color::accent : look::t::color::lineEdge),
					.borderWidth = theme.get(picked ? look::t::border::bwThick : look::t::border::bw),
				});
			}

			std::string								name;
			std::string								detail;
			bool									picked;
			std::function<void(const std::string&)> onPick;
			ui::Ref<ui::Text>						detailLine;
		};

	} // namespace

	ConstructionConfigStrip::ConstructionConfigStrip(const Args& args)
		: onMaterialSelected(args.onMaterialSelected),
		  onThicknessSelected(args.onThicknessSelected) {}

	void ConstructionConfigStrip::onMount() {
		const ui::Theme& theme = getContext()->getTheme();
		const ui::Size	 spec = getSizeSpec();
		setSize({spec.width.isAuto() ? ui::percent(60.0F) : spec.width, spec.height.isAuto() ? ui::Length(kHeight) : spec.height});
		ui::Layout row = getLayout();
		row.display = ui::Display::Flex;
		row.flexDirection = ui::FlexDirection::Row;
		row.gap = kCardSpacing;
		row.padding = {0.0F, kPadding};
		row.alignItems = ui::AlignItems::Center;
		setLayout(row);
		setStyle(
			ui::Style{
				.backgroundColor = theme.get(look::t::color::bgPanel),
				.borderColor = theme.get(look::t::color::lineEdge),
				.borderWidth = theme.get(look::t::border::bw),
			}
				.extend(getDeclaredStyle())
		);
		setVisible(status.active);

		const ui::Layout cards{.display = ui::Display::Flex, .gap = kCardSpacing, .alignItems = ui::AlignItems::Center};
		materialCards = addChild<ui::Component>({.id = "config_strip_materials", .layout = cards});
		presetCards =
			addChild<ui::Component>({.id = "config_strip_presets", .margin = {0.0F, 0.0F, 0.0F, kCardSpacing * 2.0F}, .layout = cards});
		readouts = addChild<ui::Component>({
			.id = "config_strip_readouts",
			.margin = {0.0F, 0.0F, 0.0F, kReadoutGap},
			.layout = {
				.display = ui::Display::Flex,
				.flexDirection = ui::FlexDirection::Column,
				.gap = theme.get(look::t::spacing::space05),
				.alignItems = ui::AlignItems::Start
			},
		});
		const ui::Style readout = look::text(theme, look::t::color::text, look::t::fontSize::fsSm);
		readoutTop = readouts.addChild<ui::Text>({.id = "config_strip_readout_top", .style = readout});
		readoutBottom = readouts.addChild<ui::Text>({.id = "config_strip_readout_bottom", .style = readout});
		messageColor = look::t::color::statusOk;
		message = addChild<ui::Text>(
			{.id = "config_strip_message", .style = look::text(theme, messageColor, look::t::fontSize::fsBase), .text = "Ready"}
		);
		showCards();
		showReadouts();
	}

	void ConstructionConfigStrip::setMaterials(std::vector<std::pair<std::string, float>> value) {
		materials = std::move(value);
		showCards();
	}

	void ConstructionConfigStrip::setThicknessPresets(std::vector<ThicknessPresetInfo> value) {
		presets = std::move(value);
		showCards();
	}

	void ConstructionConfigStrip::setStatus(const DrawingStatus& value) {
		const bool picksChanged = status.material != value.material || status.thicknessPreset != value.thicknessPreset ||
								  status.wall != value.wall || status.foundationEdit != value.foundationEdit;
		status = value;
		setVisible(status.active);
		if (picksChanged) {
			showCards();
		}
		showReadouts();
	}

	void ConstructionConfigStrip::showCards() {
		ui::Component* materialRow = materialCards.get();
		ui::Component* presetRow = presetCards.get();
		if (materialRow == nullptr || presetRow == nullptr) {
			return;
		}
		// Add / Subtract keeps the edited foundation's material, so there's nothing to pick.
		materialRow->setVisible(!status.foundationEdit && !materials.empty());
		materialRow->reconcileChildren(
			materials,
			[](const std::pair<std::string, float>& material) { return material.first; },
			[&](const std::pair<std::string, float>& material) -> ui::Ref<ui::Component> {
				return materialRow->addChild<StripCard>({
					.id = "config_strip_card",
					.size = {kCardWidth, kCardHeight},
					.name = material.first,
					.detail = costText(material.second),
					.picked = material.first == status.material,
					.onPick = onMaterialSelected,
				});
			},
			[&](ui::Component& card, const std::pair<std::string, float>& material) {
				static_cast<StripCard&>(card).show(costText(material.second), material.first == status.material);
			}
		);
		presetRow->setVisible(status.wall && !presets.empty());
		presetRow->reconcileChildren(
			presets,
			[](const ThicknessPresetInfo& preset) { return preset.name; },
			[&](const ThicknessPresetInfo& preset) -> ui::Ref<ui::Component> {
				return presetRow->addChild<StripCard>({
					.id = "config_strip_preset",
					.size = {kPresetCardWidth, kCardHeight},
					.name = preset.name,
					.detail = thicknessText(preset.thicknessMeters),
					.picked = preset.name == status.thicknessPreset,
					.onPick = onThicknessSelected,
				});
			},
			[&](ui::Component& card, const ThicknessPresetInfo& preset) {
				static_cast<StripCard&>(card).show(thicknessText(preset.thicknessMeters), preset.name == status.thicknessPreset);
			}
		);
	}

	void ConstructionConfigStrip::showReadouts() {
		ui::Text* top = readoutTop.get();
		ui::Text* bottom = readoutBottom.get();
		ui::Text* line = message.get();
		if (top == nullptr || bottom == nullptr || line == nullptr) {
			return;
		}
		const ui::Theme& theme = getContext()->getTheme();

		const float offset = status.foundationEdit ? kEditMessageOffset : kMessageOffset;
		if (readouts.get()->getSizeSpec().width.resolve(0.0F) != offset - kCardSpacing) {
			readouts.get()->setSize({offset - kCardSpacing, ui::kAuto});
			ui::Style topStyle = top->getDeclaredStyle();
			topStyle.color = theme.get(status.foundationEdit ? look::t::color::textBright : look::t::color::text);
			top->setStyle(topStyle);
		}

		if (status.foundationEdit) {
			// The mode and its locked material, then the outline the edit would leave and how much it
			// changes the foundation by.
			top->setText(status.editLabel + " (" + status.material + ")");
			bottom->setText(std::format("Area: {:.1f} m\xC2\xB2 ({:+.1f})", status.areaSquareMeters, status.areaDeltaSquareMeters));
		} else if (status.opening) {
			// The type comes from the Build menu; the validity line carries the snap and placement feedback.
			top->setText("Opening: " + status.openingType);
			bottom->setText(std::format("Width: {:.2f} m", status.openingWidthMeters));
		} else if (status.wall) {
			top->setText(std::format("Len: {:.1f} m  (seg {:.1f} m)", status.totalLengthMeters, status.segmentLengthMeters));
			bottom->setText(std::format("Cost: {:.0f}  Work: {:.0f}", status.wallCost, status.wallWork));
		} else {
			top->setText(std::format("Area: {:.1f} m\xC2\xB2", status.areaSquareMeters));
			bottom->setText(std::format("Points: {}", status.pointCount));
		}

		ui::ColorToken color = look::t::color::statusOk;
		std::string	   text = "Ready";
		if (status.opening) {
			if (status.valid) {
				text = "Click a wall to place";
			} else {
				// No wall in range is pending (move onto a built wall); a wall in range that fails the
				// placement gate is blocked.
				const bool blocked = !status.message.empty() && status.message != "no wall in range";
				color = blocked ? look::t::color::statusCrit : look::t::color::statusWarn;
				text = status.message.empty() ? "Hover a built wall" : status.message;
			}
		} else if (status.wall) {
			if (status.pointCount == 0) {
				color = look::t::color::statusWarn;
				text = "Click a foundation to start";
			} else {
				text = "Right-click / Enter to finish";
			}
			if (!status.valid && !status.message.empty()) {
				color = look::t::color::statusCrit;
				text = status.message;
			}
		} else {
			if (status.foundationEdit && status.pointCount == 0) {
				text = "Draw a region across the foundation edge";
			}
			if (status.pointCount > 0 && status.pointCount < 3) {
				color = look::t::color::statusWarn;
				text = "Keep placing points";
			}
			if (!status.valid && !status.message.empty()) {
				color = look::t::color::statusCrit;
				text = status.message;
			} else if (status.valid && status.pointCount >= 3) {
				color = look::t::color::statusOk;
				text = "Click origin to close";
			}
		}
		line->setText(std::move(text));
		if (!sameColor(color, messageColor)) {
			messageColor = color;
			ui::Style lineStyle = line->getDeclaredStyle();
			lineStyle.color = theme.get(color);
			line->setStyle(lineStyle);
		}
	}

} // namespace world_sim
