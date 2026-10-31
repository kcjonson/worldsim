#pragma once

// GameUI - the game scene's ui, mounted under the scene's root: the top bar, the roster down the left,
// the selected entity's panel and its task list at the bottom left, the minimap, resources and global
// task list down the right, the gameplay bar and construction strip at the bottom middle, the zoom
// column at the bottom right, the build menu, toasts and the three dialogs. It feeds them from the
// world each frame; salal's ui lays them out, draws them and takes their input, so a press on any of
// them never reaches the world.

#include "scenes/game/ui/dialogs/ColonistDetailsDialog.h"
#include "scenes/game/ui/dialogs/CraftingDialog.h"
#include "scenes/game/ui/dialogs/StorageConfigDialog.h"
#include "scenes/game/ui/models/ColonistListModel.h"
#include "scenes/game/ui/models/GlobalTaskListModel.h"
#include "scenes/game/ui/models/ResourcesModel.h"
#include "scenes/game/ui/models/TimeModel.h"
#include "scenes/game/ui/views/BuildMenu.h"
#include "scenes/game/ui/views/ColonistListView.h"
#include "scenes/game/ui/views/ConstructionConfigStrip.h"
#include "scenes/game/ui/views/DebugOverlay.h"
#include "scenes/game/ui/views/EntityInfoView.h"
#include "scenes/game/ui/views/GameplayBar.h"
#include "scenes/game/ui/views/GlobalTaskListView.h"
#include "scenes/game/ui/views/RegionMinimapPanel.h"
#include "scenes/game/ui/views/ResourcesPanel.h"
#include "scenes/game/ui/views/TaskListView.h"
#include "scenes/game/ui/views/TopBar.h"
#include "scenes/game/ui/views/ZoomControlPanel.h"
#include "scenes/game/world/selection/SelectionTypes.h"

#include <assets/AssetRegistry.h>
#include <assets/RecipeRegistry.h>
#include <ecs/World.h>
#include <ecs/systems/RoomDetectionSystem.h>
#include <ecs/systems/TimeSystem.h>
#include <world/camera/WorldCamera.h>
#include <world/chunk/ChunkManager.h>

#include <ui/Component.h>
#include <ui/components/Toast.h>

#include <functional>
#include <memory>

namespace world_sim {

	/// Callback to query remaining resource count for a world entity
	using ResourceQueryCallback = std::function<std::optional<uint32_t>(const std::string& defName, Foundation::Vec2 position)>;

	class GameUI {
	  public:
		struct Args {
			ui::Ref<ui::Component>					root; // the scene's root, which the views mount under
			std::function<void()>					onZoomIn;
			std::function<void()>					onZoomOut;
			std::function<void()>					onZoomReset;
			std::function<void()>					onSelectionCleared;
			std::function<void(ecs::EntityID)>		onColonistSelected;
			std::function<void(ecs::EntityID)>		onColonistFollowed;		 ///< Called on double-click to follow
			std::function<void(ecs::EntityID)>		onColonistControlToggle; ///< Called when the colonist Control/Release button is clicked
			std::function<void(const std::string&)> onBuildItemSelected;	 ///< Called when item selected from build menu
			std::function<void(const std::string&)> onProductionSelected;	 ///< Called when production item selected (e.g., CraftingSpot)
			QueueRecipeCallback						onQueueRecipe;			 ///< Called when recipe queued at station
			std::function<void(const std::string&)> onCancelJob;			 ///< Called when job canceled from queue
			std::function<void(ecs::EntityID, const std::string&)> onOpenCraftingDialog; ///< Called to open crafting dialog
			std::function<void(ecs::EntityID, const std::string&)> onOpenStorageConfig;	 ///< Called to open storage config dialog
			std::function<void()>								   onPause;				 ///< Called when pause button clicked
			std::function<void(ecs::GameSpeed)>					   onSpeedChange;		 ///< Called when speed changed
			std::function<void()>								   onMenuClick;			 ///< Called when menu button clicked
			std::function<void()> onPlaceFurniture;			///< Called when Place button clicked for packaged furniture
			std::function<void()> onMoveFurniture;			///< Called when Move button clicked for an installed box
			std::function<void()> onDemolishFoundation;		///< Called when Demolish foundation clicked (clear foundation only)
			std::function<void()> onDemolishBuilding;		///< Called when Demolish building clicked (walls + openings + foundation)
			std::function<void()> onAddToFoundation;		///< Called when a foundation panel's Add clicked
			std::function<void()> onSubtractFromFoundation; ///< Called when a foundation panel's Subtract clicked
			std::function<void()> onDemolishWallSegment;	///< Called when Demolish button clicked for a wall segment
			std::function<void()> onDemolishOpening;		///< Called when Demolish button clicked for an opening
			ResourceQueryCallback queryResources;			///< Query remaining resource count for harvestable entities
			std::function<void(const std::string&)> onStructureSelected; ///< Called when a structure tool (e.g. "foundation") is chosen
			std::function<void(const std::string&)> onConstructionMaterialSelected; ///< Called when a config-strip material card is clicked
			std::function<void(const std::string&)> onConstructionThicknessSelected; ///< Called when a wall thickness preset is clicked
			std::function<void()>					onRoomsToggle; ///< Called when the GameplayBar Rooms toggle is clicked
		};

		explicit GameUI(const Args& args);
		// Closes the dialogs, whose overlays call back into them; the scene destroys GameUI in onExit,
		// while its ui context still lives.
		~GameUI();
		GameUI(const GameUI&) = delete;
		GameUI& operator=(const GameUI&) = delete;
		GameUI(GameUI&&) = delete;
		GameUI& operator=(GameUI&&) = delete;

		void update(
			float								   deltaTime,
			const world::WorldCamera&			   camera,
			const world::ChunkManager&			   chunkManager,
			ecs::World&							   ecsWorld,
			const assets::AssetRegistry&		   assetRegistry,
			const assets::RecipeRegistry&		   recipeRegistry,
			const Selection&					   selection,
			const construction::ConstructionWorld* constructionWorld = nullptr,
			const ecs::RoomDetectionSystem*		   roomDetection = nullptr
		);

		void setMinimapContext(Foundation::Vec2 crashSite, double latDeg, double lonDeg, float pixelsPerMeter);

		void pushNotification(
			const std::string&	  title,
			const std::string&	  message,
			ui::ToastSeverity	  severity = ui::ToastSeverity::Info,
			float				  autoDismissTime = 5.0F, // seconds; 0 keeps it
			std::function<void()> onClick = nullptr
		);

		void			   showBuildMenu(const std::vector<BuildMenuItem>& items);
		void			   hideBuildMenu();
		[[nodiscard]] bool isBuildMenuVisible() const;
		void			   setProductionItems(const std::vector<std::pair<std::string, std::string>>& items);
		void			   setConstructionMaterials(const std::vector<std::pair<std::string, float>>& materials);
		void			   setConstructionThicknessPresets(const std::vector<ConstructionConfigStrip::ThicknessPresetInfo>& presets);
		void			   setConstructionStatus(const DrawingStatus& status);
		void			   setRoomsOverlayActive(bool active);

		void			   showColonistDetails(ecs::EntityID colonistId);
		void			   hideColonistDetails();
		[[nodiscard]] bool isColonistDetailsVisible() const;
		void			   showCraftingDialog(ecs::EntityID stationId, const std::string& stationDefName);
		void			   hideCraftingDialog();
		[[nodiscard]] bool isCraftingDialogVisible() const;
		void			   showStorageConfigDialog(ecs::EntityID containerId, const std::string& containerDefName);
		void			   hideStorageConfigDialog();
		[[nodiscard]] bool isStorageConfigVisible() const;
		[[nodiscard]] bool isGlobalTaskListExpanded() const;
		void			   toggleDecisionInspector();

	  private:
		void toggleTaskList();
		// What depends on the viewport's size: the task list's cap and the build menu's centring.
		void			  fitViewport(Foundation::Vec2 viewport);
		[[nodiscard]] int criticalToastCount() const;

		ui::Ref<ui::Component>				   root;
		ui::Ref<TopBar>						   topBar;
		ui::Ref<DebugOverlay>				   debugOverlay;
		ui::Ref<ZoomControlPanel>			   zoomControlPanel;
		ui::Ref<GameplayBar>				   gameplayBar;
		ui::Ref<ConstructionConfigStrip>	   configStrip;
		ui::Ref<BuildMenu>					   buildMenu;
		ui::Ref<ColonistListView>			   colonistList;
		ui::Ref<EntityInfoView>				   infoPanel;
		ui::Ref<TaskListView>				   taskListPanel;
		ui::Ref<RegionMinimapPanel>			   minimapPanel;
		ui::Ref<ResourcesPanel>				   resourcesPanel;
		ui::Ref<GlobalTaskListView>			   globalTaskList;
		ui::Ref<ui::ToastStack>				   toastStack;
		std::unique_ptr<ColonistDetailsDialog> colonistDetailsDialog;
		std::unique_ptr<CraftingDialog>		   craftingDialog;
		std::unique_ptr<StorageConfigDialog>   storageConfigDialog;

		TimeModel			timeModel;
		ColonistListModel	colonistListModel;
		GlobalTaskListModel globalTaskListModel;
		ResourcesModel		resourcesModel;

		bool				  taskListExpanded = false;
		ecs::EntityID		  selectedColonistId{0};
		float				  worldPixelsPerMeter = 0.0F;
		Foundation::Vec2	  fittedViewport{0.0F, 0.0F};
		std::function<void()> onSelectionCleared;
	};

} // namespace world_sim
