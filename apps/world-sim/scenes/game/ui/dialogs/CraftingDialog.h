#pragma once

// CraftingDialog - a crafting station's recipes and queue, a modal ui::Dialog in three columns: the
// recipe list (dimmed where the colony lacks an input), the selected recipe's details with a quantity
// picker and Add to Queue, and the running job's progress over the queue with a Cancel for each job. It
// stays open after adding, for batches. The game runs on while it is open, and update() feeds the queue
// from the world every frame.

#include "CraftingDialogModel.h"
#include "scenes/game/ui/adapters/CraftingAdapter.h"

#include <ecs/EntityID.h>
#include <ecs/World.h>
#include <ui/components/Button.h>
#include <ui/components/Dialog.h>
#include <ui/shapes/Text.h>

#include <functional>
#include <string>

namespace world_sim {

	class CraftingDialog {
	  public:
		struct Args {
			// The player closed it: its close button, cancel or a press on the scrim. Not called by close().
			std::function<void()>								  onClose;
			QueueRecipeCallback									  onQueueRecipe; // a recipe and how many to queue
			std::function<void(const std::string& recipeDefName)> onCancelJob;
		};

		explicit CraftingDialog(const Args& args);

		// Its controls' callbacks hold it.
		CraftingDialog(const CraftingDialog&) = delete;
		CraftingDialog& operator=(const CraftingDialog&) = delete;
		CraftingDialog(CraftingDialog&&) = delete;
		CraftingDialog& operator=(CraftingDialog&&) = delete;
		~CraftingDialog() = default;

		// Opens it from `opener` on a station; already open, it moves to that station.
		void open(ui::Component& opener, ecs::EntityID stationId, const std::string& stationDefName, int player = 0);
		void close();

		[[nodiscard]] bool			isOpen() const { return dialog.get() != nullptr; }
		[[nodiscard]] ecs::EntityID getStationId() const { return model.stationId(); }

		// Each frame while open.
		void update(ecs::World& world, const engine::assets::RecipeRegistry& registry);

	  private:
		void build(ui::Dialog& shown);
		void showRecipes();
		void showDetails();
		void showQuantity();
		void showQueue();
		void closed();

		void selectRecipe(const std::string& defName);
		void changeQuantity(int delta);
		void addToQueue();

		std::function<void()>					onClose;
		QueueRecipeCallback						onQueueRecipe;
		std::function<void(const std::string&)> onCancelJob;

		CraftingDialogModel model;
		bool				fresh = false;			// opened or moved, so everything shows after the next refresh
		bool				detailsChanged = false; // a selection, shown after the next refresh

		ui::Ref<ui::Dialog>	   dialog;
		ui::Ref<ui::Component> recipeList;
		ui::Ref<ui::Text>	   noSelection;
		ui::Ref<ui::Component> details;
		ui::Ref<ui::Text>	   recipeName;
		ui::Ref<ui::Text>	   description;
		ui::Ref<ui::Component> requiresSection;
		ui::Ref<ui::Component> materials;
		ui::Ref<ui::Component> producesSection;
		ui::Ref<ui::Component> outputs;
		ui::Ref<ui::Text>	   workTime;
		ui::Ref<ui::Text>	   quantity;
		ui::Ref<ui::Button>	   minusTen;
		ui::Ref<ui::Button>	   minusOne;
		ui::Ref<ui::Text>	   emptyQueue;
		ui::Ref<ui::Component> queueList;
	};

} // namespace world_sim
