#include "AssetRow.h"

#include <ui/Context.h>
#include <ui/shapes/Text.h>
#include <ui/theme/Tokens.h>

#include <cctype>
#include <string>

namespace asset_manager {

	namespace {
		constexpr float kRowHeight = 34.0F;
		constexpr float kThumbSize = 26.0F;
		constexpr float kIndent = 22.0F;
		constexpr float kGap = 8.0F;
		constexpr float kChevron = 12.0F;

		std::string toUpper(std::string s) {
			for (auto& c : s) {
				c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
			}
			return s;
		}

		ui::Layout row(float indent) {
			return {
				.display = ui::Display::Flex,
				.flexDirection = ui::FlexDirection::Row,
				.gap = kGap,
				.padding = {0.0F, kGap, 0.0F, indent},
				.alignItems = ui::AlignItems::Center,
			};
		}
	} // namespace

	AssetRow::AssetRow(const Args& args)
		: defName(args.defName),
		  selectedAtStart(args.selected),
		  onSelect(args.onSelect) {}

	ui::StyleId AssetRow::themeStyle() const {
		return ui::styles::listRow.theme;
	}

	void AssetRow::onMount() {
		Control::onMount();
		const ui::Theme& theme = getContext()->getTheme();
		setLayout(row(kIndent));
		if (getSizeSpec().height.isAuto()) {
			setSize({getSizeSpec().width, kRowHeight});
		}
		bar = addChild<ui::Rectangle>({
			.size = {theme.get(ui::tokens::lineWidth::thick), ui::kAuto},
			.layout = {.position = ui::Position::Absolute, .inset = {.top = 0.0F, .bottom = 0.0F, .left = 0.0F}},
			.style = {.backgroundColor = theme.get(ui::tokens::tone::accent::color)},
		});
		addChild<AssetThumbnail>({.size = {kThumbSize, kThumbSize}, .defName = defName});
		addChild<ui::Text>({.layout = {.flexGrow = 1.0F}, .style = ui::styles::text::body, .text = defName});
		setSelected(selectedAtStart);
	}

	void AssetRow::setSelected(bool value) {
		setActive(value);
		if (ui::Rectangle* shown = bar.get()) {
			shown->setVisible(value);
		}
	}

	void AssetRow::activate() {
		if (onSelect) {
			onSelect(defName);
		}
	}

	GroupHeaderRow::GroupHeaderRow(const Args& args)
		: label(args.label),
		  expanded(args.expanded),
		  onToggle(args.onToggle) {}

	ui::StyleId GroupHeaderRow::themeStyle() const {
		return ui::styles::listRow.theme;
	}

	void GroupHeaderRow::onMount() {
		Control::onMount();
		setLayout(row(kGap));
		if (getSizeSpec().height.isAuto()) {
			setSize({getSizeSpec().width, kRowHeight});
		}
		addChild<ui::Icon>(
			{.size = {kChevron, kChevron}, .style = ui::styles::text::secondary, .name = expanded ? "chevronDown" : "chevronRight"}
		);
		addChild<ui::Text>({.style = ui::styles::label, .text = toUpper(label)});
	}

	void GroupHeaderRow::activate() {
		if (onToggle) {
			onToggle();
		}
	}

} // namespace asset_manager
