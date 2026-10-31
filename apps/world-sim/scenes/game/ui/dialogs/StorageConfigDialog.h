#pragma once

// StorageConfigDialog - a storage container's rules, a modal ui::Dialog in three columns: the items it
// can hold, grouped by category (expand and collapse, with counts), a form for a new rule (priority, min
// and max, and the add buttons), and the selected item's rules, each with a delete. A change takes
// effect at once; there is no Apply. The game runs on while it is open, and update() feeds it from the
// world every frame.

#include "StorageConfigDialogModel.h"

#include <ecs/EntityID.h>
#include <ecs/World.h>
#include <ui/components/Checkbox.h>
#include <ui/components/Dialog.h>
#include <ui/components/Select.h>
#include <ui/components/TextInput.h>
#include <ui/shapes/Text.h>

#include <functional>
#include <string>
#include <vector>

namespace world_sim {

	using OpenStorageConfigCallback = std::function<void(ecs::EntityID containerId, const std::string& containerDefName)>;

	class StorageConfigDialog {
	  public:
		struct Args {
			// The player closed it: its close button, cancel or a press on the scrim. Not called by close().
			std::function<void()> onClose;
			// Once per (container, item) when a configured specific-item rule has no colony-known source;
			// the toast stack's.
			std::function<void(const std::string& title, const std::string& message)> onNotify;
		};

		explicit StorageConfigDialog(const Args& args);

		// Its controls' callbacks hold it.
		StorageConfigDialog(const StorageConfigDialog&) = delete;
		StorageConfigDialog& operator=(const StorageConfigDialog&) = delete;
		StorageConfigDialog(StorageConfigDialog&&) = delete;
		StorageConfigDialog& operator=(StorageConfigDialog&&) = delete;
		~StorageConfigDialog() = default;

		// Opens it from `opener` on a container; already open, it moves to that container.
		void open(ui::Component& opener, ecs::EntityID containerId, const std::string& containerDefName, int player = 0);
		void close();

		[[nodiscard]] bool			isOpen() const { return dialog.get() != nullptr; }
		[[nodiscard]] ecs::EntityID getContainerId() const { return model.containerId(); }

		// Each frame while open.
		void update(ecs::World& world, const engine::assets::AssetRegistry& registry);

	  private:
		void build(ui::Dialog& shown);
		void showItems();
		void showForm();
		void showRules();
		void closed();

		void toggleCategory(int categoryIndex);
		void selectItem(const std::string& defName);
		void addRule();
		void addAll();
		void removeRule(size_t ruleIndex);
		void selectAll();
		void selectNone();

		std::function<void()>													  onClose;
		std::function<void(const std::string& title, const std::string& message)> onNotify;

		StorageConfigDialogModel model;
		ecs::World*				 world = nullptr; // from update, for the controls' handlers
		std::vector<int>		 expandedCategories;
		bool					 fresh = false; // opened or moved, so everything shows after the next refresh
		// A control changed the rules or the selection; the item counts and rules show after the next
		// refresh, which reads them from the world.
		bool stale = false;

		ui::Ref<ui::Dialog>	   dialog;
		ui::Ref<ui::Component> itemList;
		ui::Ref<ui::Text>	   noSelection;
		ui::Ref<ui::Component> form;
		ui::Ref<ui::Text>	   itemName;
		ui::Ref<ui::Select>	   priority;
		ui::Ref<ui::TextInput> minAmount;
		ui::Ref<ui::TextInput> maxAmount;
		ui::Ref<ui::Checkbox>  unlimited;
		ui::Ref<ui::Text>	   rulesHeader;
		ui::Ref<ui::Text>	   rulesStatus;
		ui::Ref<ui::Component> ruleList;
	};

} // namespace world_sim
