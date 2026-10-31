#include "ColonistDetailsDialog.h"

#include <ui/Context.h>
#include <ui/components/Button.h>

#include <string>

namespace world_sim {

	namespace {

		constexpr const char* kTabBio = "bio";
		constexpr const char* kTabNeeds = "needs";
		constexpr const char* kTabSkills = "skills";
		constexpr const char* kTabSocial = "social";
		constexpr const char* kTabGear = "gear";
		constexpr const char* kTabMemory = "memory";
		constexpr const char* kTabTasks = "tasks";
		constexpr const char* kTabLog = "log";

		// The prototype dossier's proportions, 760x900 design px.
		constexpr float kDialogWidth = 760.0F;
		constexpr float kDialogHeight = 900.0F;
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
		if (dialog.get() == nullptr) {
			dialog = ui::Dialog::open(
				opener,
				{
					.title = "Colonist Details",
					.kicker = "Personnel File",
					.panelSize = {kDialogWidth, kDialogHeight},
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
				build(*shown);
			}
		}
		if (ui::TabBar* bar = tabBar.get()) {
			bar->setSelectedId(kTabBio);
		}
		showTab(kTabBio);
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

		tabBar = content.addChild<ui::TabBar>({
			.tabs =
				{{.id = kTabBio, .label = "Bio"},
				 {.id = kTabNeeds, .label = "Needs"},
				 {.id = kTabSkills, .label = "Skills"},
				 {.id = kTabSocial, .label = "Social"},
				 {.id = kTabGear, .label = "Gear"},
				 {.id = kTabMemory, .label = "Memory"},
				 {.id = kTabTasks, .label = "Tasks"},
				 {.id = kTabLog, .label = "Log"}},
			.selectedId = kTabBio,
			.onSelect = [this](const std::string& tabId) { showTab(tabId); },
		});
		// Takes what the band and the bar leave, and scrolls a taller tab.
		tabArea = content.addChild<ui::Component>({
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
		area->scrollTo({0.0F, 0.0F});
		if (!fresh) {
			feedTab();
		}
	}

	void ColonistDetailsDialog::update(ecs::World& world) {
		if (!isOpen()) {
			return;
		}
		const ColonistDetailsModel::UpdateType change = model.refresh(world, colonistId);
		if (!model.isValid() || (!fresh && change == ColonistDetailsModel::UpdateType::None)) {
			return;
		}
		fresh = false;
		// The title is the colonist's name; the kicker stays "Personnel File".
		if (dialog.get()->getTitle() != model.bio().name) {
			dialog.get()->setTitle(model.bio().name);
		}
		showHeader();
		feedTab();
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
