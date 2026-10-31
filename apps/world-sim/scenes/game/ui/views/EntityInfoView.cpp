#include "EntityInfoView.h"

#include "scenes/game/ui/dialogs/tabs/MeterDraw.h"
#include "scenes/shared/SalalUi.h"

#include <ecs/components/Needs.h>

#include <ui/Context.h>

#include <format>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

namespace world_sim {

	namespace {

		constexpr float kAvatarSize = 52.0F;
		constexpr float kIconButtonSize = 18.0F;
		constexpr float kActionButtonHeight = 26.0F;
		constexpr float kSlotButtonHeight = 28.0F;
		constexpr float kLogEmptyHeight = 64.0F;
		constexpr int	kBeltSlots = 2;

		// One row of a generic selection's slots. A TextListSlot is its header row and a row per item; a
		// key always names the same kind, so an update knows the component it gets.
		struct SlotRow {
			enum class Kind : std::uint8_t { Text, Bar, ListHeader, ListItem, Button };

			std::string			  key;
			Kind				  kind = Kind::Text;
			std::string			  text;
			float				  percent = 0.0F;
			std::function<void()> onClick;
		};

		std::vector<SlotRow> rowsOf(const std::vector<InfoSlot>& slots, std::uint32_t generation) {
			std::vector<SlotRow> rows;
			for (std::size_t index = 0; index < slots.size(); ++index) {
				const std::string key = std::format("{}_{}", generation, index);
				std::visit(
					[&](const auto& slot) {
						using T = std::decay_t<decltype(slot)>;
						if constexpr (std::is_same_v<T, TextSlot>) {
							rows.push_back({.key = key + "_text", .kind = SlotRow::Kind::Text, .text = slot.label + ": " + slot.value});
						} else if constexpr (std::is_same_v<T, ProgressBarSlot>) {
							rows.push_back({.key = key + "_bar", .kind = SlotRow::Kind::Bar, .text = slot.label, .percent = slot.value});
						} else if constexpr (std::is_same_v<T, TextListSlot>) {
							rows.push_back({.key = key + "_list", .kind = SlotRow::Kind::ListHeader, .text = slot.header + ":"});
							for (std::size_t item = 0; item < slot.items.size(); ++item) {
								rows.push_back(
									{.key = std::format("{}_item_{}", key, item),
									 .kind = SlotRow::Kind::ListItem,
									 .text = "- " + slot.items[item]}
								);
							}
						} else if constexpr (std::is_same_v<T, ActionButtonSlot>) {
							rows.push_back(
								{.key = key + "_button", .kind = SlotRow::Kind::Button, .text = slot.label, .onClick = slot.onClick}
							);
						}
					},
					slots[index]
				);
			}
			return rows;
		}

	} // namespace

	EntityInfoView::EntityInfoView(const Args& args)
		: theme::Panel({.variant = theme::PanelVariant::Panel, .accent = theme::PanelAccent::Accent, .flush = true}),
		  modelCallbacks({
			  .onOpenCraftingDialog = args.onOpenCraftingDialog,
			  .onPlace = args.onPlace,
			  .onMoveFurniture = args.onMoveFurniture,
			  .onOpenStorageConfig = args.onOpenStorageConfig,
			  .queryResources = args.queryResources,
			  .onDemolishFoundation = args.onDemolishFoundation,
			  .onDemolishBuilding = args.onDemolishBuilding,
			  .onAddToFoundation = args.onAddToFoundation,
			  .onSubtractFromFoundation = args.onSubtractFromFoundation,
			  .onDemolishWallSegment = args.onDemolishWallSegment,
			  .onDemolishOpening = args.onDemolishOpening,
		  }),
		  onClose(args.onClose),
		  onDetails(args.onDetails),
		  onToggleControl(args.onToggleControl),
		  onGoTo(args.onGoTo) {}

	void EntityInfoView::onMount() {
		if (getSizeSpec().width.isAuto()) {
			setSize({kWidth, getSizeSpec().height});
		}
		theme::Panel::onMount();
		// Nothing is selected until the model says otherwise.
		setVisible(false);

		const ui::Theme& theme = getContext()->getTheme();
		const float		 gap = theme.get(tokens::spacing::space2);
		const float		 tight = theme.get(tokens::spacing::space1);
		const ui::Style	 bodyText = world_sim::text(theme, {.size = tokens::fontSize::fsSm, .color = tokens::color::text});
		const ui::Style	 dimText = world_sim::text(theme, {.size = tokens::fontSize::fsSm, .color = tokens::color::textDim});

		const auto column = [](float spacing) {
			return ui::Layout{.display = ui::Display::Flex, .flexDirection = ui::FlexDirection::Column, .gap = spacing};
		};

		ui::Ref<ui::Component> root = body().addChild<ui::Component>({
			.id = "entity_info_root",
			.size = {ui::percent(100.0F), ui::kAuto},
			.layout = {
				.display = ui::Display::Flex,
				.flexDirection = ui::FlexDirection::Column,
				.gap = gap,
				.padding = theme.get(tokens::spacing::space3)
			},
		});

		// The header: avatar, identity column, the eye and close buttons.
		ui::Ref<ui::Component> header = root.addChild<ui::Component>({
			.id = "entity_info_header",
			.layout = {.display = ui::Display::Flex, .gap = gap, .alignItems = ui::AlignItems::Start},
		});
		avatarSlot = header.addChild<ui::Component>({.id = "entity_info_avatar", .size = {kAvatarSize, kAvatarSize}});
		ui::Layout identityLayout = column(tight);
		identityLayout.flexGrow = 1.0F;
		ui::Ref<ui::Component> identity =
			header.addChild<ui::Component>({.id = "entity_info_identity", .size = {0.0F, ui::kAuto}, .layout = identityLayout});
		nameText = identity.addChild<ui::Text>(
			{.id = "entity_info_name",
			 .style = world_sim::text(theme, {.size = tokens::fontSize::fsMd, .color = tokens::color::textBright}),
			 .wrap = true}
		);
		subtitleText = identity.addChild<ui::Text>({
			.id = "entity_info_subtitle",
			.visible = false,
			.style = world_sim::text(theme, {.size = tokens::fontSize::fsXs, .color = tokens::color::textDim}),
			.wrap = true,
		});
		moodBar = identity.addChild<ui::ProgressBar>({
			.id = "entity_info_mood",
			.value = 1.0F,
			.tone = ui::Tone::Auto,
			.label = "Mood",
			.controlSize = ui::ControlSize::Sm,
			.inlineLabel = true,
		});
		taskBar = identity.addChild<ui::ProgressBar>({
			.id = "entity_info_task",
			.value = 0.0F,
			.tone = ui::Tone::Accent,
			.label = "Idle",
			.controlSize = ui::ControlSize::Sm,
			.inlineLabel = true,
		});
		ui::Ref<ui::Component> buttons = header.addChild<ui::Component>({
			.id = "entity_info_buttons",
			.layout = {.display = ui::Display::Flex, .gap = tight, .flexShrink = 0.0F},
		});

		// Small inset squares; the state layers give them their hover.
		const ui::Style glyph{
			.backgroundColor = theme.get(tokens::color::bgInset),
			.borderColor = theme.get(tokens::color::lineEdge),
			.borderWidth = theme.get(tokens::border::bw),
			.borderRadius = theme.get(tokens::radius::rSm),
			.color = theme.get(tokens::color::textDim),
		};
		const auto glyphButton = [&](const char* id, const char* icon, std::function<void()> press) {
			return buttons.addChild<ui::Button>({
				.id = id,
				.size = {kIconButtonSize, kIconButtonSize},
				.style = glyph,
				.icon = icon,
				.controlSize = ui::ControlSize::Sm,
				.onPress = std::move(press),
			});
		};
		eyeButton = glyphButton("entity_info_details", "eye", [this]() {
			if (onDetails) {
				onDetails();
			}
		});
		glyphButton("entity_info_close", "close", [this]() {
			if (onClose) {
				onClose();
			}
		});

		tabBar = root.addChild<ui::TabBar>({
			.id = "entity_info_tabs",
			.tabs =
				{{.id = "needs", .label = "Needs"},
				 {.id = "bio", .label = "Bio"},
				 {.id = "gear", .label = "Gear"},
				 {.id = "log", .label = "Log"}},
			.selectedId = "needs",
			.onSelect = [this](const std::string& tabId) { setActiveTab(tabId); },
		});

		// Needs: a bar per need.
		needsBody = root.addChild<ui::Component>({.id = "entity_info_needs", .layout = column(tight)});
		const auto needCount = static_cast<std::size_t>(ecs::NeedType::Count);
		needBars.clear();
		for (std::size_t i = 0; i < needCount; ++i) {
			needBars.push_back(needsBody.addChild<NeedBar>(
				{.id = "entity_info_need_" + std::to_string(i), .label = ecs::needLabel(static_cast<ecs::NeedType>(i))}
			));
		}

		bioBody = root.addChild<ui::Component>({.id = "entity_info_bio", .visible = false, .layout = column(tight)});
		bioAge = bioBody.addChild<ui::Text>({.id = "entity_info_bio_age", .style = bodyText, .wrap = true});
		bioMood = bioBody.addChild<ui::Text>({.id = "entity_info_bio_mood", .style = bodyText, .wrap = true});

		// Gear: what's in hand, the belt's tools, the carry meter, the backpack.
		gearBody = root.addChild<ui::Component>(
			{.id = "entity_info_gear", .visible = false, .layout = column(theme.get(tokens::spacing::space15))}
		);
		gearHands =
			gearBody.addChild<ui::Text>({.id = "entity_info_gear_hands", .style = bodyText, .text = "Armed: (empty)", .wrap = true});
		ui::Ref<ui::Component> belt = gearBody.addChild<ui::Component>({
			.id = "entity_info_belt",
			.layout = {.display = ui::Display::Flex, .gap = tight, .alignItems = ui::AlignItems::Center},
		});
		belt.addChild<ui::Text>({.id = "entity_info_belt_label", .style = dimText, .text = "Belt:"});
		beltChips.clear();
		for (int i = 0; i < kBeltSlots; ++i) {
			beltChips.push_back(
				belt.addChild<ui::Badge>({.id = "entity_info_belt_chip_" + std::to_string(i), .visible = false, .tone = ui::Tone::Info})
			);
		}
		beltEmpty = belt.addChild<ui::Text>({.id = "entity_info_belt_empty", .style = dimText, .text = "(empty)"});
		carryBar = gearBody.addChild<ui::ProgressBar>({
			.id = "entity_info_carry",
			.value = 0.0F,
			.tone = ui::Tone::Info,
			.label = "Carry",
			.controlSize = ui::ControlSize::Sm,
			.inlineLabel = true,
		});
		gearPack =
			gearBody.addChild<ui::Text>({.id = "entity_info_gear_pack", .style = dimText, .text = "Backpack: (empty)", .wrap = true});

		// Log: a stub until an activity log exists.
		logBody = root.addChild<tabs::EmptyState>({
			.id = "entity_info_log",
			.visible = false,
			.minSize = {ui::kAuto, kLogEmptyHeight},
			.title = "No activity log yet",
			.subtitle = "A chronological log arrives with the events update.",
		});

		actionRow = root.addChild<ui::Component>({.id = "entity_info_actions", .layout = {.display = ui::Display::Flex, .gap = gap}});
		const auto actionButton = [&](const char* id, const char* label, bool enabled, std::function<void()> press) {
			return actionRow.addChild<ui::Button>({
				.id = id,
				.enabled = enabled,
				.size = {0.0F, kActionButtonHeight},
				.layout = {.flexGrow = 1.0F},
				.label = label,
				.variant = ui::ButtonVariant::Secondary,
				.onPress = std::move(press),
			});
		};
		draftButton = actionButton("entity_info_draft", "Draft", true, [this]() {
			if (onToggleControl && selectedColonistId != ecs::EntityID{0}) {
				onToggleControl(selectedColonistId);
			}
		});
		actionButton("entity_info_goto", "Go to", true, [this]() {
			if (onGoTo && selectedColonistId != ecs::EntityID{0}) {
				onGoTo(selectedColonistId);
			}
		});
		actionButton("entity_info_priorities", "Priorities", false, {});

		slotsBody = root.addChild<ui::Component>({.id = "entity_info_slots", .visible = false, .layout = column(gap)});
	}

	void EntityInfoView::update(
		ecs::World&							   world,
		const assets::AssetRegistry&		   assetRegistry,
		const assets::RecipeRegistry&		   recipeRegistry,
		const Selection&					   selection,
		const construction::ConstructionWorld* constructionWorld,
		const ecs::RoomDetectionSystem*		   roomDetection
	) {
		const auto updateType =
			model.refresh(selection, world, assetRegistry, recipeRegistry, modelCallbacks, constructionWorld, roomDetection);
		switch (updateType) {
			case EntityInfoModel::UpdateType::None:
				break;
			case EntityInfoModel::UpdateType::Hide:
				setVisible(false);
				break;
			case EntityInfoModel::UpdateType::Show:
				setVisible(true);
				applyContent(model.content(), true);
				break;
			case EntityInfoModel::UpdateType::Structure:
				applyContent(model.content(), true);
				break;
			case EntityInfoModel::UpdateType::Values:
				applyContent(model.content(), false);
				break;
		}
	}

	void EntityInfoView::applyContent(const PanelContent& content, bool structural) {
		if (nameText.get() == nullptr) {
			return;
		}
		if (content.colonist.has_value()) {
			nameText.get()->setText(content.colonist->name);
			applyColonist(*content.colonist, structural);
		} else {
			nameText.get()->setText(content.title);
			applyGeneric(content, structural);
		}
	}

	void EntityInfoView::applyColonist(const ColonistPanelData& data, bool structural) {
		selectedColonistId = data.id;

		avatarSlot.get()->setVisible(true);
		showAvatar(data.name, data.moodValue / 100.0F);
		subtitleText.get()->setVisible(false);
		moodBar.get()->setVisible(true);
		taskBar.get()->setVisible(true);
		eyeButton.get()->setVisible(static_cast<bool>(onDetails));
		tabBar.get()->setVisible(true);
		actionRow.get()->setVisible(true);
		slotsBody.get()->setVisible(false);

		ui::ProgressBar& mood = *moodBar.get();
		mood.setValue(data.moodValue / 100.0F);
		mood.setValueText(std::format("{}% {}", static_cast<int>(data.moodValue), data.moodLabel));

		const bool		 acting = data.taskProgress >= 0.0F;
		ui::ProgressBar& task = *taskBar.get();
		task.setLabel(data.currentTask);
		task.setValue(acting ? data.taskProgress : 0.0F);
		task.setValueText(acting ? std::format("{}%", static_cast<int>(data.taskProgress * 100.0F)) : std::string());

		for (std::size_t i = 0; i < needBars.size(); ++i) {
			NeedBar*   bar = needBars[i].get();
			const bool has = i < data.needs.size();
			bar->setVisible(has);
			if (has) {
				bar->setLabel(data.needs[i].label);
				bar->setPercent(data.needs[i].value);
			}
		}

		bioAge.get()->setText("Age: " + data.age);
		bioMood.get()->setText(std::format("Mood: {}% {}", static_cast<int>(data.moodValue), data.moodLabel));

		gearHands.get()->setText("Armed: " + data.hands);
		for (std::size_t i = 0; i < beltChips.size(); ++i) {
			ui::Badge* chip = beltChips[i].get();
			const bool has = i < data.belt.size();
			chip->setVisible(has);
			if (has) {
				chip->setLabel(data.belt[i]);
			}
		}
		beltEmpty.get()->setVisible(data.belt.empty());

		const float ratio = data.capacityKg > 0.0F ? data.carriedKg / data.capacityKg : 0.0F;
		carryBar.get()->setValue(ratio);
		carryBar.get()->setValueText(
			std::format("{} / {} kg", static_cast<int>(data.carriedKg + 0.5F), static_cast<int>(data.capacityKg + 0.5F))
		);

		std::string pack = "Backpack:";
		if (data.backpack.empty()) {
			pack += " (empty)";
		}
		for (const std::string& line : data.backpack) {
			pack += "\n" + line;
		}
		gearPack.get()->setText(std::move(pack));

		draftButton.get()->setLabel(data.controlled ? "Release" : "Draft");

		if (structural) {
			tabBar.get()->setSelectedId("needs");
			setActiveTab("needs");
		}
	}

	void EntityInfoView::applyGeneric(const PanelContent& content, bool structural) {
		selectedColonistId = ecs::EntityID{0};

		avatarSlot.get()->setVisible(false);
		moodBar.get()->setVisible(false);
		taskBar.get()->setVisible(false);
		eyeButton.get()->setVisible(false);
		tabBar.get()->setVisible(false);
		needsBody.get()->setVisible(false);
		bioBody.get()->setVisible(false);
		gearBody.get()->setVisible(false);
		logBody.get()->setVisible(false);
		actionRow.get()->setVisible(false);
		slotsBody.get()->setVisible(!content.slots.empty());

		subtitleText.get()->setText(content.subtitle);
		subtitleText.get()->setVisible(!content.subtitle.empty());

		if (structural) {
			++slotGeneration;
		}
		showSlots(content.slots);
	}

	// Keyed by the slot's place and the selection's generation: a values update keeps every row, a list
	// that grows or shrinks gains or loses only its items, and a new selection builds them all.
	void EntityInfoView::showSlots(const std::vector<InfoSlot>& slots) {
		ui::Component* column = slotsBody.get();
		if (column == nullptr) {
			return;
		}
		const ui::Theme& theme = getContext()->getTheme();
		column->reconcileChildren(
			rowsOf(slots, slotGeneration),
			[](const SlotRow& row) { return row.key; },
			[&](const SlotRow& row) -> ui::Ref<ui::Component> {
				switch (row.kind) {
					case SlotRow::Kind::Bar:
						return column->addChild<NeedBar>({.label = row.text, .percent = row.percent});
					case SlotRow::Kind::Button:
						return column->addChild<ui::Button>({
							.size = {ui::percent(100.0F), kSlotButtonHeight},
							.label = row.text,
							.variant = ui::ButtonVariant::Secondary,
							.onPress = row.onClick,
						});
					case SlotRow::Kind::ListHeader:
						return column->addChild<ui::Text>(
							{.style = world_sim::text(theme, {.size = tokens::fontSize::fsSm, .color = tokens::color::textDim}),
							 .text = row.text,
							 .wrap = true}
						);
					case SlotRow::Kind::Text:
					case SlotRow::Kind::ListItem:
						break;
				}
				return column->addChild<ui::Text>(
					{.style = world_sim::text(theme, {.size = tokens::fontSize::fsSm, .color = tokens::color::text}),
					 .text = row.text,
					 .wrap = true}
				);
			},
			[](ui::Component& shown, const SlotRow& row) {
				switch (row.kind) {
					case SlotRow::Kind::Bar: {
						auto& bar = static_cast<NeedBar&>(shown);
						bar.setLabel(row.text);
						bar.setPercent(row.percent);
						return;
					}
					case SlotRow::Kind::Button: {
						auto& button = static_cast<ui::Button&>(shown);
						button.setLabel(row.text);
						button.setOnPress(row.onClick);
						return;
					}
					case SlotRow::Kind::Text:
					case SlotRow::Kind::ListHeader:
					case SlotRow::Kind::ListItem:
						break;
				}
				static_cast<ui::Text&>(shown).setText(row.text);
			}
		);
	}

	// theme::Avatar takes its seed once, so another colonist gets a new one.
	void EntityInfoView::showAvatar(const std::string& seed, float mood) {
		if (theme::Avatar* shown = avatar.get(); shown != nullptr && seed == avatarSeed) {
			shown->setMood(mood);
			return;
		}
		if (avatar.get() != nullptr) {
			avatarSlot.get()->removeChild(avatar);
		}
		avatarSeed = seed;
		avatar = avatarSlot.addChild<theme::Avatar>({.size = {kAvatarSize, kAvatarSize}, .seed = seed, .mood = mood});
	}

	void EntityInfoView::setActiveTab(const std::string& tabId) {
		needsBody.get()->setVisible(tabId == "needs");
		bioBody.get()->setVisible(tabId == "bio");
		gearBody.get()->setVisible(tabId == "gear");
		logBody.get()->setVisible(tabId == "log");
	}

} // namespace world_sim
