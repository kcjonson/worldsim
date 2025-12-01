add_executable(ui-sandbox
    Main.cpp
    NavigationMenu.cpp
    scenes/ShapesScene.cpp
    scenes/ArenaScene.cpp
    scenes/HandleScene.cpp
    scenes/VectorStarScene.cpp
    scenes/VectorPerfScene.cpp
    scenes/GrassScene.cpp
    scenes/TextShapesScene.cpp
    scenes/LayerScene.cpp
    scenes/ButtonScene.cpp
    scenes/InputTestScene.cpp
    scenes/SdfMinimalScene.cpp
    scenes/TextInputScene.cpp
    scenes/ClipScene.cpp
    scenes/SvgScene.cpp
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
        ui
        renderer
        foundation
        assets
)

# Copy shaders to executable directory
add_custom_command(TARGET ui-sandbox POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_BINARY_DIR}/shaders
    $<TARGET_FILE_DIR:ui-sandbox>/shaders
    COMMENT "Copying shaders to ui-sandbox directory"
)

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
