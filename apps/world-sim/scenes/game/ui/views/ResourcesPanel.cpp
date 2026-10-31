#include "ResourcesPanel.h"

#include "scenes/shared/SalalUi.h"

#include <ui/Context.h>
#include <ui/components/Button.h>
#include <ui/components/ListRow.h>

#include <string>

namespace world_sim {

	namespace {
		constexpr float kHeaderHeight = 28.0F;
		constexpr float kMaxExpandedHeight = 240.0F; // header and content
		constexpr float kRowHeight = 24.0F;
		constexpr float kChevronSize = 12.0F;
		constexpr float kChevronRightPadding = 8.0F;
		constexpr float kScrollbarReserve = 16.0F; // so a count never sits under the scrollbar
	} // namespace

	ResourcesPanel::ResourcesPanel(const Args& args)
		: onToggle(args.onToggle) {}

	void ResourcesPanel::onMount() {
		const ui::Theme& theme = getContext()->getTheme();
		const float		 inset = theme.get(tokens::spacing::space2);
		if (getSizeSpec().width.isAuto()) {
			setSize({kWidth, getSizeSpec().height});
		}
		ui::Layout column = getLayout();
		column.display = ui::Display::Flex;
		column.flexDirection = ui::FlexDirection::Column;
		setLayout(column);

		ui::Ref<ui::Button> header = addChild<ui::Button>({
			.id = "resources_header",
			.size = {ui::percent(100.0F), kHeaderHeight},
			.label = "Storage",
			.variant = ui::ButtonVariant::Secondary,
			.onPress = [this]() { toggle(); },
		});
		chevron = header.addChild<ui::Icon>({
			.id = "resources_chevron",
			.size = {kChevronSize, kChevronSize},
			.layout =
				{.position = ui::Position::Absolute,
				 .inset = {.top = (kHeaderHeight - kChevronSize) * 0.5F, .right = kChevronRightPadding}},
			.style = {.color = theme.get(tokens::color::textBright)},
			.name = "chevron_down",
		});

		content = addChild<ui::Rectangle>({
			.id = "resources_content",
			.visible = false,
			.layout = {.display = ui::Display::Flex, .flexDirection = ui::FlexDirection::Column, .padding = inset},
			.style = {
				.backgroundColor = theme.get(tokens::color::bgPanel),
				.borderColor = theme.get(tokens::color::lineEdge),
				.borderWidth = theme.get(tokens::border::bw),
				.borderRadius = theme.get(tokens::radius::rMd)
			},
		});
		emptyMessage = content.addChild<ui::Text>({
			.id = "resources_empty_msg",
			.size = {ui::percent(100.0F), ui::kAuto},
			.style = world_sim::text(theme, {.size = tokens::fontSize::fsSm, .color = tokens::color::textDim}),
			.text = "No stockpiles built. Create one to track colony resources.",
			.wrap = true,
		});
		list = content.addChild<ui::Component>({
			.id = "resources_scroll",
			.maxSize = {ui::kAuto, kMaxExpandedHeight - kHeaderHeight - (inset * 2.0F)},
			.layout =
				{.display = ui::Display::Flex,
				 .flexDirection = ui::FlexDirection::Column,
				 .padding = {0.0F, kScrollbarReserve, 0.0F, 0.0F}},
			.overflow = ui::Overflow::Scroll,
		});
		show();
	}

	void ResourcesPanel::setResources(const std::vector<adapters::ResourceRowData>& value, std::size_t containers) {
		rows = value;
		containerCount = containers;
		if (ui::Component* column = list.get()) {
			column->reconcileChildren(
				rows,
				[](const adapters::ResourceRowData& row) { return row.defName; },
				[&](const adapters::ResourceRowData& row) -> ui::Ref<ui::Component> {
					return column->addChild<ui::ListRow>({
						.id = "resource_row_" + row.defName,
						.size = {ui::kAuto, kRowHeight},
						.label = row.displayName,
						.trailing = std::to_string(row.count),
					});
				},
				[](ui::Component& shown, const adapters::ResourceRowData& row) {
					auto& listRow = static_cast<ui::ListRow&>(shown);
					listRow.setLabel(row.displayName);
					listRow.setTrailing(std::to_string(row.count));
				}
			);
		}
		show();
	}

	void ResourcesPanel::toggle() {
		expanded = !expanded;
		show();
		if (onToggle) {
			onToggle();
		}
	}

	void ResourcesPanel::show() {
		if (content.get() == nullptr) {
			return;
		}
		content.get()->setVisible(expanded);
		emptyMessage.get()->setVisible(containerCount == 0);
		list.get()->setVisible(containerCount > 0);
		// Up while expanded, a press collapses it; down while collapsed.
		chevron.get()->setName(expanded ? "chevron_up" : "chevron_down");
	}

} // namespace world_sim
