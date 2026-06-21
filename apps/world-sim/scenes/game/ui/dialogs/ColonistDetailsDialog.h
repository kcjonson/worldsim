#pragma once

// ColonistDetailsDialog - Full-screen dialog showing detailed colonist information
//
// Displays 8 tabs: Bio, Health, Skills, Social, Gear, Memory, Tasks, Log.
// A persistent header band (Avatar + 2x2 Stat grid) sits above the tab bar,
// shared across all tabs. A footer holds Close / Work Priorities / Draft.
//
// Game continues running while dialog is open - data refreshes per-frame.

#include "ColonistDetailsModel.h"
#include "tabs/BioTabView.h"
#include "tabs/GearTabView.h"
#include "tabs/HealthTabView.h"
#include "tabs/LogTabView.h"
#include "tabs/MemoryTabView.h"
#include "tabs/SkillsTabView.h"
#include "tabs/SocialTabView.h"
#include "tabs/TasksTabView.h"

#include <component/Component.h>
#include <components/dialog/Dialog.h>
#include <components/tabbar/TabBar.h>
#include <ecs/EntityID.h>
#include <ecs/World.h>

#include <functional>
#include <memory>
#include <string>

namespace world_sim {

class ColonistDetailsDialog : public UI::Component {
  public:
	struct Args {
		std::function<void()> onClose;	// Called when dialog closes
	};

	explicit ColonistDetailsDialog(const Args& args);
	~ColonistDetailsDialog() override = default;

	// Disable copy
	ColonistDetailsDialog(const ColonistDetailsDialog&) = delete;
	ColonistDetailsDialog& operator=(const ColonistDetailsDialog&) = delete;

	// Allow move
	ColonistDetailsDialog(ColonistDetailsDialog&&) noexcept = default;
	ColonistDetailsDialog& operator=(ColonistDetailsDialog&&) noexcept = default;

	// Open dialog for a specific colonist
	void open(ecs::EntityID colonistId, float screenWidth, float screenHeight);

	// Close dialog
	void close();

	// Query state
	[[nodiscard]] bool isOpen() const;
	[[nodiscard]] ecs::EntityID getColonistId() const { return colonistId; }

	// Per-frame update with ECS world for live data
	// Note: non-const world required for GoalTaskRegistry queries
	void update(ecs::World& world, float deltaTime);

	// IComponent overrides
	void render() override;
	bool handleEvent(UI::InputEvent& event) override;
	bool containsPoint(Foundation::Vec2 point) const override;

  private:
	// Tab IDs
	static constexpr const char* kTabBio = "bio";
	static constexpr const char* kTabHealth = "health";
	static constexpr const char* kTabSkills = "skills";
	static constexpr const char* kTabSocial = "social";
	static constexpr const char* kTabGear = "gear";
	static constexpr const char* kTabMemory = "memory";
	static constexpr const char* kTabTasks = "tasks";
	static constexpr const char* kTabLog = "log";

	// Dialog dimensions
	static constexpr float kDialogWidth = 760.0F;
	static constexpr float kDialogHeight = 600.0F;
	static constexpr float kFooterHeight = 52.0F;
	static constexpr float kHeaderBandHeight = 92.0F; // Avatar + stat grid band
	static constexpr float kTabBarHeight = 36.0F;
	static constexpr float kContentPadding = 16.0F;

	// Callbacks
	std::function<void()> onCloseCallback;

	// State
	ecs::EntityID colonistId{0};
	std::string currentTab{kTabBio};

	// Model (extracts ECS data)
	ColonistDetailsModel model;

	// Child components (TabBar + tab views are direct children of the dialog)
	UI::LayerHandle dialogHandle;
	UI::LayerHandle tabBarHandle;

	// Tab views (children of content layout)
	UI::LayerHandle bioTabHandle;
	UI::LayerHandle healthTabHandle;
	UI::LayerHandle skillsTabHandle;
	UI::LayerHandle socialTabHandle;
	UI::LayerHandle gearTabHandle;
	UI::LayerHandle memoryTabHandle;
	UI::LayerHandle tasksTabHandle;
	UI::LayerHandle logTabHandle;

	// Internal methods
	void createDialog();
	void createContent();  // Creates TabBar and tab views
	void switchToTab(const std::string& tabId);
	void updateTabContent();
	void renderHeaderBand();  // Persistent Avatar + Stat grid above the tabs
	void renderFooter();	  // Close / Work Priorities / Draft buttons

	// Footer button hit-testing (footer buttons are drawn, not child components).
	[[nodiscard]] Foundation::Rect closeButtonBounds() const;
};

} // namespace world_sim
