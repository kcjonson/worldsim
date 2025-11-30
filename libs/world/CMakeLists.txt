add_library(world
    generation/Placeholder.cpp
)

target_include_directories(world
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(world PUBLIC cxx_std_20)

# Link dependencies
target_link_libraries(world
    PUBLIC
        foundation
        renderer
        nlohmann_json::nlohmann_json
)

# Tests
if(BUILD_TESTING)
    # Discover test files using naming pattern
    file(GLOB_RECURSE WORLD_TEST_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/*.test.cpp"
    )

    # Discover benchmark files using naming pattern
    file(GLOB_RECURSE WORLD_BENCHMARK_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/*.bench.cpp"
    )

    # Create test executable if tests found
    if(WORLD_TEST_SOURCES)
        find_package(GTest CONFIG REQUIRED)
        add_executable(world-tests ${WORLD_TEST_SOURCES})
        target_link_libraries(world-tests
            PRIVATE
                world
                GTest::gtest
                GTest::gtest_main
        )
        target_compile_options(world-tests PRIVATE -O0 -g)
        add_test(NAME world-tests COMMAND world-tests)
    endif()

    # Create benchmark executable if benchmarks found
    if(WORLD_BENCHMARK_SOURCES)
        find_package(benchmark CONFIG REQUIRED)
        add_executable(world-benchmarks ${WORLD_BENCHMARK_SOURCES})
        target_link_libraries(world-benchmarks
            PRIVATE
                world
                benchmark::benchmark
                benchmark::benchmark_main
        )
        target_compile_options(world-benchmarks PRIVATE -O3)
        add_test(NAME world-benchmarks COMMAND world-benchmarks)
    endif()
endif()
