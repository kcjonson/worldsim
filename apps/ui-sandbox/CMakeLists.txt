add_executable(ui-sandbox
    Main.cpp
    NavigationMenu.cpp
    SceneTypes.cpp
    scenes/ArenaScene.cpp
    scenes/HandleScene.cpp
    scenes/VectorPerfScene.cpp
    scenes/GrassScene.cpp
    scenes/TreeScene.cpp
    scenes/SvgScene.cpp
    scenes/PlanetScene.cpp
    scenes/GalleryScene.cpp
    scenes/GalleryDialogScene.cpp
)

target_include_directories(ui-sandbox
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(ui-sandbox PRIVATE cxx_std_20)

# Link required libraries
target_link_libraries(ui-sandbox
    PRIVATE
        engine
        theme
        salal::renderer
        salal::foundation
        assets
        planet-view
        world
)

# Mirror the staged shaders beside the exe on every build (see apps/world-sim/CMakeLists.txt).
add_custom_target(ui-sandbox-shaders ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_BINARY_DIR}/shaders
        $<TARGET_FILE_DIR:ui-sandbox>/shaders
    COMMENT "Mirroring shaders beside ui-sandbox"
    VERBATIM
)
add_dependencies(ui-sandbox-shaders engine-shaders planet-view-shaders)
add_dependencies(ui-sandbox ui-sandbox-shaders)

# Copy fonts to executable directory
add_custom_command(TARGET ui-sandbox POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/fonts
    $<TARGET_FILE_DIR:ui-sandbox>/fonts
    COMMENT "Copying fonts to ui-sandbox directory"
)

# Copy all assets to executable directory (definitions, svg, lua, etc.)
add_custom_command(TARGET ui-sandbox POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/assets
    $<TARGET_FILE_DIR:ui-sandbox>/assets
    COMMENT "Copying assets to ui-sandbox directory"
)
