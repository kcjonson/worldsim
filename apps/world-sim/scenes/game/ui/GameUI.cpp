#include "GameUI.h"

#include "scenes/shared/SalalUi.h"

#include <primitives/Primitives.h>
#include <ui/Context.h>

#include <algorithm>

namespace world_sim {

	namespace {

		constexpr float kEdge = 12.0F;			  // the HUD's distance from the viewport's edges
		constexpr float kRightStackTop = 60.0F;	  // just below the top bar
		constexpr float kDockBottom = 12.0F;	  // the gameplay bar's distance from the bottom
		// The dock's box spans the bottom edge under the corner panels on purpose; only its children take the pointer.
		constexpr int kDockLayer = -1;
		constexpr float kBuildMenuBottom = 70.0F; // above the gameplay bar
		constexpr float kTaskListRoom = 100.0F;	  // the viewport height the task list leaves free
		// Toasts stack up from the bottom right, above the zoom column, which owns the corner.
		constexpr float kToastBottom = 130.0F;

		ui::Layout absolute(ui::Inset inset) {
			return {.position = ui::Position::Absolute, .inset = inset};
		}

		ui::Layout stack(ui::Inset inset, ui::AlignItems align, float gap) {
			return {
				.display = ui::Display::Flex,
				.flexDirection = ui::FlexDirection::Column,
				.gap = gap,
				.alignItems = align,
				.position = ui::Position::Absolute,
				.inset = inset,
			};
		}

	} // namespace

	GameUI::GameUI(const Args& args)
		: root(args.root),
		  onSelectionCleared(args.onSelectionCleared) {
		const ui::Theme& theme = root.get()->getContext()->getTheme();
		// The HUD is the pointer's and the hotkeys': a click leaves no focus in it for Space or Enter to
		// press again. The dialogs opened from it are their own, and take focus as ever.
		root.get()->setDescendantsFocusable(false);

		topBar = root.addChild<TopBar>({
			.id = "top_bar",
			.layout = absolute({.top = 0.0F, .left = 0.0F}),
			.onPause = args.onPause,
			.onSpeedChange = args.onSpeedChange,
			.onMenuClick = args.onMenuClick,
		});
		debugOverlay = root.addChild<DebugOverlay>({.id = "debug_overlay", .layout = absolute({.bottom = 20.0F, .left = 0.0F})});
		zoomControlPanel = root.addChild<ZoomControlPanel>({
			.id = "zoom_panel",
			.layout = absolute({.right = kEdge, .bottom = kEdge}),
			.onZoomIn = args.onZoomIn,
			.onZoomOut = args.onZoomOut,
			.onZoomReset = args.onZoomReset,
		});

		// The bottom middle, top down: the key guide and the construction strip while a drawing tool is
		// active, then the gameplay bar.
		ui::Ref<ui::Component> dock = root.addChild<ui::Component>({
			.id = "bottom_dock",
			.layout =
				stack({.right = 0.0F, .bottom = kDockBottom, .left = 0.0F}, ui::AlignItems::Center, theme.get(tokens::spacing::space1)),
			.zIndex = kDockLayer,
		});
		keyGuide = dock.addChild<DrawingKeyGuide>({.id = "key_guide", .onOpen = args.onKeyGuideOpened});
		configStrip = dock.addChild<ConstructionConfigStrip>({
			.id = "construction_config_strip",
			.onFoundationToolSelected = args.onFoundationToolSelected,
			.onMaterialSelected = args.onConstructionMaterialSelected,
			.onThicknessSelected = args.onConstructionThicknessSelected,
		});
		gameplayBar = dock.addChild<GameplayBar>({
			.id = "gameplay_bar",
			.onProductionSelected = args.onProductionSelected,
			.onStructureSelected = args.onStructureSelected,
			.onRoomsToggle = args.onRoomsToggle,
		});
		buildMenu = root.addChild<BuildMenu>({
			.id = "build_menu",
			.visible = false,
			.layout = absolute({.bottom = kBuildMenuBottom, .left = 0.0F}),
			.onSelect = args.onBuildItemSelected,
			.onClose = [this] { hideBuildMenu(); },
		});

		colonistList = root.addChild<ColonistListView>({
			.id = "colonist_list",
			.layout = absolute({.top = TopBar::kHeight + kEdge, .left = kEdge}),
			.onColonistSelected = args.onColonistSelected,
			.onColonistFollowed = args.onColonistFollowed,
		});

		// The bottom left: the task list, while it's open, on the selected entity's panel. Hidden while both
		// are, so an empty box isn't laid out.
		bottomLeft = root.addChild<ui::Component>(
			{.id = "bottom_left", .visible = false, .layout = stack({.bottom = 0.0F, .left = kEdge}, ui::AlignItems::Start, 0.0F)}
		);
		taskListPanel = bottomLeft.addChild<TaskListView>({.id = "task_list", .visible = false, .onClose = [this] { toggleTaskList(); }});
		infoPanel = bottomLeft.addChild<EntityInfoView>({
			.id = "entity_panel",
			.onClose =
				[this] {
					if (onSelectionCleared) {
						onSelectionCleared();
					}
				},
			.onDetails =
				[this] {
					if (selectedColonistId != 0) {
						showColonistDetails(selectedColonistId);
					}
				},
			.onToggleControl = args.onColonistControlToggle,
			.onGoTo = args.onColonistFollowed, // the roster's double-click follow
			.onOpenCraftingDialog = args.onOpenCraftingDialog,
			.onPlace = args.onPlaceFurniture,
			.onMoveFurniture = args.onMoveFurniture,
			.onOpenStorageConfig = args.onOpenStorageConfig,
			.queryResources = args.queryResources,
			.onDemolishFoundation = args.onDemolishFoundation,
			.onDemolishBuilding = args.onDemolishBuilding,
			.onAddToFoundation = args.onAddToFoundation,
			.onSubtractFromFoundation = args.onSubtractFromFoundation,
			.onDemolishWallSegment = args.onDemolishWallSegment,
			.onDemolishOpening = args.onDemolishOpening,
		});

		// The right: the minimap, the resources and the global task list, restacking as they grow.
		ui::Ref<ui::Component> right = root.addChild<ui::Component>({
			.id = "right_stack",
			.layout = stack(
				{.top = kRightStackTop, .right = theme.get(tokens::spacing::space3)},
				ui::AlignItems::End,
				theme.get(tokens::spacing::space2)
			),
		});
		minimapPanel = right.addChild<RegionMinimapPanel>({.id = "region_minimap"});
		resourcesPanel = right.addChild<ResourcesPanel>({.id = "resources_panel"});
		globalTaskList = right.addChild<GlobalTaskListView>({.id = "global_task_list"});

		toastStack = ui::ToastStack::open(
			*root.get(),
			{.id = "toast_stack",
			 .anchor = ui::ToastAnchor::BottomRight,
			 .spacing = 8.0F,
			 .inset = {16.0F, 20.0F, kToastBottom, 16.0F},
			 .maxToasts = 5,
			 .toastWidth = 300.0F}
		);

		colonistDetailsDialog =
			std::make_unique<ColonistDetailsDialog>(ColonistDetailsDialog::Args{.traits = args.traits, .onClose = [this] { hideColonistDetails(); }});
		craftingDialog = std::make_unique<CraftingDialog>(CraftingDialog::Args{
			.onClose = [this] { hideCraftingDialog(); }, .onQueueRecipe = args.onQueueRecipe, .onCancelJob = args.onCancelJob
		});
		storageConfigDialog = std::make_unique<StorageConfigDialog>(StorageConfigDialog::Args{
			.onClose = [this] { hideStorageConfigDialog(); },
			.onNotify = [this](
							const std::string& title, const std::string& message
						) { pushNotification(title, message, ui::ToastSeverity::Warning); },
		});
		pauseMenuDialog = std::make_unique<PauseMenuDialog>(PauseMenuDialog::Args{
			.onClose = args.onPauseMenuClosed,
			.onMainMenu = args.onMainMenu,
			.onQuitGame = args.onQuitGame,
		});

		fitViewport(root.get()->getContext()->getViewport());
	}

	GameUI::~GameUI() {
		colonistDetailsDialog->close();
		craftingDialog->close();
		storageConfigDialog->close();
		pauseMenuDialog->close();
	}

	void GameUI::fitViewport(Foundation::Vec2 viewport) {
		if (viewport == fittedViewport) {
			return;
		}
		fittedViewport = viewport;
		taskListPanel.get()->setMaxSize({ui::kAuto, std::min(TaskListView::kMaxHeight, viewport.y - kTaskListRoom)});
		ui::Layout menu = buildMenu.get()->getLayout();
		menu.inset.left = (viewport.x - BuildMenu::kWidth) * 0.5F;
		buildMenu.get()->setLayout(menu);
	}

	void GameUI::setMinimapContext(Foundation::Vec2 crashSite, double latDeg, double lonDeg, float pixelsPerMeter) {
		worldPixelsPerMeter = pixelsPerMeter;
		minimapPanel.get()->setContext(crashSite, latDeg, lonDeg);
	}

	void GameUI::update(
		float								   deltaTime,
		const world::WorldCamera&			   camera,
		const world::ChunkManager&			   chunkManager,
		ecs::World&							   ecsWorld,
		const assets::AssetRegistry&		   assetRegistry,
		const assets::RecipeRegistry&		   recipeRegistry,
		const Selection&					   selection,
		const construction::ConstructionWorld* constructionWorld,
		const ecs::RoomDetectionSystem*		   roomDetection
	) {
		fitViewport(root.get()->getContext()->getViewport());

		timeModel.refresh(ecsWorld);
		topBar.get()->updateData(timeModel, static_cast<int>(colonistListModel.colonists().size()));
		topBar.get()->setAlertCount(criticalToastCount());
		debugOverlay.get()->updateData(camera, chunkManager);
		zoomControlPanel.get()->setZoomPercent(camera.zoomPercent());

		ecs::EntityID selectedColonist{0};
		if (const auto* colonistSel = std::get_if<ColonistSelection>(&selection)) {
			selectedColonist = colonistSel->entityId;
		}
		colonistListModel.setSelectedId(selectedColonist);
		colonistList.get()->update(colonistListModel, ecsWorld);

		if (worldPixelsPerMeter > 0.0F) {
			// The world is drawn over the window's logical px, not the ui's.
			int windowWidth = 0;
			int windowHeight = 0;
			Renderer::Primitives::getLogicalViewport(windowWidth, windowHeight);
			minimapPanel.get()->updateData(
				camera.getVisibleRect(windowWidth, windowHeight, worldPixelsPerMeter),
				colonistListModel.colonists()
			);
		}

		// Throttled: the resources at 2 Hz, the global tasks at 5 Hz.
		if (resourcesModel.refresh(ecsWorld, assetRegistry, deltaTime)) {
			resourcesPanel.get()->setResources(resourcesModel.rows(), resourcesModel.containerCount());
		}
		const glm::vec2 cameraCenter{camera.position().x, camera.position().y};
		if (globalTaskListModel.refresh(ecsWorld, cameraCenter, deltaTime)) {
			globalTaskList.get()->setTasks(globalTaskListModel.tasks());
		}
		globalTaskList.get()->setTaskCount(globalTaskListModel.taskCount());

		// The task list closes when the selection moves off its colonist.
		if (selectedColonist != selectedColonistId) {
			selectedColonistId = selectedColonist;
			if (taskListExpanded) {
				toggleTaskList();
			}
		}
		infoPanel.get()->update(ecsWorld, assetRegistry, recipeRegistry, selection, constructionWorld, roomDetection);
		if (taskListExpanded && selectedColonistId != 0) {
			taskListPanel.get()->update(ecsWorld, selectedColonistId);
		}
		bottomLeft.get()->setVisible(taskListPanel.get()->isVisible() || infoPanel.get()->isVisible());

		if (colonistDetailsDialog->isOpen()) {
			colonistDetailsDialog->update(ecsWorld, deltaTime);
		}
		if (craftingDialog->isOpen()) {
			craftingDialog->update(ecsWorld, recipeRegistry);
		}
		if (storageConfigDialog->isOpen()) {
			storageConfigDialog->update(ecsWorld, assetRegistry);
		}
	}

	int GameUI::criticalToastCount() const {
		const std::vector<ui::Ref<ui::Toast>> shown = toastStack.get()->toasts();
		return static_cast<int>(std::ranges::count_if(shown, [](const ui::Ref<ui::Toast>& toast) {
			return toast.get() != nullptr && toast.get()->getSeverity() == ui::ToastSeverity::Critical;
		}));
	}

	void GameUI::pushNotification(
		const std::string&	  title,
		const std::string&	  message,
		ui::ToastSeverity	  severity,
		float				  autoDismissTime,
		std::function<void()> onClick
	) {
		toastStack.get()->add({
			.title = title,
			.message = message,
			.severity = severity,
			.autoDismissMs = static_cast<double>(autoDismissTime) * 1000.0,
			.onPress = std::move(onClick),
		});
	}

	void GameUI::toggleTaskList() {
		taskListExpanded = !taskListExpanded;
		taskListPanel.get()->setVisible(taskListExpanded);
	}

	void GameUI::toggleDecisionInspector() {
		toggleTaskList();
	}

	void GameUI::showBuildMenu(const std::vector<BuildMenuItem>& items) {
		buildMenu.get()->setItems(items);
		buildMenu.get()->setVisible(true);
	}

	void GameUI::hideBuildMenu() {
		buildMenu.get()->setVisible(false);
	}

	bool GameUI::isBuildMenuVisible() const {
		return buildMenu.get()->isVisible();
	}

	void GameUI::showCatalogue(
		const assets::RecipeRegistry&		recipes,
		const assets::ConstructionRegistry& construction,
		const std::string&					wallMaterial
	) {
		std::vector<std::pair<std::string, std::string>> productionItems;
		for (const assets::RecipeDef* recipe : recipes.getInnateRecipes()) {
			if (recipe->isStationless() && !recipe->outputs.empty()) {
				productionItems.emplace_back(recipe->outputs[0].defName, recipe->label);
			}
		}
		gameplayBar.get()->setProductionItems(productionItems);

		std::vector<std::pair<std::string, float>> materials;
		for (const auto& [name, material] : construction.getAllMaterials()) {
			materials.emplace_back(name, material.costRatePerSquareMeter);
		}
		configStrip.get()->setMaterials(materials);

		showWallThicknesses(construction, wallMaterial);
	}

	// A material with no wall presets clears the cards, and the wall tool refuses a commit with a toast.
	void GameUI::showWallThicknesses(const assets::ConstructionRegistry& construction, const std::string& material) {
		std::vector<ConstructionConfigStrip::ThicknessPresetInfo> presets;
		if (const auto* wall = construction.getWallMaterial(material)) {
			for (const auto& preset : wall->wallThicknesses) {
				presets.push_back({preset.name, preset.thicknessMeters});
			}
		}
		configStrip.get()->setThicknessPresets(presets);
	}

	void GameUI::setConstructionStatus(const DrawingStatus& status) {
		configStrip.get()->setStatus(status);
	}

	void GameUI::setKeyGuide(std::optional<ToolKind> tool, const DrawingSettings& settings, bool collapsed) {
		keyGuide.get()->show(tool, settings, collapsed);
	}

	void GameUI::setRoomsOverlayActive(bool active) {
		gameplayBar.get()->setRoomsActive(active);
	}

	void GameUI::showColonistDetails(ecs::EntityID colonistId) {
		colonistDetailsDialog->open(*root.get(), colonistId);
	}

	void GameUI::hideColonistDetails() {
		colonistDetailsDialog->close();
	}

	bool GameUI::isColonistDetailsVisible() const {
		return colonistDetailsDialog->isOpen();
	}

	void GameUI::showCraftingDialog(ecs::EntityID stationId, const std::string& stationDefName) {
		craftingDialog->open(*root.get(), stationId, stationDefName);
	}

	void GameUI::hideCraftingDialog() {
		craftingDialog->close();
	}

	bool GameUI::isCraftingDialogVisible() const {
		return craftingDialog->isOpen();
	}

	void GameUI::showStorageConfigDialog(ecs::EntityID containerId, const std::string& containerDefName) {
		storageConfigDialog->open(*root.get(), containerId, containerDefName);
	}

	void GameUI::hideStorageConfigDialog() {
		storageConfigDialog->close();
	}

	bool GameUI::isStorageConfigVisible() const {
		return storageConfigDialog->isOpen();
	}

	void GameUI::showPauseMenu() {
		pauseMenuDialog->open(*root.get());
	}

	bool GameUI::isPauseMenuVisible() const {
		return pauseMenuDialog->isOpen();
	}

	bool GameUI::isGlobalTaskListExpanded() const {
		return globalTaskList.get()->isExpanded();
	}

} // namespace world_sim
