add_executable(ui-sandbox
    main.cpp
    scenes/shapes_scene.cpp
    scenes/arena_scene.cpp
    scenes/handle_scene.cpp
    scenes/vector_star_scene.cpp
    scenes/vector_perf_scene.cpp
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
