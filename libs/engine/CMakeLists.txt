add_library(engine
    application/Application.cpp
    application/AppLauncher.cpp
    scene/SceneManager.cpp
    input/InputManager.cpp
    clipboard/ClipboardManager.cpp
    world/TileGrid.cpp
    world/chunk/Chunk.cpp
    world/chunk/ChunkManager.cpp
    world/chunk/MockWorldSampler.cpp
    world/rendering/ChunkRenderer.cpp
    world/rendering/EntityRenderer.cpp
)

target_include_directories(engine
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(engine PUBLIC cxx_std_20)

# Link dependencies
target_link_libraries(engine
    PUBLIC
        foundation
        renderer
        ui
        assets
        nlohmann_json::nlohmann_json
        glfw
)

# Tests
if(BUILD_TESTING)
    # Discover test files using naming pattern
    file(GLOB_RECURSE ENGINE_TEST_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/*.test.cpp"
    )

    # Discover benchmark files using naming pattern
    file(GLOB_RECURSE ENGINE_BENCHMARK_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/*.bench.cpp"
    )

    # Create test executable if tests found
    if(ENGINE_TEST_SOURCES)
        find_package(GTest CONFIG REQUIRED)
        add_executable(engine-tests ${ENGINE_TEST_SOURCES})
        target_link_libraries(engine-tests
            PRIVATE
                engine
                GTest::gtest
                GTest::gtest_main
        )
        target_compile_options(engine-tests PRIVATE -O0 -g)
        add_test(NAME engine-tests COMMAND engine-tests)
    endif()

    # Create benchmark executable if benchmarks found
    if(ENGINE_BENCHMARK_SOURCES)
        find_package(benchmark CONFIG REQUIRED)
        add_executable(engine-benchmarks ${ENGINE_BENCHMARK_SOURCES})
        target_link_libraries(engine-benchmarks
            PRIVATE
                engine
                benchmark::benchmark
                benchmark::benchmark_main
        )
        target_compile_options(engine-benchmarks PRIVATE -O3)
        add_test(NAME engine-benchmarks COMMAND engine-benchmarks)
    endif()
endif()
