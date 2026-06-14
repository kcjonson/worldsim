add_library(world
    worldgen/grid/SphereGrid.cpp
    worldgen/data/PlanetParams.cpp
    worldgen/pipeline/PlanetGenerator.cpp
    worldgen/stages/TectonicHistoryStage.cpp
    worldgen/stages/CrustStage.cpp
    worldgen/stages/TerrainStage.cpp
    worldgen/tectonics/PlateSim.cpp
    worldgen/tectonics/TectonicHistory.cpp
    worldgen/stages/AtmosphereStage.cpp
    worldgen/stages/PrecipitationStage.cpp
    worldgen/stages/OceanStage.cpp
    worldgen/stages/BiomeStage.cpp
    worldgen/stages/SnowStage.cpp
    worldgen/sampling/SphericalProjection.cpp
    worldgen/sampling/PlanetSampler.cpp
    worldgen/sampling/LandingSite.cpp
    worldgen/io/PlanetIO.cpp
    worldgen/debug/DebugImageExporter.cpp
    worldgen/debug/WorldStats.cpp
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
        if(NOT MSVC)
            target_compile_options(world-tests PRIVATE -O0 -g)
        endif()
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
        # MSVC: rely on per-config defaults (/O2 in Release); forcing /O2 here
        # collides with Debug's /RTC1 (D8016).
        if(NOT MSVC)
            target_compile_options(world-benchmarks PRIVATE -O3)
        endif()
        add_test(NAME world-benchmarks COMMAND world-benchmarks)
    endif()
endif()
