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

	void ColonistDetailsDialog::createTabBar() {
		auto* dialog = getChild<UI::Dialog>(dialogHandle);
		if (dialog == nullptr)
			return;

		auto contentBounds = dialog->getContentBounds();

		auto tabBar = UI::TabBar(
			UI::TabBar::Args{
				.position = {contentBounds.x, contentBounds.y},
				.width = contentBounds.width,
				.tabs =
					{{.id = kTabBio, .label = "Bio"},
					 {.id = kTabHealth, .label = "Health"},
					 {.id = kTabSocial, .label = "Social"},
					 {.id = kTabGear, .label = "Gear"},
					 {.id = kTabMemory, .label = "Memory"}},
				.selectedId = kTabBio,
				.onSelect = [this](const std::string& tabId) { switchToTab(tabId); }
			}
		);
		tabBarHandle = addChild(std::move(tabBar));
	}

	void ColonistDetailsDialog::createTabContent() {
		auto* dialog = getChild<UI::Dialog>(dialogHandle);
		if (dialog == nullptr)
			return;

		auto			 contentBounds = dialog->getContentBounds();
		Foundation::Rect tabContentBounds{
			contentBounds.x,
			contentBounds.y + kTabBarHeight + kContentPadding,
			contentBounds.width,
			contentBounds.height - kTabBarHeight - kContentPadding
		};

		// Create Bio tab
		auto bioTab = BioTabView();
		bioTab.create(tabContentBounds);
		bioTabHandle = addChild(std::move(bioTab));

		// Create Health tab (initially hidden)
		auto healthTab = HealthTabView();
		healthTab.create(tabContentBounds);
		healthTab.visible = false;
		healthTabHandle = addChild(std::move(healthTab));

		// Create Social tab (initially hidden)
		auto socialTab = SocialTabView();
		socialTab.create(tabContentBounds);
		socialTab.visible = false;
		socialTabHandle = addChild(std::move(socialTab));

		// Create Gear tab (initially hidden)
		auto gearTab = GearTabView();
		gearTab.create(tabContentBounds);
		gearTab.visible = false;
		gearTabHandle = addChild(std::move(gearTab));

		// Create Memory tab (initially hidden)
		auto memoryTab = MemoryTabView();
		memoryTab.create(tabContentBounds);
		memoryTab.visible = false;
		memoryTabHandle = addChild(std::move(memoryTab));
	}

	void ColonistDetailsDialog::open(ecs::EntityID newColonistId, float screenWidth, float screenHeight) {
		colonistId = newColonistId;
		currentTab = kTabBio;

		auto* dialog = getChild<UI::Dialog>(dialogHandle);
		if (dialog != nullptr) {
			dialog->open(screenWidth, screenHeight);

			// Create tab bar and content after dialog opens (needs content bounds)
			if (tabBarHandle == UI::LayerHandle{}) {
				createTabBar();
				createTabContent();
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

	void ColonistDetailsDialog::update(const ecs::World& world, float deltaTime) {
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

		// Render dialog
		auto* dialog = getChild<UI::Dialog>(dialogHandle);
		if (dialog != nullptr) {
			dialog->render();
		}

		// Render tab bar
		auto* tabBar = getChild<UI::TabBar>(tabBarHandle);
		if (tabBar != nullptr) {
			tabBar->render();
		}

		// Render active tab
		if (currentTab == kTabBio) {
			if (auto* tab = getChild<BioTabView>(bioTabHandle)) {
				tab->render();
			}
		} else if (currentTab == kTabHealth) {
			if (auto* tab = getChild<HealthTabView>(healthTabHandle)) {
				tab->render();
			}
		} else if (currentTab == kTabSocial) {
			if (auto* tab = getChild<SocialTabView>(socialTabHandle)) {
				tab->render();
			}
		} else if (currentTab == kTabGear) {
			if (auto* tab = getChild<GearTabView>(gearTabHandle)) {
				tab->render();
			}
		} else if (currentTab == kTabMemory) {
			if (auto* tab = getChild<MemoryTabView>(memoryTabHandle)) {
				tab->render();
			}
		}
	}

	bool ColonistDetailsDialog::handleEvent(UI::InputEvent& event) {
		if (!isOpen())
			return false;

		// Let dialog handle events first
		auto* dialog = getChild<UI::Dialog>(dialogHandle);
		if (dialog != nullptr && dialog->handleEvent(event)) {
			return true;
		}

		// Tab bar
		auto* tabBar = getChild<UI::TabBar>(tabBarHandle);
		if (tabBar != nullptr && tabBar->handleEvent(event)) {
			return true;
		}

		// Active tab
		if (currentTab == kTabBio) {
			if (auto* tab = getChild<BioTabView>(bioTabHandle)) {
				if (tab->handleEvent(event))
					return true;
			}
		} else if (currentTab == kTabHealth) {
			if (auto* tab = getChild<HealthTabView>(healthTabHandle)) {
				if (tab->handleEvent(event))
					return true;
			}
		} else if (currentTab == kTabSocial) {
			if (auto* tab = getChild<SocialTabView>(socialTabHandle)) {
				if (tab->handleEvent(event))
					return true;
			}
		} else if (currentTab == kTabGear) {
			if (auto* tab = getChild<GearTabView>(gearTabHandle)) {
				if (tab->handleEvent(event))
					return true;
			}
		} else if (currentTab == kTabMemory) {
			if (auto* tab = getChild<MemoryTabView>(memoryTabHandle)) {
				if (tab->handleEvent(event))
					return true;
			}
		}

		// Dialog consumes all events when open
		return true;
	}

	bool ColonistDetailsDialog::containsPoint(Foundation::Vec2 point) const {
		if (!isOpen())
			return false;
		auto* dialog = getChild<UI::Dialog>(dialogHandle);
		return dialog != nullptr && dialog->containsPoint(point);
	}

	void ColonistDetailsDialog::switchToTab(const std::string& tabId) {
		currentTab = tabId;

		// Update visibility
		if (auto* tab = getChild<BioTabView>(bioTabHandle)) {
			tab->visible = (tabId == kTabBio);
		}
		if (auto* tab = getChild<HealthTabView>(healthTabHandle)) {
			tab->visible = (tabId == kTabHealth);
		}
		if (auto* tab = getChild<SocialTabView>(socialTabHandle)) {
			tab->visible = (tabId == kTabSocial);
		}
		if (auto* tab = getChild<GearTabView>(gearTabHandle)) {
			tab->visible = (tabId == kTabGear);
		}
		if (auto* tab = getChild<MemoryTabView>(memoryTabHandle)) {
			tab->visible = (tabId == kTabMemory);
		}
	}

	void ColonistDetailsDialog::updateTabContent() {
		if (!model.isValid())
			return;

		// Update all tabs (only visible one will be rendered)
		if (auto* tab = getChild<BioTabView>(bioTabHandle)) {
			tab->update(model.bio());
		}
		if (auto* tab = getChild<HealthTabView>(healthTabHandle)) {
			tab->update(model.health());
		}
		if (auto* tab = getChild<SocialTabView>(socialTabHandle)) {
			tab->update(model.social());
		}
		if (auto* tab = getChild<GearTabView>(gearTabHandle)) {
			tab->update(model.gear());
		}
		if (auto* tab = getChild<MemoryTabView>(memoryTabHandle)) {
			tab->update(model.memory());
		}
	}

} // namespace world_sim
