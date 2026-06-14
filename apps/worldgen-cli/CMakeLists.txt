add_executable(worldgen-cli
    Main.cpp
)

target_include_directories(worldgen-cli
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(worldgen-cli PRIVATE cxx_std_20)

target_link_libraries(worldgen-cli
    PRIVATE
        world
        foundation
)
