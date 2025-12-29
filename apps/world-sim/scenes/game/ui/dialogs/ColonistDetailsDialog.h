#pragma once

// ColonistDetailsDialog - Full-screen dialog showing detailed colonist information
//
// Displays 5 tabs:
// - Bio: name, placeholder age/traits/background, current task
// - Health: all 8 needs as progress bars, mood
// - Social: placeholder for future relationships
// - Gear: inventory items
// - Memory: TreeView of known entities by category
//
// Game continues running while dialog is open - data refreshes per-frame.

#include "ColonistDetailsModel.h"

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
	void update(const ecs::World& world, float deltaTime);

	// IComponent overrides
	void render() override;
	bool handleEvent(UI::InputEvent& event) override;
	bool containsPoint(Foundation::Vec2 point) const override;

  private:
	// Tab IDs
	static constexpr const char* kTabBio = "bio";
	static constexpr const char* kTabHealth = "health";
	static constexpr const char* kTabSocial = "social";
	static constexpr const char* kTabGear = "gear";
	static constexpr const char* kTabMemory = "memory";

	// Dialog dimensions
	static constexpr float kDialogWidth = 600.0F;
	static constexpr float kDialogHeight = 500.0F;
	static constexpr float kTabBarHeight = 36.0F;
	static constexpr float kContentPadding = 16.0F;

	// Callbacks
	std::function<void()> onCloseCallback;

	// State
	ecs::EntityID colonistId{0};
	std::string currentTab{kTabBio};

	// Model (extracts ECS data)
	ColonistDetailsModel model;

	// Child components
	UI::LayerHandle dialogHandle;
	UI::LayerHandle tabBarHandle;

	// Tab content handles (one per tab)
	UI::LayerHandle bioContentHandle;
	UI::LayerHandle healthContentHandle;
	UI::LayerHandle socialContentHandle;
	UI::LayerHandle gearContentHandle;
	UI::LayerHandle memoryContentHandle;

	// Bio tab element handles (for updating)
	UI::LayerHandle bioNameHandle;
	UI::LayerHandle bioAgeHandle;
	UI::LayerHandle bioTaskHandle;
	UI::LayerHandle bioMoodHandle;

	// Health tab element handles
	UI::LayerHandle healthMoodHandle;
	std::array<UI::LayerHandle, 8> needBarHandles;

	// Gear tab element handles
	UI::LayerHandle gearHeaderHandle;
	UI::LayerHandle gearItemsHandle;

	// Memory tab element handles
	UI::LayerHandle memoryHeaderHandle;
	UI::LayerHandle memoryTreeHandle;

	// Internal methods
	void createDialog();
	void createTabBar();
	void createTabContent();
	void switchToTab(const std::string& tabId);
	void updateTabContent();

	// Update individual tab content from model
	void updateBioTab();
	void updateHealthTab();
	void updateSocialTab();
	void updateGearTab();
	void updateMemoryTab();
};

} // namespace world_sim
