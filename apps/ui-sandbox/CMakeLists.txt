add_executable(ui-sandbox
    main.cpp
    demos/arena_demo.cpp
    demos/handle_demo.cpp
)

target_include_directories(ui-sandbox
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(ui-sandbox PRIVATE cxx_std_20)

# Link required libraries
target_link_libraries(ui-sandbox
    PRIVATE
        ui
        renderer
        foundation
)
