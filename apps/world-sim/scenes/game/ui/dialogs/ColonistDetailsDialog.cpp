#include "ColonistDetailsDialog.h"

#include <ui/Context.h>
#include <ui/components/Button.h>

#include <algorithm>
#include <array>
#include <string>
#include <utility>
#include <vector>

namespace world_sim {

	namespace {

		// The tab bar makes each tab's ui id "tab_" + its id, so these don't share the entity panel's.
		constexpr const char* kTabBio = "dossier_bio";
		constexpr const char* kTabNeeds = "dossier_needs";
		constexpr const char* kTabSkills = "dossier_skills";
		constexpr const char* kTabSocial = "dossier_social";
		constexpr const char* kTabGear = "dossier_gear";
		constexpr const char* kTabMemory = "dossier_memory";
		constexpr const char* kTabTasks = "dossier_tasks";
		constexpr const char* kTabLog = "dossier_log";

		// The bar's tabs, in order, and every tab the dossier measures for its height.
		struct DossierTab {
			const char* id;
			const char* label;
		};
		constexpr std::array<DossierTab, 8> kTabs{{
			{.id = kTabBio, .label = "Bio"},
			{.id = kTabNeeds, .label = "Needs"},
			{.id = kTabSkills, .label = "Skills"},
			{.id = kTabSocial, .label = "Social"},
			{.id = kTabGear, .label = "Gear"},
			{.id = kTabMemory, .label = "Memory"},
			{.id = kTabTasks, .label = "Tasks"},
			{.id = kTabLog, .label = "Log"},
		}};

		// The prototype dossier's width; its height is what its tallest tab needs. Held like the prototype's modal to
		// 92% x 86% of the window, the tab scrolling in what's left.
		constexpr float kDialogWidth = 760.0F;
		constexpr float kDialogMaxWidthPercent = 92.0F;
		constexpr float kDialogMaxHeightPercent = 86.0F;
		constexpr float kAvatarSize = 72.0F;
		constexpr float kBandGap = 20.0F;
		constexpr float kStatRowGap = 8.0F;
		constexpr float kTabTopPad = 8.0F; // with the body's gap, 16 between the tab bar and the tab

	} // namespace

	ColonistDetailsDialog::ColonistDetailsDialog(const Args& args)
		: onClose(args.onClose) {}

	void ColonistDetailsDialog::open(ui::Component& opener, ecs::EntityID newColonistId, int player) {
		colonistId = newColonistId;
		fresh = true;
		model.invalidateTasks();
		if (dialog.get() == nullptr) {
			dialog = ui::Dialog::open(
				opener,
				{
					.id = "colonist_dossier",
					.title = "Colonist Details",
					.kicker = "Personnel File",
					.panelSize = {kDialogWidth, ui::kAuto},
					.modal = false,
					.footer = true,
					.onClose =
						[this] {
							closed();
							if (onClose) {
								onClose();
							}
						},
				},
				player
			);
			if (ui::Dialog* shown = dialog.get()) {
				shown->panel().get()->setMaxSize({ui::percent(kDialogMaxWidthPercent), ui::percent(kDialogMaxHeightPercent)});
				build(*shown);
			}
		}
		if (ui::TabBar* bar = tabBar.get()) {
			bar->setSelectedId(kTabBio);
		}
		showTab(kTabBio);
		if (ui::Component* area = tabArea.get()) {
			area->scrollTo({0.0F, 0.0F});
		}
	}

	void ColonistDetailsDialog::close() {
		if (ui::Dialog* shown = dialog.get()) {
			ui::Context&			  context = *shown->getContext();
			const ui::Ref<ui::Dialog> entry = dialog;
			closed();
			context.closeOverlay(entry);
		}
	}

	// Everything it built lives under the dialog and reads null from here.
	void ColonistDetailsDialog::closed() {
		dialog = {};
		currentTab.clear();
		sized = false;
	}

	void ColonistDetailsDialog::build(ui::Dialog& shown) {
		const ui::Ref<ui::Component> content = shown.content();

		// The header band: the avatar and a 2x2 Stat grid filling the width beside it.
		const ui::Ref<ui::Component> band = content.addChild<ui::Component>(
			{.layout = {.display = ui::Display::Flex, .gap = kBandGap, .alignItems = ui::AlignItems::Center}}
		);
		avatarSlot = band.addChild<ui::Component>({.size = {kAvatarSize, kAvatarSize}});
		const ui::Ref<ui::Component> stats = band.addChild<ui::Component>({
			.size = {0.0F, ui::kAuto},
			.layout = {.display = ui::Display::Flex, .flexDirection = ui::FlexDirection::Column, .gap = kStatRowGap, .flexGrow = 1.0F},
		});
		const ui::Component::Args	 rowArgs{.layout = {.display = ui::Display::Flex, .gap = kBandGap}};
		const ui::Ref<ui::Component> top = stats.addChild<ui::Component>(rowArgs);
		const ui::Ref<ui::Component> bottom = stats.addChild<ui::Component>(rowArgs);
		const ui::Size				 cell{0.0F, ui::kAuto};
		top.addChild<ui::Stat>(
			{.size = cell, .layout = {.flexGrow = 1.0F}, .label = "Role", .value = "--", .controlSize = ui::ControlSize::Sm}
		);
		top.addChild<ui::Stat>(
			{.size = cell, .layout = {.flexGrow = 1.0F}, .label = "Origin", .value = "--", .controlSize = ui::ControlSize::Sm}
		);
		ageStat = bottom.addChild<ui::Stat>(
			{.size = cell, .layout = {.flexGrow = 1.0F}, .label = "Age", .value = "--", .unit = "yrs", .controlSize = ui::ControlSize::Sm}
		);
		moodStat = bottom.addChild<ui::Stat>(
			{.size = cell, .layout = {.flexGrow = 1.0F}, .label = "Mood", .value = "--", .controlSize = ui::ControlSize::Sm}
		);

		std::vector<ui::Tab> tabs;
		tabs.reserve(kTabs.size());
		for (const DossierTab& tab : kTabs) {
			tabs.push_back({.id = tab.id, .label = tab.label});
		}
		tabBar = content.addChild<ui::TabBar>({
			.id = "dossier_tabs",
			.tabs = std::move(tabs),
			.selectedId = kTabBio,
			.onSelect = [this](const std::string& tabId) { showTab(tabId); },
		});
		// Takes what the band and the bar leave, and scrolls a taller tab.
		tabArea = content.addChild<ui::Component>({
			.id = "dossier_tab_area",
			.size = {ui::kAuto, 0.0F},
			.layout =
				{.display = ui::Display::Flex,
				 .flexDirection = ui::FlexDirection::Column,
				 .padding = {kTabTopPad, 0.0F, 0.0F, 0.0F},
				 .flexGrow = 1.0F},
			.overflow = ui::Overflow::Scroll,
		});

		const ui::Ref<ui::Component> footer = shown.footer();
		footer.addChild<ui::Button>({
			.label = "Close",
			.variant = ui::ButtonVariant::Ghost,
			.controlSize = ui::ControlSize::Sm,
			.onPress = [this] {
				if (ui::Dialog* open = dialog.get()) {
					open->close();
				}
			},
		});
		// Work Priorities and Draft wait on their systems.
		footer.addChild<ui::Button>(
			{.enabled = false, .label = "Work Priorities", .variant = ui::ButtonVariant::Secondary, .controlSize = ui::ControlSize::Sm}
		);
		footer.addChild<ui::Button>(
			{.enabled = false, .label = "Draft", .variant = ui::ButtonVariant::Primary, .controlSize = ui::ControlSize::Sm}
		);
	}

	void ColonistDetailsDialog::showTab(const std::string& tabId) {
		ui::Component* area = tabArea.get();
		if (area == nullptr || (tabId == currentTab && tabView.get() != nullptr)) {
			return;
		}
		mountTab(tabId);
		area->scrollTo({0.0F, 0.0F});
		if (!fresh) {
			feedTab();
		}
	}

	void ColonistDetailsDialog::mountTab(const std::string& tabId) {
		ui::Component* area = tabArea.get();
		currentTab = tabId;
		if (tabView.get() != nullptr) {
			area->removeChild(tabView);
		}
		if (tabId == kTabBio) {
			tabView = bioTab = area->addChild<BioTabView>({});
		} else if (tabId == kTabNeeds) {
			tabView = needsTab = area->addChild<NeedsTabView>({});
		} else if (tabId == kTabSkills) {
			tabView = skillsTab = area->addChild<SkillsTabView>({});
		} else if (tabId == kTabSocial) {
			tabView = socialTab = area->addChild<SocialTabView>({});
		} else if (tabId == kTabGear) {
			tabView = gearTab = area->addChild<GearTabView>({});
		} else if (tabId == kTabMemory) {
			tabView = memoryTab = area->addChild<MemoryTabView>({});
		} else if (tabId == kTabTasks) {
			tabView = tasksTab = area->addChild<TasksTabView>({});
		} else if (tabId == kTabLog) {
			tabView = logTab = area->addChild<LogTabView>({});
		}
	}

	// The panel's height for this opening: what it needs around the tab area, and the tallest tab as the model has it
	// now, each tab mounted, fed and laid out once. The shown tab goes last, so it stays mounted. Held until it closes:
	// a tab switch, new data or another colonist doesn't move it, and a tab that outgrows it scrolls.
	void ColonistDetailsDialog::sizeToContent() {
		ui::Rectangle&	  panel = *dialog.get()->panel().get();
		ui::Context&	  context = *panel.getContext();
		const std::string shownTab = currentTab;
		float			  around = 0.0F;
		float			  tallest = 0.0F;
		const auto		  measure = [&](const std::string& tabId) {
			   mountTab(tabId);
			   feedTab();
			   context.layout();
			   around = panel.getSize().y - tabArea.get()->getSize().y;
			   tallest = std::max(tallest, tabView.get()->getBounds().height);
		};
		panel.setSize({kDialogWidth, ui::kAuto});
		for (const DossierTab& tab : kTabs) {
			if (tab.id != shownTab) {
				measure(tab.id);
			}
		}
		measure(shownTab);
		panel.setSize({kDialogWidth, around + kTabTopPad + tallest});
		sized = true;
	}

	void ColonistDetailsDialog::update(ecs::World& world, float deltaTime) {
		if (!isOpen()) {
			return;
		}
		const ColonistDetailsModel::UpdateType change = model.refresh(world, colonistId, deltaTime);
		if (!model.isValid() || (!fresh && change == ColonistDetailsModel::UpdateType::None)) {
			return;
		}
		const bool opened = std::exchange(fresh, false);
		// The title is the colonist's name; the kicker stays "Personnel File".
		if (dialog.get()->getTitle() != model.bio().name) {
			dialog.get()->setTitle(model.bio().name);
		}
		showHeader();
		if (opened && !sized) {
			sizeToContent();
		} else {
			feedTab();
		}
	}

	void ColonistDetailsDialog::showHeader() {
		const BioData& bio = model.bio();
		// An avatar's seed is fixed, so another colonist gets another avatar.
		if (avatar.get() == nullptr || avatarSeed != bio.name) {
			if (avatar.get() != nullptr) {
				avatarSlot.get()->removeChild(avatar);
			}
			avatarSeed = bio.name;
			avatar = avatarSlot.addChild<theme::Avatar>({.size = {kAvatarSize, kAvatarSize}, .seed = bio.name});
		}
		avatar.get()->setMood(bio.mood / 100.0F);
		ageStat.get()->setValue(bio.age);
		moodStat.get()->setValue(std::to_string(static_cast<int>(bio.mood)) + "%");
		moodStat.get()->setTone(bio.mood < 40.0F ? ui::Tone::Danger : ui::Tone::Positive);
	}

	// The mounted tab; the others aren't built.
	void ColonistDetailsDialog::feedTab() {
		if (!model.isValid()) {
			return;
		}
		if (BioTabView* tab = bioTab.get()) {
			tab->update(model.bio());
		}
		if (NeedsTabView* tab = needsTab.get()) {
			tab->update(model.needs());
		}
		if (SkillsTabView* tab = skillsTab.get()) {
			tab->update(SkillsData{});
		}
		if (SocialTabView* tab = socialTab.get()) {
			tab->update(model.social());
		}
		if (GearTabView* tab = gearTab.get()) {
			tab->update(model.gear());
		}
		if (MemoryTabView* tab = memoryTab.get()) {
			tab->update(model.memory());
		}
		if (TasksTabView* tab = tasksTab.get()) {
			tab->update(model.tasks());
		}
		if (LogTabView* tab = logTab.get()) {
			tab->update(LogData{});
		}
	}

} // namespace world_sim
