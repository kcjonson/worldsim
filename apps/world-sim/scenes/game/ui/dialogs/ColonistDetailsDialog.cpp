#include "ColonistDetailsDialog.h"

#include <layout/LayoutContainer.h>
#include <shapes/Shapes.h>
#include <theme/Theme.h>

namespace world_sim {

	ColonistDetailsDialog::ColonistDetailsDialog(const Args& args)
		: onCloseCallback(args.onClose) {
		createDialog();
	}

	void ColonistDetailsDialog::createDialog() {
		auto dialog = UI::Dialog(
			UI::Dialog::Args{
				.title = "Colonist Details",
				.size = {kDialogWidth, kDialogHeight},
				.onClose =
					[this]() {
						if (onCloseCallback) {
							onCloseCallback();
						}
					},
				.modal = false
			}
		);
		dialogHandle = addChild(std::move(dialog));
	}

	// Helper to access content layout
	UI::LayoutContainer* ColonistDetailsDialog::getContentLayout() {
		auto* dialog = getChild<UI::Dialog>(dialogHandle);
		if (dialog == nullptr) {
			return nullptr;
		}
		return dialog->getChild<UI::LayoutContainer>(contentLayoutHandle);
	}

	void ColonistDetailsDialog::createContent() {
		auto* dialog = getChild<UI::Dialog>(dialogHandle);
		if (dialog == nullptr)
			return;

		auto contentBounds = dialog->getContentBounds();

		// Create vertical layout for TabBar + tab content
		auto contentLayout = UI::LayoutContainer(UI::LayoutContainer::Args{
			.position = {0, 0},  // Relative to content area (Dialog applies offset)
			.size = {contentBounds.width, contentBounds.height},
			.direction = UI::Direction::Vertical,
			.hAlign = UI::HAlign::Left,
			.vAlign = UI::VAlign::Top,
			.id = "content-layout"
		});

		// Add TabBar at top
		tabBarHandle = contentLayout.addChild(UI::TabBar(
			UI::TabBar::Args{
				.position = {0, 0},
				.width = contentBounds.width,
				.tabs =
					{{.id = kTabBio, .label = "Bio"},
					 {.id = kTabHealth, .label = "Health"},
					 {.id = kTabSocial, .label = "Social"},
					 {.id = kTabGear, .label = "Gear"},
					 {.id = kTabMemory, .label = "Memory"},
					 {.id = kTabTasks, .label = "Tasks"}},
				.selectedId = kTabBio,
				.onSelect = [this](const std::string& tabId) { switchToTab(tabId); }
			}
		));

		// Tab content bounds (below TabBar)
		Foundation::Rect tabContentBounds{
			0, 0,  // Relative positions
			contentBounds.width,
			contentBounds.height - kTabBarHeight - kContentPadding
		};

		// Create Bio tab (visible by default)
		auto bioTab = BioTabView();
		bioTab.create(tabContentBounds);
		bioTabHandle = contentLayout.addChild(std::move(bioTab));

		// Create Health tab (initially hidden)
		auto healthTab = HealthTabView();
		healthTab.create(tabContentBounds);
		healthTab.visible = false;
		healthTabHandle = contentLayout.addChild(std::move(healthTab));

		// Create Social tab (initially hidden)
		auto socialTab = SocialTabView();
		socialTab.create(tabContentBounds);
		socialTab.visible = false;
		socialTabHandle = contentLayout.addChild(std::move(socialTab));

		// Create Gear tab (initially hidden)
		auto gearTab = GearTabView();
		gearTab.create(tabContentBounds);
		gearTab.visible = false;
		gearTabHandle = contentLayout.addChild(std::move(gearTab));

		// Create Memory tab (initially hidden)
		auto memoryTab = MemoryTabView();
		memoryTab.create(tabContentBounds);
		memoryTab.visible = false;
		memoryTabHandle = contentLayout.addChild(std::move(memoryTab));

		// Create Tasks tab (initially hidden)
		auto tasksTab = TasksTabView();
		tasksTab.create(tabContentBounds);
		tasksTab.visible = false;
		tasksTabHandle = contentLayout.addChild(std::move(tasksTab));

		// Add content layout to Dialog
		contentLayoutHandle = dialog->addChild(std::move(contentLayout));
	}

	void ColonistDetailsDialog::open(ecs::EntityID newColonistId, float screenWidth, float screenHeight) {
		colonistId = newColonistId;
		currentTab = kTabBio;

		auto* dialog = getChild<UI::Dialog>(dialogHandle);
		if (dialog != nullptr) {
			dialog->open(screenWidth, screenHeight);

			// Create content after dialog opens (needs content bounds)
			if (contentLayoutHandle == UI::LayerHandle{}) {
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

		// Update dialog animation
		auto* dialog = getChild<UI::Dialog>(dialogHandle);
		if (dialog != nullptr) {
			dialog->update(deltaTime);
		}

		// Refresh model data
		auto updateType = model.refresh(world, colonistId);

		// Update dialog title with colonist name
		if (model.isValid() && dialog != nullptr) {
			dialog->setTitle(model.bio().name);
		}

		// Update content if data changed
		if (updateType == ColonistDetailsModel::UpdateType::Structure || updateType == ColonistDetailsModel::UpdateType::Values) {
			updateTabContent();
		}
	}

	void ColonistDetailsDialog::render() {
		if (!isOpen())
			return;

		// Render dialog (includes TabBar and tabs via content children)
		auto* dialog = getChild<UI::Dialog>(dialogHandle);
		if (dialog != nullptr) {
			dialog->render();
		}
	}

	bool ColonistDetailsDialog::handleEvent(UI::InputEvent& event) {
		if (!isOpen())
			return false;

		// Let Dialog handle all events (content children, chrome)
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

		auto* contentLayout = getContentLayout();
		if (contentLayout == nullptr)
			return;

		// Update visibility
		if (auto* tab = contentLayout->getChild<BioTabView>(bioTabHandle)) {
			tab->visible = (tabId == kTabBio);
		}
		if (auto* tab = contentLayout->getChild<HealthTabView>(healthTabHandle)) {
			tab->visible = (tabId == kTabHealth);
		}
		if (auto* tab = contentLayout->getChild<SocialTabView>(socialTabHandle)) {
			tab->visible = (tabId == kTabSocial);
		}
		if (auto* tab = contentLayout->getChild<GearTabView>(gearTabHandle)) {
			tab->visible = (tabId == kTabGear);
		}
		if (auto* tab = contentLayout->getChild<MemoryTabView>(memoryTabHandle)) {
			tab->visible = (tabId == kTabMemory);
		}
		if (auto* tab = contentLayout->getChild<TasksTabView>(tasksTabHandle)) {
			tab->visible = (tabId == kTabTasks);
		}
	}

	void ColonistDetailsDialog::updateTabContent() {
		if (!model.isValid())
			return;

		auto* contentLayout = getContentLayout();
		if (contentLayout == nullptr)
			return;

		// Update all tabs (only visible one will be rendered)
		if (auto* tab = contentLayout->getChild<BioTabView>(bioTabHandle)) {
			tab->update(model.bio());
		}
		if (auto* tab = contentLayout->getChild<HealthTabView>(healthTabHandle)) {
			tab->update(model.health());
		}
		if (auto* tab = contentLayout->getChild<SocialTabView>(socialTabHandle)) {
			tab->update(model.social());
		}
		if (auto* tab = contentLayout->getChild<GearTabView>(gearTabHandle)) {
			tab->update(model.gear());
		}
		if (auto* tab = contentLayout->getChild<MemoryTabView>(memoryTabHandle)) {
			tab->update(model.memory());
		}
		if (auto* tab = contentLayout->getChild<TasksTabView>(tasksTabHandle)) {
			tab->update(model.tasks());
		}
	}

} // namespace world_sim
