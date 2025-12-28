add_executable(world-sim
    Main.cpp
    SceneTypes.cpp
    GameWorldState.cpp
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
    # Game scene UI - components
    scenes/game/ui/components/ColonistListItem.cpp
    scenes/game/ui/components/NeedBar.cpp
    scenes/game/ui/components/ZoomControl.cpp
    scenes/game/ui/components/DateTimeDisplay.cpp
    scenes/game/ui/components/SpeedButton.cpp
    # Game scene UI - adapters
    scenes/game/ui/adapters/ColonistAdapter.cpp
    scenes/game/ui/adapters/CraftingAdapter.cpp
    scenes/game/ui/adapters/SelectionAdapter.cpp
    scenes/game/ui/adapters/TimeAdapter.cpp
    # Game scene UI - models
    scenes/game/ui/models/ColonistListModel.cpp
    scenes/game/ui/models/EntityInfoModel.cpp
    scenes/game/ui/models/TimeModel.cpp
    # Game scene UI - main container
    scenes/game/ui/GameUI.cpp
    # Game scene - world rendering
    scenes/game/world/GhostRenderer.cpp
    scenes/game/world/PlacementMode.cpp
    # Scenes
    scenes/splash/SplashScene.cpp
    scenes/menu/MainMenuScene.cpp
    scenes/world-creator/WorldCreatorScene.cpp
    scenes/loading/GameLoadingScene.cpp
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

# Create a custom target that always syncs assets (runs every build)
add_custom_target(world-sim-assets ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/assets
    ${CMAKE_CURRENT_BINARY_DIR}/assets
    COMMENT "Syncing assets to world-sim directory"
)

# Make world-sim depend on shader and asset sync so they are copied before running
add_dependencies(world-sim world-sim-assets renderer-shaders)
