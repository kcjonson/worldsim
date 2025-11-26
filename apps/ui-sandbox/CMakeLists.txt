add_executable(ui-sandbox
    main.cpp
    navigation_menu.cpp
    scenes/shapes_scene.cpp
    scenes/arena_scene.cpp
    scenes/handle_scene.cpp
    scenes/vector_star_scene.cpp
    scenes/vector_perf_scene.cpp
    scenes/font_test_scene.cpp
    scenes/text_shapes_scene.cpp
    scenes/layer_scene.cpp
    scenes/input_test_scene.cpp
    scenes/button_scene.cpp
    scenes/sdf_minimal_scene.cpp
    scenes/text_input_scene.cpp
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
