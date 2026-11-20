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
salal_windows_manifest(ui-sandbox)

# Link required libraries
target_link_libraries(ui-sandbox
    PRIVATE
        world-view
        theme
        salal::app
        salal::renderer
        salal::foundation
        assets
        planet-view
        worldgen
        OpenGL::GL
        GLEW::GLEW
)

# Mirror the staged shaders beside the exe on every build (see apps/world-sim/CMakeLists.txt).
add_custom_target(ui-sandbox-shaders ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_BINARY_DIR}/shaders
        $<TARGET_FILE_DIR:ui-sandbox>/shaders
    COMMENT "Mirroring shaders beside ui-sandbox"
    VERBATIM
)
add_dependencies(ui-sandbox-shaders world-view-shaders planet-view-shaders)
add_dependencies(ui-sandbox ui-sandbox-shaders)

# Fonts and assets are staged beside the exe by the root CMakeLists (stage_fonts, stage_assets).
