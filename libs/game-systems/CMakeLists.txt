add_library(game-systems
    chunks/placeholder.cpp
)

target_include_directories(game-systems
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(game-systems PUBLIC cxx_std_20)

# Link dependencies
target_link_libraries(game-systems
    PUBLIC
        foundation
        renderer
        world
)

# Tests
if(BUILD_TESTING)
    # Discover test files using naming pattern
    file(GLOB_RECURSE GAME_SYSTEMS_TEST_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/**/*.test.cpp"
    )

    # Discover benchmark files using naming pattern
    file(GLOB_RECURSE GAME_SYSTEMS_BENCHMARK_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/**/*.bench.cpp"
    )

    # Create test executable if tests found
    if(GAME_SYSTEMS_TEST_SOURCES)
        find_package(GTest CONFIG REQUIRED)
        add_executable(game-systems-tests ${GAME_SYSTEMS_TEST_SOURCES})
        target_link_libraries(game-systems-tests
            PRIVATE
                game-systems
                GTest::gtest
                GTest::gtest_main
        )
        target_compile_options(game-systems-tests PRIVATE -O0 -g)
        add_test(NAME game-systems-tests COMMAND game-systems-tests)
    endif()

    # Create benchmark executable if benchmarks found
    if(GAME_SYSTEMS_BENCHMARK_SOURCES)
        find_package(benchmark CONFIG REQUIRED)
        add_executable(game-systems-benchmarks ${GAME_SYSTEMS_BENCHMARK_SOURCES})
        target_link_libraries(game-systems-benchmarks
            PRIVATE
                game-systems
                benchmark::benchmark
                benchmark::benchmark_main
        )
        target_compile_options(game-systems-benchmarks PRIVATE -O3)
        add_test(NAME game-systems-benchmarks COMMAND game-systems-benchmarks)
    endif()
endif()
