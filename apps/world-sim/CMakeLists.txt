add_executable(world-sim
    Main.cpp
    scenes/SplashScene.cpp
    scenes/MainMenuScene.cpp
    scenes/WorldCreatorScene.cpp
    scenes/GameScene.cpp
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
)

# Copy assets to build directory when available
set(WORLD_SIM_ASSETS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/assets")
if(EXISTS "${WORLD_SIM_ASSETS_DIR}")
    add_custom_command(TARGET world-sim POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${WORLD_SIM_ASSETS_DIR}"
            "$<TARGET_FILE_DIR:world-sim>/assets"
        COMMENT "Copying assets to build directory"
    )
else()
    message(WARNING "world-sim assets directory not found; skipping asset copy")
endif()
