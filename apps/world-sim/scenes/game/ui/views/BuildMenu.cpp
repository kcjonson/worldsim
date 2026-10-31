#include "BuildMenu.h"

#include "scenes/game/ui/Look.h"

#include <ui/Context.h>
#include <ui/components/Button.h>
#include <ui/shapes/Text.h>

namespace world_sim {

	namespace {
		constexpr float kPadding = 10.0F;
		constexpr float kTitleHeight = 24.0F;
		constexpr float kButtonHeight = 32.0F;
		constexpr float kButtonSpacing = 4.0F;
	} // namespace

	BuildMenu::BuildMenu(const Args& args)
		: onSelect(args.onSelect),
		  onClose(args.onClose) {}

	void BuildMenu::onMount() {
		const ui::Theme& theme = getContext()->getTheme();
		if (getSizeSpec().width.isAuto()) {
			setSize({kWidth, getSizeSpec().height});
		}
		ui::Layout column = getLayout();
		column.display = ui::Display::Flex;
		column.flexDirection = ui::FlexDirection::Column;
		column.gap = kPadding;
		column.padding = kPadding;
		setLayout(column);
		setStyle(
			ui::Style{
				.backgroundColor = theme.get(look::t::color::bgPanelRaised),
				.borderColor = theme.get(look::t::color::lineEdge),
				.borderWidth = theme.get(look::t::border::bw),
				.borderRadius = theme.get(look::t::radius::rMd),
			}
				.extend(getDeclaredStyle())
		);
		setOnKeyDown([this](ui::KeyEvent& event) {
			if (event.type == ui::KeyEvent::Type::Cancel && onClose) {
				onClose();
				event.consume();
			}
		});

		ui::Style title = look::text(theme, look::t::color::textBright, look::t::fontSize::fsMd);
		title.textAlign = Foundation::HorizontalAlign::Center;
		title.verticalAlign = Foundation::VerticalAlign::Middle;
		addChild<ui::Text>({.id = "build_menu_title", .size = {ui::percent(100.0F), kTitleHeight}, .style = title, .text = "Build"});
		list = addChild<ui::Component>(
			{.layout = {.display = ui::Display::Flex, .flexDirection = ui::FlexDirection::Column, .gap = kButtonSpacing}}
		);
		showItems();
	}

	void BuildMenu::setItems(const std::vector<BuildMenuItem>& value) {
		items = value;
		showItems();
	}

	void BuildMenu::showItems() {
		ui::Component* column = list.get();
		if (column == nullptr) {
			return;
		}
		const auto press = [this](const std::string& defName) {
			return [this, defName]() {
				if (onSelect) {
					onSelect(defName);
				}
			};
		};
		column->reconcileChildren(
			items,
			[](const BuildMenuItem& item) { return item.defName; },
			[&](const BuildMenuItem& item) -> ui::Ref<ui::Component> {
				return column->addChild<ui::Button>({
					.id = "build_item",
					.size = {ui::kAuto, kButtonHeight},
					.label = item.label,
					.variant = ui::ButtonVariant::Secondary,
					.onPress = press(item.defName),
				});
			},
			[](ui::Component& row, const BuildMenuItem& item) { static_cast<ui::Button&>(row).setLabel(item.label); }
		);
	}

} // namespace world_sim
