#pragma once

// ColonistDetailsDialog - a colonist's dossier, a non-modal ui::Dialog: a header band (the Avatar and a
// 2x2 Stat grid) over eight tabs, Bio, Needs, Skills, Social, Gear, Memory, Tasks and Log, and a footer
// of Close, Work Priorities and Draft. Only the selected tab's view is mounted. The game runs on while
// it is open, and update() feeds it from the world every frame.

#include "ColonistDetailsModel.h"
#include "tabs/BioTabView.h"
#include "tabs/GearTabView.h"
#include "tabs/LogTabView.h"
#include "tabs/MemoryTabView.h"
#include "tabs/NeedsTabView.h"
#include "tabs/SkillsTabView.h"
#include "tabs/SocialTabView.h"
#include "tabs/TasksTabView.h"

#include <ecs/EntityID.h>
#include <ecs/World.h>
#include <theme/Avatar.h>
#include <ui/components/Dialog.h>
#include <ui/components/Stat.h>
#include <ui/components/TabBar.h>

#include <functional>
#include <string>

namespace world_sim {

	class ColonistDetailsDialog {
	  public:
		struct Args {
			// The player closed it: its close button, cancel or the footer's Close. Not called by close().
			std::function<void()> onClose;
		};

		explicit ColonistDetailsDialog(const Args& args);

		// Its tabs' callbacks hold it.
		ColonistDetailsDialog(const ColonistDetailsDialog&) = delete;
		ColonistDetailsDialog& operator=(const ColonistDetailsDialog&) = delete;
		ColonistDetailsDialog(ColonistDetailsDialog&&) = delete;
		ColonistDetailsDialog& operator=(ColonistDetailsDialog&&) = delete;
		~ColonistDetailsDialog() = default;

		// Opens it from `opener` on a colonist's Bio tab; already open, it moves to that colonist.
		void open(ui::Component& opener, ecs::EntityID colonistId, int player = 0);
		void close();

		[[nodiscard]] bool			isOpen() const { return dialog.get() != nullptr; }
		[[nodiscard]] ecs::EntityID getColonistId() const { return colonistId; }

		// Each frame while open. The world is non-const for GoalTaskRegistry's queries.
		void update(ecs::World& world);

	  private:
		void build(ui::Dialog& shown);
		void showTab(const std::string& tabId);
		void showHeader();
		void feedTab();
		void closed();

		std::function<void()> onClose;
		ecs::EntityID		  colonistId{0};
		std::string			  currentTab;
		std::string			  avatarSeed;
		bool				  fresh = false; // opened or moved since the last feed
		ColonistDetailsModel  model;

		ui::Ref<ui::Dialog>	   dialog;
		ui::Ref<ui::Component> avatarSlot;
		ui::Ref<theme::Avatar> avatar;
		ui::Ref<ui::Stat>	   ageStat;
		ui::Ref<ui::Stat>	   moodStat;
		ui::Ref<ui::TabBar>	   tabBar;
		ui::Ref<ui::Component> tabArea;
		ui::Ref<ui::Component> tabView;

		ui::Ref<BioTabView>	   bioTab;
		ui::Ref<NeedsTabView>  needsTab;
		ui::Ref<SkillsTabView> skillsTab;
		ui::Ref<SocialTabView> socialTab;
		ui::Ref<GearTabView>   gearTab;
		ui::Ref<MemoryTabView> memoryTab;
		ui::Ref<TasksTabView>  tasksTab;
		ui::Ref<LogTabView>	   logTab;
	};

} // namespace world_sim
