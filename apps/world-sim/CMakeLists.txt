add_executable(world-sim
    Main.cpp
    SceneTypes.cpp
    GameWorldState.cpp
    components/GameOverlay.cpp
    components/ZoomControl.cpp
    components/NeedBar.cpp
    components/EntityInfoPanel.cpp
    scenes/SplashScene.cpp
    scenes/MainMenuScene.cpp
    scenes/WorldCreatorScene.cpp
    scenes/GameLoadingScene.cpp
    scenes/GameScene.cpp
    scenes/SettingsScene.cpp
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

# Copy shaders to executable directory
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

# Make world-sim depend on assets so they're copied before running
add_dependencies(world-sim world-sim-assets)
