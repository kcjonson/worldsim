add_executable(world-sim
    Main.cpp
    SceneTypes.cpp
    GameWorldState.cpp
    GameStartConfig.cpp
    # Game scene UI - views
    scenes/game/ui/views/ColonistListView.cpp
    scenes/game/ui/views/EntityInfoView.cpp
    scenes/game/ui/views/TaskListView.cpp
    scenes/game/ui/views/BuildMenu.cpp
    scenes/game/ui/views/GameplayBar.cpp
    scenes/game/ui/views/DebugOverlay.cpp
    scenes/game/ui/views/ZoomControlPanel.cpp
    scenes/game/ui/views/TopBar.cpp
    scenes/game/ui/views/ResourcesPanel.cpp
    scenes/game/ui/views/GlobalTaskListView.cpp
    # Game scene UI - components
    scenes/game/ui/components/ColonistListItem.cpp
    scenes/game/ui/components/GlobalTaskRow.cpp
    scenes/game/ui/components/NeedBar.cpp
    scenes/game/ui/components/ZoomControl.cpp
    scenes/game/ui/components/SpeedButton.cpp
    # Game scene UI - adapters
    scenes/game/ui/adapters/ColonistAdapter.cpp
    scenes/game/ui/adapters/CraftingAdapter.cpp
    scenes/game/ui/adapters/GlobalTaskAdapter.cpp
    scenes/game/ui/adapters/SelectionAdapter.cpp
    scenes/game/ui/adapters/TimeAdapter.cpp
    # Game scene UI - models
    scenes/game/ui/models/ColonistListModel.cpp
    scenes/game/ui/models/GlobalTaskListModel.cpp
    scenes/game/ui/models/EntityInfoModel.cpp
    scenes/game/ui/models/TimeModel.cpp
    # Game scene UI - dialogs
    scenes/game/ui/dialogs/ColonistDetailsDialog.cpp
    scenes/game/ui/dialogs/ColonistDetailsModel.cpp
    scenes/game/ui/dialogs/CraftingDialog.cpp
    scenes/game/ui/dialogs/CraftingDialogModel.cpp
    scenes/game/ui/dialogs/StorageConfigDialog.cpp
    scenes/game/ui/dialogs/StorageConfigDialogModel.cpp
    scenes/game/ui/dialogs/tabs/BioTabView.cpp
    scenes/game/ui/dialogs/tabs/HealthTabView.cpp
    scenes/game/ui/dialogs/tabs/SkillsTabView.cpp
    scenes/game/ui/dialogs/tabs/SocialTabView.cpp
    scenes/game/ui/dialogs/tabs/GearTabView.cpp
    scenes/game/ui/dialogs/tabs/MemoryTabView.cpp
    scenes/game/ui/dialogs/tabs/TasksTabView.cpp
    scenes/game/ui/dialogs/tabs/LogTabView.cpp
    # Game scene UI - main container
    scenes/game/ui/GameUI.cpp
    # Game scene - world subsystems
    scenes/game/world/placement/GhostRenderer.cpp
    scenes/game/world/placement/PlacementMode.cpp
    scenes/game/world/placement/PlacementSystem.cpp
    scenes/game/world/selection/SelectionSystem.cpp
    scenes/game/world/construction/DrawingSystem.cpp
    scenes/game/world/rooms/RoomOverlay.cpp
    scenes/game/world/nav/NavOverlay.cpp
    scenes/game/world/vision/VisionOverlay.cpp
    scenes/game/ui/views/ConstructionConfigStrip.cpp
    # Scenes
    scenes/shared/GlobeView.cpp
    scenes/splash/SplashScene.cpp
    scenes/menu/MainMenuScene.cpp
    scenes/world-creator/WorldCreatorScene.cpp
    scenes/world-creator/WorldCreatorModel.cpp
    scenes/world-creator/ui/ParameterPanel.cpp
    scenes/landing/LandingSiteDetailsModel.cpp
    scenes/landing/LandingSiteDetailsPanel.cpp
    scenes/loading/GameLoadingScene.cpp
    scenes/game/dev/DevCommandHandler.cpp
    scenes/game/GameScene.cpp
    scenes/settings/SettingsScene.cpp
)

target_include_directories(world-sim
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(world-sim PRIVATE cxx_std_20)

# Link all required libraries
target_link_libraries(world-sim
    PRIVATE
        engine
        game-systems
        world
        ui
        renderer
        foundation
        assets
        planet-view
)

# Copy shaders after build as well (kept for redundancy during full rebuilds)
add_custom_command(TARGET world-sim POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_BINARY_DIR}/shaders
    $<TARGET_FILE_DIR:world-sim>/shaders
    COMMENT "Copying shaders to world-sim directory"
)

# Copy fonts to executable directory
add_custom_command(TARGET world-sim POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/fonts
    $<TARGET_FILE_DIR:world-sim>/fonts
    COMMENT "Copying fonts to world-sim directory"
)

# Mirror assets to the executable directory every build.
# Uses cmake/mirror-assets.cmake which mirrors each source subdir independently
# via robocopy /MIR (Win) / rsync --delete (non-Win). Per-subdir mirroring means
# extra dest dirs not in source (e.g. assets/planets/ staged separately) are left
# untouched, while deletions within source-managed subdirs are propagated.
add_custom_target(world-sim-assets ALL
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_DIR=${CMAKE_SOURCE_DIR}/assets
        -DDEST_DIR=$<TARGET_FILE_DIR:world-sim>/assets
        -P ${CMAKE_SOURCE_DIR}/cmake/mirror-assets.cmake
    COMMENT "Mirroring assets to world-sim directory"
)

# Make world-sim depend on shader and asset sync so they are copied before running
add_dependencies(world-sim world-sim-assets renderer-shaders)
