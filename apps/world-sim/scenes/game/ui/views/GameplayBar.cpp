#include "GameplayBar.h"

#include "scenes/game/ui/Look.h"

#include <ui/Context.h>

namespace world_sim {

	namespace {

		constexpr float kButtonWidth = 100.0F;
		constexpr float kButtonHeight = 36.0F;
		constexpr float kButtonSpacing = 8.0F;
		constexpr float kHorizontalPadding = 12.0F;
		constexpr float kRoomsButtonWidth = 72.0F; // a single word, narrower than the dropdowns

		ui::MenuItem item(std::string label, const std::function<void(const std::string&)>& callback, std::string value) {
			return {.label = std::move(label), .onSelect = [callback, value = std::move(value)]() {
						if (callback) {
							callback(value);
						}
					}};
		}

	} // namespace

	GameplayBar::GameplayBar(const Args& args)
		: onActionSelected(args.onActionSelected),
		  onProductionSelected(args.onProductionSelected),
		  onFurnitureSelected(args.onFurnitureSelected),
		  onStructureSelected(args.onStructureSelected),
		  onRoomsToggle(args.onRoomsToggle) {}

	void GameplayBar::onMount() {
		const ui::Theme& theme = getContext()->getTheme();
		if (getSizeSpec().height.isAuto()) {
			setSize({getSizeSpec().width, kHeight});
		}
		ui::Layout row = getLayout();
		row.display = ui::Display::Flex;
		row.flexDirection = ui::FlexDirection::Row;
		row.gap = kButtonSpacing;
		row.padding = {0.0F, kHorizontalPadding};
		row.alignItems = ui::AlignItems::Center;
		setLayout(row);
		setStyle(
			ui::Style{
				.backgroundColor = theme.get(look::t::color::bgPanel),
				.borderColor = theme.get(look::t::color::lineEdge),
				.borderWidth = theme.get(look::t::border::bw),
				.borderRadius = theme.get(look::t::radius::rMd),
			}
				.extend(getDeclaredStyle())
		);

		const auto dropdown = [&](const char* id, const char* label, std::vector<ui::MenuItem> items) {
			ui::Ref<ui::DropdownButton> made = addChild<ui::DropdownButton>(
				{.id = id, .size = {kButtonWidth, kButtonHeight}, .label = label, .items = std::move(items), .side = ui::Side::Above}
			);
			dropdowns.push_back(made);
			return made;
		};
		dropdown(
			"actions_dropdown",
			"Actions",
			{item("Hunt", onActionSelected, "hunt"), item("Harvest", onActionSelected, "harvest"), item("Haul", onActionSelected, "haul")}
		);
		dropdown(
			"build_dropdown",
			"Build",
			{item("Foundation", onStructureSelected, "foundation"),
			 item("Wall", onStructureSelected, "wall"),
			 item("Door", onStructureSelected, "door"),
			 item("Window", onStructureSelected, "window")}
		);
		production = dropdown("production_dropdown", "Production", {});
		dropdown(
			"furniture_dropdown",
			"Furniture",
			{item("Beds", onFurnitureSelected, "beds"),
			 item("Tables", onFurnitureSelected, "tables"),
			 item("Storage", onFurnitureSelected, "storage")}
		);
		// Placeholders: no zone system exists yet, this holds the slot the zones work will fill.
		dropdown(
			"zones_dropdown",
			"Zones",
			{{.label = "Stockpile", .enabled = false}, {.label = "Growing", .enabled = false}, {.label = "Dumping", .enabled = false}}
		);

		rooms = addChild<ui::Button>({
			.id = "rooms_toggle",
			.size = {kRoomsButtonWidth, kButtonHeight},
			.label = "Rooms",
			.variant = ui::ButtonVariant::Secondary,
			.onPress = [this]() {
				if (onRoomsToggle) {
					onRoomsToggle();
				}
			},
		});
		tintRooms();
	}

	void GameplayBar::closeAllDropdowns() {
		for (const ui::Ref<ui::DropdownButton>& each : dropdowns) {
			if (ui::DropdownButton* shown = each.get()) {
				shown->closeMenu();
			}
		}
	}

	void GameplayBar::setProductionItems(const std::vector<std::pair<std::string, std::string>>& items) {
		ui::DropdownButton* shown = production.get();
		if (shown == nullptr) {
			return;
		}
		std::vector<ui::MenuItem> menu;
		menu.reserve(items.size());
		for (const auto& [defName, label] : items) {
			menu.push_back(item(label, onProductionSelected, defName));
		}
		shown->setItems(std::move(menu));
	}

	void GameplayBar::setRoomsActive(bool active) {
		if (roomsActive != active) {
			roomsActive = active;
			tintRooms();
		}
	}

	// Armed, the toggle takes the accent with a dark label; off, a quiet inset.
	void GameplayBar::tintRooms() {
		ui::Button* button = rooms.get();
		if (button == nullptr) {
			return;
		}
		const ui::Theme& theme = getContext()->getTheme();
		button->setStyle({
			.backgroundColor = theme.get(roomsActive ? look::t::color::accent : look::t::color::bgInset),
			.color = theme.get(roomsActive ? look::t::color::accentContrast : look::t::color::text),
		});
	}

} // namespace world_sim
