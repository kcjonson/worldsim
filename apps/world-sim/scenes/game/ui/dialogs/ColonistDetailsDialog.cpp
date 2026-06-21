#include "ColonistDetailsDialog.h"

#include <components/avatar/Avatar.h>
#include <components/stat/Stat.h>
#include <font/FontRenderer.h>
#include <primitives/Primitives.h>
#include <theme/Tokens.h>
#include <theme/Variants.h>

#include <array>
#include <string>

namespace world_sim {

	namespace {

		// Footer button geometry.
		constexpr float kBtnHeight = 30.0F;
		constexpr float kBtnPadX   = 14.0F; // horizontal text padding
		constexpr float kBtnGap	   = 8.0F;

		float textWidth(const std::string& text, float fontPx, Renderer::FontFamily font) {
			if (const ui::FontRenderer* fonts = Renderer::Primitives::getFontRenderer(); fonts != nullptr) {
				return fonts->MeasureText(text, fontPx / 16.0F, font, fontPx * UI::ls_wide).x;
			}
			return static_cast<float>(text.size()) * fontPx * 0.6F;
		}

		float buttonWidth(const std::string& label) {
			return textWidth(label, UI::fs_xs, UI::fontDisplay) + kBtnPadX * 2.0F;
		}

		// Draw a Salvage-style footer button. Variant drives fill/border/label color.
		enum class BtnVariant { Ghost, Secondary, Primary };

		void drawButton(const Foundation::Rect& bounds, const std::string& label, BtnVariant variant) {
			Foundation::RectStyle style;
			Foundation::Color	  labelColor;
			switch (variant) {
				case BtnVariant::Primary:
					style.gradient = Foundation::LinearGradient{.from = UI::accent_bright, .to = UI::accent, .horizontal = false};
					style.border   = Foundation::BorderStyle{.color = UI::accent_bright, .width = UI::bw, .cornerRadius = UI::r_sm, .position = Foundation::BorderPosition::Inside};
					labelColor	   = UI::accent_contrast;
					break;
				case BtnVariant::Secondary:
					style.fill	 = Foundation::Color::transparent();
					style.border = Foundation::BorderStyle{.color = UI::line_edge, .width = UI::bw, .cornerRadius = UI::r_sm, .position = Foundation::BorderPosition::Inside};
					labelColor	 = UI::text;
					break;
				case BtnVariant::Ghost:
					style.fill = Foundation::Color::transparent();
					labelColor = UI::text_dim;
					break;
			}

			Renderer::Primitives::drawRect({.bounds = bounds, .style = style});
			Renderer::Primitives::drawText({
				.text		   = label,
				.position	   = bounds.position(),
				.scale		   = UI::fs_xs / 16.0F,
				.color		   = labelColor,
				.font		   = UI::fontDisplay,
				.hAlign		   = Foundation::HorizontalAlign::Center,
				.vAlign		   = Foundation::VerticalAlign::Middle,
				.boxWidth	   = bounds.width,
				.boxHeight	   = bounds.height,
				.letterSpacing = UI::fs_xs * UI::ls_wide,
				.transform	   = Foundation::TextTransform::Uppercase,
			});
		}

	} // namespace

	ColonistDetailsDialog::ColonistDetailsDialog(const Args& args)
		: onCloseCallback(args.onClose) {
		createDialog();
	}

	void ColonistDetailsDialog::createDialog() {
		auto dialog = UI::Dialog(
			UI::Dialog::Args{
				.title = "Colonist Details",
				.kicker = "PERSONNEL FILE",
				.size = {kDialogWidth, kDialogHeight},
				.onClose =
					[this]() {
						if (onCloseCallback) {
							onCloseCallback();
						}
					},
				.modal = false,
				.footerHeight = kFooterHeight
			}
		);
		dialogHandle = addChild(std::move(dialog));
	}

	void ColonistDetailsDialog::createContent() {
		auto* dialog = getChild<UI::Dialog>(dialogHandle);
		if (dialog == nullptr)
			return;

		const auto	contentBounds = dialog->getContentBounds();
		const float contentTop	  = kHeaderBandHeight + kTabBarHeight + kContentPadding;

		// TabBar + tab views are DIRECT dialog children with explicit positions. The
		// persistent header band (avatar + stats) is drawn absolutely in renderHeaderBand
		// over the reserved top strip; the tab bar sits below it, the tab content below
		// that. We deliberately avoid an auto-stacking LayoutContainer: it assigned
		// siblings inconsistent offsets, dropping the active tab on top of the header band.
		tabBarHandle = dialog->addChild(UI::TabBar(
			UI::TabBar::Args{
				.position = {0, kHeaderBandHeight},
				.width = contentBounds.width,
				.tabs =
					{{.id = kTabBio, .label = "Bio"},
					 {.id = kTabHealth, .label = "Health"},
					 {.id = kTabSkills, .label = "Skills"},
					 {.id = kTabSocial, .label = "Social"},
					 {.id = kTabGear, .label = "Gear"},
					 {.id = kTabMemory, .label = "Memory"},
					 {.id = kTabTasks, .label = "Tasks"},
					 {.id = kTabLog, .label = "Log"}},
				.selectedId = kTabBio,
				.onSelect = [this](const std::string& tabId) { switchToTab(tabId); }
			}
		));

		// Each tab view is pinned to the content origin below the tab bar; only the active
		// one is visible. contentBounds.width drives wrapping/columns inside the view.
		const Foundation::Rect tabContentBounds{0, contentTop, contentBounds.width, contentBounds.height - contentTop};

		auto bioTab = BioTabView();
		bioTab.create(tabContentBounds);
		bioTab.setPosition(0.0F, contentTop);
		bioTabHandle = dialog->addChild(std::move(bioTab));

		auto healthTab = HealthTabView();
		healthTab.create(tabContentBounds);
		healthTab.setPosition(0.0F, contentTop);
		healthTab.visible = false;
		healthTabHandle = dialog->addChild(std::move(healthTab));

		auto skillsTab = SkillsTabView();
		skillsTab.create(tabContentBounds);
		skillsTab.setPosition(0.0F, contentTop);
		skillsTab.visible = false;
		skillsTabHandle = dialog->addChild(std::move(skillsTab));

		auto socialTab = SocialTabView();
		socialTab.create(tabContentBounds);
		socialTab.setPosition(0.0F, contentTop);
		socialTab.visible = false;
		socialTabHandle = dialog->addChild(std::move(socialTab));

		auto gearTab = GearTabView();
		gearTab.create(tabContentBounds);
		gearTab.setPosition(0.0F, contentTop);
		gearTab.visible = false;
		gearTabHandle = dialog->addChild(std::move(gearTab));

		auto memoryTab = MemoryTabView();
		memoryTab.create(tabContentBounds);
		memoryTab.setPosition(0.0F, contentTop);
		memoryTab.visible = false;
		memoryTabHandle = dialog->addChild(std::move(memoryTab));

		auto tasksTab = TasksTabView();
		tasksTab.create(tabContentBounds);
		tasksTab.setPosition(0.0F, contentTop);
		tasksTab.visible = false;
		tasksTabHandle = dialog->addChild(std::move(tasksTab));

		auto logTab = LogTabView();
		logTab.create(tabContentBounds);
		logTab.setPosition(0.0F, contentTop);
		logTab.visible = false;
		logTabHandle = dialog->addChild(std::move(logTab));
	}

	void ColonistDetailsDialog::open(ecs::EntityID newColonistId, float screenWidth, float screenHeight) {
		colonistId = newColonistId;
		currentTab = kTabBio;

		auto* dialog = getChild<UI::Dialog>(dialogHandle);
		if (dialog != nullptr) {
			dialog->open(screenWidth, screenHeight);

			// Create content after dialog opens (needs content bounds)
			if (tabBarHandle == UI::LayerHandle{}) {
				createContent();
			}
		}
	}

	void ColonistDetailsDialog::close() {
		auto* dialog = getChild<UI::Dialog>(dialogHandle);
		if (dialog != nullptr) {
			dialog->close();
		}
	}

	bool ColonistDetailsDialog::isOpen() const {
		auto* dialog = getChild<UI::Dialog>(dialogHandle);
		return dialog != nullptr && dialog->isOpen();
	}

	void ColonistDetailsDialog::update(ecs::World& world, float deltaTime) {
		if (!isOpen())
			return;

		auto* dialog = getChild<UI::Dialog>(dialogHandle);
		if (dialog != nullptr) {
			dialog->update(deltaTime);
		}

		auto updateType = model.refresh(world, colonistId);

		// Dialog title = colonist name (kicker stays "PERSONNEL FILE").
		if (model.isValid() && dialog != nullptr) {
			dialog->setTitle(model.bio().name);
		}

		if (updateType == ColonistDetailsModel::UpdateType::Structure || updateType == ColonistDetailsModel::UpdateType::Values) {
			updateTabContent();
		}
	}

	void ColonistDetailsDialog::render() {
		if (!isOpen())
			return;

		auto* dialog = getChild<UI::Dialog>(dialogHandle);
		if (dialog == nullptr)
			return;

		// Dialog draws the panel, chrome, tab bar, and tab content.
		dialog->render();

		// Persistent header band + footer buttons are drawn on top.
		renderHeaderBand();
		renderFooter();
	}

	void ColonistDetailsDialog::renderHeaderBand() {
		auto* dialog = getChild<UI::Dialog>(dialogHandle);
		if (dialog == nullptr || !model.isValid())
			return;

		const Foundation::Rect content = dialog->getContentBounds();
		const float			   ox	   = content.x;
		const float			   oy	   = content.y;

		const auto& bio = model.bio();

		// Avatar (72px) on the left.
		constexpr float kAvatarSize = 72.0F;
		UI::Avatar({.position = {ox, oy}, .size = kAvatarSize, .seed = bio.name, .mood = bio.mood / 100.0F}).render();

		// 2x2 Stat grid to the right of the avatar.
		constexpr float kGap	= 20.0F;
		constexpr float kStatCol = 200.0F;
		constexpr float kStatRow = 40.0F;
		const float		gx		= ox + kAvatarSize + kGap;

		const std::string moodValue = std::to_string(static_cast<int>(bio.mood)) + "%";
		const UI::Tone	  moodTone	= bio.mood < 40.0F ? UI::Tone::Crit : UI::Tone::Ok;

		UI::Stat({.position = {gx, oy}, .label = "ROLE", .value = "--", .size = UI::Size::Sm}).render();
		UI::Stat({.position = {gx + kStatCol, oy}, .label = "ORIGIN", .value = "--", .size = UI::Size::Sm}).render();
		UI::Stat({.position = {gx, oy + kStatRow}, .label = "AGE", .value = bio.age, .unit = "yrs", .size = UI::Size::Sm}).render();
		UI::Stat({.position = {gx + kStatCol, oy + kStatRow}, .label = "MOOD", .value = moodValue, .tone = moodTone, .size = UI::Size::Sm}).render();
	}

	Foundation::Rect ColonistDetailsDialog::closeButtonBounds() const {
		auto* dialog = getChild<UI::Dialog>(dialogHandle);
		if (dialog == nullptr) {
			return {};
		}
		const Foundation::Rect footer = dialog->getFooterBounds();
		if (footer.width <= 0.0F) {
			return {};
		}

		// Buttons are right-aligned: [Close] [Work Priorities] [Draft].
		const float wDraft = buttonWidth("Draft");
		const float wWork  = buttonWidth("Work Priorities");
		const float wClose = buttonWidth("Close");

		const float btnY  = footer.y + (footer.height - kBtnHeight) * 0.5F;
		const float rightX = footer.x + footer.width - kContentPadding;
		const float draftX = rightX - wDraft;
		const float workX  = draftX - kBtnGap - wWork;
		const float closeX = workX - kBtnGap - wClose;
		return {closeX, btnY, wClose, kBtnHeight};
	}

	void ColonistDetailsDialog::renderFooter() {
		auto* dialog = getChild<UI::Dialog>(dialogHandle);
		if (dialog == nullptr)
			return;

		const Foundation::Rect footer = dialog->getFooterBounds();
		if (footer.width <= 0.0F)
			return;

		const float wDraft = buttonWidth("Draft");
		const float wWork  = buttonWidth("Work Priorities");
		const float wClose = buttonWidth("Close");

		const float btnY   = footer.y + (footer.height - kBtnHeight) * 0.5F;
		const float rightX = footer.x + footer.width - kContentPadding;
		const float draftX = rightX - wDraft;
		const float workX  = draftX - kBtnGap - wWork;
		const float closeX = workX - kBtnGap - wClose;

		drawButton({closeX, btnY, wClose, kBtnHeight}, "Close", BtnVariant::Ghost);
		drawButton({workX, btnY, wWork, kBtnHeight}, "Work Priorities", BtnVariant::Secondary);
		drawButton({draftX, btnY, wDraft, kBtnHeight}, "Draft", BtnVariant::Primary);
	}

	bool ColonistDetailsDialog::handleEvent(UI::InputEvent& event) {
		if (!isOpen())
			return false;

		// Footer Close button: replicate the X-button close on click.
		if (event.type == UI::InputEvent::Type::MouseDown || event.type == UI::InputEvent::Type::MouseUp) {
			const Foundation::Rect cb = closeButtonBounds();
			if (cb.width > 0.0F && event.position.x >= cb.x && event.position.x < cb.x + cb.width && event.position.y >= cb.y && event.position.y < cb.y + cb.height) {
				if (event.type == UI::InputEvent::Type::MouseUp) {
					close();
				}
				event.consume();
				return true;
			}
		}

		// Let Dialog handle remaining events (content children, chrome).
		auto* dialog = getChild<UI::Dialog>(dialogHandle);
		if (dialog != nullptr && dialog->handleEvent(event)) {
			return true;
		}

		return false;
	}

	bool ColonistDetailsDialog::containsPoint(Foundation::Vec2 point) const {
		if (!isOpen())
			return false;
		auto* dialog = getChild<UI::Dialog>(dialogHandle);
		return dialog != nullptr && dialog->containsPoint(point);
	}

	void ColonistDetailsDialog::switchToTab(const std::string& tabId) {
		currentTab = tabId;

		auto* dialog = getChild<UI::Dialog>(dialogHandle);
		if (dialog == nullptr)
			return;

		if (auto* tab = dialog->getChild<BioTabView>(bioTabHandle)) {
			tab->visible = (tabId == kTabBio);
		}
		if (auto* tab = dialog->getChild<HealthTabView>(healthTabHandle)) {
			tab->visible = (tabId == kTabHealth);
		}
		if (auto* tab = dialog->getChild<SkillsTabView>(skillsTabHandle)) {
			tab->visible = (tabId == kTabSkills);
		}
		if (auto* tab = dialog->getChild<SocialTabView>(socialTabHandle)) {
			tab->visible = (tabId == kTabSocial);
		}
		if (auto* tab = dialog->getChild<GearTabView>(gearTabHandle)) {
			tab->visible = (tabId == kTabGear);
		}
		if (auto* tab = dialog->getChild<MemoryTabView>(memoryTabHandle)) {
			tab->visible = (tabId == kTabMemory);
		}
		if (auto* tab = dialog->getChild<TasksTabView>(tasksTabHandle)) {
			tab->visible = (tabId == kTabTasks);
		}
		if (auto* tab = dialog->getChild<LogTabView>(logTabHandle)) {
			tab->visible = (tabId == kTabLog);
		}
	}

	void ColonistDetailsDialog::updateTabContent() {
		if (!model.isValid())
			return;

		auto* dialog = getChild<UI::Dialog>(dialogHandle);
		if (dialog == nullptr)
			return;

		if (auto* tab = dialog->getChild<BioTabView>(bioTabHandle)) {
			tab->update(model.bio());
		}
		if (auto* tab = dialog->getChild<HealthTabView>(healthTabHandle)) {
			tab->update(model.health());
		}
		if (auto* tab = dialog->getChild<SkillsTabView>(skillsTabHandle)) {
			tab->update(SkillsData{});
		}
		if (auto* tab = dialog->getChild<SocialTabView>(socialTabHandle)) {
			tab->update(model.social());
		}
		if (auto* tab = dialog->getChild<GearTabView>(gearTabHandle)) {
			tab->update(model.gear());
		}
		if (auto* tab = dialog->getChild<MemoryTabView>(memoryTabHandle)) {
			tab->update(model.memory());
		}
		if (auto* tab = dialog->getChild<TasksTabView>(tasksTabHandle)) {
			tab->update(model.tasks());
		}
		if (auto* tab = dialog->getChild<LogTabView>(logTabHandle)) {
			tab->update(LogData{});
		}
	}

} // namespace world_sim
