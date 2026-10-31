#pragma once

// EntityInfoView - the selected entity, in a Panel at the bottom left; hidden while nothing is selected.
// - A colonist: a header (mood-ringed Avatar, name, mood and task meters, the dossier and close
//   buttons), a TabBar (Needs, Bio, Gear, Log) over the chosen tab's body, and an action row (Draft or
//   Release, Go to, and Priorities, disabled until a priorities system exists).
// - Anything else: the title and a dim subtitle with the close button, then the model's InfoSlot rows,
//   built again when the selection's structure changes and updated in place when only values do.
// kWidth wide unless its size says otherwise; its height follows the content.

#include "scenes/game/ui/components/NeedBar.h"
#include "scenes/game/ui/models/EntityInfoModel.h"
#include "scenes/game/world/selection/SelectionTypes.h"

#include <assets/AssetRegistry.h>
#include <assets/RecipeRegistry.h>
#include <construction/ConstructionWorld.h>
#include <ecs/World.h>

#include <theme/Avatar.h>
#include <theme/Panel.h>

#include <ui/components/Badge.h>
#include <ui/components/Button.h>
#include <ui/components/ProgressBar.h>
#include <ui/components/TabBar.h>
#include <ui/shapes/Text.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace world_sim {

	class EntityInfoView : public theme::Panel {
	  public:
		static constexpr float kWidth = 320.0F;

		using OpenCraftingDialogCallback = EntityInfoModel::OpenCraftingDialogCallback;
		using OpenStorageConfigCallback = EntityInfoModel::OpenStorageConfigCallback;

		struct Args {
			UI_COMMON_ARGS
			std::function<void()>				   onClose;			// the close button
			std::function<void()>				   onDetails;		// the eye button, which opens the dossier
			std::function<void(ecs::EntityID)>	   onToggleControl; // Draft or Release
			std::function<void(ecs::EntityID)>	   onGoTo;			// centres the camera on the colonist
			OpenCraftingDialogCallback			   onOpenCraftingDialog;
			std::function<void()>				   onPlace;
			std::function<void()>				   onMoveFurniture;
			OpenStorageConfigCallback			   onOpenStorageConfig;
			EntityInfoModel::ResourceQueryCallback queryResources;
			std::function<void()>				   onDemolishFoundation;
			std::function<void()>				   onDemolishBuilding;
			std::function<void()>				   onAddToFoundation;
			std::function<void()>				   onSubtractFromFoundation;
			std::function<void()>				   onDemolishWallSegment;
			std::function<void()>				   onDemolishOpening;
		};

		explicit EntityInfoView(const Args& args);

		// Each frame, with the current selection: shows, hides or refreshes the panel as the model says.
		void update(
			ecs::World&									   world,
			const engine::assets::AssetRegistry&		   assetRegistry,
			const engine::assets::RecipeRegistry&		   recipeRegistry,
			const Selection&							   selection,
			const engine::construction::ConstructionWorld* constructionWorld = nullptr,
			const ecs::RoomDetectionSystem*				   roomDetection = nullptr
		);

		[[nodiscard]] const char* typeName() const override { return "EntityInfoView"; }

	  protected:
		void onMount() override;

	  private:
		void applyContent(const PanelContent& content, bool structural);
		void applyColonist(const ColonistPanelData& data, bool structural);
		void applyGeneric(const PanelContent& content, bool structural);
		void showSlots(const std::vector<InfoSlot>& slots);
		void showAvatar(const std::string& seed, float mood);
		void setActiveTab(const std::string& tabId);

		EntityInfoModel					   model;
		EntityInfoModel::Callbacks		   modelCallbacks;
		std::function<void()>			   onClose;
		std::function<void()>			   onDetails;
		std::function<void(ecs::EntityID)> onToggleControl;
		std::function<void(ecs::EntityID)> onGoTo;
		ecs::EntityID					   selectedColonistId{0};
		std::string						   avatarSeed;
		// Bumped by each structural change, so the slot rows are built again rather than reused.
		std::uint32_t slotGeneration = 0;

		ui::Ref<ui::Component>			avatarSlot;
		ui::Ref<theme::Avatar>			avatar;
		ui::Ref<ui::Text>				nameText;
		ui::Ref<ui::Text>				subtitleText;
		ui::Ref<ui::ProgressBar>		moodBar;
		ui::Ref<ui::ProgressBar>		taskBar;
		ui::Ref<ui::Button>				eyeButton;
		ui::Ref<ui::TabBar>				tabBar;
		ui::Ref<ui::Component>			needsBody;
		ui::Ref<ui::Component>			bioBody;
		ui::Ref<ui::Component>			gearBody;
		ui::Ref<ui::Component>			logBody;
		ui::Ref<ui::Component>			actionRow;
		ui::Ref<ui::Button>				draftButton;
		ui::Ref<ui::Component>			slotsBody;
		std::vector<ui::Ref<NeedBar>>	needBars; // one per ecs::NeedType
		ui::Ref<ui::Text>				bioAge;
		ui::Ref<ui::Text>				bioMood;
		ui::Ref<ui::Text>				gearHands;
		std::vector<ui::Ref<ui::Badge>> beltChips; // ecs::Inventory's two belt slots
		ui::Ref<ui::Text>				beltEmpty;
		ui::Ref<ui::ProgressBar>		carryBar;
		ui::Ref<ui::Text>				gearPack;
	};

} // namespace world_sim
