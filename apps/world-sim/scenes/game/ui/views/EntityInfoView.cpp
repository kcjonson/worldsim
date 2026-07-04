#include "EntityInfoView.h"

#include "scenes/game/ui/dialogs/tabs/MeterDraw.h"

#include <components/avatar/Avatar.h>
#include <components/badge/Badge.h>
#include <components/icon/Icon.h>
#include <components/panel/Panel.h>
#include <ecs/components/Needs.h>
#include <primitives/Primitives.h>
#include <theme/Tokens.h>
#include <theme/Variants.h>

#include <string>
#include <utility>

namespace world_sim {

	// ========================================================================
	// Leaf components (arena-owned, layout-engine sized)
	// ========================================================================

	/// Deterministic colonist portrait, mood-tinted (wraps the immediate-mode
	/// UI::Avatar so it can live in a LayoutContainer).
	class EntityInfoView::AvatarChip : public UI::Component {
	  public:
		explicit AvatarChip(float sizePx) { size = {sizePx, sizePx}; }

		void setIdentity(const std::string& newSeed, float newMood) {
			seed = newSeed;
			mood = newMood;
		}

		void render() override {
			UI::Avatar(UI::Avatar::Args{.position = getContentPosition(), .size = size.x, .seed = seed, .mood = mood}).render();
		}

		const char* debugTypeName() const override { return "AvatarChip"; }
		const char* debugId() const override { return "entity_info_avatar"; }

		bool containsPoint(Foundation::Vec2 point) const override {
			return point.x >= position.x && point.x <= position.x + size.x && point.y >= position.y && point.y <= position.y + size.y;
		}

	  private:
		std::string seed;
		float		mood{1.0F};
	};

	/// Small square icon button (eye/close) drawn from a Salvage glyph.
	class EntityInfoView::GlyphButton : public UI::Component {
	  public:
		struct Args {
			std::string			  glyph;
			std::function<void()> onClick;
			const char*			  id{nullptr};
		};

		explicit GlyphButton(Args args)
			: onClick(std::move(args.onClick)),
			  id(args.id) {
			size = {kIconButtonSize, kIconButtonSize};
			iconHandle = addChild(UI::Icon(UI::Icon::Args{
				.size = kIconButtonSize - kIconPad * 2.0F,
				.glyph = std::move(args.glyph),
				.tint = UI::text_dim,
			}));
		}

		void setPosition(float x, float y) override {
			Component::setPosition(x, y);
			if (auto* icon = getChild<UI::Icon>(iconHandle)) {
				icon->setPosition(x + kIconPad, y + kIconPad);
			}
		}

		void render() override {
			const Foundation::Rect bounds{position.x, position.y, size.x, size.y};
			Renderer::Primitives::drawRect(
				{.bounds = bounds,
				 .style = {
					 .fill = UI::bg_inset,
					 .border = Foundation::BorderStyle{
						 .color = UI::line_edge, .width = UI::bw, .cornerRadius = UI::r_sm, .position = Foundation::BorderPosition::Inside}}}
			);
			if (hovered) {
				Renderer::Primitives::drawRect({.bounds = bounds, .style = {.fill = UI::bg_hover}});
			}
			Component::render();
		}

		bool handleEvent(UI::InputEvent& event) override {
			if (event.type == UI::InputEvent::Type::MouseMove) {
				hovered = containsPoint(event.position);
				return false;
			}
			if (event.type == UI::InputEvent::Type::MouseUp && event.button == engine::MouseButton::Left &&
				containsPoint(event.position)) {
				if (onClick) {
					onClick();
				}
				event.consume();
				return true;
			}
			return false;
		}

		bool containsPoint(Foundation::Vec2 point) const override {
			return point.x >= position.x && point.x <= position.x + size.x && point.y >= position.y && point.y <= position.y + size.y;
		}

		const char* debugTypeName() const override { return "GlyphButton"; }
		const char* debugId() const override { return id; }

	  private:
		static constexpr float kIconPad = 3.0F;

		std::function<void()> onClick;
		const char*			  id{nullptr};
		UI::LayerHandle		  iconHandle;
		bool				  hovered{false};
	};

	/// Inline badge pill (belt tool chip); width follows the label.
	class EntityInfoView::BadgeChip : public UI::Component {
	  public:
		BadgeChip() { size = {16.0F, kBadgeHeight}; }

		void setLabel(const std::string& newLabel) {
			label = newLabel;
			size = {UI::Badge::MeasureWidth(label), kBadgeHeight};
		}

		void render() override {
			UI::Badge(UI::Badge::Args{.position = position, .label = label, .tone = UI::Tone::Data}).render();
		}

		const char* debugTypeName() const override { return "BadgeChip"; }
		const char* debugId() const override { return "entity_info_belt_chip"; }

	  private:
		// Matches Badge.cpp's fixed pill height
		static constexpr float kBadgeHeight = 20.0F;

		std::string label;
	};

	/// Dashed empty-state panel (Log tab stub; no activity-log system yet).
	class EntityInfoView::EmptyStateBox : public UI::Component {
	  public:
		EmptyStateBox(float width, std::string newTitle, std::string newSubtitle)
			: title(std::move(newTitle)),
			  subtitle(std::move(newSubtitle)) {
			size = {width, 64.0F};
		}

		void render() override {
			tabs::drawEmptyState({position.x, position.y, size.x, size.y}, title, subtitle);
		}

		const char* debugTypeName() const override { return "EmptyStateBox"; }
		const char* debugId() const override { return "entity_info_log_empty"; }

	  private:
		std::string title;
		std::string subtitle;
	};

	// ========================================================================
	// EntityInfoView
	// ========================================================================

	EntityInfoView::EntityInfoView(const Args& args)
		: onCloseCallback(args.onClose),
		  onDetailsCallback(args.onDetails),
		  onToggleControlCallback(args.onToggleControl),
		  onGoToCallback(args.onGoTo),
		  onOpenCraftingDialogCallback(args.onOpenCraftingDialog),
		  onPlaceCallback(args.onPlace),
		  onMoveFurnitureCallback(args.onMoveFurniture),
		  onOpenStorageConfigCallback(args.onOpenStorageConfig),
		  queryResourcesCallback(args.queryResources),
		  onDemolishFoundationCallback(args.onDemolishFoundation),
		  onDemolishBuildingCallback(args.onDemolishBuilding),
		  onDemolishWallSegmentCallback(args.onDemolishWallSegment),
		  onDemolishOpeningCallback(args.onDemolishOpening),
		  m_id(args.id),
		  panelWidth(args.width),
		  contentWidth(args.width - kPad * 2.0F) {
		size = {panelWidth, 0.0F};
		zIndex = static_cast<short>(UI::z_panel); // exempts root-sibling overlaps (e.g. debug overlay) in the layout lint

		auto rootLocal = UI::LayoutContainer(UI::LayoutContainer::Args{
			.size = {panelWidth, 0.0F}, // fixed width, hug height
			.direction = UI::Direction::Vertical,
			.gap = kGap,
			.padding = UI::Insets{kPad},
			.id = "entity_info_root",
		});

		// ---- Header row: avatar | identity column | eye+close buttons ----
		auto headerLocal = UI::LayoutContainer(UI::LayoutContainer::Args{
			.direction = UI::Direction::Horizontal,
			.gap = kGap,
			.id = "entity_info_header",
		});
		headerLocal.widthMode = UI::SizeMode::Fill; // stretch across the panel

		auto avatarHandle = headerLocal.addChild(AvatarChip(kAvatarSize));
		avatar = headerLocal.getChild<AvatarChip>(avatarHandle);

		auto identityLocal = UI::LayoutContainer(UI::LayoutContainer::Args{
			.direction = UI::Direction::Vertical,
			.gap = UI::space_1,
			.id = "entity_info_identity",
		});
		identityLocal.widthMode = UI::SizeMode::Fill; // take the leftover header width

		{
			auto text = UI::Text(UI::Text::Args{
				.text = "",
				.style = {.color = UI::text_bright, .fontSize = UI::fs_md, .wordWrap = true},
				.id = "entity_info_name",
			});
			text.widthMode = UI::SizeMode::Fill;
			nameText = identityLocal.getChild<UI::Text>(identityLocal.addChild(std::move(text)));
		}
		{
			auto text = UI::Text(UI::Text::Args{
				.text = "",
				.style = {.color = UI::text_dim, .fontSize = UI::fs_xs, .wordWrap = true},
				.id = "entity_info_subtitle",
			});
			text.widthMode = UI::SizeMode::Fill;
			subtitleText = identityLocal.getChild<UI::Text>(identityLocal.addChild(std::move(text)));
		}
		moodBar = identityLocal.getChild<UI::ProgressBar>(identityLocal.addChild(UI::ProgressBar(UI::ProgressBar::Args{
			.width = kMeterWidth,
			.value = 1.0F,
			.tone = UI::Tone::Auto,
			.label = "Mood",
			.size = UI::Size::Sm,
			.inlineLabel = true,
			.id = "entity_info_mood",
		})));
		taskBar = identityLocal.getChild<UI::ProgressBar>(identityLocal.addChild(UI::ProgressBar(UI::ProgressBar::Args{
			.width = kMeterWidth,
			.value = 0.0F,
			.tone = UI::Tone::Accent,
			.label = "Idle",
			.size = UI::Size::Sm,
			.inlineLabel = true,
			.id = "entity_info_task",
		})));

		auto buttonsLocal = UI::LayoutContainer(UI::LayoutContainer::Args{
			.direction = UI::Direction::Horizontal,
			.gap = UI::space_1,
			.id = "entity_info_buttons",
		});
		eyeButton = buttonsLocal.getChild<GlyphButton>(buttonsLocal.addChild(GlyphButton(GlyphButton::Args{
			.glyph = "eye",
			.onClick = [this]() {
				if (onDetailsCallback) {
					onDetailsCallback();
				}
			},
			.id = "entity_info_details",
		})));
		closeButton = buttonsLocal.getChild<GlyphButton>(buttonsLocal.addChild(GlyphButton(GlyphButton::Args{
			.glyph = "close",
			.onClick = [this]() {
				if (onCloseCallback) {
					onCloseCallback();
				}
			},
			.id = "entity_info_close",
		})));

		identityCol = headerLocal.getChild<UI::LayoutContainer>(headerLocal.addChild(std::move(identityLocal)));
		buttonCol = headerLocal.getChild<UI::LayoutContainer>(headerLocal.addChild(std::move(buttonsLocal)));
		headerRow = rootLocal.getChild<UI::LayoutContainer>(rootLocal.addChild(std::move(headerLocal)));

		// ---- Tab bar (colonists only) ----
		tabBar = rootLocal.getChild<UI::TabBar>(rootLocal.addChild(UI::TabBar(UI::TabBar::Args{
			.width = contentWidth,
			.tabs =
				{{.id = "needs", .label = "Needs"},
				 {.id = "bio", .label = "Bio"},
				 {.id = "gear", .label = "Gear"},
				 {.id = "log", .label = "Log"}},
			.selectedId = "needs",
			.onSelect = [this](const std::string& tabId) { setActiveTab(tabId); },
			.id = "entity_info_tabs",
		})));

		// ---- Needs tab: one bar per need ----
		auto needsLocal = UI::LayoutContainer(UI::LayoutContainer::Args{
			.direction = UI::Direction::Vertical,
			.gap = UI::space_1,
			.id = "entity_info_needs",
		});
		const auto needCount = static_cast<size_t>(ecs::NeedType::Count);
		needBars.reserve(needCount);
		for (size_t i = 0; i < needCount; ++i) {
			auto handle = needsLocal.addChild(NeedBar(NeedBar::Args{
				.width = contentWidth,
				.label = ecs::needLabel(static_cast<ecs::NeedType>(i)),
				.id = "entity_info_need_" + std::to_string(i),
			}));
			needBars.push_back(needsLocal.getChild<NeedBar>(handle));
		}
		needsBody = rootLocal.getChild<UI::LayoutContainer>(rootLocal.addChild(std::move(needsLocal)));

		// ---- Bio tab ----
		auto bioLocal = UI::LayoutContainer(UI::LayoutContainer::Args{
			.direction = UI::Direction::Vertical,
			.gap = UI::space_1,
			.id = "entity_info_bio",
		});
		auto makeBioRow = [this, &bioLocal](const char* id) {
			auto text = UI::Text(UI::Text::Args{
				.width = contentWidth,
				.text = "",
				.style = {.color = UI::text, .fontSize = UI::fs_sm, .wordWrap = true},
				.id = id,
			});
			return bioLocal.getChild<UI::Text>(bioLocal.addChild(std::move(text)));
		};
		bioAge = makeBioRow("entity_info_bio_age");
		bioMood = makeBioRow("entity_info_bio_mood");
		bioBody = rootLocal.getChild<UI::LayoutContainer>(rootLocal.addChild(std::move(bioLocal)));

		// ---- Gear tab: armed line, belt chips, carry meter, backpack ----
		auto gearLocal = UI::LayoutContainer(UI::LayoutContainer::Args{
			.direction = UI::Direction::Vertical,
			.gap = UI::space_1_5,
			.id = "entity_info_gear",
		});
		{
			auto text = UI::Text(UI::Text::Args{
				.width = contentWidth,
				.text = "Armed: (empty)",
				.style = {.color = UI::text, .fontSize = UI::fs_sm, .wordWrap = true},
				.id = "entity_info_gear_hands",
			});
			gearHands = gearLocal.getChild<UI::Text>(gearLocal.addChild(std::move(text)));
		}
		auto beltLocal = UI::LayoutContainer(UI::LayoutContainer::Args{
			.direction = UI::Direction::Horizontal,
			.gap = UI::space_1,
			.crossAlign = UI::CrossAlign::Center,
			.id = "entity_info_belt",
		});
		{
			auto label = UI::Text(UI::Text::Args{
				.text = "Belt:",
				.style = {.color = UI::text_dim, .fontSize = UI::fs_sm},
				.id = "entity_info_belt_label",
			});
			beltLocal.addChild(std::move(label));
		}
		// ecs::Inventory has two belt slots
		for (size_t i = 0; i < 2; ++i) {
			auto chip = BadgeChip();
			chip.visible = false;
			beltChips.push_back(beltLocal.getChild<BadgeChip>(beltLocal.addChild(std::move(chip))));
		}
		{
			auto text = UI::Text(UI::Text::Args{
				.text = "(empty)",
				.style = {.color = UI::text_dim, .fontSize = UI::fs_sm},
				.id = "entity_info_belt_empty",
			});
			beltEmpty = beltLocal.getChild<UI::Text>(beltLocal.addChild(std::move(text)));
		}
		beltRow = gearLocal.getChild<UI::LayoutContainer>(gearLocal.addChild(std::move(beltLocal)));
		carryBar = gearLocal.getChild<UI::ProgressBar>(gearLocal.addChild(UI::ProgressBar(UI::ProgressBar::Args{
			.width = contentWidth,
			.value = 0.0F,
			.tone = UI::Tone::Data,
			.label = "Carry",
			.size = UI::Size::Sm,
			.inlineLabel = true,
			.id = "entity_info_carry",
		})));
		{
			auto text = UI::Text(UI::Text::Args{
				.width = contentWidth,
				.text = "Backpack: (empty)",
				.style = {.color = UI::text_dim, .fontSize = UI::fs_sm, .wordWrap = true},
				.id = "entity_info_gear_pack",
			});
			gearPack = gearLocal.getChild<UI::Text>(gearLocal.addChild(std::move(text)));
		}
		gearBody = rootLocal.getChild<UI::LayoutContainer>(rootLocal.addChild(std::move(gearLocal)));

		// ---- Log tab: empty-state stub (no activity-log system yet) ----
		auto logLocal = UI::LayoutContainer(UI::LayoutContainer::Args{
			.direction = UI::Direction::Vertical,
			.id = "entity_info_log",
		});
		logLocal.addChild(EmptyStateBox(contentWidth, "No activity log yet", "A chronological log arrives with the events update."));
		logBody = rootLocal.getChild<UI::LayoutContainer>(rootLocal.addChild(std::move(logLocal)));

		// ---- Action row: Draft | Go to | Priorities ----
		auto actionLocal = UI::LayoutContainer(UI::LayoutContainer::Args{
			.direction = UI::Direction::Horizontal,
			.gap = kGap,
			.id = "entity_info_actions",
		});
		const float actionButtonWidth = (contentWidth - kGap * 2.0F) / 3.0F;
		draftButton = actionLocal.getChild<UI::Button>(actionLocal.addChild(UI::Button(UI::Button::Args{
			.label = "Draft",
			.size = {actionButtonWidth, kActionButtonHeight},
			.type = UI::Button::Type::Secondary,
			.onClick =
				[this]() {
					if (onToggleControlCallback && selectedColonistId != ecs::EntityID{0}) {
						onToggleControlCallback(selectedColonistId);
					}
				},
			.id = "entity_info_draft",
		})));
		goToButton = actionLocal.getChild<UI::Button>(actionLocal.addChild(UI::Button(UI::Button::Args{
			.label = "Go to",
			.size = {actionButtonWidth, kActionButtonHeight},
			.type = UI::Button::Type::Secondary,
			.onClick =
				[this]() {
					if (onGoToCallback && selectedColonistId != ecs::EntityID{0}) {
						onGoToCallback(selectedColonistId);
					}
				},
			.id = "entity_info_goto",
		})));
		// Disabled stub: no priorities system yet
		actionLocal.addChild(UI::Button(UI::Button::Args{
			.label = "Priorities",
			.size = {actionButtonWidth, kActionButtonHeight},
			.type = UI::Button::Type::Secondary,
			.disabled = true,
			.id = "entity_info_priorities",
		}));
		actionRow = rootLocal.getChild<UI::LayoutContainer>(rootLocal.addChild(std::move(actionLocal)));

		// ---- Generic slot rows (non-colonist selections) ----
		auto slotsLocal = UI::LayoutContainer(UI::LayoutContainer::Args{
			.size = {contentWidth, 0.0F},
			.direction = UI::Direction::Vertical,
			.gap = kGap,
			.id = "entity_info_slots",
		});
		slotsBody = rootLocal.getChild<UI::LayoutContainer>(rootLocal.addChild(std::move(slotsLocal)));

		root = getChild<UI::LayoutContainer>(addChild(std::move(rootLocal)));

		visible = false;
	}

	void EntityInfoView::update(
		ecs::World&									   world,
		const engine::assets::AssetRegistry&		   assetRegistry,
		const engine::assets::RecipeRegistry&		   recipeRegistry,
		const Selection&							   selection,
		const engine::construction::ConstructionWorld* constructionWorld,
		const ecs::RoomDetectionSystem*				   roomDetection
	) {
		EntityInfoModel::Callbacks callbacks{
			.onOpenCraftingDialog = onOpenCraftingDialogCallback,
			.onPlace = onPlaceCallback,
			.onMoveFurniture = onMoveFurnitureCallback,
			.onOpenStorageConfig = onOpenStorageConfigCallback,
			.queryResources = queryResourcesCallback,
			.onDemolishFoundation = onDemolishFoundationCallback,
			.onDemolishBuilding = onDemolishBuildingCallback,
			.onDemolishWallSegment = onDemolishWallSegmentCallback,
			.onDemolishOpening = onDemolishOpeningCallback,
		};

		const auto updateType = m_model.refresh(selection, world, assetRegistry, recipeRegistry, callbacks, constructionWorld, roomDetection);

		switch (updateType) {
			case EntityInfoModel::UpdateType::None:
				break;
			case EntityInfoModel::UpdateType::Hide:
				visible = false;
				break;
			case EntityInfoModel::UpdateType::Show:
				visible = true;
				applyContent(m_model.content(), true);
				break;
			case EntityInfoModel::UpdateType::Structure:
				applyContent(m_model.content(), true);
				break;
			case EntityInfoModel::UpdateType::Values:
				applyContent(m_model.content(), false);
				break;
		}

		if (visible) {
			applyAnchor();
		}
	}

	void EntityInfoView::applyContent(const PanelContent& content, bool structural) {
		const bool isColonist = content.colonist.has_value();
		nameText->text = isColonist ? content.colonist->name : content.title;
		if (isColonist) {
			applyColonist(*content.colonist, structural);
		} else {
			applyGeneric(content, structural);
		}
		markLayoutDirty();
	}

	void EntityInfoView::applyColonist(const ColonistPanelData& data, bool structural) {
		selectedColonistId = data.id;

		avatar->visible = true;
		avatar->setIdentity(data.name, data.moodValue / 100.0F);
		subtitleText->visible = false;
		moodBar->visible = true;
		taskBar->visible = true;
		eyeButton->visible = static_cast<bool>(onDetailsCallback);
		tabBar->visible = true;
		actionRow->visible = true;
		slotsBody->visible = false;

		moodBar->setValue(data.moodValue / 100.0F);
		moodBar->setValueText(std::to_string(static_cast<int>(data.moodValue)) + "% " + data.moodLabel);

		const bool acting = data.taskProgress >= 0.0F;
		taskBar->setLabel(data.currentTask);
		taskBar->setValue(acting ? data.taskProgress : 0.0F);
		taskBar->setValueText(acting ? std::to_string(static_cast<int>(data.taskProgress * 100.0F)) + "%" : "");

		for (size_t i = 0; i < needBars.size(); ++i) {
			const bool has = i < data.needs.size();
			needBars[i]->visible = has;
			if (has) {
				needBars[i]->setLabel(data.needs[i].label);
				needBars[i]->setValue(data.needs[i].value);
			}
		}

		bioAge->text = "Age: " + data.age;
		bioMood->text = "Mood: " + std::to_string(static_cast<int>(data.moodValue)) + "% " + data.moodLabel;

		gearHands->text = "Armed: " + data.hands;
		for (size_t i = 0; i < beltChips.size(); ++i) {
			const bool has = i < data.belt.size();
			beltChips[i]->visible = has;
			if (has) {
				beltChips[i]->setLabel(data.belt[i]);
			}
		}
		beltEmpty->visible = data.belt.empty();

		const float ratio = data.capacityKg > 0.0F ? data.carriedKg / data.capacityKg : 0.0F;
		carryBar->setValue(ratio);
		carryBar->setValueText(
			std::to_string(static_cast<int>(data.carriedKg + 0.5F)) + " / " + std::to_string(static_cast<int>(data.capacityKg + 0.5F)) +
			" kg"
		);

		if (data.backpack.empty()) {
			gearPack->text = "Backpack: (empty)";
		} else {
			std::string lines = "Backpack:";
			for (const auto& line : data.backpack) {
				lines += "\n" + line;
			}
			gearPack->text = std::move(lines);
		}

		draftButton->setLabel(data.controlled ? "Release" : "Draft");

		if (structural) {
			tabBar->setSelected("needs");
			setActiveTab("needs");
		}
	}

	void EntityInfoView::applyGeneric(const PanelContent& content, bool structural) {
		selectedColonistId = ecs::EntityID{0};

		avatar->visible = false;
		moodBar->visible = false;
		taskBar->visible = false;
		eyeButton->visible = false;
		tabBar->visible = false;
		needsBody->visible = false;
		bioBody->visible = false;
		gearBody->visible = false;
		logBody->visible = false;
		actionRow->visible = false;
		// Hidden when empty: a visible zero-height container trips the layout lint
		slotsBody->visible = !content.slots.empty();

		subtitleText->text = content.subtitle;
		subtitleText->visible = !content.subtitle.empty();

		// Values updates rewrite rows in place, which can't absorb an item-count
		// drift inside a TextListSlot (e.g. work orders queued while selected);
		// rebuild when any list changed size.
		bool needsRebuild = structural;
		if (!needsRebuild) {
			size_t listIdx = 0;
			for (const auto& slot : content.slots) {
				if (const auto* list = std::get_if<TextListSlot>(&slot)) {
					if (listIdx >= slotListSizes.size() || slotListSizes[listIdx] != list->items.size()) {
						needsRebuild = true;
						break;
					}
					++listIdx;
				}
			}
		}

		if (needsRebuild) {
			rebuildSlots(content.slots);
		} else {
			updateSlots(content.slots);
		}
	}

	void EntityInfoView::rebuildSlots(const std::vector<InfoSlot>& slots) {
		slotsBody->clearChildren();
		slotTexts.clear();
		slotBars.clear();
		slotButtons.clear();
		slotListItems.clear();
		slotListSizes.clear();

		for (const auto& slot : slots) {
			std::visit(
				[this](const auto& s) {
					using T = std::decay_t<decltype(s)>;
					if constexpr (std::is_same_v<T, TextSlot>) {
						auto handle = slotsBody->addChild(UI::Text(UI::Text::Args{
							.width = contentWidth,
							.text = s.label + ": " + s.value,
							.style = {.color = UI::text, .fontSize = UI::fs_sm, .wordWrap = true},
						}));
						slotTexts.push_back(slotsBody->getChild<UI::Text>(handle));
					} else if constexpr (std::is_same_v<T, ProgressBarSlot>) {
						auto bar = NeedBar(NeedBar::Args{.width = contentWidth, .label = s.label});
						bar.setValue(s.value);
						auto handle = slotsBody->addChild(std::move(bar));
						slotBars.push_back(slotsBody->getChild<NeedBar>(handle));
					} else if constexpr (std::is_same_v<T, TextListSlot>) {
						slotsBody->addChild(UI::Text(UI::Text::Args{
							.width = contentWidth,
							.text = s.header + ":",
							.style = {.color = UI::text_dim, .fontSize = UI::fs_sm, .wordWrap = true},
						}));
						std::vector<UI::Text*> items;
						items.reserve(s.items.size());
						for (const auto& item : s.items) {
							auto handle = slotsBody->addChild(UI::Text(UI::Text::Args{
								.width = contentWidth,
								.text = "- " + item,
								.style = {.color = UI::text, .fontSize = UI::fs_sm, .wordWrap = true},
							}));
							items.push_back(slotsBody->getChild<UI::Text>(handle));
						}
						slotListItems.push_back(std::move(items));
						slotListSizes.push_back(s.items.size());
					} else if constexpr (std::is_same_v<T, ActionButtonSlot>) {
						auto handle = slotsBody->addChild(UI::Button(UI::Button::Args{
							.label = s.label,
							.size = {contentWidth, kSlotButtonHeight},
							.type = UI::Button::Type::Secondary,
							.onClick = s.onClick,
						}));
						slotButtons.push_back(slotsBody->getChild<UI::Button>(handle));
					}
				},
				slot
			);
		}
	}

	void EntityInfoView::updateSlots(const std::vector<InfoSlot>& slots) {
		size_t textIdx = 0;
		size_t barIdx = 0;
		size_t buttonIdx = 0;
		size_t listIdx = 0;

		for (const auto& slot : slots) {
			std::visit(
				[&](const auto& s) {
					using T = std::decay_t<decltype(s)>;
					if constexpr (std::is_same_v<T, TextSlot>) {
						if (textIdx < slotTexts.size()) {
							slotTexts[textIdx]->text = s.label + ": " + s.value;
						}
						++textIdx;
					} else if constexpr (std::is_same_v<T, ProgressBarSlot>) {
						if (barIdx < slotBars.size()) {
							slotBars[barIdx]->setValue(s.value);
						}
						++barIdx;
					} else if constexpr (std::is_same_v<T, TextListSlot>) {
						if (listIdx < slotListItems.size()) {
							auto& items = slotListItems[listIdx];
							for (size_t i = 0; i < s.items.size() && i < items.size(); ++i) {
								items[i]->text = "- " + s.items[i];
							}
						}
						++listIdx;
					} else if constexpr (std::is_same_v<T, ActionButtonSlot>) {
						if (buttonIdx < slotButtons.size()) {
							slotButtons[buttonIdx]->setLabel(s.label);
							slotButtons[buttonIdx]->onClick = s.onClick;
						}
						++buttonIdx;
					}
				},
				slot
			);
		}
	}

	void EntityInfoView::setActiveTab(const std::string& tabId) {
		needsBody->visible = tabId == "needs";
		bioBody->visible = tabId == "bio";
		gearBody->visible = tabId == "gear";
		logBody->visible = tabId == "log";
		markLayoutDirty();
		applyAnchor();
	}

	void EntityInfoView::applyAnchor() {
		const float height = root->getHeight();
		position = {panelX, m_viewportHeight - height};
		size = {panelWidth, height};
		root->setPosition(position.x, position.y);
	}

	void EntityInfoView::markLayoutDirty() {
		// The engine's nested layout() is a final-rect assignment: it adopts the
		// measured size, freezing a Hug container at its last extent. Reset the
		// hug axes so visibility/content changes re-measure instead of keeping a
		// stale height (e.g. an emptied slots body holding its old size).
		root->setLayoutSize(UI::kSizeKeep, 0.0F);
		headerRow->setLayoutSize(UI::kSizeKeep, 0.0F);
		identityCol->setLayoutSize(UI::kSizeKeep, 0.0F);
		slotsBody->setLayoutSize(UI::kSizeKeep, 0.0F);
		for (UI::LayoutContainer* container : {buttonCol, needsBody, bioBody, gearBody, logBody, actionRow, beltRow}) {
			container->setLayoutSize(0.0F, 0.0F);
		}
		for (UI::LayoutContainer* container :
			 {root, headerRow, identityCol, buttonCol, needsBody, bioBody, gearBody, logBody, actionRow, beltRow, slotsBody}) {
			container->invalidateLayout();
		}
	}

	void EntityInfoView::setBottomLeftPosition(float x, float viewportHeight) {
		panelX = x;
		m_viewportHeight = viewportHeight;
		applyAnchor();
	}

	void EntityInfoView::render() {
		if (!visible) {
			return;
		}
		UI::Panel(UI::Panel::Args{
					  .position = position,
					  .size = size,
					  .variant = UI::PanelVariant::Panel,
					  .accent = UI::PanelAccent::Accent,
		})
			.render();
		Component::render();
	}

	bool EntityInfoView::handleEvent(UI::InputEvent& event) {
		if (!visible) {
			return false;
		}

		if (dispatchEvent(event)) {
			return true;
		}
		if (event.isConsumed()) {
			return true;
		}

		// Swallow clicks on the panel surface so they don't reach the world
		if (event.type == UI::InputEvent::Type::MouseUp && event.button == engine::MouseButton::Left && containsPoint(event.position)) {
			event.consume();
			return true;
		}

		return false;
	}

	bool EntityInfoView::containsPoint(Foundation::Vec2 point) const {
		return point.x >= position.x && point.x <= position.x + size.x && point.y >= position.y && point.y <= position.y + size.y;
	}

} // namespace world_sim
